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

TreatmentLibraryDialog::TreatmentLibraryDialog(beam::gui::TreatmentTargetStore store,
                                               QWidget* parent)
    : QDialog(parent), store_(std::move(store)) {
    setWindowTitle(QStringLiteral("Manage targets and protocols"));
    setModal(true);
    resize(860, 520);

    auto* layout = new QVBoxLayout(this);
    auto* banner = new QLabel(
        QStringLiteral("These are shared across patients. Changes take effect when you save."),
        this);
    banner->setWordWrap(true);
    layout->addWidget(banner);

    auto* tabs = new QTabWidget(this);
    auto* targetsPage = new QWidget(tabs);
    buildTargetsTab(targetsPage);
    tabs->addTab(targetsPage, QStringLiteral("Targets"));
    auto* protocolsPage = new QWidget(tabs);
    buildProtocolsTab(protocolsPage);
    tabs->addTab(protocolsPage, QStringLiteral("Protocols"));
    layout->addWidget(tabs, 1);

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
    // name-only: the Treatment grid writes to the selected protocol's copy, so
    // nothing else can reach these values.
    pointCaption_ = new QLabel(page);
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

    connect(targetList_, &QListWidget::currentRowChanged, this,
            [this](int) { reloadTargetPoints(); });
    connect(pointTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        if (!reloadingPoints_) commitTargetPoints();
    });

    connect(addPoint, &QPushButton::clicked, this, [this] {
        const beam::gui::TreatmentTarget* target = store_.findTarget(selectedTarget().toStdString());
        if (target == nullptr) return;
        std::vector<beam::gui::TreatmentTargetPoint> points = target->points;
        // createSonicationUpdateTable.m appends by copying the previous row.
        points.push_back(points.empty() ? beam::gui::TreatmentTargetPoint{} : points.back());
        store_.setTargetPoints(target->name, std::move(points));
        reloadTargetPoints();
    });

    connect(removePoint, &QPushButton::clicked, this, [this] {
        const beam::gui::TreatmentTarget* target = store_.findTarget(selectedTarget().toStdString());
        const int row = pointTable_->currentRow();
        if (target == nullptr || row < 0 || row >= static_cast<int>(target->points.size())) return;
        if (target->points.size() <= 1) {
            QMessageBox::warning(this, QStringLiteral("Remove point"),
                                 QStringLiteral("A target must keep at least one point."));
            return;
        }
        std::vector<beam::gui::TreatmentTargetPoint> points = target->points;
        points.erase(points.begin() + row);
        store_.setTargetPoints(target->name, std::move(points));
        reloadTargetPoints();
    });

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
        const QString question =
            scheduled > 0
                ? QStringLiteral("Delete \"%1\"? %2 schedule entries use it and will be removed.")
                      .arg(name).arg(scheduled)
                : QStringLiteral("Delete \"%1\"?").arg(name);
        if (QMessageBox::question(this, QStringLiteral("Delete target"), question) !=
            QMessageBox::Yes)
            return;
        for (const auto& protocol : store_.protocols()) {
            std::vector<beam::gui::TreatmentProtocolEntry> kept;
            for (const auto& entry : protocol.entries)
                if (entry.target != name.toStdString()) kept.push_back(entry);
            if (kept.size() != protocol.entries.size())
                store_.setProtocolEntries(protocol.name, std::move(kept));
        }
        store_.removeTarget(name.toStdString());
        reloadTargets();
        reloadEntries();
    });
}

