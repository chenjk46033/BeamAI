#include "treatment_internal.hpp"

namespace beam::app {

void WorkflowWindow::recordSonicationStart(const beam::gui_qt::StimParamRow& row, bool sham,
                                           std::optional<double> attenuation,
                                           std::optional<double> dutyCycle) {
    beam::gui::TreatmentLogRow entry;
    entry.visit = currentVisitNumber();
    entry.protocol = currentProtocolName().toStdString();
    const QListWidgetItem* item = targetListWidget_ ? targetListWidget_->currentItem() : nullptr;
    entry.target = item != nullptr ? targetListName(item).toStdString() : std::string();
    entry.xMm = row.x;
    entry.yMm = row.y;
    entry.zMm = row.z;
    entry.amplitude = row.amplitude;
    entry.startTimeSeconds = row.startTime;
    entry.endTimeSeconds = row.endTime;
    entry.burstDurationSeconds = row.bd;
    entry.burstIntervalSeconds = row.bi;
    entry.pulseDurationSeconds = row.pd;
    entry.pulseIntervalSeconds = row.pi;
    entry.sham = sham;
    entry.attenuation = attenuation;
    entry.dutyCycle = dutyCycle;
    // What produced it: the board, and the curve that set the duty cycle.
    if (deviceLink_ != nullptr) {
        entry.deviceModel = deviceLink_->board().toStdString();
        entry.deviceSerial = deviceLink_->serial().toStdString();
    }
    entry.calDuty = calibrationDutyText();
    entry.calPressure = calibrationPressureText();
    entry.mriFile = mriPath_.toStdString();
    entry.mriFingerprint = mriFingerprint_.toStdString();
    entry.fiducialsMm = fiducialsAsText().toStdString();
    if (leftHorizontalPositionSlider_ != nullptr)
        entry.lockHorizontal = leftHorizontalPositionSlider_->value();
    if (leftVerticalPositionSlider_ != nullptr)
        entry.lockVertical = leftVerticalPositionSlider_->value();
    entry.startedAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
    entry.statusCode = beam::gui::TreatmentLog::kRunning;
    // BeamV0 writes into treatmentProtocolTable row i, where i is the Current Sonication
    entry.sequence = currentSonication_;
    openLogSequence_ = entry.sequence;
    const int shownSequence = entry.sequence;
    const QString sentParameters =
        QString::fromStdString(beam::gui::TreatmentLog::parameterString(entry));
    const QString sentTarget = QString::fromStdString(entry.target);
    const QString startedStamp = QString::fromStdString(entry.startedAt);
    treatmentLog_.record(std::move(entry));
    // The row said what was planned; from here it says what went out.
    if (protocolModel_ != nullptr) {
        std::vector<beam::gui_qt::TreatmentProtocolRow> shown = protocolModel_->rows();
        const std::size_t at = static_cast<std::size_t>(shownSequence) - 1;
        if (shownSequence >= 1 && at < shown.size()) {
            shown[at].parameters = sentParameters;
            shown[at].target = sentTarget;
            shown[at].amplitude = row.amplitude;
            shown[at].duration = row.endTime - row.startTime;
            // Start fills in now, not when the row closes: the operator is watching this
            const QDateTime started = QDateTime::fromString(startedStamp, Qt::ISODateWithMs);
            shown[at].startedAt =
                started.isValid() ? started.toString(QStringLiteral("HH:mm:ss.zzz")) : QString();
            shown[at].startedAtFull = startedStamp;
            shown[at].endedAt.clear();
            shown[at].endedAtFull.clear();
            shown[at].statusCode = beam::gui::TreatmentLog::kRunning;
            protocolModel_->setRows(std::move(shown));
            protocolModel_->setCurrentSonication(shownSequence);
            placeSonicateButton(shownSequence);
        }
    }
    // Persist now -- a row that is never closed still has to survive a crash mid-delivery --
    saveTreatmentTargetStore();
}

// The countdown reaching zero, or an abort, ends the row.
void WorkflowWindow::closeOpenLogRow(bool aborted) {
    const std::string protocol = currentProtocolName().toStdString();
    const int visit = currentVisitNumber();
    if (openLogSequence_ > 0) {
        treatmentLog_.setStatus(protocol, visit, openLogSequence_,
                                aborted ? beam::gui::TreatmentLog::kStopped
                                        : beam::gui::TreatmentLog::kCompleted);
        // The status column carries the code; Notes carries what it means, so the warning
        if (aborted) {
            const beam::gui::TreatmentLogRow* row =
                treatmentLog_.find(protocol, visit, openLogSequence_);
            const std::string warning = "Stopped by operator before it finished (499).";
            if (row != nullptr && row->notes.find(warning) == std::string::npos) {
                QString notes = QString::fromStdString(row->notes);
                if (!notes.isEmpty()) notes += QLatin1Char('\n');
                notes += QDateTime::currentDateTime().toString(
                             QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
                         QStringLiteral("  ") + QString::fromStdString(warning);
                treatmentLog_.setNotes(protocol, visit, openLogSequence_, notes.toStdString());
            }
        }
    }
    if (openLogSequence_ <= 0) return;
    treatmentLog_.close(protocol, visit, openLogSequence_,
                        QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString());
    // Read before the visit moves on, and only for one that ran to the end: a stopped
    const int justDelivered = aborted ? 0 : openLogSequence_;
    openLogSequence_ = 0;
    // Reload before saving, not after.
    loadTreatmentProtocol(currentProtocolName());
    saveTreatmentTargetStore();
    // BeamV0 re-ranks here, inside SonicateButtonPushed and ShamButtonPushed.
    applyTargetRankingIfAutomatic();
    advanceToNextScheduledSonication();
    remeasureCouplingIfDue(justDelivered);
}

// Where the visit has got to: one past the last delivery, which is BeamV0's own numbering --
void WorkflowWindow::advanceToNextScheduledSonication() {
    if (!protocolModel_) return;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& rows = protocolModel_->rows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].delivered) continue;
        selectScheduledSonication(static_cast<int>(i) + 1);
        return;
    }
    // Nothing left: the visit is done.
    selectScheduledSonication(static_cast<int>(rows.size()));
}

