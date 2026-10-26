#include "treatment_internal.hpp"

namespace beam::app {

void WorkflowWindow::loadTreatmentProtocol(const QString& protocolName) {
    // The table on this page is the VISIT LOG, not the protocol.
    const std::string protocol = protocolName.toStdString();
    // Visit numbers belong to a protocol.
    if (protocolName != lastLoadedProtocol_ && visitNumberCombo_ != nullptr) {
        const int opening = visitForProtocol(protocol);
        selectVisit(opening);
    }
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentProtocolDefinition* definition =
        targetStore_.findProtocol(protocol);

    // Nothing is seeded here.
    std::vector<beam::gui_qt::TreatmentProtocolRow> protocolRows;
    QStringList targetNames;
    for (const beam::gui::TreatmentLogRow& logged : treatmentLog_.rowsFor(protocol, visit)) {
        beam::gui_qt::TreatmentProtocolRow row;
        row.number = logged.sequence;
        row.target = QString::fromStdString(logged.target);
        row.duration = logged.endTimeSeconds - logged.startTimeSeconds;
        row.amplitude = logged.amplitude;
        row.parameters =
            QString::fromStdString(beam::gui::TreatmentLog::parameterString(logged));
        // Time of day to the millisecond in the cell, the whole stamp in the tooltip: a visit
        const auto timeOfDay = [](const std::string& stamp) {
            const QDateTime when =
                QDateTime::fromString(QString::fromStdString(stamp), Qt::ISODateWithMs);
            return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss.zzz")) : QString();
        };
        row.startedAt = timeOfDay(logged.startedAt);
        row.endedAt = timeOfDay(logged.endedAt);
        row.startedAtFull = QString::fromStdString(logged.startedAt);
        row.endedAtFull = QString::fromStdString(logged.endedAt);
        row.statusCode = logged.statusCode;
        row.responsePain = logged.responsePain.value_or(0.0);
        row.responseMood = logged.responseMood.value_or(0.0);
        row.notes = QString::fromStdString(logged.notes);
        row.delivered = beam::gui::TreatmentLog::delivered(logged);
        row.sham = logged.sham;
        row.aborted = beam::gui::TreatmentLog::stopped(logged);
        protocolRows.push_back(row);
        if (!targetNames.contains(row.target)) targetNames.push_back(row.target);
    }

    const double defaultAmplitude = protocolRows.empty() ? 0.75 : protocolRows.front().amplitude;
    const double defaultDuration = protocolRows.empty() ? 30.0 : protocolRows.front().duration;
    protocolModel_->setRows(std::move(protocolRows));

    // Before a visit is created there is nothing to show: not rows, not the blank row the model
    const bool visitCreated = visitExists(protocolName, visit);
    // The page header carries the visit now that the case page does not ask for one.
    ui_->visitHeader->setText(visitCreated ? QStringLiteral("Visit: %1").arg(visit)
                                           : QStringLiteral("Visit: —"));
    if (protocolView_ != nullptr) {
        protocolView_->setVisible(visitCreated);
        if (protocolEmptySpacer_ != nullptr) {
            protocolEmptySpacer_->changeSize(0, 0, QSizePolicy::Minimum,
                                             visitCreated ? QSizePolicy::Minimum
                                                          : QSizePolicy::Expanding);
            if (protocolView_->parentWidget() != nullptr &&
                protocolView_->parentWidget()->layout() != nullptr)
                protocolView_->parentWidget()->layout()->invalidate();
        }
    }

    beam::gui_qt::StimParamRow stimRow;
    stimRow.order = 1;
    stimRow.show = true;
    stimRow.amplitude = defaultAmplitude;
    stimRow.startTime = 0.0;
    stimRow.endTime = defaultDuration;
    stimParamModel_->setRows({stimRow});

    // A different protocol is a different set of sonications, so the per-target records start
    targetSonications_.clear();
    currentTargetId_ = -1;
    gridTargetId_ = -1;

    // The Target List is BeamV0's twelve and does not grow to fit a protocol.
    QStringList undefined;
    for (const QString& name : targetNames) {
        bool known = false;
        for (int i = 0; i < targetListWidget_->count() && !known; ++i)
            known = targetListName(targetListWidget_->item(i)) == name;
        if (!known) undefined.push_back(name);
    }
    if (!undefined.isEmpty()) {
        showMessage(QStringLiteral("Protocol \"%1\" schedules %2 target(s) the Target List does not "
                                   "define (%3); those sonications cannot be placed.")
                        .arg(protocolName)
                        .arg(undefined.size())
                        .arg(undefined.join(QStringLiteral(", "))),
                    true);
    }

    selectSonicationRow(0);
    switchToSelectedTarget();
    // A protocol opens on its first scheduled sonication.
    refreshVisitCombo();
    // Where this visit had got to.
    const QString key = visitKey(protocolName, visit);
    const bool sameplace = protocolName == lastLoadedProtocol_ && visit == lastLoadedVisit_;
    int keep = 1;
    if (sameplace) {
        keep = currentSonication_;
    } else {
        // Where the visit actually got to.
        int delivered = 0;
        int firstFree = 0;
        for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rowsFor(protocol, visit)) {
            if (beam::gui::TreatmentLog::delivered(row)) delivered = row.sequence;
            else if (firstFree == 0) firstFree = row.sequence;
        }
        // A visit that has delivered nothing starts at its first sonication, whatever was
        if (delivered == 0) keep = 1;
        else if (visitSonication_.contains(key)) keep = visitSonication_.value(key);
        // The first row still to run, which is not always one past the last delivered:
        else keep = firstFree > 0 ? firstFree : delivered + 1;
    }
    // A sonication in progress keeps the number, whatever else this reload concluded.
    if (openLogSequence_ > 0) {
        keep = openLogSequence_;
    } else {
        // Wherever that landed, it must not be a row that has already run while another has
        const std::vector<beam::gui::TreatmentLogRow> rows =
            treatmentLog_.rowsFor(protocol, visit);
        const std::size_t at = static_cast<std::size_t>(keep) - 1;
        if (keep >= 1 && at < rows.size() && beam::gui::TreatmentLog::delivered(rows[at])) {
            for (const beam::gui::TreatmentLogRow& row : rows) {
                if (beam::gui::TreatmentLog::delivered(row)) continue;
                keep = row.sequence;
                break;
            }
        }
    }
    lastLoadedProtocol_ = protocolName;
    lastLoadedVisit_ = visit;
    QMetaObject::invokeMethod(
        this, [this] { precheckPendingSonications(); }, Qt::QueuedConnection);
    selectScheduledSonication(keep);
    updateBestTargets();
    updatePlanBlocksLabel();
    // The checkbox belongs to the visit, so it follows the one just opened.
    if (autoRankCheckBox_ != nullptr) {
        const QSignalBlocker block(autoRankCheckBox_);
        autoRankCheckBox_->setChecked(automaticRankingOn());
    }
}

