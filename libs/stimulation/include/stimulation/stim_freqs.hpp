#pragma once

#include <string>

#include <Eigen/Core>

namespace beam::stimulation {

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/defineStimFreqs.m.
// control: "650" -> 0.65 MHz for every element; "high" -> 0.7 MHz.
//
// The source's "MFS252" / "MFS21" branches load a hardcoded lab-machine
// .mat file (multi-frequency-superposition tables) -- not ported; passing
// them throws std::invalid_argument. Any other value throws too (MATLAB:
// error('Undefined control value')).
Eigen::VectorXd defineStimFreqs(const std::string& control, int arrayNumElements);

}  // namespace beam::stimulation