// Moves to one scheduled sonication: the number field, the highlighted row, and the target the
void WorkflowWindow::paintRunningButton() {
    QPushButton* const performing =
        runningSham_ ? inRowShamButton_.data() : inRowSonicateButton_.data();
    if (performing == nullptr) return;
    const char* const bright = runningSham_ ? kShamRunningStyle : kSonicatingStyle;
    const char* const dim = runningSham_ ? kShamRunningDimStyle : kSonicatingDimStyle;
    performing->setStyleSheet(QString::fromLatin1(runningFlashOn_ ? bright : dim));
    // The icon changes ink with the fill, or half the pulse is an empty button.
    const QColor ink = runningSham_
                           ? (runningFlashOn_ ? kInkOnYellow : kInkOnDimYellow)
                           : (runningFlashOn_ ? kInkOnBrightBlue : kInkOnDimBlue);
    performing->setIcon(runningSham_ ? makeShamIcon(18, ink) : makeSonicateIcon(18, ink));
}

// Three buttons with fixed jobs.
bool WorkflowWindow::currentSonicationIsPending() const {
    if (protocolModel_ == nullptr) return false;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& rows = protocolModel_->rows();
    if (currentSonication_ < 1 || currentSonication_ > static_cast<int>(rows.size())) return false;
    // The one going out is pending until its row closes.
    if (openLogSequence_ == currentSonication_) return true;
    return !rows[static_cast<std::size_t>(currentSonication_) - 1].delivered;
}

void WorkflowWindow::setSonicateEnabled(bool enabled) {
    sonicateEnabled_ = enabled;
    // Before the early return: the top-bar ABORT is on every page and must follow the delivery
    updateAbortButton();
    if (inRowSonicateButton_.isNull()) return;
    const bool running =
        sonicationCountdownTimer_ != nullptr && sonicationCountdownTimer_->isActive();
    // checkSonicationSafety.m ends by enabling or disabling SonicateButton on exactly this
    const bool live = !running && enabled && sonicationSafetyPass_ &&
                      workflow_.canStartTreatment(nullptr) &&
                      currentSonicationIsPending() && !mcuConnectionInvalid_;
    inRowSonicateButton_->setEnabled(live);
    if (!running)
        inRowSonicateButton_->setIcon(makeSonicateIcon(18, live ? kInkOnBlue : kInkOnDead));
    inRowSonicateButton_->setToolTip(running && !runningSham_
                                         ? QStringLiteral("Sonication in progress")
                                         : QStringLiteral("Sonicate this row"));
    if (!inRowShamButton_.isNull()) {
        inRowShamButton_->setEnabled(live);
        if (!running)
            inRowShamButton_->setIcon(makeShamIcon(18, live ? kInkOnYellow : kInkOnDead));
        inRowShamButton_->setToolTip(
            running && runningSham_
                ? QStringLiteral("Sham in progress")
                : QStringLiteral("Sham -- masking audio only, no ultrasound sent"));
    }
    if (!inRowStopButton_.isNull()) {
        inRowStopButton_->setEnabled(running);
        // Red whether or not anything is running: the control that interrupts a delivery is
        inRowStopButton_->setIcon(makeStopIcon(22, kInkOnRed));
        inRowStopButton_->setToolTip(running ? QStringLiteral("Stop the sonication in progress")
                                             : QStringLiteral("Nothing is running"));
    }
    if (!inRowOnLamp_.isNull()) {
        inRowOnLamp_->setStyleSheet(QString::fromLatin1(
            !running ? kLampOffStyle
                     : (runningSham_ ? kLampShamStyle : kLampSonicatingStyle)));
        inRowOnLamp_->setText(
            !running ? lampText(QStringLiteral("#27343a"), QStringLiteral("#27343a"))
            : runningSham_ ? lampText(QStringLiteral("#ffd83d"), QStringLiteral("#ffeea8"))
                           : lampText(QStringLiteral("#3dff7a"), QStringLiteral("#a8ffc4")));
        inRowOnLamp_->setToolTip(running ? (runningSham_
                                               ? QStringLiteral("Sham in progress")
                                               : QStringLiteral("Sonication in progress"))
                                         : QStringLiteral("Nothing is being delivered"));
    }
    // Qt drops a mouse event aimed at a disabled widget before any policy or filter sees it, so
    const auto passThroughWhileDisabled = [](QPushButton* button) {
        if (button != nullptr)
            button->setAttribute(Qt::WA_TransparentForMouseEvents, !button->isEnabled());
    };
    // Only the ones that sit on top of a table row.
    passThroughWhileDisabled(inRowStopButton_);
    if (running) {
        paintRunningButton();
    } else {
        inRowSonicateButton_->setStyleSheet(QString::fromLatin1(kSonicateStyle));
        if (!inRowShamButton_.isNull())
            inRowShamButton_->setStyleSheet(QString::fromLatin1(kShamStyle));
    }
}

