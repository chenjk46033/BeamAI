#include "main_window_startup.hpp"

#include "correction/receive_waveform.hpp"
#include "gui/initial_placement_presenter.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "gui/sham_orchestrator.hpp"
#include "gui/sonicate_orchestrator.hpp"
#include "gui_qt/correction_tab_chart.hpp"
#include "gui_qt/sonication_tab_chart.hpp"
#include "registration/array_transform.hpp"
#include "registration/fiducial_markers.hpp"
#include "serialcom/command.hpp"
#include "startup_data.hpp"

namespace beam::app {

namespace {

using Ctx = MainWindowStartupContext;
using Window = beam::gui_qt::BeamMainWindow;

// Free functions, not nested lambdas: window.setXxxHandler(...) stores
// its handler for later invocation (real button clicks during app.exec(),
// which only happens after wireMainWindow() itself has already returned).
// A lambda that captures another wireMainWindow()-local lambda by
// reference (as nested `[&]` lambdas calling each other by name would)
// dangles the moment wireMainWindow() returns and its own stack frame is
// gone -- free functions taking ctx/window as explicit parameters have no
// such lifetime dependency.

void recomputeSafety(Ctx& ctx, Window& window) {
    ctx.pre.currentRegistrationComplete = ctx.currentRegistrationComplete;
    ctx.pre.mriRegistrationComplete = ctx.mriRegistrationComplete;
    ctx.pre.throughTransmitAtt = ctx.tt.amp;
    ctx.safety = beam::gui::checkSonicationSafety(ctx.sonications, ctx.pre);
    window.setSafetyReport(ctx.safety);
}

void refreshRegistrationCheck(Ctx& ctx, Window& window) {
    window.setRegistrationCheckLampState(
        beam::gui::computeRegistrationCheckLampState(ctx.mriRegistrationComplete, ctx.currentRegistrationComplete));
}

void applyRegistrationResult(Ctx& ctx, Window& window, const beam::registration::AffineArrayResult& result) {
    std::vector<beam::gui_qt::FiducialRow> rows;
    rows.reserve(result.fiducialMarkers.size());
    ctx.fiducialPositionsMm.clear();
    for (const beam::registration::FiducialMarker& m : result.fiducialMarkers) {
        beam::gui_qt::FiducialRow row;
        row.name = QString::fromStdString(m.name);
        row.x = m.position.x() * 1000.0;
        row.y = m.position.y() * 1000.0;
        row.z = m.position.z() * 1000.0;
        row.group = QStringLiteral("Transducer");
        ctx.fiducialPositionsMm.emplace_back(row.x, row.y, row.z);
        rows.push_back(row);
    }
    ctx.fiducialModel.setRows(std::move(rows));

    ctx.centerArrayMm = Eigen::Vector3d::Zero();
    for (const beam::array::ArrayElement& element : result.arrayData.arrayTotal.element) {
        ctx.centerArrayMm += element.center;
    }
    ctx.centerArrayMm = (ctx.centerArrayMm / static_cast<double>(result.arrayData.arrayTotal.element.size())) * 1000.0;

    if (ctx.mriAxes.has_value()) {
        ctx.arrayMask = beam::gui::rasterizeArrayOntoMriGrid(result.arrayData.arrayTotal, *ctx.mriAxes, ctx.mriVolume.nx,
                                                          ctx.mriVolume.ny, ctx.mriVolume.nz);
        ctx.fiducialMask = beam::gui::rasterizeFiducialMarkersOntoMriGrid(ctx.fiducialPositionsMm, *ctx.mriAxes,
                                                                       ctx.mriVolume.nx, ctx.mriVolume.ny, ctx.mriVolume.nz);
        ctx.focusMask = beam::gui::rasterizeFocusEllipsoidOntoMriGrid(ctx.centerArrayMm, *ctx.mriAxes, ctx.mriVolume.nx,
                                                                   ctx.mriVolume.ny, ctx.mriVolume.nz);
    }
    window.setMriOverlays(ctx.arrayMask, ctx.fiducialMask, ctx.focusMask);
    window.setArrayCenterMm(ctx.centerArrayMm);
}

void loadTreatmentSession(Ctx& ctx, Window& window, const QString& protocolName, int visitNumber) {
    const std::vector<beam::gui::TreatmentProtocolRecord>& records =
        beam::gui::currentSessionRows(ctx.treatmentProtocols, protocolName.toStdString(), visitNumber);
    std::vector<beam::gui_qt::TreatmentProtocolRow> rows;
    rows.reserve(records.size());
    for (const beam::gui::TreatmentProtocolRecord& r : records) rows.push_back(beam::app::toRow(r));
    ctx.protocolModel.setRows(std::move(rows));
    (void)window;
}

void persistCurrentTreatmentSession(Ctx& ctx, Window& window) {
    std::vector<beam::gui::TreatmentProtocolRecord> records;
    records.reserve(ctx.protocolModel.rows().size());
    for (const beam::gui_qt::TreatmentProtocolRow& r : ctx.protocolModel.rows()) records.push_back(beam::app::toRecord(r));
    beam::gui::setCurrentSessionRows(ctx.treatmentProtocols, window.currentTreatmentProtocolName().toStdString(),
                                     window.currentVisitNumber(), std::move(records));
}

void rebuildCorrectionCharts(Ctx& ctx, Window& window) {
    window.setCorrectionCharts(
        beam::gui_qt::buildTransmissionBarChart(ctx.bars),
        beam::gui_qt::buildRfWaveformChart(ctx.ch0, ctx.f0, ctx.yLimit, QStringLiteral("First array")),
        beam::gui_qt::buildRfWaveformChart(ctx.ch1, ctx.f1, ctx.yLimit, QStringLiteral("Second array")));
}

void refreshCharts(Ctx& ctx, Window& window) {
    window.setPulseWaveformChart(beam::gui_qt::buildPulseWaveformChart(
        ctx.pulsePlotForShownRow(), QStringLiteral("Pulse waveform"), QStringLiteral("Pulse Interval (s)")));
    window.setSonicationTimelineChart(
        beam::gui_qt::buildSonicationTimelineChart(ctx.timelineForShownRow(), QStringLiteral("Sonication timeline")));
}

void performLoadMri(Ctx& ctx, Window& window, const QString& path) {
    const beam::app::MriLoadResult r = beam::app::tryLoadMri(path.toStdString());
    if (!r.error.empty()) {
        window.setRegisterStatus(QStringLiteral("Load MRI failed: %1").arg(QString::fromStdString(r.error)));
        return;
    }
    ctx.mriVolume = r.volume;
    ctx.mriAxes = r.axes;
    ctx.mriSource = "file:" + path.toStdString();

    const std::vector<beam::registration::FiducialMarker> markers =
        beam::registration::setArrayFiducialMarkers(ctx.originArrayData);
    std::vector<beam::gui_qt::FiducialRow> rows;
    rows.reserve(markers.size());
    ctx.fiducialPositionsMm.clear();
    for (const beam::registration::FiducialMarker& m : markers) {
        beam::gui_qt::FiducialRow row;
        row.name = QString::fromStdString(m.name);
        row.x = m.position.x() * 1000.0;
        row.y = m.position.y() * 1000.0;
        row.z = m.position.z() * 1000.0;
        row.group = QStringLiteral("Transducer");
        ctx.fiducialPositionsMm.emplace_back(row.x, row.y, row.z);
        rows.push_back(row);
    }
    ctx.fiducialModel.setRows(std::move(rows));

    ctx.centerArrayMm = Eigen::Vector3d::Zero();
    for (const beam::array::ArrayElement& element : ctx.originArrayData.arrayTotal.element) {
        ctx.centerArrayMm += element.center;
    }
    ctx.centerArrayMm = (ctx.centerArrayMm / static_cast<double>(ctx.originArrayData.arrayTotal.element.size())) * 1000.0;

    ctx.mriResolution = beam::mri::VoxelResolution{};
    ctx.mriDisplayWindow = beam::mri::computeDisplayWindow(ctx.mriVolume);
    ctx.arrayMask = beam::mri::Volume3D{};
    ctx.fiducialMask = beam::mri::Volume3D{};
    ctx.focusMask = beam::mri::Volume3D{};
    if (ctx.mriAxes.has_value()) {
        ctx.mriResolution = beam::mri::computeVoxelResolution(*ctx.mriAxes);
        const beam::registration::AffineArrayResult centeredArray =
            beam::gui::centerArrayOnMri(ctx.originArrayData, *ctx.mriAxes);
        ctx.arrayMask = beam::gui::rasterizeArrayOntoMriGrid(centeredArray.arrayData.arrayTotal, *ctx.mriAxes,
                                                          ctx.mriVolume.nx, ctx.mriVolume.ny, ctx.mriVolume.nz);
        ctx.fiducialMask = beam::gui::rasterizeFiducialMarkersOntoMriGrid(ctx.fiducialPositionsMm, *ctx.mriAxes, ctx.mriVolume.nx,
                                                                       ctx.mriVolume.ny, ctx.mriVolume.nz);
        ctx.focusMask = beam::gui::rasterizeFocusEllipsoidOntoMriGrid(ctx.centerArrayMm, *ctx.mriAxes, ctx.mriVolume.nx,
                                                                   ctx.mriVolume.ny, ctx.mriVolume.nz);
    }
    window.setMriVolume(ctx.mriVolume);
    window.setMriAxes(ctx.mriAxes);
    window.setMriOverlays(ctx.arrayMask, ctx.fiducialMask, ctx.focusMask);
    window.setArrayCenterMm(ctx.centerArrayMm);
    window.setRegisterStatus(QStringLiteral("Loaded MRI: %1").arg(path));
}

void performRegistration(Ctx& ctx, Window& window) {
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const beam::gui_qt::FiducialRow& row : ctx.fiducialModel.rows()) {
        mriFiducialsMm.emplace_back(row.x, row.y, row.z);
    }
    try {
        applyRegistrationResult(ctx, window, beam::registration::registerArrayToFiducials(ctx.originArrayData, mriFiducialsMm));
        window.setRegisterStatus(QStringLiteral("Registered."));
        ctx.mriRegistrationComplete = true;
        ctx.currentRegistrationComplete = false;
        refreshRegistrationCheck(ctx, window);
        recomputeSafety(ctx, window);
    } catch (const std::exception& e) {
        window.setRegisterStatus(QStringLiteral("Registration failed: %1").arg(QString::fromStdString(e.what())));
    }
}

void performRegisterCurrentPosition(Ctx& ctx, Window& window, double horizontalValue, double verticalValue) {
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const beam::gui_qt::FiducialRow& row : ctx.fiducialModel.rows()) {
        mriFiducialsMm.emplace_back(row.x, row.y, row.z);
    }
    try {
        applyRegistrationResult(ctx, window,
                                beam::registration::registerCurrentTransducerPosition(
                                    ctx.originArrayData, mriFiducialsMm, horizontalValue, verticalValue));
        window.setRegisterStatus(QStringLiteral("Registered to current position (h=%1, v=%2).")
                                      .arg(horizontalValue)
                                      .arg(verticalValue));
        ctx.currentRegistrationComplete = true;
        refreshRegistrationCheck(ctx, window);
        recomputeSafety(ctx, window);
    } catch (const std::exception& e) {
        window.setRegisterStatus(QStringLiteral("Registration failed: %1").arg(QString::fromStdString(e.what())));
    }
}

