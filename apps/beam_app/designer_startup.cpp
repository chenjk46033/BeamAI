#include "designer_startup.hpp"

#include <algorithm>
#include <cstdio>

#include "gui/initial_placement_presenter.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "gui/registration_check_presenter.hpp"
#include "gui/sham_orchestrator.hpp"
#include "gui/sonicate_orchestrator.hpp"
#include "gui_qt/startup_screen_window.hpp"
#include "registration/array_transform.hpp"
#include "registration/fiducial_markers.hpp"
#include "serialcom/command.hpp"
#include "startup_data.hpp"

namespace beam::app {

namespace {

// The array's own nominal fiducials, always computed fresh from the
// fixed (never-reassigned) origin array data -- this window has no
// editable fiducial table the way BeamMainWindow does (the real
// RegisterTab doesn't have one either).
std::vector<Eigen::Vector3d> currentMriFiducialsMm(const beam::array::ArrayData& originArrayData) {
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const beam::registration::FiducialMarker& m : beam::registration::setArrayFiducialMarkers(originArrayData)) {
        mriFiducialsMm.emplace_back(m.position.x() * 1000.0, m.position.y() * 1000.0, m.position.z() * 1000.0);
    }
    return mriFiducialsMm;
}

void refreshRegistrationCheck(DesignerStartupContext& ctx, beam::gui_qt::DesignerStartupWindow& window) {
    window.setRegistrationCheckLampState(
        beam::gui::computeRegistrationCheckLampState(ctx.mriRegistrationComplete, ctx.currentRegistrationComplete));
}

void recomputeSafety(DesignerStartupContext& ctx, beam::gui_qt::DesignerStartupWindow& window) {
    ctx.pre.currentRegistrationComplete = ctx.currentRegistrationComplete;
    ctx.pre.mriRegistrationComplete = ctx.mriRegistrationComplete;
    ctx.pre.throughTransmitAtt = ctx.tt.amp;
    ctx.safety = beam::gui::checkSonicationSafety(ctx.sonications, ctx.pre);
    window.setSafetyReport(ctx.safety);
}

void applyRegistrationResult(DesignerStartupContext& ctx, beam::gui_qt::DesignerStartupWindow& window,
                             const beam::registration::AffineArrayResult& result) {
    Eigen::Vector3d centerArrayMm = Eigen::Vector3d::Zero();
    for (const beam::array::ArrayElement& element : result.arrayData.arrayTotal.element) {
        centerArrayMm += element.center;
    }
    centerArrayMm = (centerArrayMm / static_cast<double>(result.arrayData.arrayTotal.element.size())) * 1000.0;

    if (ctx.mriAxes.has_value()) {
        std::vector<Eigen::Vector3d> fiducialPositionsMm;
        std::vector<std::pair<QString, Eigen::Vector3d>> fiducialMarkersMm;
        for (const beam::registration::FiducialMarker& m : result.fiducialMarkers) {
            fiducialPositionsMm.emplace_back(m.position.x() * 1000.0, m.position.y() * 1000.0, m.position.z() * 1000.0);
            fiducialMarkersMm.emplace_back(QString::fromStdString(m.name), fiducialPositionsMm.back());
        }
        window.setMriOverlays(
            beam::gui::rasterizeArrayOntoMriGrid(result.arrayData.arrayTotal, *ctx.mriAxes, ctx.mriVolume.nx,
                                                 ctx.mriVolume.ny, ctx.mriVolume.nz),
            beam::gui::rasterizeFiducialMarkersOntoMriGrid(fiducialPositionsMm, *ctx.mriAxes, ctx.mriVolume.nx,
                                                           ctx.mriVolume.ny, ctx.mriVolume.nz),
            beam::gui::rasterizeFocusEllipsoidOntoMriGrid(centerArrayMm, *ctx.mriAxes, ctx.mriVolume.nx,
                                                          ctx.mriVolume.ny, ctx.mriVolume.nz));
        window.setFiducialMarkers(std::move(fiducialMarkersMm));
    }
    window.setArrayCenterMm(centerArrayMm);
}

}  // namespace