void WorkflowWindow::selectSonicationRow(int row) {
    if (!stimParamView_ || stimParamModel_->rows().empty()) return;
    const int clamped = std::clamp(row, 0, static_cast<int>(stimParamModel_->rows().size()) - 1);
    stimParamView_->selectRow(clamped);
    updateSonicationPlots();
}

int WorkflowWindow::selectedSonicationRow() const {
    if (!stimParamView_ || stimParamModel_->rows().empty()) return -1;
    const QModelIndex current = stimParamView_->selectionModel()->currentIndex();
    if (!current.isValid()) return 0;
    return std::clamp(current.row(), 0, static_cast<int>(stimParamModel_->rows().size()) - 1);
}

// The sonication grid holds one row in normal use, so a fixed slice of the body left a band of
void WorkflowWindow::updateStimGridHeight() {
    if (!stimParamView_) return;
    const int rows = stimParamModel_->rowCount();
    const int rowHeight = rows > 0 ? stimParamView_->rowHeight(0) : 26;
    const int wanted = stimParamView_->horizontalHeader()->height() + rows * rowHeight +
                       2 * stimParamView_->frameWidth();
    stimParamView_->setFixedHeight(std::clamp(wanted, 56, 220));
}

// Copies the selected target's placed position into the grid row's X/Y/Z, so the grid always
void WorkflowWindow::switchToSelectedTarget() {
    if (!stimParamModel_ || !targetListWidget_) return;
    saveCurrentTargetSonications();

    const QListWidgetItem* item = targetListWidget_->currentItem();
    currentTargetId_ = targetListId(item);
    if (currentTargetId_ < 0) {
        gridTargetId_ = -1;
        stimParamModel_->setTargetName(QString());
        return;
    }

    const auto stored = targetSonications_.constFind(currentTargetId_);
    if (stored != targetSonications_.constEnd() && !stored.value().empty()) {
        stimParamModel_->setRows(stored.value());
    } else {
        // First visit to this target: every point the Target List defines for it, not just one.
        const QString name = targetListName(item);

        // The protocol contributes the duration and nothing else:
        std::optional<double> scheduledDuration;
        const std::vector<beam::gui_qt::TreatmentProtocolRow>& planned = protocolModel_->rows();
        if (currentSonication_ >= 1 && currentSonication_ <= static_cast<int>(planned.size())) {
            const beam::gui_qt::TreatmentProtocolRow& current =
                planned[static_cast<std::size_t>(currentSonication_) - 1];
            if (current.target == name) scheduledDuration = current.duration;
        }
        if (!scheduledDuration.has_value()) {
            for (const beam::gui_qt::TreatmentProtocolRow& protocolRow : planned) {
                if (protocolRow.target == name) {
                    scheduledDuration = protocolRow.duration;
                    break;
                }
            }
        }

        std::vector<beam::gui_qt::StimParamRow> rows;
        // No entry means this protocol does not schedule this target.
        for (const beam::gui::TreatmentTargetPoint& point :
             targetStore_.pointsForSonication(name.toStdString(), scheduledDuration)) {
            beam::gui_qt::StimParamRow row;
            row.order = point.order;
            row.show = point.show;
            row.amplitude = point.amplitude;
            row.startTime = point.startTimeSeconds;
            row.endTime = point.endTimeSeconds;
            row.bd = point.burstDurationSeconds;
            row.bi = point.burstIntervalSeconds;
            row.pd = point.pulseDurationSeconds;
            row.pi = point.pulseIntervalSeconds;
            row.x = targetMm_.x();
            row.y = targetMm_.y();
            row.z = targetMm_.z();
            rows.push_back(row);
        }
        // Either the Target List does not define this target, or this protocol does not
        if (rows.empty()) {
            beam::gui_qt::StimParamRow row;
            row.order = 1;
            row.show = true;
            if (scheduledDuration) row.endTime = *scheduledDuration;
            row.x = targetMm_.x();
            row.y = targetMm_.y();
            row.z = targetMm_.z();
            rows.push_back(row);
        }
        stimParamModel_->setRows(std::move(rows));
    }
    // A target the operator has never placed follows the array centre, which moves as
    if (item && !item->data(Qt::UserRole).isValid() && !targetMm_.isZero()) {
        std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
        // Every point of an unplaced target, not just the first -- the list item carries one
        for (beam::gui_qt::StimParamRow& row : rows) {
            row.x = targetMm_.x();
            row.y = targetMm_.y();
            row.z = targetMm_.z();
        }
        stimParamModel_->setRows(std::move(rows));
    }
    // The grid now holds this target, so it may be cached against it -- and only now may the
    gridTargetId_ = currentTargetId_;
    stimParamModel_->setTargetName(targetListName(item));
    stimParamView_->selectRow(0);
    saveCurrentTargetSonications();
    updateSonicationPlots();
}

