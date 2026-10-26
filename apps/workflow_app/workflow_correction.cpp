#include "workflow_internal.hpp"

namespace beam::app {

void WorkflowWindow::fillSimulatedCouplingWaveforms() {
    constexpr int kSamples = 600;
    couplingCh0Raw_ = Eigen::VectorXd::Zero(kSamples);
    couplingCh1Raw_ = Eigen::VectorXd::Zero(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        const double t = i / 40.0;
        const double envelope = std::exp(-std::pow((i - 260.0) / 70.0, 2.0));
        couplingCh0Raw_[i] = 120.0 * envelope * std::sin(t * 3.1) + 6.0 * std::sin(t * 11.0);
        couplingCh1Raw_[i] = 90.0 * envelope * std::sin(t * 3.1 + 0.6) + 5.0 * std::sin(t * 9.0);
    }
    couplingCh0Filtered_ = beam::correction::filterTransmitSignal(couplingCh0Raw_).filtWave;
    couplingCh1Filtered_ = beam::correction::filterTransmitSignal(couplingCh1Raw_).filtWave;
    couplingPeakToPeak_ =
        std::max(beam::correction::txRxSignalAmplitude(couplingCh0Filtered_).peak2peak,
                 beam::correction::txRxSignalAmplitude(couplingCh1Filtered_).peak2peak);
}

// Clearing a measurement is discarding it, not hiding it: the reading the Treatment stage would
void WorkflowWindow::clearPatientImagingAndRegistration() {
    mriLoaded_ = false;
    mriVolume_ = {};
    mriAxes_ = {};
    mriPath_.clear();
    focusImageLoaded_ = false;
    focusImage_ = {};
    registrationGeometryLoaded_ = false;
    arrayMask_ = {};
    arrayData_ = {};
    registrationOriginArrayData_ = {};
    targetMm_.setZero();
    fiducials_.clear();
    registrationSourceFiducials_.clear();
    fiducialConfirmed_.fill(false);
    sourceFiducialConfirmed_.fill(false);
    fiducialLocated_.fill(false);
    sourceFiducialLocated_.fill(false);
    fiducialDetected_.fill(false);
    fiducialConfidence_.fill(0.0);
    fiducialStability_.fill(0.0);
    registrationStartFiducialRow_ = -1;
    // The transducer lock position: where this patient's array was clamped, in 7.5 mm
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_}) {
        if (slider == nullptr) continue;
        const QSignalBlocker blocker(slider);
        slider->setValue(slider->minimum());
    }

    // A view keeps the last slice it was handed, so dropping the volume does not blank it --
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                  ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview, ui_->registrationAxialPreview,
                                  treatmentSagittalPreview_, treatmentCoronalPreview_,
                                  treatmentAxialPreview_}) {
        if (view == nullptr) continue;
        view->setSlice(Eigen::MatrixXd{});
        view->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        view->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        view->setMarkers({});
    }
    // Pan and zoom too: they were set looking at somebody else's head.
    resetMriViews();
    // Where each target sits is a registration output, carried on the list item by
    if (targetListWidget_ != nullptr) {
        for (int row = 0; row < targetListWidget_->count(); ++row)
            targetListWidget_->item(row)->setData(Qt::UserRole, QVariant());
    }
    ui_->imagingFiducialLayout->clearMarkers();
    ui_->registrationFiducialLayout->clearMarkers();
    ui_->registrationTable->clearContents();
    // The strings the .ui ships with, which is what "nothing loaded" looks like on a machine
    ui_->mriPathLabel->setText(QStringLiteral("No MRI loaded"));
    ui_->mriPathLabel->setToolTip(QString());
    ui_->mriMetadataLabel->setText(
        QStringLiteral("Dimensions and spatial axes will appear after loading."));
    ui_->registrationResult->setText(
        QStringLiteral("Load and accept the patient MRI before registration."));
    ui_->registrationReturnStartButton->setEnabled(false);
    updateArrayPositionReadout();
    setRegistrationPhase(RegistrationPhase::Locating);
}