void WorkflowWindow::placeSonicateButton(int oneBased) {
    if (protocolView_ == nullptr || protocolModel_ == nullptr) return;
    const int rows = static_cast<int>(protocolModel_->rows().size());
    for (int row = 0; row < rows; ++row) {
        const QModelIndex cell =
            protocolModel_->index(row, beam::gui_qt::TreatmentProtocolTableModel::kAction);
        const bool goingOut = openLogSequence_ > 0 && oneBased == openLogSequence_;
        const bool wanted =
            row == oneBased - 1 &&
            (goingOut || !protocolModel_->rows()[static_cast<std::size_t>(row)].delivered);
        if (!wanted) {
            if (protocolView_->indexWidget(cell) != nullptr) {
                // The pair widget owns both buttons, so dropping it takes them.
                protocolView_->setIndexWidget(cell, nullptr);
            }
            continue;
        }
        if (protocolView_->indexWidget(cell) != nullptr) continue;
        // What belongs to the row is what says this row is the one running: stop, and the lamp.
        auto* pair = new QWidget(protocolView_);
        auto* row_layout = new QHBoxLayout(pair);
        row_layout->setContentsMargins(1, 0, 1, 0);
        row_layout->setSpacing(4);
        auto* halt = new QPushButton(pair);
        halt->setIconSize(QSize(22, 22));
        halt->setFixedSize(44, 26);
        halt->setCursor(Qt::PointingHandCursor);
        halt->setStyleSheet(QString::fromLatin1(kStopStyle));
        halt->setIcon(makeStopIcon(22, kInkOnRed));
        connect(halt, &QPushButton::clicked, this, [this] { abortSonication(); });
        auto* lamp = new QLabel(pair);
        lamp->setTextFormat(Qt::RichText);
        lamp->setText(lampText(QStringLiteral("#27343a"), QStringLiteral("#27343a")));
        lamp->setAlignment(Qt::AlignCenter);
        lamp->setFixedSize(56, 26);
        lamp->setStyleSheet(QString::fromLatin1(kLampOffStyle));
        // Nothing to click: it reports, so clicks belong to the row behind it.
        lamp->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        row_layout->addWidget(halt);
        row_layout->addWidget(lamp);
        row_layout->addStretch(1);
        // These sit on top of the row's first cell, so without this a right-click on them is
        for (QWidget* over : {static_cast<QWidget*>(pair), static_cast<QWidget*>(halt),
                              static_cast<QWidget*>(lamp)}) {
            over->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(over, &QWidget::customContextMenuRequested, this,
                    [this, over](const QPoint& at) {
                        showScheduleRowMenu(protocolView_->viewport()->mapFromGlobal(
                            over->mapToGlobal(at)));
                    });
        }
        protocolView_->setIndexWidget(cell, pair);
        protocolView_->setRowHeight(row, 30);
        inRowStopButton_ = halt;
        inRowOnLamp_ = lamp;
        setSonicateEnabled(sonicateEnabled_);  // paints them into whichever state applies
    }
}

// Port of what updateSonicateSettingsForCurrentSonication.m does to the grid:
void WorkflowWindow::applyScheduledDurationToGrid() {
    if (stimParamModel_ == nullptr || protocolModel_ == nullptr) return;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& planned = protocolModel_->rows();
    const int position = currentSonication_;
    if (position < 1 || position > static_cast<int>(planned.size())) return;
    const beam::gui_qt::TreatmentProtocolRow& planRow =
        planned[static_cast<std::size_t>(position) - 1];
    if (planRow.duration <= 0.0) return;
    // Only when the grid is showing the target this sonication runs.
    if (planRow.target != stimParamModel_->targetName()) return;

    std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
    if (rows.empty()) return;
    bool changed = false;
    for (beam::gui_qt::StimParamRow& row : rows) {
        if (row.startTime != 0.0 || std::abs(row.endTime - planRow.duration) > 1e-12)
            changed = true;
        row.startTime = 0.0;
        row.endTime = planRow.duration;
    }
    if (!changed) return;
    stimParamModel_->setRows(std::move(rows));
}

