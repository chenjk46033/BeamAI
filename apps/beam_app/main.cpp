// beam_app -- the Beam GUI shell. Shows DesignerStartupWindow, the
// active window (BeamMainWindow only remains as --check's headless
// harness -- see below).
//
//   beam_app                  healthy synthetic state, shows the window
//   beam_app fail             failing safety + weak coupling
//   beam_app --check          headless: populate tabs, print a summary, exit 0
//   beam_app --mri <path.nii> load a real NIfTI file instead of the synthetic volume

#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QApplication>
#include <QLabel>
#include <QDir>
#include <QFile>

#include <Eigen/Core>

#include "array/array_data.hpp"
#include "correction/receive_waveform.hpp"
#include "correction/signal.hpp"
#include "gui/correction_tab_presenter.hpp"
#include "gui/countdown_presenter.hpp"
#include "gui/initial_placement_presenter.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "gui/safety_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/sham_orchestrator.hpp"
#include "gui/sonicate_orchestrator.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "gui/treatment_session_store.hpp"
#include "gui_qt/correction_tab_chart.hpp"
#include "gui_qt/fiducial_table_model.hpp"
#include "gui_qt/main_window.hpp"
#include "gui_qt/sonication_tab_chart.hpp"
#include "gui_qt/startup_screen_window.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "serialcom/command.hpp"
#include "serialcom/serial_port.hpp"
#include "infra_dicom/load_mri_ras.hpp"
#include "mri/mri_loader.hpp"
#include "mri/slice.hpp"
#include "registration/array_transform.hpp"
#include "registration/fiducial_markers.hpp"
#include "stimulation/events.hpp"
#include "safety/intensity.hpp"

#include "check_diagnostics.hpp"
#include "designer_startup.hpp"
#include "main_window_startup.hpp"
#include "startup_data.hpp"

using namespace beam::app;