// Port of registerCurrentTransducerPostion.m's tail: once the array is registered to the
void WorkflowWindow::applyArrayCentreToAllTargets(const Eigen::Vector3d& centreMm) {
    if (!stimParamModel_ || !targetListWidget_) return;
    for (auto it = targetSonications_.begin(); it != targetSonications_.end(); ++it) {
        for (beam::gui_qt::StimParamRow& row : it.value()) {
            row.x = centreMm.x();
            row.y = centreMm.y();
            row.z = centreMm.z();
        }
    }
    // The list items carry the same position for Move To Target.
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        targetListWidget_->item(i)->setData(
            Qt::UserRole, QList<QVariant>{centreMm.x(), centreMm.y(), centreMm.z()});
    }
    // And the grid currently on screen, which may not have been saved yet.
    if (!stimParamModel_->rows().empty()) {
        std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
        for (beam::gui_qt::StimParamRow& row : rows) {
            row.x = centreMm.x();
            row.y = centreMm.y();
            row.z = centreMm.z();
        }
        stimParamModel_->setRows(std::move(rows));
        saveCurrentTargetSonications();
    }
}

void WorkflowWindow::saveCurrentTargetSonications() {
    // gridTargetId_ guards the window between choosing a target and the grid being refilled for
    if (currentTargetId_ >= 0 && currentTargetId_ == gridTargetId_ && stimParamModel_ &&
        !stimParamModel_->rows().empty()) {
        targetSonications_.insert(currentTargetId_, stimParamModel_->rows());
    }
}

