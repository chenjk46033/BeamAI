#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QApplication>
#include <Eigen/Core>

#include "array/array_data.hpp"
#include "correction/signal.hpp"
#include "gui/safety_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/treatment_session_store.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"
#include "safety/intensity.hpp"
#include "serialcom/serial_port.hpp"

namespace beam::app {

// References into main()'s own local state, for the --designer-startup
// entry point (beam::gui_qt::DesignerStartupWindow). Built via aggregate
// init at the one call site, before main() has constructed BeamMainWindow
// or anything specific to it.
struct DesignerStartupContext {
    beam::gui::SonicationSafetyPreconditions& pre;
    bool& currentRegistrationComplete;
    bool& mriRegistrationComplete;
    const beam::correction::ThroughTransmitAmplitude& tt;
    beam::gui::SonicationSafetyReport& safety;
    const std::vector<beam::safety::SonicationSafetyParams>& sonications;
    const Eigen::Vector3d& centerArrayMm;
    const beam::mri::Volume3D& mriVolume;
    const std::optional<beam::mri::RasAxisVectors>& mriAxes;
    const beam::mri::Volume3D& arrayMask;
    const beam::mri::Volume3D& fiducialMask;
    const beam::mri::Volume3D& focusMask;
    const beam::array::ArrayData& originArrayData;
    beam::gui_qt::StimParamTableModel& tableModel;
    double kCouplingThreshold;
    std::function<beam::safety::SonicationSafetyParams()> stimParamsForShownRow;
    std::unique_ptr<beam::serialcom::SerialPort>& serialLink;
    beam::gui_qt::TreatmentProtocolTableModel& protocolModel;
    const std::vector<std::string>& treatmentProtocolNames;
    std::vector<beam::gui::TreatmentProtocol>& treatmentProtocols;
    const std::unordered_map<std::string, std::vector<beam::gui::TreatmentProtocolRecord>>& treatmentProtocolBlankRows;
};

// Wires and shows beam::gui_qt::DesignerStartupWindow, then runs the Qt
// event loop. Returns the process exit code.
int runDesignerStartup(DesignerStartupContext& ctx, QApplication& app);

}  // namespace beam::app