int runDesignerStartup(DesignerStartupContext& ctx, QApplication& app) {
    beam::gui_qt::DesignerStartupWindow designerWindow;
    ctx.pre.currentRegistrationComplete = ctx.currentRegistrationComplete;
    ctx.pre.mriRegistrationComplete = ctx.mriRegistrationComplete;
    ctx.pre.throughTransmitAtt = ctx.tt.amp;
    ctx.safety = beam::gui::checkSonicationSafety(ctx.sonications, ctx.pre);
    designerWindow.setSafetyReport(ctx.safety);
    designerWindow.setArrayCenterMm(ctx.centerArrayMm);
    designerWindow.setMriVolume(ctx.mriVolume);
    designerWindow.setMriAxes(ctx.mriAxes);
    designerWindow.setMriOverlays(ctx.arrayMask, ctx.fiducialMask, ctx.focusMask);
    if (ctx.mriAxes.has_value()) {
        designerWindow.centerSagittalSliceOnMm(ctx.centerArrayMm.x());
    }
    if (ctx.mriAxes.has_value()) {
        std::vector<std::pair<QString, Eigen::Vector3d>> fiducialMarkersMm;
        for (const auto& [name, posMm] : beam::app::defaultSubjectFiducialMarkers()) {
            fiducialMarkersMm.emplace_back(QString::fromStdString(name), posMm);
        }
        if (fiducialMarkersMm.empty()) {
            for (const beam::registration::FiducialMarker& m :
                 beam::gui::centerArrayOnMri(ctx.originArrayData, *ctx.mriAxes).fiducialMarkers) {
                fiducialMarkersMm.emplace_back(QString::fromStdString(m.name),
                                                Eigen::Vector3d(m.position * 1000.0));
            }
        }
        designerWindow.setFiducialMarkers(std::move(fiducialMarkersMm));
    }
    refreshRegistrationCheck(ctx, designerWindow);
    designerWindow.setRegisterHandler([&ctx, &designerWindow]() {
        try {
            applyRegistrationResult(ctx, designerWindow,
                                    beam::registration::registerArrayToFiducials(
                                        ctx.originArrayData, currentMriFiducialsMm(ctx.originArrayData)));
            ctx.mriRegistrationComplete = true;
            ctx.currentRegistrationComplete = false;
            refreshRegistrationCheck(ctx, designerWindow);
            recomputeSafety(ctx, designerWindow);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "beam_app --designer-startup: registration failed (%s)\n", e.what());
        }
    });
    designerWindow.setRegisterCurrentPositionHandler([&ctx, &designerWindow](double horizontalValue, double verticalValue) {
        try {
            applyRegistrationResult(ctx, designerWindow,
                                    beam::registration::registerCurrentTransducerPosition(
                                        ctx.originArrayData, currentMriFiducialsMm(ctx.originArrayData),
                                        horizontalValue, verticalValue));
            ctx.currentRegistrationComplete = true;
            refreshRegistrationCheck(ctx, designerWindow);
            recomputeSafety(ctx, designerWindow);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "beam_app --designer-startup: registration failed (%s)\n", e.what());
        }
    });
    designerWindow.setPositionSlidersChangedHandler([&ctx, &designerWindow]() {
        ctx.currentRegistrationComplete = false;
        refreshRegistrationCheck(ctx, designerWindow);
        recomputeSafety(ctx, designerWindow);
    });
    const beam::app::DefaultSubjectCrf defaultCrf = beam::app::defaultSubjectCrf();
    designerWindow.setSiteId(defaultCrf.siteId);
    designerWindow.setParticipantId(defaultCrf.participantId);
    designerWindow.setVisitNumberField(defaultCrf.visitNumber);
    designerWindow.setLoadMriHandler([&](const QString& path) {
        const beam::app::MriLoadResult r = beam::app::tryLoadMri(path.toStdString());
        if (!r.error.empty()) {
            std::fprintf(stderr, "beam_app --designer-startup: failed to load MRI %s (%s)\n",
                        path.toStdString().c_str(), r.error.c_str());
            return;
        }
        beam::mri::Volume3D loadedVolume = r.volume;
        std::optional<beam::mri::RasAxisVectors> loadedAxes = r.axes;
        Eigen::Vector3d loadedCenterArrayMm = Eigen::Vector3d::Zero();
        for (const beam::array::ArrayElement& element : ctx.originArrayData.arrayTotal.element) {
            loadedCenterArrayMm += element.center;
        }
        loadedCenterArrayMm =
            (loadedCenterArrayMm / static_cast<double>(ctx.originArrayData.arrayTotal.element.size())) * 1000.0;
        beam::mri::Volume3D loadedArrayMask;
        beam::mri::Volume3D loadedFiducialMask;
        beam::mri::Volume3D loadedFocusMask;
        if (loadedAxes.has_value()) {
            const beam::registration::AffineArrayResult centeredArray =
                beam::gui::centerArrayOnMri(ctx.originArrayData, *loadedAxes);
            loadedArrayMask = beam::gui::rasterizeArrayOntoMriGrid(
                centeredArray.arrayData.arrayTotal, *loadedAxes, loadedVolume.nx, loadedVolume.ny,
                loadedVolume.nz);
            loadedFocusMask = beam::gui::rasterizeFocusEllipsoidOntoMriGrid(
                loadedCenterArrayMm, *loadedAxes, loadedVolume.nx, loadedVolume.ny, loadedVolume.nz);
        }
        designerWindow.setMriVolume(loadedVolume);
        designerWindow.setMriAxes(loadedAxes);
        designerWindow.setMriOverlays(loadedArrayMask, loadedFiducialMask, loadedFocusMask);
        designerWindow.setArrayCenterMm(loadedCenterArrayMm);
        if (loadedAxes.has_value()) {
            designerWindow.centerSagittalSliceOnMm(loadedCenterArrayMm.x());
        }
    });

    designerWindow.setStimParamTableModel(&ctx.tableModel);
    designerWindow.setSonicateHandler([&](bool waitForExternalTrigger) {
        const beam::gui::SonicateOutcome outcome = beam::gui::prepareSonication(
            ctx.stimParamsForShownRow(), ctx.tt.amp, ctx.kCouplingThreshold, waitForExternalTrigger);
        if (!outcome.started) {
            QString msg = QStringLiteral("Not started: ");
            for (std::size_t i = 0; i < outcome.messages.size(); ++i) {
                if (i != 0) msg += QStringLiteral("; ");
                msg += QString::fromStdString(outcome.messages[i]);
            }
            designerWindow.setSonicationStatus(msg);
            return;
        }
        designerWindow.startSonicationCountdown(static_cast<int>(outcome.durationSeconds));

        const std::size_t maskingAudioLen =
            beam::gui::prepareShamSonication(ctx.stimParamsForShownRow(), beam::app::syntheticShamBurstSound(),
                                             beam::app::kShamSoundFs)
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
                                : QStringLiteral(" | Link on %1 closed before send.")
                                      .arg(QString::fromStdString(ports.front()));
            } catch (const std::exception& e) {
                status += QStringLiteral(" | Failed to open %1: %2")
                               .arg(QString::fromStdString(ports.front()), QString::fromStdString(e.what()));
            }
        }
        designerWindow.setSonicationStatus(status);
    });
    designerWindow.setShamHandler([&]() {
        const beam::gui::ShamSonicationOutcome outcome = beam::gui::prepareShamSonication(
            ctx.stimParamsForShownRow(), beam::app::syntheticShamBurstSound(), beam::app::kShamSoundFs);
        designerWindow.startSonicationCountdown(static_cast<int>(outcome.durationSeconds));
        designerWindow.setSonicationStatus(
            QStringLiteral("Sham: masking audio ready (%1 samples, %2s), no ultrasound sent.")
                .arg(outcome.maskingAudio.size())
                .arg(outcome.durationSeconds));
    });
    {
        std::vector<QString> ports;
        for (const std::string& port : beam::serialcom::listAvailableComPorts()) {
            ports.push_back(QString::fromStdString(port));
        }
        designerWindow.setSerialPorts(ports);
    }
    designerWindow.setSerialConnectHandler([&](QString port) {
        designerWindow.setSerialConnectedState(false);
        try {
            ctx.serialLink = std::make_unique<beam::serialcom::SerialPort>(port.toStdString());
            designerWindow.setSerialConnectedState(true);
            designerWindow.setSonicationStatus(QStringLiteral("Connected to %1.").arg(port));
        } catch (const std::exception& e) {
            ctx.serialLink.reset();
            designerWindow.setSonicationStatus(
                QStringLiteral("Connection failed: %1").arg(QString::fromStdString(e.what())));
        }
    });
    designerWindow.setGetParamsHandler([&designerWindow]() {
        designerWindow.setSonicationStatus(
            QStringLiteral("Get Params has no effect here -- the source's own action just exposes MATLAB's "
                           "workspace for debugging, nothing this build has an equivalent to."));
    });
    designerWindow.setAbortSonicationHandler([&designerWindow]() {
        const std::vector<std::string> ports = beam::serialcom::listAvailableComPorts();
        if (ports.empty()) {
            designerWindow.setSonicationStatus(
                QStringLiteral("Abort: no serial port available -- command not sent."));
            return;
        }
        try {
            beam::serialcom::SerialPort link(ports.front());
            beam::serialcom::sendSerialCommand(link, "Correction");
            designerWindow.setSonicationStatus(
                QStringLiteral("Abort command sent on %1.").arg(QString::fromStdString(ports.front())));
        } catch (const std::exception& e) {
            designerWindow.setSonicationStatus(
                QStringLiteral("Abort failed: %1").arg(QString::fromStdString(e.what())));
        }
    });

    designerWindow.setTreatmentProtocolTableModel(&ctx.protocolModel);
    {
        std::vector<QString> names;
        names.reserve(ctx.treatmentProtocolNames.size());
        for (const std::string& n : ctx.treatmentProtocolNames) names.push_back(QString::fromStdString(n));
        designerWindow.setTreatmentProtocolNames(names);
    }
    designerWindow.setVisitNumbers({1}, 1);
    const auto loadDesignerTreatmentSession = [&](const QString& protocolName, int visitNumber) {
        beam::gui::TreatmentProtocol& protocol =
            beam::gui::findProtocol(ctx.treatmentProtocols, protocolName.toStdString());
        const bool visitExists =
            std::any_of(protocol.sessions.begin(), protocol.sessions.end(),
                        [visitNumber](const beam::gui::TreatmentSession& s) { return s.visitNumber == visitNumber; });
        if (!visitExists) {
            std::vector<int> visits;
            for (const beam::gui::TreatmentSession& s : protocol.sessions) visits.push_back(s.visitNumber);
            designerWindow.setVisitNumbers(visits, visits.front());
            return;
        }
        const std::vector<beam::gui::TreatmentProtocolRecord>& records =
            beam::gui::currentSessionRows(ctx.treatmentProtocols, protocolName.toStdString(), visitNumber);
        std::vector<beam::gui_qt::TreatmentProtocolRow> rows;
        rows.reserve(records.size());
        for (const beam::gui::TreatmentProtocolRecord& r : records) rows.push_back(beam::app::toRow(r));
        ctx.protocolModel.setRows(std::move(rows));
    };
    designerWindow.setTreatmentProtocolSelectorHandler(loadDesignerTreatmentSession);
    designerWindow.setNewVisitHandler([&]() {
        const std::string protocolName = designerWindow.currentTreatmentProtocolName().toStdString();
        beam::gui::addTreatmentProtocolSession(beam::gui::findProtocol(ctx.treatmentProtocols, protocolName),
                                               ctx.treatmentProtocolBlankRows.at(protocolName));
        std::vector<int> visits;
        for (const beam::gui::TreatmentSession& s :
             beam::gui::findProtocol(ctx.treatmentProtocols, protocolName).sessions) {
            visits.push_back(s.visitNumber);
        }
        const int newVisit = visits.back();
        designerWindow.setVisitNumbers(visits, newVisit);
        loadDesignerTreatmentSession(designerWindow.currentTreatmentProtocolName(), newVisit);
    });
    designerWindow.setTreatmentProtocolDataChangedHandler([&]() {
        std::vector<beam::gui::TreatmentProtocolRecord> records;
        records.reserve(ctx.protocolModel.rows().size());
        for (const beam::gui_qt::TreatmentProtocolRow& r : ctx.protocolModel.rows())
            records.push_back(beam::app::toRecord(r));
        beam::gui::setCurrentSessionRows(ctx.treatmentProtocols,
                                         designerWindow.currentTreatmentProtocolName().toStdString(),
                                         designerWindow.currentVisitNumber(), std::move(records));
    });
    loadDesignerTreatmentSession(designerWindow.currentTreatmentProtocolName(),
                                 designerWindow.currentVisitNumber());

    designerWindow.showMaximized();
    return app.exec();
}

}  // namespace beam::app