// BeamV0 checks only the sonication about to go out, so a plan can carry a row that cannot be
void WorkflowWindow::precheckPendingSonications() {
    if (protocolModel_ == nullptr || prechecking_) return;
    prechecking_ = true;
    const auto done = qScopeGuard([this] { prechecking_ = false; });
    std::vector<beam::gui_qt::TreatmentProtocolRow> rows = protocolModel_->rows();
    QStringList blockedRows;
    bool changed = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        beam::gui_qt::TreatmentProtocolRow& row = rows[i];
        QStringList found;
        // Delivered rows are a record, not a plan; the current one already has the banner above
        const bool isCurrent = static_cast<int>(i) + 1 == currentSonication_;
        if (!row.delivered && !isCurrent) {
            // Composed the way the grid would be: the duration from this row, everything else
            const beam::gui::TreatmentLogRow planned =
                rowForTarget(row.target.toStdString(), row.duration);
            const beam::safety::SonicationSafetyParams params =
                beam::gui::stimParamsFromTableRow(planned.pulseDurationSeconds,
                                                  planned.pulseIntervalSeconds,
                                                  planned.burstDurationSeconds,
                                                  planned.burstIntervalSeconds, planned.amplitude,
                                                  planned.startTimeSeconds,
                                                  planned.endTimeSeconds);
            const beam::safety::SafetyReport report =
                beam::safety::checkSonicationParameters({params});
            for (const std::string& message : report.messages)
                found << QString::fromStdString(message);
            if (!found.isEmpty()) blockedRows << QString::number(i + 1);
        }
        if (row.blockedBy != found) {
            row.blockedBy = found;
            changed = true;
        }
    }
    if (changed) protocolModel_->setRows(std::move(rows));

    if (sonicationPrecheckLabel_ == nullptr) return;
    if (blockedRows.isEmpty()) {
        sonicationPrecheckLabel_->hide();
        return;
    }
    sonicationPrecheckLabel_->setText(
        QStringLiteral("%1 later sonication(s) would be blocked as the plan stands: row %2. "
                       "Hover the row for the rule.")
            .arg(blockedRows.size())
            .arg(blockedRows.join(QStringLiteral(", "))));
    sonicationPrecheckLabel_->show();
}

// Filed as a note on the row it was aimed at, stamped and worded like every other note, so a
void WorkflowWindow::recordRefusedSonication(bool sham) {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row =
        treatmentLog_.find(protocol, visit, currentSonication_);
    if (row == nullptr) return;
    const QString entry =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
        QStringLiteral("  %1 refused: %2")
            .arg(sham ? QStringLiteral("Sham") : QStringLiteral("Sonicate"),
                 sonicationSafetyMessages_.join(QStringLiteral("; ")));
    QString notes = QString::fromStdString(row->notes);
    if (!notes.isEmpty()) notes += QLatin1Char('\n');
    notes += entry;
    treatmentLog_.setNotes(protocol, visit, currentSonication_, notes.toStdString());
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
}