void WorkflowWindow::selectScheduledSonication(int oneBased) {
    if (!protocolModel_) return;
    const int scheduled = static_cast<int>(protocolModel_->rows().size());
    if (scheduled == 0) {
        // An empty plan is a legitimate state, not a reason to leave the page showing the last
        currentSonication_ = 0;
        protocolModel_->setCurrentSonication(0);
        placeSonicateButton(0);
        if (currentSonicationNumberLabel_ != nullptr)
            currentSonicationNumberLabel_->setText(QStringLiteral("--"));
        return;
    }
    const int position = std::clamp(oneBased, 1, scheduled);

    currentSonication_ = position;
    if (protocolView_) protocolView_->selectRow(position - 1);
    // Every row delivered means there is no "next" one: marking a finished row as current, and
    bool anyLeft = false;
    for (const beam::gui_qt::TreatmentProtocolRow& row : protocolModel_->rows())
        anyLeft = anyLeft || !row.delivered;
    // Marked the way setTreatmentProtocolTableData.m marks it; 0 marks nothing.
    protocolModel_->setCurrentSonication(anyLeft ? position : 0);
    placeSonicateButton(anyLeft ? position : 0);
    if (currentSonicationNumberLabel_ != nullptr)
        currentSonicationNumberLabel_->setText(
            anyLeft ? QStringLiteral("%1 of %2").arg(position).arg(scheduled)
                    : QStringLiteral("all %1 delivered").arg(scheduled));
    if (!anyLeft && !protocolModel_->rows().empty())
        setExecutionStatus(QStringLiteral("Visit %1 of \"%2\" is complete: all %3 sonications "
                                          "delivered. Add a sonication to continue, or finish "
                                          "the treatment.")
                               .arg(currentVisitNumber())
                               .arg(currentProtocolName())
                               .arg(protocolModel_->rows().size()));
    // Remembered against the visit, so coming back to one resumes where it was rather than at
    visitSonication_.insert(visitKey(currentProtocolName(), currentVisitNumber()), position);
    saveVisitState();
    const QString target = protocolModel_->rows()[static_cast<std::size_t>(position) - 1].target;
    // Here rather than in loadTreatmentProtocol: that calls switchToSelectedTarget before this
    applyScheduledDurationToGrid();
    if (!targetListWidget_) return;
    int found = -1;
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        QListWidgetItem* item = targetListWidget_->item(i);
        const bool matches = found < 0 && targetListName(item) == target;
        item->setData(kCurrentTargetRole, matches);
        if (matches) found = i;
        if (QWidget* rowWidget = targetListWidget_->itemWidget(item)) {
            if (QLabel* label =
                    rowWidget->findChild<QLabel*>(QStringLiteral("targetNameLabel")))
                label->setStyleSheet(
                    matches ? QStringLiteral("QLabel { color: #ffffff; background: transparent; }")
                            : QStringLiteral("QLabel { color: #17313f; background: transparent; }"));
        }
    }
    targetListWidget_->viewport()->update();
    if (found >= 0 && targetListWidget_->currentRow() != found)
        targetListWidget_->setCurrentRow(found);
}