void performNewVisit(Ctx& ctx, Window& window) {
    const std::string protocolName = window.currentTreatmentProtocolName().toStdString();
    beam::gui::addTreatmentProtocolSession(beam::gui::findProtocol(ctx.treatmentProtocols, protocolName),
                                           ctx.treatmentProtocolBlankRows.at(protocolName));
    std::vector<int> visits;
    for (const beam::gui::TreatmentSession& s : beam::gui::findProtocol(ctx.treatmentProtocols, protocolName).sessions) {
        visits.push_back(s.visitNumber);
    }
    const int newVisit = visits.back();
    window.setVisitNumbers(visits, newVisit);
    loadTreatmentSession(ctx, window, window.currentTreatmentProtocolName(), newVisit);
}

void performRunCorrection(Ctx& ctx, Window& window) {
    const std::vector<std::string> ports = beam::serialcom::listAvailableComPorts();
    if (ports.empty()) {
        window.setCorrectionStatus(QStringLiteral("No serial port available -- showing previous data."));
        return;
    }
    try {
        beam::serialcom::SerialPort link(ports.front());
        beam::serialcom::sendSerialCommand(link, "Correction");
        std::vector<double> floatData;
        constexpr int kMaxLines = 100000;  // bounds the poll loop
        for (int i = 0; i < kMaxLines && link.bytesAvailable() > 0; ++i) {
            const std::string line = link.readLine();
            if (line.empty()) break;
            const std::vector<double> vals = beam::correction::parseCorrectionWaveformLine(line);
            floatData.insert(floatData.end(), vals.begin(), vals.end());
        }
        const beam::correction::ReceiveWaveformSplit split = beam::correction::splitAndFilterReceiveWaveform(floatData);
        ctx.ch0 = split.ch0rcv;
        ctx.ch1 = split.ch1rcv;
        ctx.tt = beam::correction::throughTransmitAmplitude(ctx.ch0, ctx.ch1);
        ctx.bars = beam::gui::computeAvgTransmissionBars(ctx.tt.amp, ctx.tt.amp, ctx.kCouplingThreshold);
        ctx.f0 = beam::correction::filterTransmitSignal(ctx.ch0).filtWave;
        ctx.f1 = beam::correction::filterTransmitSignal(ctx.ch1).filtWave;
        ctx.yLimit = beam::gui::computeRfPlotYLimit(ctx.f0, ctx.f1);
        rebuildCorrectionCharts(ctx, window);
        recomputeSafety(ctx, window);
        window.setCorrectionStatus(QStringLiteral("Updated from %1.").arg(QString::fromStdString(ports.front())));
    } catch (const std::exception& e) {
        window.setCorrectionStatus(QStringLiteral("Run Correction failed: %1").arg(QString::fromStdString(e.what())));
    }
}

}  // namespace