// Port of SonicateButtonPushed.m's last act: if mod(i,4)==0 RunCorrectionButtonPushed(app,
void WorkflowWindow::remeasureCouplingIfDue(int delivered) {
    if (delivered <= 0 || delivered % kCouplingRecheckEvery != 0) return;
    if (runCorrectionButton_ == nullptr) return;
    showMessage(QStringLiteral("Sonication %1 delivered. Re-measuring coupling, as every %2 "
                               "sonications.")
                    .arg(delivered)
                    .arg(kCouplingRecheckEvery),
                false);
    // Queued: this is reached from the countdown timer finishing a delivery, and the
    QMetaObject::invokeMethod(
        this,
        [this, delivered] {
            autoCouplingRecheck_ = true;
            startCorrectionMeasurement([this, delivered] {
            // Cleared here, not on the way out: applyCorrectionResult reads the flag, and it
            autoCouplingRecheck_ = false;
            // The Coupling page owns the verdict; mirror it the way the page does, so the
            couplingCheckPassed_ = correctionCheckPassed_;
            if (!correctionCheckPassed_) {
                showMessage(
                    QStringLiteral("Coupling has fallen below the threshold after sonication "
                                   "%1. Readjust coupling or add coupling gel over uncoupled "
                                   "elements, then run the measurement again on the Coupling "
                                   "page.")
                        .arg(delivered),
                    true);
            }
            // Rule 320 reads the new transmission.
            updateSonicationSafety();
            refresh();
            if (correctionCheckPassed_) {
                showMessage(
                    QStringLiteral("Coupling re-measured after sonication %1: transmission %2%. "
                                   "Treatment continues.")
                        .arg(delivered)
                        .arg(std::clamp(
                            static_cast<int>(std::lround(transmissionAmplitude_ * 100.0)), 0,
                            100)),
                    false);
            }
            });
        },
        Qt::QueuedConnection);
}

void WorkflowWindow::updateSonicationSafety() {
    if (sonicationSafetyLabel_ == nullptr || stimParamModel_ == nullptr) return;
    if (checkingSonicationSafety_) return;
    checkingSonicationSafety_ = true;
    const auto done = qScopeGuard([this] { checkingSonicationSafety_ = false; });
    std::vector<beam::safety::SonicationSafetyParams> sonications;
    for (const beam::gui_qt::StimParamRow& row : stimParamModel_->rows()) {
        sonications.push_back(beam::gui::stimParamsFromTableRow(row.pd, row.pi, row.bd, row.bi,
                                                                row.amplitude, row.startTime,
                                                                row.endTime));
    }
    const beam::safety::SafetyReport report =
        beam::safety::checkSonicationParameters(sonications);
    sonicationSafetyMessages_.clear();
    for (const std::string& message : report.messages)
        sonicationSafetyMessages_ << QString::fromStdString(message);
    sonicationSafetyPass_ = report.pass;

    // Which cells each broken rule is about.
    using Model = beam::gui_qt::StimParamTableModel;
    using Kind = beam::safety::ViolationKind;
    std::set<std::pair<int, int>> unsafe;
    for (const beam::safety::Violation& violation : report.violations) {
        const int row = violation.sonicationIndex;
        const auto blame = [&unsafe, row](std::initializer_list<int> columns) {
            for (const int column : columns) unsafe.insert({row, column});
        };
        switch (violation.kind) {
            case Kind::kIsppa:
            case Kind::kMechanicalIndex:
                // Both are functions of amplitude alone.
                blame({Model::kAmplitude});
                break;
            case Kind::kPulseDurationOverInterval:
                blame({Model::kPulseDuration, Model::kPulseInterval});
                break;
            case Kind::kBurstDurationOverInterval:
                blame({Model::kBurstDuration, Model::kBurstInterval});
                break;
            case Kind::kPulseIntervalOverBurst:
                blame({Model::kPulseInterval, Model::kBurstDuration});
                break;
            case Kind::kBurstIntervalOverDuration:
                // "Duration" is not a column; Start and End are what make it.
                blame({Model::kBurstInterval, Model::kStartTime, Model::kEndTime});
                break;
            case Kind::kIsptaAll:
            case Kind::kIsptaBurstInterval:
                // Time-averaged intensity reads the amplitude and every term of the duty cycle.
                blame({Model::kAmplitude, Model::kBurstDuration, Model::kBurstInterval,
                       Model::kPulseDuration, Model::kPulseInterval});
                break;
        }
    }

    // The source's own two preconditions, in its own words.
    if (workflow_.state(beam::gui::WorkflowStage::Registration).status !=
        beam::gui::WorkflowStatus::Complete) {
        sonicationSafetyMessages_ << QStringLiteral(
            "[Rule 310] Registration incomplete, please complete registration tab");
        sonicationSafetyPass_ = false;
    }
    if (transmissionAmplitude_ < kCouplingThreshold) {
        sonicationSafetyMessages_ << QStringLiteral(
            "[Rule 320] Through transmit too low, please complete correction tab");
        sonicationSafetyPass_ = false;
    }

    if (sonicationSafetyPass_) {
        // The source's own word for a clean report.
        sonicationSafetyLabel_->setText(
            QStringLiteral("<b style='color:#5ec46a'>&#9679; Sonication Online</b>"));
        sonicationSafetyLabel_->setStyleSheet(QStringLiteral(
            "QLabel { color: #dcebef; background: #12301c; border: 1px solid #2f6b3f; "
            "border-radius: 4px; padding: 5px 10px; }"));
    } else {
        QStringList shown;
        for (const QString& message : sonicationSafetyMessages_)
            shown << message.toHtmlEscaped();
        sonicationSafetyLabel_->setText(
            QStringLiteral("<b style='color:#ff554a'>&#9679; Sonication blocked</b> &nbsp; %1")
                .arg(shown.join(QStringLiteral("<br>"))));
        sonicationSafetyLabel_->setStyleSheet(QStringLiteral(
            "QLabel { color: #ffd9d6; background: #351a19; border: 1px solid #8e1b1b; "
            "border-radius: 4px; padding: 5px 10px; }"));
    }
    stimParamModel_->setUnsafeCells(unsafe);
    precheckPendingSonications();
    // Re-applies the gate above to whatever buttons are on the current row.
    setSonicateEnabled(sonicateEnabled_);
}

