#include "treatment_library_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace beam::app {
namespace {

QString uniqueName(const QString& stem, const std::vector<std::string>& taken) {
    for (int suffix = 1; suffix < 1000; ++suffix) {
        const QString candidate = QStringLiteral("%1%2").arg(stem).arg(suffix);
        bool clash = false;
        for (const std::string& name : taken)
            if (QString::fromStdString(name) == candidate) clash = true;
        if (!clash) return candidate;
    }
    return stem;
}

}  // namespace

TreatmentLibraryDialog::TreatmentLibraryDialog(beam::gui::TreatmentTargetStore store, Page page,
                                               QWidget* parent)
    : QDialog(parent), page_(page), store_(std::move(store)) {
    const bool targetsPage = page == Page::Targets;
    setWindowTitle(targetsPage ? QStringLiteral("Manage targets")
                               : QStringLiteral("Manage protocols"));
    setModal(true);
    resize(targetsPage ? 980 : 1180, 620);

    auto* layout = new QVBoxLayout(this);
    auto* banner = new QLabel(
        targetsPage
            ? QStringLiteral("The Target List is shared across patients. Changes take effect when "
                             "you save.")
            : QStringLiteral("Protocols schedule the shared Target List. Changes take effect when "
                             "you save."),
        this);
    banner->setWordWrap(true);
    layout->addWidget(banner);

    auto* body = new QWidget(this);
    if (targetsPage) {
        buildTargetsTab(body);
    } else {
        buildProtocolsTab(body);
    }
    layout->addWidget(body, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    reloadTargets();
    reloadTargetPoints();
    reloadProtocols();
}

void TreatmentLibraryDialog::buildTargetsTab(QWidget* page) {
    auto* outer = new QVBoxLayout(page);
    auto* caption = new QLabel(
        QStringLiteral("The Target List is a template. A treatment protocol copies a target's "
                       "points when the target is added to it, so changes here reach only "
                       "protocols that take the target afterwards."),
        page);
    caption->setWordWrap(true);
    caption->setStyleSheet(QStringLiteral("color: #4a5f69;"));
    outer->addWidget(caption);
    auto* layout = new QHBoxLayout;
    outer->addLayout(layout, 1);
    targetList_ = new QListWidget(page);
    layout->addWidget(targetList_, 1);

    auto* column = new QVBoxLayout;
    auto* add = new QPushButton(QStringLiteral("New"), page);
    auto* rename = new QPushButton(QStringLiteral("Rename..."), page);
    auto* remove = new QPushButton(QStringLiteral("Delete"), page);
    for (QPushButton* button : {add, rename, remove}) column->addWidget(button);
    column->addStretch(1);
    layout->addLayout(column);

    // The template's own parameters. Without this the Target List would be
    // name-only.
    buildPointsSection(page, outer);
    connect(targetList_, &QListWidget::currentRowChanged, this,
            [this](int) { reloadTargetPoints(); });

    connect(add, &QPushButton::clicked, this, [this] {
        std::vector<std::string> taken;
        for (const auto& target : store_.targets()) taken.push_back(target.name);
        beam::gui::TreatmentTarget created;
        created.name = uniqueName(QStringLiteral("Target"), taken).toStdString();
        if (!store_.addTarget(created)) return;
        reloadTargets(QString::fromStdString(created.name));
        reloadEntries();
    });

    connect(rename, &QPushButton::clicked, this, [this] {
        const QString before = selectedTarget();
        if (before.isEmpty()) return;
        bool ok = false;
        const QString after = QInputDialog::getText(this, QStringLiteral("Rename target"),
                                                    QStringLiteral("Name:"), QLineEdit::Normal,
                                                    before, &ok);
        if (!ok || after.trimmed().isEmpty() || after.trimmed() == before) return;
        if (!store_.renameTarget(before.toStdString(), after.trimmed().toStdString())) {
            QMessageBox::warning(this, QStringLiteral("Rename target"),
                                 QStringLiteral("A target named \"%1\" already exists.")
                                     .arg(after.trimmed()));
            return;
        }
        reloadTargets(after.trimmed());
        reloadEntries();
    });

    connect(remove, &QPushButton::clicked, this, [this] {
        const QString name = selectedTarget();
        if (name.isEmpty()) return;
        if (store_.targets().size() <= 1) {
            QMessageBox::warning(this, QStringLiteral("Delete target"),
                                 QStringLiteral("The last target cannot be deleted."));
            return;
        }
        int scheduled = 0;
        for (const auto& protocol : store_.protocols())
            for (const auto& entry : protocol.entries)
                if (entry.target == name.toStdString()) ++scheduled;
        // Protocols that already took this target keep it: they hold their own
        // copy, so deleting the template entry does not disturb a plan.
        const QString question =
            scheduled > 0
                ? QStringLiteral("Delete \"%1\" from the Target List? %2 schedule entr%3 already "
                                 "hold their own copy and are not affected.")
                      .arg(name).arg(scheduled).arg(scheduled == 1 ? QStringLiteral("y")
                                                                   : QStringLiteral("ies"))
                : QStringLiteral("Delete \"%1\" from the Target List?").arg(name);
        if (QMessageBox::question(this, QStringLiteral("Delete target"), question) !=
            QMessageBox::Yes)
            return;
        store_.removeTarget(name.toStdString());
        reloadTargets();
        reloadEntries();
    });
}

// One points table serves both pages. On Targets it edits the Target List's
// template; on Protocols it edits the selected protocol's own copy. The widget
// and the editing are identical -- only where the rows come from and go to.
void TreatmentLibraryDialog::buildPointsSection(QWidget* page, QVBoxLayout* outer) {
    pointCaption_ = new QLabel(page);
    pointCaption_->setWordWrap(true);
    outer->addWidget(pointCaption_);

    pointTable_ = new QTableWidget(0, 9, page);
    pointTable_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Show"), QStringLiteral("Amplitude"),
         QStringLiteral("Start (s)"), QStringLiteral("End (s)"), QStringLiteral("Burst dur (s)"),
         QStringLiteral("Burst int (s)"), QStringLiteral("Pulse dur (s)"),
         QStringLiteral("Pulse int (s)")});
    pointTable_->horizontalHeader()->setStretchLastSection(true);
    pointTable_->verticalHeader()->setVisible(false);
    pointTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    outer->addWidget(pointTable_, 1);

    auto* pointButtons = new QHBoxLayout;
    auto* addPoint = new QPushButton(QStringLiteral("Add point"), page);
    auto* removePoint = new QPushButton(QStringLiteral("Remove point"), page);
    pointButtons->addStretch(1);
    pointButtons->addWidget(addPoint);
    pointButtons->addWidget(removePoint);
    outer->addLayout(pointButtons);

    connect(pointTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        if (!reloadingPoints_) commitTargetPoints();
    });

    connect(addPoint, &QPushButton::clicked, this, [this] {
        if (pointsOwnerTarget().isEmpty()) return;
        std::vector<beam::gui::TreatmentTargetPoint> points = currentPoints();
        // createSonicationUpdateTable.m appends by copying the previous row.
        points.push_back(points.empty() ? beam::gui::TreatmentTargetPoint{} : points.back());
        if (writePoints(std::move(points))) reloadTargetPoints();
    });

    connect(removePoint, &QPushButton::clicked, this, [this] {
        std::vector<beam::gui::TreatmentTargetPoint> points = currentPoints();
        const int row = pointTable_->currentRow();
        if (row < 0 || row >= static_cast<int>(points.size())) return;
        if (points.size() <= 1) {
            QMessageBox::warning(this, QStringLiteral("Remove point"),
                                 QStringLiteral("A target must keep at least one point."));
            return;
        }
        points.erase(points.begin() + row);
        if (writePoints(std::move(points))) reloadTargetPoints();
    });
}

