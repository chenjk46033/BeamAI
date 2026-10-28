#include "treatment_internal.hpp"

#include <QDialog>

namespace beam::app {

void WorkflowWindow::loadPatientData() {
    // The Treatment page keeps its own copy of the last message, and showMessage only mirrors
    if (sonicationCountdownLabel_ != nullptr) {
        sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
        sonicationCountdownLabel_->setStyleSheet(
            QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    }
    treatmentLog_.clear();
    visitSonication_.clear();
    editedVisits_.clear();
    // So loadTreatmentProtocol treats this as a fresh protocol and opens on this patient's
    lastLoadedProtocol_.clear();
    lastLoadedVisit_ = 0;
    currentSonication_ = 1;

    // The log and the visits come from the store; the patient directory now
    // holds nothing but exported reports.
    readPatientDataFromStore();
    loadVisitState();
    // After the load, which clears it, and whatever the file said: the registry is the
    beam::gui::Patient open;
    if (patients_.find(currentPatientId_, &open)) {
        treatmentLog_.setPatient(open.externalId, open.site,
                                 open.lastName.empty() && open.firstName.empty()
                                     ? std::string()
                                     : beam::gui::patientDisplayName(open));
    }
    if (treatmentProtocolCombo_ == nullptr) return;

    // Their Target List, which carries the parameters prescribed for them.
    // A patient with no targets of their own starts from the shipped list.
    if (targetStore_.targets().empty()) {
        const QString seed = shippedDataFile(QStringLiteral("treatment_targets.csv"));
        if (!seed.isEmpty()) {
            try {
                beam::gui::TreatmentTargetStore loaded = targetStore_;
                loaded.loadTargets(seed.toStdString());
                targetStore_ = std::move(loaded);
            } catch (const std::exception& error) {
                showMessage(QStringLiteral("The shipped Target List could not be read: %1")
                                .arg(error.what()),
                            true);
            }
        }
    }
    // Disown the grid BEFORE emptying the list: emptying it changes the selection, which saves
    targetSonications_.clear();
    currentTargetId_ = -1;
    gridTargetId_ = -1;
    stimParamModel_->setRows({});
    targetListWidget_->clear();
    for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
        addTargetListRow(QString::fromStdString(target.name));
    if (targetListWidget_->count() > 0) targetListWidget_->setCurrentRow(0);
    refreshVisitCombo();
    loadTreatmentProtocol(currentProtocolName());
}

void WorkflowWindow::savePatientData() {
    if (patientDataDirectory().isEmpty()) return;
    // The whole page, not just the log: the Target List and the protocol grid are edited on the
    saveTreatmentTargetStore(/*announce=*/false);
    saveVisitState();
}

// The install's own copy, read straight from data/ and never from the operator's runtime
void WorkflowWindow::loadShippedTargetStore() {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList seeds = {
        QDir(executableDir).filePath(QStringLiteral("../../../data/treatment_targets.csv")),
        QDir(executableDir).filePath(QStringLiteral("../../data/treatment_targets.csv")),
        QDir(executableDir).filePath(QStringLiteral("data/treatment_targets.csv")),
        QDir::current().filePath(QStringLiteral("data/treatment_targets.csv"))};
    for (const QString& seed : seeds) {
        if (!QFileInfo::exists(seed)) continue;
        try {
            shippedTargets_.loadTargets(seed.toStdString());
            return;
        } catch (const std::exception&) {
            // An unreadable seed costs the tooltips and the restore action, not the
        }
    }
}

QString WorkflowWindow::shippedTooltip(const QModelIndex& cell) const {
    if (!cell.isValid()) return QString();
    const std::optional<double> original = shippedValueFor(cell.column());
    if (!original.has_value()) return QString();
    const double now = cell.data(Qt::EditRole).toDouble();
    if (std::abs(now - *original) < 1e-12)
        return QStringLiteral("Shipped value %1, unchanged").arg(*original, 0, 'g', 6);
    return QStringLiteral("Shipped value %1 -- now %2.\nRight-click to restore the shipped "
                          "parameters.")
        .arg(*original, 0, 'g', 6)
        .arg(now, 0, 'g', 6);
}

std::optional<double> WorkflowWindow::shippedValueFor(int column) const {
    if (targetListWidget_ == nullptr || targetListWidget_->currentItem() == nullptr)
        return std::nullopt;
    const beam::gui::TreatmentTarget* shipped =
        shippedTargets_.findTarget(targetListName(targetListWidget_->currentItem()).toStdString());
    if (shipped == nullptr || shipped->points.empty()) return std::nullopt;
    const beam::gui::TreatmentTargetPoint& point = shipped->points.front();
    using Model = beam::gui_qt::StimParamTableModel;
    switch (column) {
        case Model::kAmplitude: return point.amplitude;
        case Model::kStartTime: return point.startTimeSeconds;
        case Model::kEndTime: return point.endTimeSeconds;
        case Model::kBurstDuration: return point.burstDurationSeconds;
        case Model::kBurstInterval: return point.burstIntervalSeconds;
        case Model::kPulseDuration: return point.pulseDurationSeconds;
        case Model::kPulseInterval: return point.pulseIntervalSeconds;
        // X/Y/Z are where this subject's target was placed, not something the install ships a
        default: return std::nullopt;
    }
}

// Parameters only. The placement stays: it belongs to this subject's registration, and the
void WorkflowWindow::restoreShippedParameters() {
    if (stimParamModel_ == nullptr || targetListWidget_ == nullptr) return;
    QListWidgetItem* item = targetListWidget_->currentItem();
    if (item == nullptr) return;
    const QString name = targetListName(item);
    const beam::gui::TreatmentTarget* shipped =
        shippedTargets_.findTarget(name.toStdString());
    if (shipped == nullptr || shipped->points.empty()) {
        showMessage(QStringLiteral("\"%1\" is not in the shipped Target List, so it has no "
                                   "original parameters to restore.")
                        .arg(name),
                    true);
        return;
    }
    const beam::gui::TreatmentTargetPoint& point = shipped->points.front();
    std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
    if (rows.empty()) return;
    QStringList changed;
    const auto put = [&changed](double& field, double shippedValue, const char* label) {
        if (std::abs(field - shippedValue) < 1e-12) return;
        changed << QStringLiteral("%1 %2 to %3")
                       .arg(QString::fromLatin1(label))
                       .arg(field, 0, 'g', 4)
                       .arg(shippedValue, 0, 'g', 4);
        field = shippedValue;
    };
    for (beam::gui_qt::StimParamRow& row : rows) {
        put(row.amplitude, point.amplitude, "Amplitude");
        put(row.startTime, point.startTimeSeconds, "Start Time");
        put(row.endTime, point.endTimeSeconds, "End Time");
        put(row.bd, point.burstDurationSeconds, "Burst Duration");
        put(row.bi, point.burstIntervalSeconds, "Burst Interval");
        put(row.pd, point.pulseDurationSeconds, "Pulse Duration");
        put(row.pi, point.pulseIntervalSeconds, "Pulse Interval");
    }
    if (changed.isEmpty()) {
        showMessage(QStringLiteral("\"%1\" already has its shipped parameters.").arg(name),
                    false);
        return;
    }
    stimParamModel_->setRows(std::move(rows));
    saveCurrentTargetSonications();
    saveTreatmentTargetStore();
    updateSonicationPlots();
    updateSonicationSafety();
    showMessage(QStringLiteral("%1 restored: %2.").arg(name, changed.join(QStringLiteral(", "))),
                false);
}

// Target parameters are prescribed per person, so the Target List lives with the patient.

// As shipped, never a runtime copy: a Target List is prescribed per patient, so there is no
QString WorkflowWindow::shippedDataFile(const QString& fileName) const {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList seeds = {
        QDir(executableDir).filePath(QStringLiteral("../../../data/") + fileName),
        QDir(executableDir).filePath(QStringLiteral("../../data/") + fileName),
        QDir(executableDir).filePath(QStringLiteral("data/") + fileName),
        QDir::current().filePath(QStringLiteral("data/") + fileName)};
    for (const QString& seed : seeds)
        if (QFileInfo::exists(seed)) return seed;
    return QString();
}

QString WorkflowWindow::locateDataFile(const QString& fileName) const {
    const QString user = QDir(treatmentDataDirectory()).filePath(fileName);
    if (QFileInfo::exists(user)) return user;
    return shippedDataFile(fileName);
}

void WorkflowWindow::loadTreatmentTargetStore() {
    const auto locate = [this](const QString& fileName) { return locateDataFile(fileName); };

    loadShippedTargetStore();
    // The shipped list seeds a patient who has none; the store overrides it.
    const QString targetsPath = locate(QStringLiteral("treatment_targets.csv"));
    const QString protocolsPath = locate(QStringLiteral("treatment_protocols.csv"));
    if (targetsPath.isEmpty() || protocolsPath.isEmpty()) {
        showMessage(QStringLiteral("Treatment target/protocol data was not found; using built-in "
                                   "defaults."),
                    true);
        return;
    }
    try {
        targetStore_.loadTargets(targetsPath.toStdString());
        targetStore_.loadProtocols(protocolsPath.toStdString());
        // The patient's own targets and log come from the store.
        readPatientDataFromStore();
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Treatment data could not be read: %1").arg(error.what()), true);
        return;
    }
    const std::vector<std::string> dangling = targetStore_.danglingTargetNames();
    if (!dangling.empty()) {
        showMessage(QStringLiteral("A protocol references %1 undefined target(s), starting with "
                                   "\"%2\".")
                        .arg(dangling.size())
                        .arg(QString::fromStdString(dangling.front())),
                    true);
    }
}

void WorkflowWindow::refreshProtocolCombo(const QString& select) {
    if (!treatmentProtocolCombo_ || targetStore_.protocols().empty()) return;
    const QSignalBlocker blocker(treatmentProtocolCombo_);
    treatmentProtocolCombo_->clear();
    for (const beam::gui::TreatmentProtocolDefinition& definition : targetStore_.protocols())
        treatmentProtocolCombo_->addItem(QString::fromStdString(definition.name));
    const int index = treatmentProtocolCombo_->findText(select);
    treatmentProtocolCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

// The protocol grid is the authority while it is on screen; this pushes it back so a
void WorkflowWindow::writeProtocolGridToStore() {
    // Writes the table back into the VISIT LOG and nowhere else, which is all
    if (!protocolModel_ || !treatmentProtocolCombo_) return;
    const std::string protocol = treatmentProtocolCombo_->currentText().toStdString();
    const int visit = currentVisitNumber();
    // An unanswered response shows as 0 in the grid, so a 0 coming back from a row that was
    const auto entered = [](std::optional<double> stored, double shown) -> std::optional<double> {
        if (!stored.has_value() && shown == 0.0) return std::nullopt;
        return shown;
    };
    for (std::size_t index = 0; index < protocolModel_->rows().size(); ++index) {
        const beam::gui_qt::TreatmentProtocolRow& source = protocolModel_->rows()[index];
        const int sequence = static_cast<int>(index) + 1;
        const beam::gui::TreatmentLogRow* existing = treatmentLog_.find(protocol, visit, sequence);
        if (existing == nullptr) continue;
        // Not through record(): that keeps the stored response on purpose, so a response handed
        treatmentLog_.setResponse(protocol, visit, sequence,
                                  entered(existing->responsePain, source.responsePain),
                                  entered(existing->responseMood, source.responseMood));
        treatmentLog_.setNotes(protocol, visit, sequence, source.notes.toStdString());

        // A delivered row records what was sent, so only the operator entries above can change
        if (beam::gui::TreatmentLog::delivered(*existing)) continue;
        beam::gui::TreatmentLogRow row = *existing;
        row.target = source.target.toStdString();
        row.amplitude = source.amplitude;
        row.endTimeSeconds = row.startTimeSeconds + source.duration;
        treatmentLog_.record(std::move(row));
    }
}

void WorkflowWindow::saveTreatmentTargetStore(bool announce) {
    writeProtocolGridToStore();
    writeTargetPointsToStore();
    const QString directory = treatmentDataDirectory();
    try {
        // Protocols are the site's library, shared across patients and still a
        // file. Everything belonging to a patient is in the store.
        targetStore_.saveProtocols(
            QDir(directory).filePath(QStringLiteral("treatment_protocols.csv")).toStdString());
        if (currentPatientId_ <= 0) {
            // Nobody is named, so there is nobody to file this against.
            if (!treatmentLog_.rows().empty()) {
                showMessage(QStringLiteral("Nothing was logged: the Case page has not said which "
                                           "patient is being treated."),
                            true);
            }
            return;
        }
        writeStore();
        // The log writes itself on every change, so there is no Save button to press and no way
        if (!announce) return;
        showMessage(QStringLiteral("Treatment record saved at %1")
                        .arg(QDateTime::currentDateTime().toString(
                            QStringLiteral("HH:mm:ss.zzz"))),
                    false);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Could not save treatment data: %1").arg(error.what()), true);
        return;
    }
}

beam::gui::TreatmentLogRow WorkflowWindow::rowForTarget(
    const std::string& target, std::optional<double> durationSeconds) const {
    const beam::gui::TreatmentTarget* defined = targetStore_.findTarget(target);
    const beam::gui::TreatmentTargetPoint* first =
        defined != nullptr && !defined->points.empty() ? &defined->points.front() : nullptr;
    beam::gui::TreatmentLogRow row;
    row.target = target;
    // The point amplitude, not the schedule's: BeamV0 stores a schedule amplitude but never
    row.amplitude = first != nullptr ? first->amplitude : 0.75;
    row.startTimeSeconds = 0.0;
    row.endTimeSeconds =
        durationSeconds.value_or(first != nullptr ? first->endTimeSeconds : 30.0);
    if (first != nullptr) {
        row.burstDurationSeconds = first->burstDurationSeconds;
        row.burstIntervalSeconds = first->burstIntervalSeconds;
        row.pulseDurationSeconds = first->pulseDurationSeconds;
        row.pulseIntervalSeconds = first->pulseIntervalSeconds;
    }
    row.fromPlan = false;
    return row;
}

// Adding a protocol works the way adding a sonication works: right-click the row it should
void WorkflowWindow::addProtocolToVisit(int afterSequence) {
    QStringList names;
    for (const beam::gui::TreatmentProtocolDefinition& known : targetStore_.protocols())
        names << QString::fromStdString(known.name);
    if (names.isEmpty()) {
        showMessage(QStringLiteral("There are no protocols to add."), true);
        return;
    }
    bool accepted = false;
    const QString picked = QInputDialog::getItem(
        this, QStringLiteral("Add protocol to plan"), QStringLiteral("Protocol:"), names,
        std::max(0, static_cast<int>(names.indexOf(currentProtocolName()))), false, &accepted);
    if (!accepted || picked.isEmpty()) return;
    const beam::gui::TreatmentProtocolDefinition* definition =
        targetStore_.findProtocol(picked.toStdString());
    if (definition == nullptr || definition->entries.empty()) {
        showMessage(QStringLiteral("\"%1\" schedules nothing to add.").arg(picked), true);
        return;
    }

    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    int after = afterSequence;
    for (const beam::gui::TreatmentProtocolEntry& entry : definition->entries) {
        beam::gui::TreatmentLogRow row = rowForTarget(entry.target, entry.durationSeconds);
        row.protocol = protocol;
        row.visit = visit;
        if (after >= 1 && treatmentLog_.canInsertAfter(protocol, visit, after)) {
            treatmentLog_.insertAfter(protocol, visit, after, std::move(row));
            ++after;  // the next one follows the one just placed
        } else {
            treatmentLog_.append(std::move(row));
            after = 0;  // the rest follow it at the end
        }
    }
    markVisitEdited(protocolName, visit);
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
    showMessage(QStringLiteral("%1 sonication(s) of \"%2\" added to visit %3.")
                    .arg(definition->entries.size())
                    .arg(picked)
                    .arg(visit),
                false);
}

void WorkflowWindow::addRowToVisit() {
    QStringList names;
    for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
        names << QString::fromStdString(target.name);
    if (names.isEmpty()) {
        showMessage(QStringLiteral("The Target List is empty; there is nothing to add."), true);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add rows"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("Select one or more targets. Ctrl or Shift picks several."), &dialog));

    auto* list = new QListWidget(&dialog);
    applyItemViewPalette(list);
    list->addItems(names);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setCurrentRow(0);
    layout->addWidget(list);

    auto* countRow = new QHBoxLayout;
    countRow->addWidget(new QLabel(QStringLiteral("Rows per target:"), &dialog));
    auto* count = new QSpinBox(&dialog);
    count->setRange(1, 50);
    countRow->addWidget(count);
    countRow->addStretch();
    layout->addLayout(countRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    // Row order, not selection order, so the plan reads as the list does.
    QStringList chosen;
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->isSelected()) chosen << list->item(row)->text();
    if (chosen.isEmpty()) return;

    const QString protocolName = currentProtocolName();
    const int visit = currentVisitNumber();
    int added = 0;
    for (const QString& name : chosen) {
        for (int copy = 0; copy < count->value(); ++copy) {
            beam::gui::TreatmentLogRow row = rowForTarget(name.toStdString(), std::nullopt);
            row.protocol = protocolName.toStdString();
            row.visit = visit;
            treatmentLog_.append(std::move(row));
            ++added;
        }
    }
    markVisitEdited(protocolName, visit);
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
    showMessage(added == 1
                    ? QStringLiteral("1 row added to visit %1.").arg(visit)
                    : QStringLiteral("%1 rows added to visit %2.").arg(added).arg(visit),
                false);
}

}  // namespace beam::app