void WorkflowWindow::clearCouplingMeasurement() {
    couplingCh0Raw_.resize(0);
    couplingCh1Raw_.resize(0);
    couplingCh0Filtered_.resize(0);
    couplingCh1Filtered_.resize(0);
    couplingPeakToPeak_ = 0.0;
    couplingSimulated_ = false;
    correctionRunCount_ = 0;
    transmissionAmplitude_ = 0.0;
    couplingCheckPassed_ = false;
    correctionCheckPassed_ = false;
    couplingProgressBar_->setValue(0);
    couplingStatusLabel_->setText(QStringLiteral("No coupling measurement has been run."));
    acceptCouplingButton_->setEnabled(false);
    clearCouplingButton_->setEnabled(false);
    updateCouplingCharts(0.0);
    // Clearing the measurement puts the through-transmit precondition back into failure, which
    updateSonicationSafety();

    const bool wasAccepted =
        workflow_.state(beam::gui::WorkflowStage::Coupling).status ==
            beam::gui::WorkflowStatus::Complete ||
        workflow_.state(beam::gui::WorkflowStage::Correction).status ==
            beam::gui::WorkflowStatus::Complete;
    if (wasAccepted) {
        workflow_.change(beam::gui::WorkflowStage::Coupling,
                         "Coupling measurement cleared; measure and accept again.");
    }
    refresh();
    showMessage(wasAccepted
                    ? QStringLiteral("Measurement cleared. The accepted coupling went with it, "
                                     "so measure and accept again before treatment.")
                    : QStringLiteral("Measurement cleared. Press Run to measure again."),
                false);
}

bool WorkflowWindow::deliveryRunning() const {
    return sonicationCountdownTimer_ != nullptr && sonicationCountdownTimer_->isActive();
}

void WorkflowWindow::updateAbortButton() {
    const bool running = deliveryRunning();
    ui_->abortButton->setEnabled(running);
    // Red only while it can do something.
    ui_->abortButton->setStyleSheet(
        running ? QStringLiteral("QPushButton { background: #bc3340; color: white; "
                                 "border: 1px solid #ff8290; border-radius: 5px; "
                                 "padding: 10px 22px; font-weight: 700; } "
                                 "QPushButton:hover { background: #d8414f; }")
                : QStringLiteral("QPushButton { background: #3a2b2e; color: #8d7074; "
                                 "border: 1px solid #55393e; border-radius: 5px; "
                                 "padding: 10px 22px; font-weight: 700; }"));
}

void WorkflowWindow::updateCouplingCharts(double transmissionAmp) {
    if (!couplingChartsRow_) return;
    const bool haveWaveforms = couplingCh0Raw_.size() > 0 && couplingCh1Raw_.size() > 0;
    if (couplingNumbersLabel_ != nullptr && !haveWaveforms) {
        couplingNumbersLabel_->setText(QStringLiteral("Nothing measured yet."));
    } else if (couplingNumbersLabel_ != nullptr) {
        // The same quantity checkCouplingSafety judges; peak-to-peak is a raw
        // amplitude and was never comparable to this threshold.
        const bool pass = transmissionAmp >= kCouplingThreshold;
        const QString verdict =
            couplingSimulated_
                ? QStringLiteral("<span style='color:#e0a32e'>&#9679; SIMULATED, NO DEVICE</span>")
            : pass ? QStringLiteral("<span style='color:#5ec46a'>&#9679; COUPLED</span>")
                   : QStringLiteral("<span style='color:#ff554a'>&#9679; NOT COUPLED</span>");
        couplingNumbersLabel_->setText(
            QStringLiteral("Transmission <b>%1</b> &nbsp; Peak-to-peak <b>%2</b> &nbsp; "
                           "Threshold <b>%3</b> &nbsp;&nbsp; %4")
                .arg(transmissionAmp, 0, 'f', 3)
                .arg(couplingPeakToPeak_, 0, 'f', 3)
                .arg(kCouplingThreshold, 0, 'f', 2)
                .arg(verdict));
    }
    // What the row holds is set here whatever page is up, so it is right the next time the
    couplingChartsPlaceholder_->setVisible(!haveWaveforms);
    for (QChartView* view : {couplingCh0ChartView_, couplingCh1ChartView_, couplingBarChartView_})
        view->setVisible(haveWaveforms);
    // Whether the row itself is on screen is not.
    const bool onCouplingPage = selectedStage_ == beam::gui::WorkflowStage::Coupling ||
                                selectedStage_ == beam::gui::WorkflowStage::Correction;
    couplingChartsRow_->setVisible(onCouplingPage);
    if (!haveWaveforms) return;
    // The builders are shared with beam_app's light tab, so the dark theme is applied here
    const auto show = [](QChartView* view, QChart* chart, bool theme) {
        if (theme) chart->setTheme(QChart::ChartThemeDark);
        chart->setBackgroundBrush(QColor(24, 57, 68));
        chart->setPlotAreaBackgroundBrush(QColor(18, 44, 53));
        chart->setPlotAreaBackgroundVisible(true);
        chart->setTitleBrush(QColor(220, 235, 239));
        for (QAbstractAxis* axis : chart->axes()) {
            axis->setLabelsBrush(QColor(200, 219, 225));
            axis->setTitleBrush(QColor(220, 235, 239));
        }
        if (chart->legend()) chart->legend()->setLabelBrush(QColor(220, 235, 239));
        view->setChart(chart);
    };
    const double yLimit = beam::gui::computeRfPlotYLimit(couplingCh0Filtered_, couplingCh1Filtered_);
    const QString mark = couplingSimulated_ ? QStringLiteral(" (simulated)") : QString();
    show(couplingCh0ChartView_,
         beam::gui_qt::buildRfWaveformChart(couplingCh0Raw_, couplingCh0Filtered_, yLimit,
                                            QStringLiteral("First array") + mark),
         true);
    show(couplingCh1ChartView_,
         beam::gui_qt::buildRfWaveformChart(couplingCh1Raw_, couplingCh1Filtered_, yLimit,
                                            QStringLiteral("Second array") + mark),
         true);
    show(couplingBarChartView_, beam::gui_qt::buildTransmissionBarChart(
        beam::gui::computeAvgTransmissionBars(transmissionAmp, couplingPeakToPeak_, kCouplingThreshold)),
         false);
}

