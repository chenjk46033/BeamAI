#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QString>
#include <Eigen/Core>

#include "correction/signal.hpp"
#include "gui/correction_tab_presenter.hpp"
#include "gui/safety_presenter.hpp"
#include "gui/treatment_session_store.hpp"
#include "gui_qt/fiducial_table_model.hpp"
#include "gui_qt/main_window.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"
#include "safety/intensity.hpp"
#include "stimulation/events.hpp"

namespace beam::app {

// References into main()'s own local state, bundled so the --check
// diagnostic body can live in its own file instead of main()'s. Built via
// aggregate init at the one call site; every field is a reference (or a
// closure over references) into main()'s locals, so this changes nothing
// about ownership or lifetime -- runCheckDiagnostics() runs once,
// synchronously, before ctx goes out of scope.
struct CheckContext {
    beam::mri::Volume3D& mriVolume;
    std::function<void(const QString&)> performLoadMri;
    beam::gui_qt::TreatmentProtocolTableModel& protocolModel;
    beam::gui_qt::StimParamTableModel& tableModel;
    Eigen::Vector3d& centerArrayMm;
    beam::gui_qt::FiducialTableModel& fiducialModel;
    std::function<void()> performRegistration;
    std::function<void(double, double)> performRegisterCurrentPosition;
    const beam::mri::Volume3D& arrayMask;
    const beam::mri::Volume3D& fiducialMask;
    const beam::mri::Volume3D& focusMask;
    std::function<std::vector<beam::stimulation::TimelineSegment>()> timelineForShownRow;
    const beam::correction::ThroughTransmitAmplitude& tt;
    double kCouplingThreshold;
    std::function<beam::safety::SonicationSafetyParams()> stimParamsForShownRow;
    beam::gui_qt::BeamMainWindow& window;
    std::vector<beam::gui::TreatmentProtocol>& treatmentProtocols;
    std::function<void()> performNewVisit;
    const std::string& mriSource;
    const std::string& arrayRectSource;
    const beam::gui::SonicationSafetyReport& safety;
    const beam::gui::AvgTransmissionBars& bars;
    const beam::mri::VoxelResolution& mriResolution;
    const beam::mri::DisplayWindow& mriDisplayWindow;
    const bool& mriRegistrationComplete;
    const bool& currentRegistrationComplete;
};

// beam_app --check's body: exercises every tab's real code path headlessly
// and prints one summary line. Returns the process exit code.
int runCheckDiagnostics(CheckContext& ctx);

}  // namespace beam::app