QString TreatmentLibraryDialog::pointsOwnerTarget() const {
    if (page_ == Page::Targets) return selectedTarget();
    if (entryTable_ == nullptr) return {};
    const int row = entryTable_->currentRow();
    if (row < 0) return {};
    const QTableWidgetItem* cell = entryTable_->item(row, 1);
    return cell == nullptr ? QString() : cell->text();
}

std::vector<beam::gui::TreatmentTargetPoint> TreatmentLibraryDialog::currentPoints() const {
    const QString name = pointsOwnerTarget();
    if (name.isEmpty()) return {};
    if (page_ == Page::Targets) {
        const beam::gui::TreatmentTarget* target = store_.findTarget(name.toStdString());
        return target == nullptr ? std::vector<beam::gui::TreatmentTargetPoint>{} : target->points;
    }
    const int entry = store_.firstEntryForTarget(selectedProtocol().toStdString(),
                                                 name.toStdString());
    if (entry < 0) return {};
    const std::vector<beam::gui::TreatmentTargetPoint>* owned =
        store_.entryPoints(selectedProtocol().toStdString(), static_cast<std::size_t>(entry));
    return owned == nullptr ? std::vector<beam::gui::TreatmentTargetPoint>{} : *owned;
}

bool TreatmentLibraryDialog::writePoints(std::vector<beam::gui::TreatmentTargetPoint> points) {
    const QString name = pointsOwnerTarget();
    if (name.isEmpty()) return false;
    if (page_ == Page::Targets) return store_.setTargetPoints(name.toStdString(), std::move(points));
    const int entry = store_.firstEntryForTarget(selectedProtocol().toStdString(),
                                                 name.toStdString());
    if (entry < 0) return false;
    return store_.setEntryPoints(selectedProtocol().toStdString(),
                                 static_cast<std::size_t>(entry), std::move(points));
}