// Right-click a row to change what this VISIT runs, without touching the protocol: skip one
void WorkflowWindow::showScheduleRowMenu(const QPoint& where) {
    if (!protocolView_ || !protocolModel_) return;
    // The position arrives in the VIEWPORT's coordinates, which is the frame indexAt reads: a
    QPoint inViewport = where;
    QModelIndex index = protocolView_->indexAt(inViewport);
    if (!index.isValid()) {
        const QPoint mapped = protocolView_->viewport()->mapFrom(protocolView_, where);
        if (protocolView_->indexAt(mapped).isValid()) {
            inViewport = mapped;
            index = protocolView_->indexAt(mapped);
        }
    }
    // Two cases land here: the blank row an empty plan draws, and the space below the last row
    if (!index.isValid() || protocolModel_->rows().empty()) {
        QMenu blank(this);
        QAction* const add = blank.addAction(QStringLiteral("Add Row"));
        QAction* const wholeProtocol =
            targetStore_.protocols().empty()
                ? nullptr
                : blank.addAction(QStringLiteral("Add Protocol to Plan"));
        const int here = currentVisitNumber();
        QAction* dropVisit = nullptr;
        if (visitExists(currentProtocolName(), here) &&
            treatmentLog_.canRemoveVisit(currentProtocolName().toStdString(), here)) {
            blank.addSeparator();
            dropVisit = blank.addAction(QStringLiteral("Delete Visit"));
        }
        QAction* const picked = blank.exec(protocolView_->viewport()->mapToGlobal(inViewport));
        if (picked == add) addRowToVisit();
        else if (picked != nullptr && picked == wholeProtocol) addProtocolToVisit(0);
        else if (picked != nullptr && picked == dropVisit) deleteVisit(here);
        return;
    }
    const int sequence = index.row() + 1;
    const std::string protocol = currentProtocolName().toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row = treatmentLog_.find(protocol, visit, sequence);
    if (row == nullptr) return;

    // A menu opened inside a selection acts on the selection and says how much it is about to
    std::vector<int> block;
    if (protocolView_->selectionModel() != nullptr)
        for (const QModelIndex& picked : protocolView_->selectionModel()->selectedRows())
            block.push_back(picked.row() + 1);
    std::sort(block.begin(), block.end());
    block.erase(std::unique(block.begin(), block.end()), block.end());
    const bool wholeBlock =
        block.size() > 1 && std::find(block.begin(), block.end(), sequence) != block.end();

    QMenu menu(this);
    // Ask the log what it would accept rather than guessing: a row below this one that has
    const bool anyTarget = !targetStore_.targets().empty();
    const bool roomAfter = treatmentLog_.canInsertAfter(protocol, visit, sequence);
    const bool canInsert = roomAfter && anyTarget;
    const bool canAddProtocol = roomAfter && !targetStore_.protocols().empty();
    const bool canInsertAbove =
        treatmentLog_.canInsertBefore(protocol, visit, sequence) && anyTarget;
    const bool canSkip = treatmentLog_.canRemoveAt(protocol, visit, sequence);
    const bool fired = beam::gui::TreatmentLog::delivered(*row);
    const QString blocked =
        fired ? QStringLiteral("Sonication %1 has already been delivered.").arg(sequence)
              : QStringLiteral("A sonication below %1 has already been delivered, so the order "
                               "cannot change.")
                    .arg(sequence);

    // Only what can actually be done to this row.
    QAction* const insertAbove =
        canInsertAbove ? menu.addAction(QStringLiteral("Add Row Before")) : nullptr;
    QAction* const insert =
        canInsert ? menu.addAction(QStringLiteral("Add Row After")) : nullptr;
    QAction* const wholeProtocol =
        canAddProtocol ? menu.addAction(QStringLiteral("Add Protocol to Plan")) : nullptr;
    // The lowest row of a block is the one the log is strictest about: it refuses while
    const bool canSkipBlock =
        wholeBlock && treatmentLog_.canRemoveAt(protocol, visit, block.front());
    QAction* const skip =
        wholeBlock
            ? (canSkipBlock
                   ? menu.addAction(QStringLiteral("Delete %1 Rows").arg(block.size()))
                   : nullptr)
            : (canSkip ? menu.addAction(QStringLiteral("Delete Row")) : nullptr);
    // The whole visit, when none of it has run.
    QAction* dropVisit = nullptr;
    if (treatmentLog_.canRemoveVisit(protocol, visit)) {
        if (!menu.isEmpty()) menu.addSeparator();
        dropVisit = menu.addAction(QStringLiteral("Delete Visit"));
    }
    if (menu.isEmpty()) {
        // An empty menu on right-click reads as a broken control, so the one entry says why
        menu.addAction(blocked)->setEnabled(false);
    }

    QAction* chosen = menu.exec(protocolView_->viewport()->mapToGlobal(inViewport));
    if (chosen == nullptr) return;
    if (chosen == wholeProtocol) {
        // Reloads and saves on its own.
        addProtocolToVisit(sequence);
        return;
    }
    if (chosen == dropVisit) {
        deleteVisit(visit);
        return;
    }

    if (chosen == skip) {
        const std::vector<int> doomed = wholeBlock ? block : std::vector<int>{sequence};
        // Last one first: removing a row closes the gap and renumbers the ones below it, so
        int gone = 0;
        for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
            if (treatmentLog_.removeAt(protocol, visit, *it)) ++gone;
        if (gone == 0) {
            showMessage(QStringLiteral("Row %1 cannot be deleted: already delivered.")
                            .arg(sequence),
                        true);
            return;
        }
        markVisitEdited(currentProtocolName(), visit);
        showMessage(gone == 1 ? QStringLiteral("Row %1 deleted.").arg(doomed.front())
                              : QStringLiteral("%1 rows deleted.").arg(gone),
                    false);
    } else if (chosen == insert || chosen == insertAbove) {
        QStringList names;
        for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
            names << QString::fromStdString(target.name);
        bool accepted = false;
        const QString picked =
            QInputDialog::getItem(this, QStringLiteral("Insert sonication"),
                                  QStringLiteral("Target:"), names, 0, false, &accepted);
        if (!accepted || picked.isEmpty()) return;

        // The added sonication takes the target's own parameters, the same way a seeded row
        const beam::gui::TreatmentTarget* target = targetStore_.findTarget(picked.toStdString());
        if (target == nullptr || target->points.empty()) return;
        const beam::gui::TreatmentTargetPoint& point = target->points.front();
        beam::gui::TreatmentLogRow added;
        added.target = target->name;
        added.amplitude = point.amplitude;
        added.startTimeSeconds = 0.0;
        added.endTimeSeconds = point.endTimeSeconds;
        added.burstDurationSeconds = point.burstDurationSeconds;
        added.burstIntervalSeconds = point.burstIntervalSeconds;
        added.pulseDurationSeconds = point.pulseDurationSeconds;
        added.pulseIntervalSeconds = point.pulseIntervalSeconds;
        const bool above = chosen == insertAbove;
        const bool placed =
            above ? treatmentLog_.insertBefore(protocol, visit, sequence, std::move(added))
                  : treatmentLog_.insertAfter(protocol, visit, sequence, std::move(added));
        if (!placed) {
            showMessage(QStringLiteral("Nothing can be inserted here: a sonication at or below %1 "
                                       "has already been delivered.")
                            .arg(sequence),
                        true);
            return;
        }
        markVisitEdited(currentProtocolName(), visit);
        showMessage(QStringLiteral("%1 added %2 sonication %3 for this visit. The protocol is "
                                   "unchanged.")
                        .arg(picked)
                        .arg(above ? QStringLiteral("before") : QStringLiteral("after"))
                        .arg(sequence),
                    false);
    }
    // Reload first: saveTreatmentTargetStore writes the displayed rows back into the log by
    loadTreatmentProtocol(currentProtocolName());
    saveTreatmentTargetStore();
}