void WorkflowWindow::setMriPathDisplay(const QString& path) {
    ui_->mriPathLabel->setText(QStringLiteral("%1  ·  Loaded: %2")
                                   .arg(path, QDateTime::currentDateTime().toString(Qt::ISODate)));
    ui_->mriPathLabel->setToolTip(path);
}

void WorkflowWindow::showMessage(const QString& text, bool error) {
    // An empty reason would paint a bare coloured box, which reads as a warning with the words
    if (text.trimmed().isEmpty()) return;
    ui_->workflowMessage->setText(text);
    ui_->workflowMessage->setStyleSheet(error
        ? QStringLiteral("padding: 10px; color: #a32018; background: #fdeceb; border-radius: 5px;")
        : QStringLiteral("padding: 10px; color: #12663f; background: #e8f5ed; border-radius: 5px;"));
    // selectStage hides the footer line on Registration and Treatment, so a message raised
    if (selectedStage_ == beam::gui::WorkflowStage::TreatmentPlan && sonicationCountdownLabel_) {
        sonicationCountdownLabel_->setText(text);
        sonicationCountdownLabel_->setStyleSheet(
            error ? QStringLiteral("QLabel { color: #ff9b8e; padding: 4px; }")
                  : QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
        return;
    }
    if (selectedStage_ != beam::gui::WorkflowStage::Registration || !ui_->registrationResult)
        return;
    if (error) {
        ui_->registrationResult->setText(text);
        ui_->registrationResult->setStyleSheet(
            QStringLiteral("padding: 9px 10px; color: #a32018; background: #fdeceb; "
                           "border: 1px solid #e3a49c; border-radius: 5px;"));
        return;
    }
    // Success keeps whatever the caller wrote -- several paths put a longer instruction there
    ui_->registrationResult->setStyleSheet(
        QStringLiteral("padding: 9px 10px; color: #17313f; background: #eef2f4; border-radius: 5px;"));
}

// `then` runs after the verdict has been applied, which is what the automatic re-check needs:
void WorkflowWindow::startCorrectionMeasurement(std::function<void()> then) {
    if (deviceLink_ == nullptr) {
        applyCorrectionResult({}, false);
        if (then) then();
        return;
    }
    runCouplingCheckButton_->setEnabled(false);
    couplingStatusLabel_->setText(QStringLiteral("Measuring through-transmit..."));
    // The device streams for seconds; off the GUI thread it does so without freezing the
    const bool started = deviceLink_->runCommand(
        "Correction", /*startQuietReads=*/10,
        [this, then](std::vector<std::string> reply, bool ok) {
            runCouplingCheckButton_->setEnabled(true);
            applyCorrectionResult(reply, ok);
            if (then) then();
        });
    if (!started) {
        runCouplingCheckButton_->setEnabled(true);
        showMessage(QStringLiteral("The device is busy with another command."), true);
        if (then) then();
    }
}

// Runs on the GUI thread once the device has answered, or straight away when there is nothing
void WorkflowWindow::applyCorrectionResult(const std::vector<std::string>& reply,
                                           bool fromDevice) {
    double transmission = 0.0;
    bool simulated = !fromDevice || reply.empty();
    if (!simulated) {
        std::vector<double> raw;
        for (const std::string& line : reply) {
            const auto values = beam::correction::parseCorrectionWaveformLine(line);
            raw.insert(raw.end(), values.begin(), values.end());
        }
        const auto split = beam::correction::splitAndFilterReceiveWaveform(raw);
        transmission =
            beam::correction::throughTransmitAmplitude(split.ch0rcv, split.ch1rcv).amp;
        couplingCh0Raw_ = split.ch0rcv;
        couplingCh1Raw_ = split.ch1rcv;
        couplingCh0Filtered_ = beam::correction::filterTransmitSignal(split.ch0rcv).filtWave;
        couplingCh1Filtered_ = beam::correction::filterTransmitSignal(split.ch1rcv).filtWave;
        couplingPeakToPeak_ =
            std::max(beam::correction::txRxSignalAmplitude(couplingCh0Filtered_).peak2peak,
                     beam::correction::txRxSignalAmplitude(couplingCh1Filtered_).peak2peak);
    }
    if (simulated) {
        fillSimulatedCouplingWaveforms();
        transmission = beam::correction::throughTransmitAmplitude(couplingCh0Raw_,
                                                                  couplingCh1Raw_)
                           .amp;
    }
    couplingSimulated_ = simulated;
    const int percent = std::clamp(static_cast<int>(std::lround(transmission * 100.0)), 0, 100);
    // checkCouplingSafety.m's own test, at its own 0.1 -- not a threshold of this application's
    const beam::safety::SafetyReport coupling = beam::safety::checkCouplingSafety(transmission);
    correctionCheckPassed_ = coupling.pass;
    // Keep the measurement itself, not only its verdict: the Treatment stage feeds it to
    transmissionAmplitude_ = transmission;
    couplingProgressBar_->setValue(percent);
    // checkCouplingSafety.m's own wording, including the line it appends when the report is not
    QStringList lines;
    ++correctionRunCount_;
    // Which run this is, and when: the numbers can come out identical two runs running, and
    lines << QStringLiteral("Run %1 at %2 -- through-transmit %3: transmission %4%.")
                 .arg(correctionRunCount_)
                 .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("HH:mm:ss.zzz")))
                 .arg(simulated ? QStringLiteral("(simulated, no device connected)")
                                : QStringLiteral("measured"))
                 .arg(percent);
    if (coupling.pass) {
        lines << QStringLiteral("Coupling Online");
    } else {
        for (const std::string& message : coupling.messages)
            lines << QString::fromStdString(message);
        lines << QStringLiteral("Readjust coupling or add coupling gel over uncoupled elements");
    }
    couplingStatusLabel_->setText(lines.join(QChar('\n')));
    updateCouplingCharts(transmission);
    // The through-transmit precondition just changed.
    updateSonicationSafety();
    // change() puts Correction back to in-progress and invalidates every stage after it --
    if (!autoCouplingRecheck_ || !correctionCheckPassed_) {
        workflow_.change(beam::gui::WorkflowStage::Correction,
                         "Correction measured; awaiting operator acceptance.");
    }
    if (!autoCouplingRecheck_) {
        showMessage(correctionCheckPassed_
                        ? QStringLiteral("Correction measurement passed. Accept correction "
                                         "to continue.")
                        : QStringLiteral("Correction measurement failed the transmission "
                                         "threshold."),
                    !correctionCheckPassed_);
    }
    refresh();
}

// Extent and sampling of the loaded scan.
QString WorkflowWindow::mriGeometryFingerprint() const {
    if (!mriLoaded_ || mriAxes_.dimLR.size() < 2) return QString();
    const auto span = [](const Eigen::VectorXd& axis) {
        return QStringLiteral("%1:%2..%3")
            .arg(axis.size())
            .arg(axis(0), 0, 'f', 3)
            .arg(axis(axis.size() - 1), 0, 'f', 3);
    };
    return QStringLiteral("%1;%2;%3")
        .arg(span(mriAxes_.dimLR), span(mriAxes_.dimAP), span(mriAxes_.dimIS));
}

}  // namespace beam::app