void TreatmentLibraryDialog::reloadTargetPoints() {
    if (pointTable_ == nullptr) return;
    const QString name = selectedTarget();
    const beam::gui::TreatmentTarget* target = store_.findTarget(name.toStdString());

    reloadingPoints_ = true;
    pointTable_->clearContents();
    pointTable_->setRowCount(target == nullptr ? 0 : static_cast<int>(target->points.size()));
    if (target != nullptr) {
        for (int row = 0; row < static_cast<int>(target->points.size()); ++row) {
            const beam::gui::TreatmentTargetPoint& point = target->points[static_cast<std::size_t>(row)];
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

    pointCaption_->setText(
        target == nullptr
            ? QStringLiteral("Template points")
            : QStringLiteral("Template points for \"%1\" - the values a protocol copies when "
                             "\"%1\" is added to it").arg(name));
}

// Reads the table back rather than tracking single cells: the table is small,
// and a partial write would leave the template disagreeing with what is shown.
void TreatmentLibraryDialog::commitTargetPoints() {
    const QString name = selectedTarget();
    const beam::gui::TreatmentTarget* target = store_.findTarget(name.toStdString());
    if (target == nullptr || pointTable_->rowCount() == 0) return;

    std::vector<beam::gui::TreatmentTargetPoint> points;
    points.reserve(static_cast<std::size_t>(pointTable_->rowCount()));
    for (int row = 0; row < pointTable_->rowCount(); ++row) {
        // Keep the stored value for any cell the edit did not supply.
        beam::gui::TreatmentTargetPoint point =
            row < static_cast<int>(target->points.size())
                ? target->points[static_cast<std::size_t>(row)]
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
    store_.setTargetPoints(name.toStdString(), std::move(points));
    reloadTargetPoints();
}

void TreatmentLibraryDialog::buildProtocolsTab(QWidget* page) {
    auto* layout = new QHBoxLayout(page);

    auto* left = new QVBoxLayout;
    protocolList_ = new QListWidget(page);
    left->addWidget(protocolList_, 1);
    auto* protocolButtons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("New"), page);
    auto* rename = new QPushButton(QStringLiteral("Rename..."), page);
    auto* remove = new QPushButton(QStringLiteral("Delete"), page);
    for (QPushButton* button : {add, rename, remove}) protocolButtons->addWidget(button);
    left->addLayout(protocolButtons);
    layout->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    entryTable_ = new QTableWidget(0, 3, page);
    entryTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Order"), QStringLiteral("Target"), QStringLiteral("Duration (s)")});
    entryTable_->horizontalHeader()->setStretchLastSection(true);
    entryTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    entryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    right->addWidget(entryTable_, 1);

    auto* entryButtons = new QHBoxLayout;
    entryTargetCombo_ = new QComboBox(page);
    auto* duration = new QDoubleSpinBox(page);
    duration->setRange(0.0, 36000.0);
    duration->setValue(30.0);
    duration->setSuffix(QStringLiteral(" s"));
    auto* addEntry = new QPushButton(QStringLiteral("Add entry"), page);
    auto* removeEntry = new QPushButton(QStringLiteral("Remove entry"), page);
    entryButtons->addWidget(entryTargetCombo_, 1);
    entryButtons->addWidget(duration);
    entryButtons->addWidget(addEntry);
    entryButtons->addWidget(removeEntry);
    right->addLayout(entryButtons);
    layout->addLayout(right, 2);

    connect(protocolList_, &QListWidget::currentRowChanged, this, [this](int) { reloadEntries(); });

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
        if (protocol.isEmpty() || row < 0) return;
        const auto* definition = store_.findProtocol(protocol.toStdString());
        if (definition == nullptr) return;
        std::vector<beam::gui::TreatmentProtocolEntry> kept;
        for (std::size_t i = 0; i < definition->entries.size(); ++i)
            if (static_cast<int>(i) != row) kept.push_back(definition->entries[i]);
        store_.setProtocolEntries(protocol.toStdString(), std::move(kept));
        reloadEntries();
    });
}

void TreatmentLibraryDialog::reloadTargets(const QString& select) {
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
    protocolList_->clear();
    for (const auto& protocol : store_.protocols())
        protocolList_->addItem(QString::fromStdString(protocol.name));
    const auto items = protocolList_->findItems(select, Qt::MatchExactly);
    protocolList_->setCurrentRow(items.isEmpty() ? (protocolList_->count() > 0 ? 0 : -1)
                                                 : protocolList_->row(items.first()));
    reloadEntries();
}

void TreatmentLibraryDialog::reloadEntries() {
    if (entryTable_ == nullptr) return;
    entryTable_->setRowCount(0);
    const QString protocol = selectedProtocol();
    if (protocol.isEmpty()) return;
    const auto* definition = store_.findProtocol(protocol.toStdString());
    if (definition == nullptr) return;
    entryTable_->setRowCount(static_cast<int>(definition->entries.size()));
    for (int row = 0; row < entryTable_->rowCount(); ++row) {
        const auto& entry = definition->entries[static_cast<std::size_t>(row)];
        entryTable_->setItem(row, 0, new QTableWidgetItem(QString::number(entry.order)));
        entryTable_->setItem(row, 1,
                             new QTableWidgetItem(QString::fromStdString(entry.target)));
        entryTable_->setItem(
            row, 2,
            new QTableWidgetItem(entry.durationSeconds.has_value()
                                     ? QString::number(*entry.durationSeconds, 'f', 1)
                                     : QStringLiteral("target default")));
    }
}

QString TreatmentLibraryDialog::selectedTarget() const {
    const QListWidgetItem* item = targetList_->currentItem();
    return item == nullptr ? QString() : item->text();
}

QString TreatmentLibraryDialog::selectedProtocol() const {
    const QListWidgetItem* item = protocolList_->currentItem();
    return item == nullptr ? QString() : item->text();
}

}  // namespace beam::app