void WorkflowWindow::updateSonicationPlots() {
    if (!pulsePlotView_ || stimParamModel_->rows().empty()) return;
    // getCurrentShownSonication is 1-based, and is what BeamV0 plots.
    const int shown = std::clamp(stimParamModel_->currentShownRow(), 1,
                                 static_cast<int>(stimParamModel_->rows().size()));
    const beam::gui_qt::StimParamRow& r = stimParamModel_->rows()[static_cast<std::size_t>(shown - 1)];

    pulsePlotView_->setData(beam::gui::computePulseWaveformPlot(r.pd, r.pi, r.amplitude),
                            QStringLiteral("Pulse waveform"), QStringLiteral("Time (s)"));
    burstPlotView_->setData(beam::gui::computeBurstWaveformPlot(r.bd, r.bi, r.pd, r.pi, r.amplitude),
                            QStringLiteral("Burst waveform"), QStringLiteral("Time (s)"));

    // An infeasible row (duration shorter than one burst) throws rather than producing a
    beam::stimulation::SonicationSchedule schedule;
    schedule.startTime = r.startTime;
    schedule.endTime = r.endTime;
    schedule.bi = r.bi;
    schedule.bd = r.bd;
    schedule.pi = r.pi;
    schedule.pd = r.pd;
    try {
        timelineView_->setData(beam::stimulation::computeSonicationEventTimeline({schedule}), r.amplitude,
                               r.endTime - r.startTime);
    } catch (const std::exception&) {
        timelineView_->setData({}, r.amplitude, r.endTime - r.startTime);
    }
}