void TreatmentLibraryDialog::reloadTargetPoints() {
    if (pointTable_ == nullptr) return;
    const QString name = pointsOwnerTarget();
    const std::vector<beam::gui::TreatmentTargetPoint> points = currentPoints();

    reloadingPoints_ = true;
    pointTable_->clearContents();
    pointTable_->setRowCount(static_cast<int>(points.size()));
    {
        for (int row = 0; row < static_cast<int>(points.size()); ++row) {
            const beam::gui::TreatmentTargetPoint& point = points[static_cast<std::size_t>(row)];
            auto* order = new QTableWidgetItem(QString::number(point.order));
            order->setFlags(order->flags() & ~Qt::ItemIsEditable);
            pointTable_->setItem(row, 0, order);

            auto* show = new QTableWidgetItem;
            show->setFlags((show->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            show->setCheckState(point.show ? Qt::Checked : Qt::Unchecked);
            pointTable_->setItem(row, 1, show);

            const double values[] = {point.amplitude,
                                     point.startTimeSeconds,
                                     point.endTimeSeconds,
                                     point.burstDurationSeconds,
                                     point.burstIntervalSeconds,
                                     point.pulseDurationSeconds,
                                     point.pulseIntervalSeconds};
            for (int i = 0; i < 7; ++i)
                pointTable_->setItem(row, i + 2, new QTableWidgetItem(QString::number(values[i])));
        }
    }
    reloadingPoints_ = false;

    if (name.isEmpty()) {
        pointCaption_->setText(page_ == Page::Targets
                                   ? QStringLiteral("Template points")
                                   : QStringLiteral("Select a scheduled target to edit its points"));
    } else if (page_ == Page::Targets) {
        pointCaption_->setText(
            QStringLiteral("Template points for \"%1\" - the values a protocol copies when "
                           "\"%1\" is added to it").arg(name));
    } else {
        pointCaption_->setText(
            QStringLiteral("Points for \"%1\" in protocol \"%2\" only - this protocol's own copy, "
                           "taken from the Target List when \"%1\" was added")
                .arg(name, selectedProtocol()));
    }
}

// Reads the table back rather than tracking single cells: the table is small,
// and a partial write would leave the template disagreeing with what is shown.
void TreatmentLibraryDialog::commitTargetPoints() {
    if (pointTable_->rowCount() == 0) return;
    const std::vector<beam::gui::TreatmentTargetPoint> stored = currentPoints();
    if (stored.empty()) return;

    std::vector<beam::gui::TreatmentTargetPoint> points;
    points.reserve(static_cast<std::size_t>(pointTable_->rowCount()));
    for (int row = 0; row < pointTable_->rowCount(); ++row) {
        // Keep the stored value for any cell the edit did not supply.
        beam::gui::TreatmentTargetPoint point =
            row < static_cast<int>(stored.size())
                ? stored[static_cast<std::size_t>(row)]
                : beam::gui::TreatmentTargetPoint{};
        if (const QTableWidgetItem* show = pointTable_->item(row, 1))
            point.show = show->checkState() == Qt::Checked;
        double* const fields[] = {&point.amplitude,
                                  &point.startTimeSeconds,
                                  &point.endTimeSeconds,
                                  &point.burstDurationSeconds,
                                  &point.burstIntervalSeconds,
                                  &point.pulseDurationSeconds,
                                  &point.pulseIntervalSeconds};
        for (int i = 0; i < 7; ++i) {
            const QTableWidgetItem* cell = pointTable_->item(row, i + 2);
            if (cell == nullptr) continue;
            bool ok = false;
            const double value = cell->text().toDouble(&ok);
            if (ok) *fields[i] = value;
        }
        points.push_back(point);
    }
    writePoints(std::move(points));
    reloadTargetPoints();
}

// One pane: the protocol is chosen above, and everything it schedules is one
// table. A row is a scheduled target carrying both its entry's duration and
// this protocol's own copy of that target's parameters, because to an operator
// reading a schedule those are one thing. Rows drag to reorder.
void TreatmentLibraryDialog::buildProtocolsTab(QWidget* page) {
    auto* outer = new QVBoxLayout(page);

    auto* top = new QHBoxLayout;
    auto* protocolLabel = new QLabel(QStringLiteral("Protocol:"), page);
    protocolCombo_ = new QComboBox(page);
    protocolCombo_->setMinimumWidth(220);
    auto* add = new QPushButton(QStringLiteral("New"), page);
    auto* rename = new QPushButton(QStringLiteral("Rename..."), page);
    auto* remove = new QPushButton(QStringLiteral("Delete"), page);
    top->addWidget(protocolLabel);
    top->addWidget(protocolCombo_);
    top->addWidget(add);
    top->addWidget(rename);
    top->addWidget(remove);
    top->addStretch(1);
    outer->addLayout(top);

    scheduleCaption_ = new QLabel(page);
    scheduleCaption_->setWordWrap(true);
    outer->addWidget(scheduleCaption_);

    entryTable_ = new QTableWidget(0, 11, page);
    entryTable_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Target"), QStringLiteral("Duration (s)"),
         QStringLiteral("Show"), QStringLiteral("Amplitude"), QStringLiteral("Start (s)"),
         QStringLiteral("End (s)"), QStringLiteral("Burst dur (s)"), QStringLiteral("Burst int (s)"),
         QStringLiteral("Pulse dur (s)"), QStringLiteral("Pulse int (s)")});
    entryTable_->horizontalHeader()->setStretchLastSection(true);
    entryTable_->verticalHeader()->setVisible(false);
    entryTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    entryTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Drag a row to reorder the schedule. InternalMove on a row selection moves
    // the whole row; the model's own rowsMoved is what we write back from.
    entryTable_->setDragEnabled(true);
    entryTable_->setAcceptDrops(true);
    entryTable_->setDragDropMode(QAbstractItemView::InternalMove);
    entryTable_->setDragDropOverwriteMode(false);
    entryTable_->setDefaultDropAction(Qt::MoveAction);
    outer->addWidget(entryTable_, 1);

    auto* entryButtons = new QHBoxLayout;
    entryTargetCombo_ = new QComboBox(page);
    entryTargetCombo_->setMinimumWidth(200);
    for (const auto& target : store_.targets())
        entryTargetCombo_->addItem(QString::fromStdString(target.name));
    auto* duration = new QDoubleSpinBox(page);
    duration->setRange(0.0, 36000.0);
    duration->setValue(30.0);
    duration->setSuffix(QStringLiteral(" s"));
    auto* addEntry = new QPushButton(QStringLiteral("Add target"), page);
    auto* removeEntry = new QPushButton(QStringLiteral("Remove selected"), page);
    entryButtons->addWidget(new QLabel(QStringLiteral("Add from Target List:"), page));
    entryButtons->addWidget(entryTargetCombo_);
    entryButtons->addWidget(duration);
    entryButtons->addWidget(addEntry);
    entryButtons->addStretch(1);
    entryButtons->addWidget(removeEntry);
    outer->addLayout(entryButtons);

    connect(protocolCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { reloadEntries(); });

    connect(entryTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!reloadingPoints_ && item != nullptr) commitScheduleRow(item->row());
    });

    connect(entryTable_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int, int, const QModelIndex&, int) {
                if (!reloadingPoints_) commitScheduleOrder();
            });

    connect(add, &QPushButton::clicked, this, [this] {
        std::vector<std::string> taken;
        for (const auto& protocol : store_.protocols()) taken.push_back(protocol.name);
        const QString name = uniqueName(QStringLiteral("Protocol"), taken);
        if (!store_.addProtocol(name.toStdString())) return;
        reloadProtocols(name);
    });

    connect(rename, &QPushButton::clicked, this, [this] {
        const QString before = selectedProtocol();
        if (before.isEmpty()) return;
        bool ok = false;
        const QString after = QInputDialog::getText(this, QStringLiteral("Rename protocol"),
                                                    QStringLiteral("Name:"), QLineEdit::Normal,
                                                    before, &ok);
        if (!ok || after.trimmed().isEmpty() || after.trimmed() == before) return;
        if (!store_.renameProtocol(before.toStdString(), after.trimmed().toStdString())) {
            QMessageBox::warning(this, QStringLiteral("Rename protocol"),
                                 QStringLiteral("A protocol named \"%1\" already exists.")
                                     .arg(after.trimmed()));
            return;
        }
        reloadProtocols(after.trimmed());
    });

    connect(remove, &QPushButton::clicked, this, [this] {
        const QString name = selectedProtocol();
        if (name.isEmpty()) return;
        if (QMessageBox::question(this, QStringLiteral("Delete protocol"),
                                  QStringLiteral("Delete \"%1\" and its schedule?").arg(name)) !=
            QMessageBox::Yes)
            return;
        store_.removeProtocol(name.toStdString());
        reloadProtocols();
    });

    connect(addEntry, &QPushButton::clicked, this, [this, duration] {
        const QString protocol = selectedProtocol();
        const QString target = entryTargetCombo_->currentText();
        if (protocol.isEmpty() || target.isEmpty()) return;
        beam::gui::TreatmentProtocolEntry entry;
        entry.target = target.toStdString();
        entry.durationSeconds = duration->value();
        store_.appendProtocolEntry(protocol.toStdString(), entry);
        reloadEntries();
    });

    connect(removeEntry, &QPushButton::clicked, this, [this] {
        const QString protocol = selectedProtocol();
        const int row = entryTable_->currentRow();
        const auto* definition = store_.findProtocol(protocol.toStdString());
        if (definition == nullptr || row < 0 ||
            row >= static_cast<int>(definition->entries.size()))
            return;
        std::vector<beam::gui::TreatmentProtocolEntry> kept = definition->entries;
        kept.erase(kept.begin() + row);
        store_.setProtocolEntries(protocol.toStdString(), std::move(kept));
        reloadEntries();
    });
}

