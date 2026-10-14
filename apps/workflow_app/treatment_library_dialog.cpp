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
    setWindowTitle(targetsPage ? QStringLiteral("Target List Template")
                               : QStringLiteral("Manage protocols"));
    setModal(true);
    resize(targetsPage ? 900 : 1180, 620);

    auto* layout = new QVBoxLayout(this);
    auto* body = new QWidget(this);
    if (targetsPage) {
        buildTargetsTab(body);
    } else {
        buildProtocolsTab(body);
    }
    layout->addWidget(body, 1);

    auto* footer = new QHBoxLayout;
    savedNotice_ = new QLabel(this);
    savedNotice_->setStyleSheet(QStringLiteral("QLabel { color: #5fcb96; }"));
    footer->addWidget(savedNotice_);
    footer->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    // Save writes and stays open; Close leaves. This dialog edits a copy, so
    // anything after the last Save simply never lands.
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (saveHandler_) saveHandler_(store_);
        if (savedNotice_) savedNotice_->setText(QStringLiteral("Saved."));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(buttons);
    layout->addLayout(footer);

    reloadTargets();
    reloadProtocols();
}


// One points table serves both pages. On Targets it edits the Target List's
// template; on Protocols it edits the selected protocol's own copy. The widget
// and the editing are identical -- only where the rows come from and go to.





// Reads the table back rather than tracking single cells: the table is small,
// and a partial write would leave the template disagreeing with what is shown.

// One pane: the protocol is chosen above, and everything it schedules is one
// table. A row is a scheduled target carrying both its entry's duration and
// this protocol's own copy of that target's parameters, because to an operator
// reading a schedule those are one thing. Rows drag to reorder.
// One pane, like the schedule: every target in the template and every point it
// defines, in one table, editable in place. A target with several points
// contributes several rows and repeats its name.
void TreatmentLibraryDialog::setSaveHandler(
    std::function<void(const beam::gui::TreatmentTargetStore&)> handler) {
    saveHandler_ = std::move(handler);
}

void TreatmentLibraryDialog::buildTargetsTab(QWidget* page) {
    auto* outer = new QVBoxLayout(page);

    auto* top = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("New target"), page);
    auto* rename = new QPushButton(QStringLiteral("Rename..."), page);
    auto* remove = new QPushButton(QStringLiteral("Delete target"), page);
    top->addWidget(add);
    top->addWidget(rename);
    top->addWidget(remove);
    top->addStretch(1);
    outer->addLayout(top);

    targetTable_ = new QTableWidget(0, 10, page);
    targetTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Target"), QStringLiteral("Point"), QStringLiteral("Show"),
         QStringLiteral("Amplitude"), QStringLiteral("Start Time (s)"),
         QStringLiteral("End Time (s)"), QStringLiteral("Burst Duration (s)"),
         QStringLiteral("Burst Interval (s)"), QStringLiteral("Pulse Duration (s)"),
         QStringLiteral("Pulse Interval (s)")});
    // Every column sized to its own label, and none stretched: whichever column
    // takes the slack ends up several times wider than the numbers in it.
    targetTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    targetTable_->horizontalHeader()->setStretchLastSection(false);
    targetTable_->verticalHeader()->setVisible(false);
    targetTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    targetTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    outer->addWidget(targetTable_, 1);

    connect(targetTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!reloadingPoints_ && item != nullptr) commitTargetRow(item->row());
    });

    connect(add, &QPushButton::clicked, this, [this] {
        std::vector<std::string> taken;
        for (const auto& target : store_.targets()) taken.push_back(target.name);
        beam::gui::TreatmentTarget created;
        created.name = uniqueName(QStringLiteral("Target"), taken).toStdString();
        if (!store_.addTarget(created)) return;
        reloadTargets(QString::fromStdString(created.name));
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
                ? QStringLiteral("Delete \"%1\" from the template? %2 schedule entr%3 already "
                                 "hold their own copy and are not affected.")
                      .arg(name).arg(scheduled).arg(scheduled == 1 ? QStringLiteral("y")
                                                                   : QStringLiteral("ies"))
                : QStringLiteral("Delete \"%1\" from the template?").arg(name);
        if (QMessageBox::question(this, QStringLiteral("Delete target"), question) !=
            QMessageBox::Yes)
            return;
        store_.removeTarget(name.toStdString());
        reloadTargets();
    });


}

