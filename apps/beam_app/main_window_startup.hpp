#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Eigen/Core>

#include "array/array_data.hpp"
#include "correction/signal.hpp"
#include "gui/correction_tab_presenter.hpp"
#include "gui/safety_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "gui/treatment_session_store.hpp"
#include "gui_qt/fiducial_table_model.hpp"
#include "gui_qt/main_window.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"
#include "safety/intensity.hpp"
#include "serialcom/serial_port.hpp"
#include "stimulation/events.hpp"

namespace beam::app {

// References into main()'s own local state, for wiring the production
// beam::gui_qt::BeamMainWindow. Built via aggregate init at the one call
// site.
struct MainWindowStartupContext {
    beam::gui::SonicationSafetyPreconditions& pre;
    bool& currentRegistrationComplete;
    bool& mriRegistrationComplete;
    beam::correction::ThroughTransmitAmplitude& tt;
    beam::gui::SonicationSafetyReport& safety;
    const std::vector<beam::safety::SonicationSafetyParams>& sonications;
    Eigen::Vector3d& centerArrayMm;
    beam::gui_qt::StimParamTableModel& tableModel;
    beam::gui_qt::TreatmentProtocolTableModel& protocolModel;
    const std::vector<std::string>& treatmentProtocolNames;
    std::vector<beam::gui::TreatmentProtocol>& treatmentProtocols;
    const std::unordered_map<std::string, std::vector<beam::gui::TreatmentProtocolRecord>>& treatmentProtocolBlankRows;
    beam::gui_qt::FiducialTableModel& fiducialModel;
    beam::mri::Volume3D& mriVolume;
    std::optional<beam::mri::RasAxisVectors>& mriAxes;
    beam::mri::Volume3D& arrayMask;
    beam::mri::Volume3D& fiducialMask;
    beam::mri::Volume3D& focusMask;
    std::function<beam::gui::PulseWaveformPlot()> pulsePlotForShownRow;
    std::function<std::vector<beam::stimulation::TimelineSegment>()> timelineForShownRow;
    Eigen::VectorXd& ch0;
    Eigen::VectorXd& ch1;
    Eigen::VectorXd& f0;
    Eigen::VectorXd& f1;
    beam::gui::AvgTransmissionBars& bars;
    double& yLimit;
    std::function<beam::safety::SonicationSafetyParams()> stimParamsForShownRow;
    double kCouplingThreshold;
    std::unique_ptr<beam::serialcom::SerialPort>& serialLink;
    std::string& mriSource;
    const beam::array::ArrayData& originArrayData;
    std::vector<Eigen::Vector3d>& fiducialPositionsMm;
    beam::mri::VoxelResolution& mriResolution;
    beam::mri::DisplayWindow& mriDisplayWindow;
};

// The 4 handlers --check also needs to call directly (headlessly, without
// going through the real Qt widgets). Same closures the real buttons use.
struct MainWindowCheckHooks {
    std::function<void(const QString&)> performLoadMri;
    std::function<void()> performRegistration;
    std::function<void(double, double)> performRegisterCurrentPosition;
    std::function<void()> performNewVisit;
};

// Wires beam::gui_qt::BeamMainWindow's tabs (Sonicate/Treatment Protocol/
// Register/Correction). Does not show the window or run the event loop --
// main() still owns --check and the final show()/exec().
MainWindowCheckHooks wireMainWindow(beam::gui_qt::BeamMainWindow& window, MainWindowStartupContext& ctx);

}  // namespace beam::app