// Fills the one table: the entry's own duration, then this protocol's copy of
// that target's first point. A target scheduled twice shares one copy, so the
// parameter columns of both its rows move together -- only the duration is
// per-entry, which is what BeamV0's schedule varies.
void TreatmentLibraryDialog::reloadEntries() {
    if (entryTable_ == nullptr) return;
    reloadingPoints_ = true;
    entryTable_->setRowCount(0);
    const QString protocol = selectedProtocol();
    const auto* definition = store_.findProtocol(protocol.toStdString());
    if (definition != nullptr) {
        entryTable_->setRowCount(static_cast<int>(definition->entries.size()));
        for (int row = 0; row < entryTable_->rowCount(); ++row) {
            const auto& entry = definition->entries[static_cast<std::size_t>(row)];
            const QString target = QString::fromStdString(entry.target);

            auto* order = new QTableWidgetItem(QString::number(row + 1));
            order->setFlags(order->flags() & ~Qt::ItemIsEditable);
            entryTable_->setItem(row, 0, order);

            auto* name = new QTableWidgetItem(target);
            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            entryTable_->setItem(row, 1, name);

            entryTable_->setItem(row, 2,
                                 new QTableWidgetItem(QString::number(
                                     entry.durationSeconds.value_or(30.0), 'f', 1)));

            const std::vector<beam::gui::TreatmentTargetPoint>* owned =
                store_.entryPoints(protocol.toStdString(), static_cast<std::size_t>(row));
            if (owned == nullptr || owned->empty()) {
                for (int column = 3; column < entryTable_->columnCount(); ++column) {
                    auto* blank = new QTableWidgetItem(QStringLiteral("-"));
                    blank->setFlags(blank->flags() & ~Qt::ItemIsEditable);
                    entryTable_->setItem(row, column, blank);
                }
                continue;
            }
            const beam::gui::TreatmentTargetPoint& point = owned->front();

            auto* show = new QTableWidgetItem;
            show->setFlags((show->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            show->setCheckState(point.show ? Qt::Checked : Qt::Unchecked);
            entryTable_->setItem(row, 3, show);

            const double values[] = {point.amplitude,
                                     point.startTimeSeconds,
                                     point.endTimeSeconds,
                                     point.burstDurationSeconds,
                                     point.burstIntervalSeconds,
                                     point.pulseDurationSeconds,
                                     point.pulseIntervalSeconds};
            for (int i = 0; i < 7; ++i)
                entryTable_->setItem(row, i + 4, new QTableWidgetItem(QString::number(values[i])));
        }
    }
    reloadingPoints_ = false;

    scheduleCaption_->setText(
        protocol.isEmpty()
            ? QStringLiteral("No protocol selected")
            : QStringLiteral("\"%1\": every target it schedules, with this protocol's own copy of "
                             "their parameters. Edit any cell in place; drag a row to reorder.")
                  .arg(protocol));
}

// One edited cell: the duration belongs to that entry, every other column to
// the protocol's copy of the target. Points past the first are preserved --
// the row shows only the first, and must not delete the rest.
void TreatmentLibraryDialog::commitScheduleRow(int row) {
    const QString protocol = selectedProtocol();
    const auto* definition = store_.findProtocol(protocol.toStdString());
    if (definition == nullptr || row < 0 || row >= static_cast<int>(definition->entries.size()))
        return;
    const std::string target = definition->entries[static_cast<std::size_t>(row)].target;

    std::vector<beam::gui::TreatmentProtocolEntry> entries = definition->entries;
    if (const QTableWidgetItem* cell = entryTable_->item(row, 2)) {
        bool ok = false;
        const double seconds = cell->text().toDouble(&ok);
        if (ok) entries[static_cast<std::size_t>(row)].durationSeconds = seconds;
    }
    store_.setProtocolEntries(protocol.toStdString(), std::move(entries));

    const std::vector<beam::gui::TreatmentTargetPoint>* owned =
        store_.entryPoints(protocol.toStdString(), static_cast<std::size_t>(row));
    if (owned == nullptr || owned->empty()) return;
    std::vector<beam::gui::TreatmentTargetPoint> points = *owned;
    beam::gui::TreatmentTargetPoint& point = points.front();
    if (const QTableWidgetItem* show = entryTable_->item(row, 3))
        point.show = show->checkState() == Qt::Checked;
    double* const fields[] = {&point.amplitude,
                              &point.startTimeSeconds,
                              &point.endTimeSeconds,
                              &point.burstDurationSeconds,
                              &point.burstIntervalSeconds,
                              &point.pulseDurationSeconds,
                              &point.pulseIntervalSeconds};
    for (int i = 0; i < 7; ++i) {
        const QTableWidgetItem* cell = entryTable_->item(row, i + 4);
        if (cell == nullptr) continue;
        bool ok = false;
        const double value = cell->text().toDouble(&ok);
        if (ok) *fields[i] = value;
    }
    store_.setEntryPoints(protocol.toStdString(), static_cast<std::size_t>(row),
                          std::move(points));
}

// After a drag: the table's row order is the schedule's new order.
void TreatmentLibraryDialog::commitScheduleOrder() {
    const QString protocol = selectedProtocol();
    const auto* definition = store_.findProtocol(protocol.toStdString());
    if (definition == nullptr) return;
    if (entryTable_->rowCount() != static_cast<int>(definition->entries.size())) return;

    std::vector<beam::gui::TreatmentProtocolEntry> reordered;
    reordered.reserve(definition->entries.size());
    std::vector<bool> used(definition->entries.size(), false);
    for (int row = 0; row < entryTable_->rowCount(); ++row) {
        const QTableWidgetItem* name = entryTable_->item(row, 1);
        const QTableWidgetItem* duration = entryTable_->item(row, 2);
        if (name == nullptr) return;
        const std::string target = name->text().toStdString();
        // The same target can appear more than once; take each stored entry at
        // most once, matching on duration first so a repeated target keeps its
        // own value rather than collapsing onto the first match.
        int chosen = -1;
        for (std::size_t i = 0; i < definition->entries.size(); ++i) {
            if (used[i] || definition->entries[i].target != target) continue;
            if (chosen < 0) chosen = static_cast<int>(i);
            if (duration != nullptr && definition->entries[i].durationSeconds.has_value() &&
                QString::number(*definition->entries[i].durationSeconds, 'f', 1) ==
                    duration->text()) {
                chosen = static_cast<int>(i);
                break;
            }
        }
        if (chosen < 0) return;
        used[static_cast<std::size_t>(chosen)] = true;
        reordered.push_back(definition->entries[static_cast<std::size_t>(chosen)]);
    }
    store_.setProtocolEntries(protocol.toStdString(), std::move(reordered));
    reloadEntries();
}

void TreatmentLibraryDialog::reloadTargets(const QString& select) {
    if (targetList_ == nullptr) return;
    targetList_->clear();
    for (const auto& target : store_.targets())
        targetList_->addItem(QString::fromStdString(target.name));
    if (entryTargetCombo_ != nullptr) {
        const QString keep = entryTargetCombo_->currentText();
        entryTargetCombo_->clear();
        for (const auto& target : store_.targets())
            entryTargetCombo_->addItem(QString::fromStdString(target.name));
        const int restored = entryTargetCombo_->findText(keep);
        if (restored >= 0) entryTargetCombo_->setCurrentIndex(restored);
    }
    const auto items = targetList_->findItems(select, Qt::MatchExactly);
    targetList_->setCurrentRow(items.isEmpty() ? (targetList_->count() > 0 ? 0 : -1)
                                               : targetList_->row(items.first()));
}

void TreatmentLibraryDialog::reloadProtocols(const QString& select) {
    if (protocolCombo_ == nullptr) return;
    const QSignalBlocker quiet(protocolCombo_);
    protocolCombo_->clear();
    for (const auto& protocol : store_.protocols())
        protocolCombo_->addItem(QString::fromStdString(protocol.name));
    const int index = protocolCombo_->findText(select);
    protocolCombo_->setCurrentIndex(index >= 0 ? index : (protocolCombo_->count() > 0 ? 0 : -1));
    reloadEntries();
}


QString TreatmentLibraryDialog::selectedTarget() const {
    if (targetList_ == nullptr) return {};
    const QListWidgetItem* item = targetList_->currentItem();
    return item == nullptr ? QString() : item->text();
}

QString TreatmentLibraryDialog::selectedProtocol() const {
    if (protocolCombo_ == nullptr) return {};
    return protocolCombo_->currentText();
}

}  // namespace beam::app