// BeamV0 re-ranks after every sonication, silently.
void WorkflowWindow::editRowNotes(int sequence) {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row = treatmentLog_.find(protocol, visit, sequence);
    if (row == nullptr) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Notes -- sonication %1 (%2)")
                              .arg(sequence)
                              .arg(QString::fromStdString(row->target)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* writtenLabel = new QLabel(QStringLiteral("Written so far:"), &dialog);
    layout->addWidget(writtenLabel);
    // What is already there is read: a record is added to, not rewritten, and a box that let
    auto* written = new QPlainTextEdit(QString::fromStdString(row->notes), &dialog);
    written->setReadOnly(true);
    written->setMinimumSize(520, 150);
    layout->addWidget(written, 1);
    layout->addWidget(new QLabel(QStringLiteral("Add a note:"), &dialog));
    auto* entry = new QPlainTextEdit(&dialog);
    entry->setMinimumHeight(74);
    entry->setPlaceholderText(QStringLiteral("Stamped with the date and time when you press OK"));
    layout->addWidget(entry, 0);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    entry->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;

    const QString typed = entry->toPlainText().trimmed();
    if (typed.isEmpty()) return;  // the box was opened and thought better of
    // The stamp is put on by the application as the note is filed, so every entry carries one
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QStringList entryLines = typed.split(QLatin1Char('\n'));
    entryLines.first() = stamp + QStringLiteral("  ") + entryLines.first().trimmed();
    QString kept = QString::fromStdString(row->notes);
    if (!kept.isEmpty()) kept += QLatin1Char('\n');
    kept += entryLines.join(QStringLiteral("\n"));
    treatmentLog_.setNotes(protocol, visit, sequence, kept.toStdString());
    // Reload before saving: the save writes the table back into the log.
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
}

bool WorkflowWindow::automaticRankingOn() const {
    return !rankingOffVisits_.contains(visitKey(currentProtocolName(), currentVisitNumber()));
}

// BeamV0's three triggers: after a sonication, after a sham, and on a visit change.
void WorkflowWindow::applyTargetRankingIfAutomatic() {
    if (applyingRanking_ || !automaticRankingOn()) return;
    applyTargetRanking(/*quiet=*/true);
}

void WorkflowWindow::applyTargetRanking(bool quiet) {
    if (applyingRanking_) return;
    // The scores it ranks on are read from the log, and a score just typed is still only in the
    writeProtocolGridToStore();
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    std::vector<beam::gui::PlanRow> plan;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rowsFor(protocol, visit)) {
        beam::gui::PlanRow entry;
        entry.sequence = row.sequence;
        entry.target = row.target;
        entry.duration = row.endTimeSeconds - row.startTimeSeconds;
        entry.response = beam::gui::TreatmentLog::responseTotal(row);
        entry.delivered = beam::gui::TreatmentLog::delivered(row);
        plan.push_back(std::move(entry));
    }
    bool anyTreatmentRow = false;
    for (const beam::gui::PlanRow& row : plan)
        if (!row.delivered && row.duration >= 60.0) anyTreatmentRow = true;
    if (!anyTreatmentRow) {
        if (!quiet)
            showMessage(QStringLiteral("Nothing to rank: this visit has no treatment sonication "
                                       "left to run. Ranking repoints the long sonications, not "
                                       "the screening ones."),
                        true);
        return;
    }
    const std::vector<beam::gui::RankedTarget> changes = beam::gui::rankTreatmentRows(plan);
    if (changes.empty()) {
        if (!quiet)
            showMessage(
                QStringLiteral("Target ranking: the plan already runs the best-scoring targets."),
                false);
        return;
    }

    applyingRanking_ = true;
    int moves = 0;
    for (const beam::gui::RankedTarget& change : changes) {
        // Not the one going out: it is already on its way to the hardware.
        if (change.sequence == openLogSequence_) continue;
        const beam::gui::TreatmentLogRow* was =
            treatmentLog_.find(protocol, visit, change.sequence);
        if (was == nullptr) continue;
        const beam::gui::TreatmentLogRow& before = *was;
        // The duration is the plan's and stays; everything else comes from the target the row
        beam::gui::TreatmentLogRow row =
            rowForTarget(change.target, before.endTimeSeconds - before.startTimeSeconds);
        if (treatmentLog_.replacePlanned(protocol, visit, change.sequence, std::move(row)))
            ++moves;
    }
    const int moved = static_cast<int>(moves);
    if (moved > 0) {
        loadTreatmentProtocol(protocolName);
        saveTreatmentTargetStore();
    }
    applyingRanking_ = false;
    if (moved > 0 && !quiet)
        showMessage(QStringLiteral("Target ranking repointed %1 sonication(s) still to run.")
                        .arg(moved),
                    false);
}