MainWindowCheckHooks wireMainWindow(beam::gui_qt::BeamMainWindow& window, MainWindowStartupContext& ctx) {
    recomputeSafety(ctx, window);
    window.setArrayCenterMm(ctx.centerArrayMm);
    window.setStimParamTableModel(&ctx.tableModel);
    window.setTreatmentProtocolTableModel(&ctx.protocolModel);

    {
        std::vector<QString> names;
        names.reserve(ctx.treatmentProtocolNames.size());
        for (const std::string& n : ctx.treatmentProtocolNames) names.push_back(QString::fromStdString(n));
        window.setTreatmentProtocolNames(names);
    }
    window.setVisitNumbers({1}, 1);
    window.setTreatmentProtocolSelectorHandler(
        [&ctx, &window](const QString& protocolName, int visitNumber) {
            loadTreatmentSession(ctx, window, protocolName, visitNumber);
        });
    window.setNewVisitHandler([&ctx, &window]() { performNewVisit(ctx, window); });
    window.setTreatmentProtocolDataChangedHandler(
        [&ctx, &window]() { persistCurrentTreatmentSession(ctx, window); });
    window.setSessionLoadedHandler([&ctx, &window]() { persistCurrentTreatmentSession(ctx, window); });

    window.setFiducialTableModel(&ctx.fiducialModel);
    window.setMriVolume(ctx.mriVolume);
    window.setMriAxes(ctx.mriAxes);
    window.setMriOverlays(ctx.arrayMask, ctx.fiducialMask, ctx.focusMask);
    if (ctx.mriAxes.has_value()) {
        window.centerSagittalSliceOnMm(ctx.centerArrayMm.x());
    }

    const beam::app::DefaultSubjectCrf defaultCrf = beam::app::defaultSubjectCrf();
    window.setSiteId(defaultCrf.siteId);
    window.setParticipantId(defaultCrf.participantId);
    window.setVisitNumberField(defaultCrf.visitNumber);
    window.setPulseWaveformChart(beam::gui_qt::buildPulseWaveformChart(
        ctx.pulsePlotForShownRow(), QStringLiteral("Pulse waveform"), QStringLiteral("Pulse Interval (s)")));
    window.setSonicationTimelineChart(beam::gui_qt::buildSonicationTimelineChart(
        ctx.timelineForShownRow(), QStringLiteral("Sonication timeline")));
    rebuildCorrectionCharts(ctx, window);

    QObject::connect(&ctx.tableModel, &beam::gui_qt::StimParamTableModel::showFlagsChanged,
                     [&ctx, &window]() { refreshCharts(ctx, window); });

    window.setSonicateHandler([&ctx, &window](bool waitForExternalTrigger) {
        const beam::gui::SonicateOutcome outcome =
            beam::gui::prepareSonication(ctx.stimParamsForShownRow(), ctx.tt.amp, ctx.kCouplingThreshold, waitForExternalTrigger);
        if (!outcome.started) {
            QString msg = QStringLiteral("Not started: ");
            for (std::size_t i = 0; i < outcome.messages.size(); ++i) {
                if (i != 0) msg += QStringLiteral("; ");
                msg += QString::fromStdString(outcome.messages[i]);
            }
            window.setSonicationStatus(msg);
            return;
        }
        window.startSonicationCountdown(static_cast<int>(outcome.durationSeconds));

        const std::size_t maskingAudioLen =
            beam::gui::prepareShamSonication(ctx.stimParamsForShownRow(), beam::app::syntheticShamBurstSound(), beam::app::kShamSoundFs)
                .maskingAudio.size();

        const beam::gui_qt::StimParamRow& shownRow =
            ctx.tableModel.rows()[static_cast<std::size_t>(ctx.tableModel.currentShownRow() - 1)];
        const beam::serialcom::SonicationTiming timing{shownRow.pd, shownRow.pi, shownRow.bd, shownRow.bi,
                                                        shownRow.startTime, shownRow.endTime};
        const std::string cmd = beam::serialcom::setSerialCommandFromStimParams(timing, outcome.dutyCycle);

        const std::vector<std::string> ports = beam::serialcom::listAvailableComPorts();
        QString status = QStringLiteral("Duty cycle %1%2 | %3 | command: %4 | masking audio: %5 samples")
                              .arg(outcome.dutyCycle, 0, 'f', 3)
                              .arg(outcome.waitForTrigger ? QStringLiteral(" (waiting for external trigger)")
                                                          : QStringLiteral(""))
                              .arg(QStringLiteral("%1s duration").arg(outcome.durationSeconds))
                              .arg(QString::fromStdString(cmd))
                              .arg(maskingAudioLen);
        if (ports.empty()) {
            status += QStringLiteral(" | No serial port available -- command not sent.");
        } else {
            try {
                beam::serialcom::SerialPort link(ports.front());
                const bool sent = beam::serialcom::sendSerialCommand(link, cmd);
                const std::string rcv = link.readLine();
                beam::serialcom::clearSerial(link);
                status += sent ? QStringLiteral(" | Sent on %1, reply: \"%2\"")
                                      .arg(QString::fromStdString(ports.front()), QString::fromStdString(rcv))
                                : QStringLiteral(" | Link on %1 closed before send.").arg(QString::fromStdString(ports.front()));
            } catch (const std::exception& e) {
                status += QStringLiteral(" | Failed to open %1: %2")
                               .arg(QString::fromStdString(ports.front()), QString::fromStdString(e.what()));
            }
        }
        window.setSonicationStatus(status);
    });

    window.setShamHandler([&ctx, &window]() {
        const beam::gui::ShamSonicationOutcome outcome =
            beam::gui::prepareShamSonication(ctx.stimParamsForShownRow(), beam::app::syntheticShamBurstSound(), beam::app::kShamSoundFs);
        window.startSonicationCountdown(static_cast<int>(outcome.durationSeconds));
        window.setSonicationStatus(QStringLiteral("Sham: masking audio ready (%1 samples, %2s), no ultrasound sent.")
                                        .arg(outcome.maskingAudio.size())
                                        .arg(outcome.durationSeconds));
    });

    {
        std::vector<QString> ports;
        for (const std::string& port : beam::serialcom::listAvailableComPorts()) {
            ports.push_back(QString::fromStdString(port));
        }
        window.setSerialPorts(ports);
    }
    window.setSerialConnectHandler([&ctx, &window](QString port) {
        window.setSerialConnectedState(false);
        try {
            ctx.serialLink = std::make_unique<beam::serialcom::SerialPort>(port.toStdString());
            window.setSerialConnectedState(true);
            window.setSonicationStatus(QStringLiteral("Connected to %1.").arg(port));
        } catch (const std::exception& e) {
            ctx.serialLink.reset();
            window.setSonicationStatus(QStringLiteral("Connection failed: %1").arg(QString::fromStdString(e.what())));
        }
    });

    window.setGetParamsHandler([&window]() {
        window.setSonicationStatus(
            QStringLiteral("Get Params has no effect here -- the source's own action just exposes MATLAB's "
                           "workspace for debugging, nothing this build has an equivalent to."));
    });

    window.setAbortSonicationHandler([&window]() {
        const std::vector<std::string> ports = beam::serialcom::listAvailableComPorts();
        if (ports.empty()) {
            window.setSonicationStatus(QStringLiteral("Abort: no serial port available -- command not sent."));
            return;
        }
        try {
            beam::serialcom::SerialPort link(ports.front());
            beam::serialcom::sendSerialCommand(link, "Correction");
            window.setSonicationStatus(
                QStringLiteral("Abort command sent on %1.").arg(QString::fromStdString(ports.front())));
        } catch (const std::exception& e) {
            window.setSonicationStatus(QStringLiteral("Abort failed: %1").arg(QString::fromStdString(e.what())));
        }
    });

    window.setLoadMriHandler([&ctx, &window](const QString& path) { performLoadMri(ctx, window, path); });
    window.setRegisterHandler([&ctx, &window]() { performRegistration(ctx, window); });
    window.setRegisterCurrentPositionHandler([&ctx, &window](double horizontalValue, double verticalValue) {
        performRegisterCurrentPosition(ctx, window, horizontalValue, verticalValue);
    });
    window.setPositionSlidersChangedHandler([&ctx, &window]() {
        ctx.currentRegistrationComplete = false;
        refreshRegistrationCheck(ctx, window);
    });
    window.setRunCorrectionHandler([&ctx, &window]() { performRunCorrection(ctx, window); });

    return MainWindowCheckHooks{
        [&ctx, &window](const QString& path) { performLoadMri(ctx, window, path); },
        [&ctx, &window]() { performRegistration(ctx, window); },
        [&ctx, &window](double horizontalValue, double verticalValue) {
            performRegisterCurrentPosition(ctx, window, horizontalValue, verticalValue);
        },
        [&ctx, &window]() { performNewVisit(ctx, window); },
    };
}

}  // namespace beam::app
