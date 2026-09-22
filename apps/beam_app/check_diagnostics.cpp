#include "check_diagnostics.hpp"

#include <cmath>
#include <cstdio>
#include <numbers>

#include <QByteArray>
#include <QDir>
#include <QFile>

#include "correction/receive_waveform.hpp"
#include "gui/countdown_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/sham_orchestrator.hpp"
#include "gui/sonicate_orchestrator.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "startup_data.hpp"

namespace beam::app {

int runCheckDiagnostics(CheckContext& ctx) {
    // axisExtent()/2, clamped to >= 1 -- a fixed index here is UB for any
    // MRI with fewer axial slices than that.
    const int axialSliceIndex = std::max<int>(static_cast<int>(ctx.mriVolume.nz / 2), 1);
    const Eigen::MatrixXd axialSlice = beam::mri::getSliceImage(ctx.mriVolume, axialSliceIndex, "axial");

    const std::size_t mriVolumeVoxelsBefore = ctx.mriVolume.kSlices.size();
    ctx.performLoadMri(QStringLiteral("Z:\\this-path-does-not-exist.nii"));
    const bool loadMriMenuHandlesBadPathGracefully = ctx.mriVolume.kSlices.size() == mriVolumeVoxelsBefore;
    const beam::gui::BestTargets best = ctx.protocolModel.computeBestTargets(beam::gui::AccFlag::kOther);
    std::string bestNames;
    for (std::size_t i = 0; i < best.name.size(); ++i) {
        if (i != 0) bestNames += ",";
        bestNames += best.name[i];
    }

    const std::size_t rowsBefore = ctx.tableModel.rows().size();
    ctx.tableModel.addSonicationRow();
    const std::size_t rowsAfterAdd = ctx.tableModel.rows().size();
    beam::gui_qt::StimParamRow lastRow = ctx.tableModel.rows().back();
    lastRow.show = true;
    {
        std::vector<beam::gui_qt::StimParamRow> rows = ctx.tableModel.rows();
        rows.back() = lastRow;
        ctx.tableModel.setRows(std::move(rows));
    }
    ctx.tableModel.removeMarkedRows();
    const std::size_t rowsAfterRemove = ctx.tableModel.rows().size();

    const Eigen::Vector3d registrationCenterBeforeMm = ctx.centerArrayMm;
    {
        std::vector<beam::gui_qt::FiducialRow> rows = ctx.fiducialModel.rows();
        for (beam::gui_qt::FiducialRow& row : rows) row.x += 12.0;  // mm
        ctx.fiducialModel.setRows(std::move(rows));
    }
    ctx.performRegistration();
    const bool registrationMovedArray = !ctx.centerArrayMm.isApprox(registrationCenterBeforeMm, 1e-6) &&
                                        std::abs((ctx.centerArrayMm - registrationCenterBeforeMm).x() - 12.0) < 1e-3;

    const Eigen::Vector3d beforeCurrentPositionMm = ctx.centerArrayMm;
    ctx.performRegisterCurrentPosition(1.0, 2.0);
    const bool currentPositionMoved =
        std::abs((ctx.centerArrayMm - beforeCurrentPositionMm).norm() - 10.0) < 1e-3;

    double arrayMaskVoxels = 0.0;
    double fiducialMaskVoxels = 0.0;
    double focusMaskCoverage = 0.0;
    for (const Eigen::MatrixXd& slice : ctx.arrayMask.kSlices) arrayMaskVoxels += slice.sum();
    for (const Eigen::MatrixXd& slice : ctx.fiducialMask.kSlices) fiducialMaskVoxels += slice.sum();
    for (const Eigen::MatrixXd& slice : ctx.focusMask.kSlices) focusMaskCoverage += slice.sum();

    const std::size_t timelineSegments = ctx.timelineForShownRow().size();

    double correctionCh0Len = 0.0;
    double correctionCh1Len = 0.0;
    {
        std::vector<double> floatData;
        for (int lineIdx = 0; lineIdx < 20; ++lineIdx) {
            std::string line;
            for (int j = 0; j < 50; ++j) {
                if (j != 0) line += ",";
                line += std::to_string(std::sin(2.0 * std::numbers::pi * (lineIdx * 50 + j) / 30.0));
            }
            const std::vector<double> vals = beam::correction::parseCorrectionWaveformLine(line);
            floatData.insert(floatData.end(), vals.begin(), vals.end());
        }
        const beam::correction::ReceiveWaveformSplit split =
            beam::correction::splitAndFilterReceiveWaveform(floatData);
        correctionCh0Len = static_cast<double>(split.ch0rcv.size());
        correctionCh1Len = static_cast<double>(split.ch1rcv.size());
    }

    const beam::gui::SonicateOutcome sonicateOutcome =
        beam::gui::prepareSonication(ctx.stimParamsForShownRow(), ctx.tt.amp, ctx.kCouplingThreshold, false);

    ctx.window.startSonicationCountdown(static_cast<int>(sonicateOutcome.durationSeconds));
    const bool countdownTextOk =
        ctx.window.sonicationCountdownText() ==
        QString::fromStdString(beam::gui::countdownDisplayText(static_cast<int>(sonicateOutcome.durationSeconds)));

    const beam::gui::ShamSonicationOutcome shamOutcome = beam::gui::prepareShamSonication(
        ctx.stimParamsForShownRow(), beam::app::syntheticShamBurstSound(), beam::app::kShamSoundFs);
    const std::size_t shamMaskingAudioLen = shamOutcome.maskingAudio.size();

    ctx.protocolModel.setData(ctx.protocolModel.index(0, beam::gui_qt::TreatmentProtocolTableModel::kTarget),
                              QStringLiteral("aMCC1"), Qt::EditRole);
    const bool treatmentEditPersisted =
        beam::gui::currentSessionRows(ctx.treatmentProtocols, "PainSCCandAMCC", 1)[0].target == "aMCC1";
    ctx.performNewVisit();
    const bool treatmentNewVisitAdded =
        beam::gui::findProtocol(ctx.treatmentProtocols, "PainSCCandAMCC").sessions.size() == 2 &&
        ctx.window.currentVisitNumber() == 2 && ctx.protocolModel.rows()[0].target == QStringLiteral("SCC1");
    const bool treatmentFirstVisitUntouched =
        beam::gui::currentSessionRows(ctx.treatmentProtocols, "PainSCCandAMCC", 1)[0].target == "aMCC1";

    const std::size_t fiducialsBefore = ctx.fiducialModel.rows().size();
    const std::size_t stimRowsBefore = ctx.tableModel.rows().size();
    const std::size_t protocolRowsBefore = ctx.protocolModel.rows().size();
    const QString sessionPath = QDir::temp().filePath(QStringLiteral("beam_app_check_session.beamsession"));
    {
        QFile file(sessionPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            const std::string text = beam::gui::serializeSession(ctx.window.collectSessionData());
            file.write(text.data(), static_cast<qint64>(text.size()));
        }
    }
    ctx.fiducialModel.setRows({});
    ctx.tableModel.setRows({});
    ctx.protocolModel.setRows({});
    {
        QFile file(sessionPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::fprintf(stderr, "beam_app --check: failed to reopen session temp file\n");
            return 1;
        }
        const QByteArray bytes = file.readAll();
        ctx.window.applySessionData(
            beam::gui::deserializeSession(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
    }
    QFile::remove(sessionPath);
    const bool sessionRoundTripOk = ctx.fiducialModel.rows().size() == fiducialsBefore &&
                                    ctx.tableModel.rows().size() == stimRowsBefore &&
                                    ctx.protocolModel.rows().size() == protocolRowsBefore;

    std::printf(
        "beam_app --check: safety.pass=%d (\"%s\") | table.rows=%zu shownRow=%d | "
        "coupling.att=%.4f pass=%d | bestTargets=[%s] | rowMgmt %zu->%zu->%zu | "
        "fiducials=%zu | mri=%s %lldx%lldx%lld mriAxialSlice=%lldx%lld | arrayRect=%s | "
        "overlay arrayVoxels=%.0f fiducialVoxels=%.0f focusCoverage=%.2f | "
        "sonicate started=%d dutyCycle=%.3f waitForTrigger=%d countdownTextOk=%d shamAudioLen=%zu | "
        "treatment editPersisted=%d newVisitAdded=%d firstVisitUntouched=%d | "
        "session roundTrip=%d | "
        "register movedArray=%d currentPositionMoved=%d | correction ch0Len=%.0f ch1Len=%.0f | "
        "timelineSegments=%zu | mriResLR=%.3f mriWindowHi=%.2f correctionInitAtt=%.1f | "
        "registrationCheckSonicateEnabled=%d | newProtocolName=\"%s\" exampleTargetHelpTextOk=%d | "
        "loadMriMenuHandlesBadPathGracefully=%d\n",
        ctx.safety.pass ? 1 : 0, ctx.safety.statusText.c_str(), ctx.tableModel.rows().size(),
        ctx.tableModel.currentShownRow(), ctx.tt.amp, ctx.bars.pass ? 1 : 0, bestNames.c_str(), rowsBefore,
        rowsAfterAdd, rowsAfterRemove, ctx.fiducialModel.rows().size(), ctx.mriSource.c_str(),
        static_cast<long long>(ctx.mriVolume.nx), static_cast<long long>(ctx.mriVolume.ny),
        static_cast<long long>(ctx.mriVolume.nz), static_cast<long long>(axialSlice.rows()),
        static_cast<long long>(axialSlice.cols()), ctx.arrayRectSource.c_str(), arrayMaskVoxels, fiducialMaskVoxels,
        focusMaskCoverage,
        sonicateOutcome.started ? 1 : 0, sonicateOutcome.dutyCycle, sonicateOutcome.waitForTrigger ? 1 : 0,
        countdownTextOk ? 1 : 0, shamMaskingAudioLen,
        treatmentEditPersisted ? 1 : 0, treatmentNewVisitAdded ? 1 : 0, treatmentFirstVisitUntouched ? 1 : 0,
        sessionRoundTripOk ? 1 : 0, registrationMovedArray ? 1 : 0, currentPositionMoved ? 1 : 0,
        correctionCh0Len, correctionCh1Len,
        timelineSegments, ctx.mriResolution.lr, ctx.mriDisplayWindow.hi, beam::gui::correctionInitialState().att,
        beam::gui::computeRegistrationCheckLampState(ctx.mriRegistrationComplete, ctx.currentRegistrationComplete)
                .sonicateButtonEnabled
            ? 1
            : 0,
        beam::gui::newProtocolName({"Protocol 0"}).c_str(),
        beam::gui::exampleTargetHelpText("SCC1") == beam::gui::exampleTargetHelpText("ACC") ? 1 : 0,
        loadMriMenuHandlesBadPathGracefully ? 1 : 0);
    return 0;
}

}  // namespace beam::app