int main(int argc, char** argv) {
    bool checkOnly = false;
    bool wantFailure = false;
    std::string mriPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) checkOnly = true;
        else if (std::strcmp(argv[i], "fail") == 0) wantFailure = true;
        else if (std::strcmp(argv[i], "--mri") == 0 && i + 1 < argc) mriPath = argv[++i];
    }

    QApplication app(argc, argv);

    std::vector<beam::safety::SonicationSafetyParams> sonications;
    {
        beam::safety::SonicationSafetyParams s;
        s.amplitudeMPa = wantFailure ? 3.0 : 0.5;
        s.centerFrequencyMHz = 0.65;
        s.pd = wantFailure ? 0.10 : 0.001;  // pd > pi in the failure case
        s.pi = 0.05;
        s.bd = 0.4;
        s.bi = 0.5;
        s.startTime = 0.0;
        s.endTime = 10.0;
        sonications.push_back(s);
    }

    bool mriRegistrationComplete = false;
    bool currentRegistrationComplete = false;

    std::unique_ptr<beam::serialcom::SerialPort> serialLink;

    Eigen::VectorXd ch0 = syntheticRf(wantFailure ? 6.0 : 45.0);
    Eigen::VectorXd ch1 = syntheticRf(wantFailure ? 5.0 : 38.0);
    constexpr double kCouplingThreshold = 0.10;
    beam::correction::ThroughTransmitAmplitude tt = beam::correction::throughTransmitAmplitude(ch0, ch1);
    beam::gui::AvgTransmissionBars bars = beam::gui::computeAvgTransmissionBars(tt.amp, tt.amp, kCouplingThreshold);
    Eigen::VectorXd f0 = beam::correction::filterTransmitSignal(ch0).filtWave;
    Eigen::VectorXd f1 = beam::correction::filterTransmitSignal(ch1).filtWave;
    double yLimit = beam::gui::computeRfPlotYLimit(f0, f1);

    beam::gui::SonicationSafetyPreconditions pre;
    pre.attenuationThreshold = 0.5;
    beam::gui::SonicationSafetyReport safety;

    beam::gui_qt::StimParamTableModel tableModel;
    {
        std::vector<beam::gui_qt::StimParamRow> rows(3);
        rows[0].order = 1;
        rows[0].show = true;
        rows[0].amplitude = sonications[0].amplitudeMPa;
        rows[0].pd = sonications[0].pd;
        rows[0].pi = sonications[0].pi;
        rows[0].bd = sonications[0].bd;
        rows[0].bi = sonications[0].bi;
        rows[0].startTime = sonications[0].startTime;
        rows[0].endTime = sonications[0].endTime;
        rows[1] = {2, false, 10.0, 0.0, 0.0, 0.6, 0.0, 20.0, 0.03, 0.7, 0.005, 0.02};
        rows[2] = {3, false, -10.0, 5.0, 0.0, 0.4, 0.0, 15.0, 0.03, 0.7, 0.005, 0.02};
        tableModel.setRows(std::move(rows));
    }

    const auto pulsePlotForShownRow = [&tableModel]() {
        const int rowi = tableModel.currentShownRow();  // 1-based
        const beam::gui_qt::StimParamRow& r =
            tableModel.rows()[static_cast<std::size_t>(rowi - 1)];
        return beam::gui::computePulseWaveformPlot(r.pd, r.pi, r.amplitude);
    };

    // Falls back to an empty timeline rather than crashing if the row's
    // parameters are infeasible (e.g. duration shorter than one burst).
    const auto timelineForShownRow = [&tableModel]() {
        const int rowi = tableModel.currentShownRow();  // 1-based
        const beam::gui_qt::StimParamRow& r = tableModel.rows()[static_cast<std::size_t>(rowi - 1)];
        beam::stimulation::SonicationSchedule s;
        s.startTime = r.startTime;
        s.endTime = r.endTime;
        s.bi = r.bi;
        s.bd = r.bd;
        s.pi = r.pi;
        s.pd = r.pd;
        try {
            return beam::stimulation::computeSonicationEventTimeline({s});
        } catch (const std::exception&) {
            return std::vector<beam::stimulation::TimelineSegment>();
        }
    };

    const auto stimParamsForShownRow = [&tableModel]() {
        const int rowi = tableModel.currentShownRow();  // 1-based
        const beam::gui_qt::StimParamRow& r = tableModel.rows()[static_cast<std::size_t>(rowi - 1)];
        return beam::gui::stimParamsFromTableRow(r.pd, r.pi, r.bd, r.bi, r.amplitude, r.startTime, r.endTime);
    };

    beam::gui_qt::TreatmentProtocolTableModel protocolModel;

    const std::vector<std::string> treatmentProtocolNames = {"PainSCCandAMCC", "PainAMCCandSCC", "PainACC"};
    // Matches BeamV0's real per-protocol CSVs verbatim, including the
    // repeating (not 1-24 sequential) `#` column.
    auto makeTreatmentRow = [](int number, const char* target, double duration) {
        beam::gui::TreatmentProtocolRecord r;
        r.number = number;
        r.target = target;
        r.duration = duration;
        r.amplitude = 0.5;
        r.parameters = "X:0,Y:0,Z:0,0.03,0.7,0.005,0.010";
        r.pain = 0.0;
        r.mood = 0.0;
        r.notes = "notes";
        return r;
    };
    const std::unordered_map<std::string, std::vector<beam::gui::TreatmentProtocolRecord>> treatmentProtocolBlankRows =
        {{"PainSCCandAMCC",
          {makeTreatmentRow(1, "SCC1", 30), makeTreatmentRow(2, "SCC2", 30), makeTreatmentRow(3, "SCC3", 30),
           makeTreatmentRow(4, "SCC4", 30), makeTreatmentRow(5, "SCC5", 30), makeTreatmentRow(6, "SCC6", 30),
           makeTreatmentRow(1, "T1", 180), makeTreatmentRow(2, "T2", 180), makeTreatmentRow(3, "T3", 180),
           makeTreatmentRow(4, "T3", 180), makeTreatmentRow(5, "T1", 180), makeTreatmentRow(6, "T2", 180),
           makeTreatmentRow(7, "aMCC1", 30), makeTreatmentRow(8, "aMCC2", 30), makeTreatmentRow(9, "aMCC3", 30),
           makeTreatmentRow(10, "aMCC4", 30), makeTreatmentRow(11, "aMCC5", 30), makeTreatmentRow(12, "aMCC6", 30),
           makeTreatmentRow(1, "T1", 180), makeTreatmentRow(2, "T2", 180), makeTreatmentRow(3, "T3", 180),
           makeTreatmentRow(4, "T4", 180), makeTreatmentRow(5, "T5", 180), makeTreatmentRow(6, "T6", 180)}},
         {"PainAMCCandSCC",
          {makeTreatmentRow(1, "aMCC1", 30), makeTreatmentRow(2, "aMCC2", 30), makeTreatmentRow(3, "aMCC3", 30),
           makeTreatmentRow(4, "aMCC4", 30), makeTreatmentRow(5, "aMCC5", 30), makeTreatmentRow(6, "aMCC6", 30),
           makeTreatmentRow(1, "T1", 180), makeTreatmentRow(2, "T2", 180), makeTreatmentRow(3, "T3", 180),
           makeTreatmentRow(4, "T3", 180), makeTreatmentRow(5, "T1", 180), makeTreatmentRow(6, "T2", 180),
           makeTreatmentRow(7, "SCC1", 30), makeTreatmentRow(8, "SCC2", 30), makeTreatmentRow(9, "SCC3", 30),
           makeTreatmentRow(10, "SCC4", 30), makeTreatmentRow(11, "SCC5", 30), makeTreatmentRow(12, "SCC6", 30),
           makeTreatmentRow(1, "T1", 180), makeTreatmentRow(2, "T2", 180), makeTreatmentRow(3, "T3", 180),
           makeTreatmentRow(4, "T4", 180), makeTreatmentRow(5, "T5", 180), makeTreatmentRow(6, "T6", 180)}},
         {"PainACC",
          {makeTreatmentRow(1, "SCC1", 30), makeTreatmentRow(2, "SCC2", 30), makeTreatmentRow(3, "SCC3", 30),
           makeTreatmentRow(4, "SCC4", 30), makeTreatmentRow(5, "SCC5", 30), makeTreatmentRow(6, "SCC6", 30),
           makeTreatmentRow(7, "aMCC1", 30), makeTreatmentRow(8, "aMCC2", 30), makeTreatmentRow(9, "aMCC3", 30),
           makeTreatmentRow(10, "aMCC4", 30), makeTreatmentRow(11, "aMCC5", 30), makeTreatmentRow(12, "aMCC6", 30),
           makeTreatmentRow(1, "T1", 180), makeTreatmentRow(2, "T2", 180), makeTreatmentRow(3, "T3", 180),
           makeTreatmentRow(4, "T3", 180), makeTreatmentRow(5, "T1", 180), makeTreatmentRow(6, "T2", 180),
           makeTreatmentRow(1, "T1", 180), makeTreatmentRow(2, "T2", 180), makeTreatmentRow(3, "T3", 180),
           makeTreatmentRow(4, "T4", 180), makeTreatmentRow(5, "T5", 180), makeTreatmentRow(6, "T6", 180)}}};
    std::vector<beam::gui::TreatmentProtocol> treatmentProtocols;
    treatmentProtocols.reserve(treatmentProtocolNames.size());
    for (const std::string& name : treatmentProtocolNames) {
        std::vector<beam::gui::TreatmentProtocol> one =
            beam::gui::initTreatmentProtocols({name}, treatmentProtocolBlankRows.at(name));
        treatmentProtocols.push_back(std::move(one.front()));
    }
    {
        const std::vector<beam::gui::TreatmentProtocolRecord>& initialRows =
            treatmentProtocolBlankRows.at(treatmentProtocolNames.front());
        std::vector<beam::gui_qt::TreatmentProtocolRow> rows;
        rows.reserve(initialRows.size());
        for (const beam::gui::TreatmentProtocolRecord& r : initialRows) rows.push_back(toRow(r));
        protocolModel.setRows(std::move(rows));
    }

    beam::gui_qt::FiducialTableModel fiducialModel;
    // Fixed nominal array registration always fits from -- never
    // reassigned, even after Register is clicked.
    const auto [originArrayRect, arrayRectSource] = defaultSubjectArrayRect();
    const beam::array::ArrayData originArrayData = beam::array::defineArrayData(originArrayRect);
    std::vector<Eigen::Vector3d> fiducialPositionsMm;
    {
        const std::vector<beam::registration::FiducialMarker> markers =
            beam::registration::setArrayFiducialMarkers(originArrayData);
        std::vector<beam::gui_qt::FiducialRow> rows;
        rows.reserve(markers.size());
        for (const beam::registration::FiducialMarker& m : markers) {
            beam::gui_qt::FiducialRow row;
            row.name = QString::fromStdString(m.name);
            row.x = m.position.x() * 1000.0;  // m -> mm
            row.y = m.position.y() * 1000.0;
            row.z = m.position.z() * 1000.0;
            row.group = QStringLiteral("Transducer");
            fiducialPositionsMm.emplace_back(row.x, row.y, row.z);
            rows.push_back(row);
        }
        fiducialModel.setRows(std::move(rows));
    }

    // Startup MRI: --mri, then the default-subject sibling folder, then
    // synthetic (see defaultSubjectMriCandidates()).
    std::string mriSource = "synthetic";
    beam::mri::Volume3D mriVolume;
    std::optional<beam::mri::RasAxisVectors> mriAxes;
    if (!mriPath.empty()) {
        const MriLoadResult r = tryLoadMri(mriPath);
        if (r.error.empty()) {
            mriVolume = r.volume;
            mriAxes = r.axes;
            mriSource = "file:" + mriPath;
        } else {
            std::fprintf(stderr, "beam_app: failed to load --mri %s (%s); using synthetic volume\n",
                        mriPath.c_str(), r.error.c_str());
        }
    }
    if (mriSource == "synthetic") {
        for (const std::string& candidate : defaultSubjectMriCandidates()) {
            const MriLoadResult r = tryLoadMri(candidate);
            if (r.error.empty()) {
                mriVolume = r.volume;
                mriAxes = r.axes;
                mriSource = "default-subject:" + candidate;
                break;
            }
        }
    }
    if (mriSource == "synthetic") {
        mriVolume = syntheticMriVolume(64, 64, 40);
    }

    // "Focus" is just the mean of every array element's center -- no
    // per-sonication targeting exists yet (docs/known_gaps_gui.md).
    Eigen::Vector3d centerArrayMm = Eigen::Vector3d::Zero();
    for (const beam::array::ArrayElement& element : originArrayData.arrayTotal.element) {
        centerArrayMm += element.center;
    }
    centerArrayMm = (centerArrayMm / static_cast<double>(originArrayData.arrayTotal.element.size())) * 1000.0;

    beam::mri::VoxelResolution mriResolution;
    beam::mri::DisplayWindow mriDisplayWindow = beam::mri::computeDisplayWindow(mriVolume);
    if (mriAxes.has_value()) {
        mriResolution = beam::mri::computeVoxelResolution(*mriAxes);
    }

    beam::mri::Volume3D arrayMask;
    beam::mri::Volume3D fiducialMask;
    beam::mri::Volume3D focusMask;
    if (mriAxes.has_value()) {
        // The array-footprint mask uses a copy re-centered on the volume;
        // fiducial/focus overlays keep using originArrayData.
        const beam::registration::AffineArrayResult centeredArray =
            beam::gui::centerArrayOnMri(originArrayData, *mriAxes);
        arrayMask = beam::gui::rasterizeArrayOntoMriGrid(centeredArray.arrayData.arrayTotal, *mriAxes, mriVolume.nx,
                                                          mriVolume.ny, mriVolume.nz);
        fiducialMask = beam::gui::rasterizeFiducialMarkersOntoMriGrid(fiducialPositionsMm, *mriAxes, mriVolume.nx,
                                                                       mriVolume.ny, mriVolume.nz);
        focusMask = beam::gui::rasterizeFocusEllipsoidOntoMriGrid(centerArrayMm, *mriAxes, mriVolume.nx,
                                                                   mriVolume.ny, mriVolume.nz);
    }

    if (checkOnly) {
        beam::gui_qt::BeamMainWindow window;
        beam::app::MainWindowStartupContext windowCtx{
            .pre = pre,
            .currentRegistrationComplete = currentRegistrationComplete,
            .mriRegistrationComplete = mriRegistrationComplete,
            .tt = tt,
            .safety = safety,
            .sonications = sonications,
            .centerArrayMm = centerArrayMm,
            .tableModel = tableModel,
            .protocolModel = protocolModel,
            .treatmentProtocolNames = treatmentProtocolNames,
            .treatmentProtocols = treatmentProtocols,
            .treatmentProtocolBlankRows = treatmentProtocolBlankRows,
            .fiducialModel = fiducialModel,
            .mriVolume = mriVolume,
            .mriAxes = mriAxes,
            .arrayMask = arrayMask,
            .fiducialMask = fiducialMask,
            .focusMask = focusMask,
            .pulsePlotForShownRow = pulsePlotForShownRow,
            .timelineForShownRow = timelineForShownRow,
            .ch0 = ch0,
            .ch1 = ch1,
            .f0 = f0,
            .f1 = f1,
            .bars = bars,
            .yLimit = yLimit,
            .stimParamsForShownRow = stimParamsForShownRow,
            .kCouplingThreshold = kCouplingThreshold,
            .serialLink = serialLink,
            .mriSource = mriSource,
            .originArrayData = originArrayData,
            .fiducialPositionsMm = fiducialPositionsMm,
            .mriResolution = mriResolution,
            .mriDisplayWindow = mriDisplayWindow,
        };
        const beam::app::MainWindowCheckHooks checkHooks = beam::app::wireMainWindow(window, windowCtx);
        beam::app::CheckContext ctx{
            .mriVolume = mriVolume,
            .performLoadMri = checkHooks.performLoadMri,
            .protocolModel = protocolModel,
            .tableModel = tableModel,
            .centerArrayMm = centerArrayMm,
            .fiducialModel = fiducialModel,
            .performRegistration = checkHooks.performRegistration,
            .performRegisterCurrentPosition = checkHooks.performRegisterCurrentPosition,
            .arrayMask = arrayMask,
            .fiducialMask = fiducialMask,
            .focusMask = focusMask,
            .timelineForShownRow = timelineForShownRow,
            .tt = tt,
            .kCouplingThreshold = kCouplingThreshold,
            .stimParamsForShownRow = stimParamsForShownRow,
            .window = window,
            .treatmentProtocols = treatmentProtocols,
            .performNewVisit = checkHooks.performNewVisit,
            .mriSource = mriSource,
            .arrayRectSource = arrayRectSource,
            .safety = safety,
            .bars = bars,
            .mriResolution = mriResolution,
            .mriDisplayWindow = mriDisplayWindow,
            .mriRegistrationComplete = mriRegistrationComplete,
            .currentRegistrationComplete = currentRegistrationComplete,
        };
        return beam::app::runCheckDiagnostics(ctx);
    }

    beam::app::DesignerStartupContext ctx{
        .pre = pre,
        .currentRegistrationComplete = currentRegistrationComplete,
        .mriRegistrationComplete = mriRegistrationComplete,
        .tt = tt,
        .safety = safety,
        .sonications = sonications,
        .centerArrayMm = centerArrayMm,
        .mriVolume = mriVolume,
        .mriAxes = mriAxes,
        .arrayMask = arrayMask,
        .fiducialMask = fiducialMask,
        .focusMask = focusMask,
        .originArrayData = originArrayData,
        .tableModel = tableModel,
        .kCouplingThreshold = kCouplingThreshold,
        .stimParamsForShownRow = stimParamsForShownRow,
        .serialLink = serialLink,
        .protocolModel = protocolModel,
        .treatmentProtocolNames = treatmentProtocolNames,
        .treatmentProtocols = treatmentProtocols,
        .treatmentProtocolBlankRows = treatmentProtocolBlankRows,
    };
    return beam::app::runDesignerStartup(ctx, app);
}