void WorkflowWindow::updatePlanBlocksLabel() {
    if (planBlocksLabel_ == nullptr || protocolModel_ == nullptr) return;
    std::vector<beam::gui::PlanRow> plan;
    for (const beam::gui_qt::TreatmentProtocolRow& row : protocolModel_->rows()) {
        beam::gui::PlanRow entry;
        entry.sequence = row.number;
        entry.target = row.target.toStdString();
        entry.duration = row.duration;
        plan.push_back(std::move(entry));
    }
    QStringList pairs;
    QStringList orphans;
    for (const beam::gui::PlanBlock& block : beam::gui::planBlocks(plan)) {
        if (block.screening) continue;
        if (block.feedFirst == 0) {
            orphans << QStringLiteral("%1-%2").arg(block.first).arg(block.last);
            continue;
        }
        pairs << QStringLiteral("%1-%2 screen \u2192 %3-%4 treat")
                     .arg(block.feedFirst).arg(block.feedLast).arg(block.first).arg(block.last);
    }
    QString text;
    if (!pairs.isEmpty()) text = pairs.join(QStringLiteral("   \u00b7   "));
    if (!orphans.isEmpty()) {
        if (!text.isEmpty()) text += QStringLiteral("   \u00b7   ");
        text += QStringLiteral("rows %1 have no screening above them, so ranking leaves them alone")
                    .arg(orphans.join(QStringLiteral(", ")));
    }
    planBlocksLabel_->setText(text.isEmpty() ? QString()
                                             : QStringLiteral("Read from the durations:  %1").arg(text));
    planBlocksLabel_->setToolTip(planBlocksLabel_->text());
    planBlocksLabel_->setVisible(!text.isEmpty());
}

void WorkflowWindow::updateBestTargets() {
    if (!bestTargetsLabel_ || !protocolModel_) return;
    const beam::gui::AccFlag flag = accFlagCombo_->currentIndex() == 1   ? beam::gui::AccFlag::kScc
                                    : accFlagCombo_->currentIndex() == 2 ? beam::gui::AccFlag::kAmcc
                                                                          : beam::gui::AccFlag::kOther;
    const beam::gui::BestTargets best = protocolModel_->computeBestTargets(flag);
    QStringList names;
    for (const std::string& name : best.name) names << QString::fromStdString(name);
    bestTargetsLabel_->setText(names.isEmpty() ? QStringLiteral("--")
                                               : names.join(QStringLiteral(", ")));
}

void WorkflowWindow::updateExampleTargetImage() {
    if (!exampleTargetImage_) return;
    // BeamV0.mlapp's own SonicationTab/ExampleTargets images, compiled in through ...
    const QString stem = accFlagCombo_->currentIndex() == 1   ? QStringLiteral("SCC1")
                         : accFlagCombo_->currentIndex() == 2 ? QStringLiteral("aMCC1")
                                                              : QStringLiteral("ACC");
    // ACC's own third view is the white-matter reference rather than a coronal one -- the file
    const QStringList suffixes = stem == QStringLiteral("ACC")
                                     ? QStringList{QStringLiteral("Sag"), QStringLiteral("Axial"),
                                                   QStringLiteral("WhiteMatter")}
                                     : QStringList{QStringLiteral("Sag"), QStringLiteral("Cor"),
                                                   QStringLiteral("Axial")};
    ExampleImageView* const imageSlots[] = {exampleTargetImage_, exampleTargetImage2_,
                                            exampleTargetImage3_};
    for (int i = 0; i < 3; ++i) {
        const QString path = QStringLiteral(":/example_targets/%1%2.bmp").arg(stem, suffixes.value(i));
        const QPixmap image(path);
        if (image.isNull()) {
            imageSlots[i]->setPlaceholder(
                QStringLiteral("%1 %2 not bundled").arg(stem, suffixes.value(i)));
            continue;
        }
        imageSlots[i]->setImage(image);
    }
    exampleTargetText_->setText(QString::fromStdString(
        beam::gui::exampleTargetHelpText(accFlagCombo_->currentText().toStdString())));
}

// --------------------------------------------------------------------------- Treatment

}  // namespace beam::app