// setShamAudio.m ends with `sound(shamData, fs)` -- the masking track is meant to be heard, and
QByteArray WorkflowWindow::wavFromSamples(const std::vector<double>& samples, int sampleRate) {
    QByteArray wav;
    if (samples.empty() || sampleRate <= 0) return wav;

    // MATLAB's sound() clips to [-1, 1]; anything louder is scaled down rather than wrapping
    double peak = 0.0;
    for (const double sample : samples) peak = std::max(peak, std::abs(sample));
    const double scale = peak > 1.0 ? 1.0 / peak : 1.0;

    const quint32 frames = static_cast<quint32>(samples.size());
    const quint32 dataBytes = frames * 2;  // 16-bit mono
    QDataStream out(&wav, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(sampleRate)
        << quint32(sampleRate * 2) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (const double sample : samples) {
        const double clamped = std::clamp(sample * scale, -1.0, 1.0);
        out << static_cast<qint16>(std::lround(clamped * 32767.0));
    }
    return wav;
}

// PLATFORM SEAM. The playback call below is the only Windows-specific line in this application,
void WorkflowWindow::playShamAudio(const std::vector<double>& samples, int sampleRate) {
    // Whatever is playing is reading the buffer this is about to replace, so it has to be
    stopShamAudio();
    shamAudioBuffer_ = wavFromSamples(samples, sampleRate);
    if (shamAudioBuffer_.isEmpty()) return;
#ifdef _WIN32
    PlaySoundW(reinterpret_cast<LPCWSTR>(shamAudioBuffer_.constData()), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    // Said out loud rather than failing quietly: an unheard mask is an unblinded sham, which is
    showMessage(QStringLiteral("Masking audio cannot be played on this platform, so this "
                               "sham is not blinded."),
                true);
#endif
}

void WorkflowWindow::stopShamAudio() {
#ifdef _WIN32
    PlaySoundW(nullptr, nullptr, SND_PURGE);
#endif
    shamAudioBuffer_.clear();
}

// What is fired is the treatment table's row for the current sonication number, never the
bool WorkflowWindow::selectScheduledTarget() {
    if (!protocolModel_ || !targetListWidget_) return true;
    const int position = currentSonication_;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& rows = protocolModel_->rows();
    if (position < 1 || position > static_cast<int>(rows.size())) return true;
    const QString scheduled = rows[static_cast<std::size_t>(position) - 1].target;
    if (scheduled.isEmpty()) return true;
    if (targetListName(targetListWidget_->currentItem()) == scheduled) return true;
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        if (targetListName(targetListWidget_->item(i)) != scheduled) continue;
        // Selecting it refills the grid from this target, which is what gets sent.
        targetListWidget_->setCurrentRow(i);
        setExecutionStatus(QStringLiteral("Sonication %1 runs \"%2\", as scheduled.")
                               .arg(position)
                               .arg(scheduled));
        return true;
    }
    setExecutionStatus(QStringLiteral("Sonication %1 schedules \"%2\", which the Target List does "
                                      "not define. Nothing was sent.")
                           .arg(position)
                           .arg(scheduled),
                       true);
    return false;
}

// The device echoes the parameters it parsed, in its own units.
void WorkflowWindow::reportStimEcho(const std::string& reply, const beam::serialcom::SonicationTiming& timing,
                                    double dutyCycle) {
    if (deviceLink_ == nullptr) return;
    if (reply.empty()) {
        deviceLink_->appendHistory(DeviceLinkPanel::Log::AckDiffers,
                                   QStringLiteral("No acknowledgement arrived."));
        return;
    }
    const std::string expected =
        beam::serialcom::normaliseEcho(beam::serialcom::expectedStimEcho(timing, dutyCycle));
    const std::string got = beam::serialcom::normaliseEcho(reply);
    const std::vector<std::string> differences =
        beam::serialcom::stimEchoDifferences(expected, got);
    if (differences.empty()) {
        deviceLink_->appendHistory(DeviceLinkPanel::Log::Ack,
                                   QStringLiteral("The device echoed the same parameters that were sent.   %1")
                                       .arg(QString::fromStdString(got)));
        return;
    }
    QStringList named;
    for (const std::string& d : differences) named << QString::fromStdString(d);
    deviceLink_->appendHistory(DeviceLinkPanel::Log::AckDiffers,
                               QStringLiteral("The device echoed different parameters: %1.").arg(named.join(QStringLiteral(", "))));
    showMessage(QStringLiteral("The device echoed different sonication parameters than were "
                               "sent. Expected \"%1\", got \"%2\".")
                    .arg(QString::fromStdString(expected), QString::fromStdString(got)),
                true);
}

void WorkflowWindow::runSonication(bool sham) {

    // Nothing goes out unless the MCU answers first.
    if (!sham && (deviceLink_ == nullptr || !deviceLink_->askDevice())) {
        mcuConnectionInvalid_ = true;
        setSonicateEnabled(sonicateEnabled_);
        if (deviceLink_ != nullptr)
            deviceLink_->appendHistory(
                DeviceLinkPanel::Log::Lost,
                QStringLiteral("pre-sonication check failed; delivery refused"));
        setExecutionStatus(QStringLiteral("MCU Connection invalid. Please power off, unplug, "
                                          "and reconnect to MCU."),
                           true);
        return;
    }

    std::string reason;
    if (!workflow_.begin(beam::gui::WorkflowStage::Treatment, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    if (!stimParamModel_ || stimParamModel_->rows().empty()) {
        setExecutionStatus(QStringLiteral("No sonication is planned."), true);
        return;
    }
    // The treatment table decides what runs, not whatever is selected.
    if (!selectScheduledTarget()) return;
    // Backstop: the buttons are already dead for a finished row, and a second delivery would
    if (!currentSonicationIsPending()) {
        setExecutionStatus(QStringLiteral("Sonication %1 has already been delivered. Nothing "
                                          "was sent.")
                               .arg(currentSonication_),
                           true);
        return;
    }
    // The button is already disabled while this is false; this is the backstop for anything
    updateSonicationSafety();
    if (!sonicationSafetyPass_) {
        // A refusal is part of what happened to this visit.
        recordRefusedSonication(sham);
        setExecutionStatus(QStringLiteral("Nothing was sent. %1")
                               .arg(sonicationSafetyMessages_.join(QStringLiteral("; "))),
                           true);
        return;
    }
    const int shown = std::clamp(stimParamModel_->currentShownRow(), 1,
                                 static_cast<int>(stimParamModel_->rows().size()));
    beam::gui_qt::StimParamRow r = stimParamModel_->rows()[static_cast<std::size_t>(shown - 1)];
    // No duration override here.
    const beam::safety::SonicationSafetyParams params =
        beam::gui::stimParamsFromTableRow(r.pd, r.pi, r.bd, r.bi, r.amplitude, r.startTime, r.endTime);

    if (sham) {
        const beam::gui::ShamSonicationOutcome outcome =
            beam::gui::prepareShamSonication(params, shamBurst().samples, shamBurst().sampleRateHz);
        // A sham sends nothing, so there is no transmission or duty cycle.
        playShamAudio(outcome.maskingAudio, static_cast<int>(shamBurst().sampleRateHz));
        recordSonicationStart(r, true, std::nullopt, std::nullopt);
        startSonicationCountdown(static_cast<int>(outcome.durationSeconds), /*sham=*/true);
        setExecutionStatus(
            QStringLiteral("Sham sonication %1: masking audio ready (%2 samples, %3 s). No ultrasound was sent.")
                .arg(shown)
                .arg(outcome.maskingAudio.size())
                .arg(outcome.durationSeconds, 0, 'f', 1));
        return;
    }

    const beam::gui::SonicateOutcome outcome = beam::gui::prepareSonication(
        params, transmissionAmplitude_, kCouplingThreshold,
        triggerModeCombo_->currentIndex() == 1, calibration_);
    if (!outcome.started) {
        QString message = QStringLiteral("Not started: ");
        for (std::size_t i = 0; i < outcome.messages.size(); ++i) {
            if (i != 0) message += QStringLiteral("; ");
            message += QString::fromStdString(outcome.messages[i]);
        }
        setExecutionStatus(message, true);
        workflow_.block(beam::gui::WorkflowStage::Treatment, message.toStdString());
        refresh();
        return;
    }

    const beam::serialcom::SonicationTiming timing{r.pd, r.pi, r.bd, r.bi, r.startTime, r.endTime};
    const std::string command = beam::serialcom::setSerialCommandFromStimParams(timing, outcome.dutyCycle);
    QString status = QStringLiteral("Sonication %1: duty cycle %2%3, %4 s. Command: %5")
                         .arg(shown)
                         .arg(outcome.dutyCycle, 0, 'f', 3)
                         .arg(outcome.waitForTrigger ? QStringLiteral(" (waiting for external trigger)") : QString())
                         .arg(outcome.durationSeconds, 0, 'f', 1)
                         .arg(QString::fromStdString(command));
    if (deviceLink_ != nullptr)
        deviceLink_->appendHistory(
            DeviceLinkPanel::Log::Sent,
            QStringLiteral("%1   expect %2")
                .arg(QString::fromStdString(command),
                     QString::fromStdString(beam::serialcom::expectedStimEcho(
                         timing, outcome.dutyCycle))));
    const bool sent = deviceLink_ != nullptr &&
                      deviceLink_->withLink([&](beam::serialcom::SerialLink& link) {
                          try {
                              const bool wrote = beam::serialcom::sendSerialCommand(link, command);
                              const std::string reply = link.readLine();
                              beam::serialcom::clearSerial(link);
                              reportStimEcho(reply, timing, outcome.dutyCycle);
                              status += wrote ? QStringLiteral(" | Sent, reply: \"%1\"")
                                                    .arg(QString::fromStdString(reply))
                                              : QStringLiteral(" | Link closed before send.");
                          } catch (const std::exception& e) {
                              status += QStringLiteral(" | Send failed: %1")
                                            .arg(QString::fromStdString(e.what()));
                          }
                      });
    if (!sent) {
        status += QStringLiteral(" | No serial connection -- command not sent.");
    } else if (deviceLink_ != nullptr) {
        // Checks keep running: the device answers throughout, and a miss here is the only sign
        deviceLink_->setDelivering(true);
    }
    setExecutionStatus(status);
    recordSonicationStart(r, false, transmissionAmplitude_, outcome.dutyCycle);
    startSonicationCountdown(static_cast<int>(outcome.durationSeconds), /*sham=*/false);
}

// The status panel earns its space only when there is something to report, so it stays out of
void WorkflowWindow::setExecutionStatus(const QString& text, bool isError) {
    if (text.trimmed().isEmpty()) return;
    showMessage(text, isError);
}

void WorkflowWindow::startSonicationCountdown(int durationSeconds, bool sham) {
    runningSham_ = sham;
    if (runningFlashTimer_ == nullptr) {
        runningFlashTimer_ = new QTimer(this);
        runningFlashTimer_->setInterval(450);
        connect(runningFlashTimer_, &QTimer::timeout, this, [this] {
            runningFlashOn_ = !runningFlashOn_;
            paintRunningButton();
        });
    }
    runningFlashOn_ = true;
    runningFlashTimer_->start();
    // The bar takes the colour of the tick this row will end up with, so the two say the same
    treatmentExecutionProgressBar_->setStyleSheet(progressBarStyle(
        sham ? QColor(0xe0, 0xa3, 0x2e) : QColor(0x5e, 0xc4, 0x6a)));
    sonicationCountdownSeconds_ = std::max(durationSeconds, 0);
    sonicationDeadlineMs_ =
        QDateTime::currentMSecsSinceEpoch() + 1000LL * sonicationCountdownSeconds_;
    treatmentExecutionProgressBar_->setValue(0);
    setSonicateEnabled(false);
    sonicationCountdownLabel_->setText(
        QString::fromStdString(beam::gui::countdownDisplayText(sonicationCountdownSeconds_)));
    treatmentExecutionProgressBar_->setVisible(true);
    sonicationCountdownTimer_->start();
    // After the timer is running, so the button reads "Sonicating..." rather than merely going
    setSonicateEnabled(false);
}

// Stops everything this application controls.
void WorkflowWindow::abortSonication() {
    sonicationCountdownTimer_->stop();
    if (runningFlashTimer_ != nullptr) runningFlashTimer_->stop();
    sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
    treatmentExecutionProgressBar_->setValue(0);
    treatmentExecutionProgressBar_->setVisible(false);
    closeOpenLogRow(/*aborted=*/true);
    stopShamAudio();
    setSonicateEnabled(true);
    // No command goes out. The firmware has no branch that reaches stopUltrasoundSequence(),
    QString status =
        QStringLiteral("Sonication aborted by operator. The countdown, the log row and any "
                       "sham audio have stopped, but ultrasound continues for the rest of its "
                       "programmed duration: this device has no stop command. Press RESET on "
                       "the board, or unplug it, to end output now.");
    if (deviceLink_ != nullptr) {
        deviceLink_->appendHistory(
            DeviceLinkPanel::Log::Lost,
            QStringLiteral("Aborted in the application. No stop command exists; output "
                           "continues until the programmed duration ends."));
        deviceLink_->setDelivering(false);
    }
    workflow_.block(beam::gui::WorkflowStage::Treatment, status.toStdString());
    setExecutionStatus(status);
    showMessage(status, true);
    refresh();
}

void WorkflowWindow::finishTreatmentStage() {
    std::string reason;
    if (workflow_.state(beam::gui::WorkflowStage::Treatment).status == beam::gui::WorkflowStatus::Complete) return;
    if (!workflow_.complete(beam::gui::WorkflowStage::Treatment, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    // Said in a form that is still true after the jump it announces.
    showMessage(QStringLiteral("Visit %1 finished. Its case report is below — export it as a "
                               "PDF or CSV to keep.")
                    .arg(currentVisitNumber()),
                false);
    refresh();
    // Report's own row, by a row the list shows.
    goToNextStage();
}

void WorkflowWindow::openTreatmentLibrary(TreatmentLibraryDialog::Page page) {
    // The dialog edits a copy, and applying it replaces the store wholesale, so anything still
    writeProtocolGridToStore();
    writeTargetPointsToStore();
    TreatmentLibraryDialog dialog(targetStore_, page, this);
    if (page == TreatmentLibraryDialog::Page::Targets) {
        // Which list this edits follows where it would be saved, so the dialog cannot say one
        beam::gui::Patient open;
        const bool belongsToSomebody =
            !patientTargetsPath().isEmpty() && patients_.find(currentPatientId_, &open);
        dialog.setTargetScope(
            belongsToSomebody
                ? QStringLiteral("%1 (%2)")
                      .arg(QString::fromStdString(beam::gui::patientDisplayName(open)),
                           QString::fromStdString(open.externalId))
                : QString(),
            shippedDataFile(QStringLiteral("treatment_targets.csv")));
    }
    // Applied on each Save rather than once at the end, so the dialog can stay open for the
    dialog.setSaveHandler([this](const beam::gui::TreatmentTargetStore& edited) {
        applyTreatmentLibrary(edited);
    });
    dialog.exec();
}

void WorkflowWindow::applyTreatmentLibrary(const beam::gui::TreatmentTargetStore& edited) {
    const QString protocol = treatmentProtocolCombo_->currentText();
    targetStore_ = edited;

    // Disowned before the list is emptied, for the reason loadPatientData spells out: emptying
    targetSonications_.clear();
    currentTargetId_ = -1;
    gridTargetId_ = -1;
    stimParamModel_->setRows({});
    targetListWidget_->clear();
    for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
        addTargetListRow(QString::fromStdString(target.name));
    if (targetListWidget_->count() > 0) targetListWidget_->setCurrentRow(0);

    const bool kept = targetStore_.findProtocol(protocol.toStdString()) != nullptr;
    refreshProtocolCombo(kept ? protocol : QString());
    if (!treatmentProtocolCombo_->currentText().isEmpty())
        loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    saveTreatmentTargetStore();
}

// X/Y/Z stay out: the store holds no placement.
void WorkflowWindow::writeTargetPointsToStore() {
    if (targetListWidget_ == nullptr) return;
    saveCurrentTargetSonications();
    for (int row = 0; row < targetListWidget_->count(); ++row) {
        const QListWidgetItem* item = targetListWidget_->item(row);
        const auto cached = targetSonications_.constFind(targetListId(item));
        if (cached == targetSonications_.constEnd() || cached.value().empty()) continue;
        std::vector<beam::gui::TreatmentTargetPoint> points;
        points.reserve(cached.value().size());
        for (const beam::gui_qt::StimParamRow& grid : cached.value()) {
            beam::gui::TreatmentTargetPoint point;
            point.order = grid.order;
            point.show = grid.show;
            point.amplitude = grid.amplitude;
            point.startTimeSeconds = grid.startTime;
            point.endTimeSeconds = grid.endTime;
            point.burstDurationSeconds = grid.bd;
            point.burstIntervalSeconds = grid.bi;
            point.pulseDurationSeconds = grid.pd;
            point.pulseIntervalSeconds = grid.pi;
            points.push_back(point);
        }
        // Straight onto the target.
        targetStore_.setTargetPoints(targetListName(item).toStdString(), std::move(points));
    }
}

// The Report stage: who was treated, what they were given this visit, and a file that carries
}  // namespace beam::app