void TreatmentLibraryDialog::reloadTargets(const QString& select) {
    if (targetTable_ == nullptr) return;
    reloadingPoints_ = true;
    targetTable_->setRowCount(0);
    targetRows_.clear();

    int rows = 0;
    for (const auto& target : store_.targets()) rows += static_cast<int>(target.points.size());
    targetTable_->setRowCount(rows);

    int row = 0;
    int selectRow = -1;
    for (std::size_t t = 0; t < store_.targets().size(); ++t) {
        const beam::gui::TreatmentTarget& target = store_.targets()[t];
        for (std::size_t p = 0; p < target.points.size(); ++p, ++row) {
            targetRows_.emplace_back(t, p);
            const beam::gui::TreatmentTargetPoint& point = target.points[p];
            if (selectRow < 0 && QString::fromStdString(target.name) == select) selectRow = row;

            auto* name = new QTableWidgetItem(QString::fromStdString(target.name));
            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            targetTable_->setItem(row, 0, name);

            auto* order = new QTableWidgetItem(QString::number(point.order));
            order->setFlags(order->flags() & ~Qt::ItemIsEditable);
            targetTable_->setItem(row, 1, order);

            auto* show = new QTableWidgetItem;
            show->setFlags((show->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            show->setCheckState(point.show ? Qt::Checked : Qt::Unchecked);
            targetTable_->setItem(row, 2, show);

            const double values[] = {point.amplitude,
                                     point.startTimeSeconds,
                                     point.endTimeSeconds,
                                     point.burstDurationSeconds,
                                     point.burstIntervalSeconds,
                                     point.pulseDurationSeconds,
                                     point.pulseIntervalSeconds};
            for (int i = 0; i < 7; ++i)
                targetTable_->setItem(row, i + 3, new QTableWidgetItem(QString::number(values[i])));
        }
    }
    reloadingPoints_ = false;
    if (targetTable_->rowCount() > 0)
        targetTable_->setCurrentCell(selectRow >= 0 ? selectRow : 0, 0);
    reloadEntries();
}

// One edited cell belongs to one point of one target; the rest of that target's
// points are carried through untouched.
void TreatmentLibraryDialog::commitTargetRow(int row) {
    if (row < 0 || row >= static_cast<int>(targetRows_.size())) return;
    const auto [targetIndex, pointIndex] = targetRows_[static_cast<std::size_t>(row)];
    if (targetIndex >= store_.targets().size()) return;
    const beam::gui::TreatmentTarget& target = store_.targets()[targetIndex];
    if (pointIndex >= target.points.size()) return;

    std::vector<beam::gui::TreatmentTargetPoint> points = target.points;
    beam::gui::TreatmentTargetPoint& point = points[pointIndex];
    if (const QTableWidgetItem* show = targetTable_->item(row, 2))
        point.show = show->checkState() == Qt::Checked;
    double* const fields[] = {&point.amplitude,
                              &point.startTimeSeconds,
                              &point.endTimeSeconds,
                              &point.burstDurationSeconds,
                              &point.burstIntervalSeconds,
                              &point.pulseDurationSeconds,
                              &point.pulseIntervalSeconds};
    for (int i = 0; i < 7; ++i) {
        const QTableWidgetItem* cell = targetTable_->item(row, i + 3);
        if (cell == nullptr) continue;
        bool ok = false;
        const double value = cell->text().toDouble(&ok);
        if (ok) *fields[i] = value;
    }
    store_.setTargetPoints(target.name, std::move(points));
}

QString TreatmentLibraryDialog::selectedTarget() const {
    if (targetTable_ == nullptr) return {};
    const int row = targetTable_->currentRow();
    if (row < 0) return {};
    const QTableWidgetItem* cell = targetTable_->item(row, 0);
    return cell == nullptr ? QString() : cell->text();
}

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



QString TreatmentLibraryDialog::selectedProtocol() const {
    if (protocolCombo_ == nullptr) return {};
    return protocolCombo_->currentText();
}

}  // namespace beam::app
