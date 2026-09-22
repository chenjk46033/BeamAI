#pragma once

#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::stimulation {

// One entry of BeamV0/GUIMatlab/BEAM/Stimulation/defineStimParams.m's
// output struct array -- the transmit parameters for one (tx-group, target)
// combination. Vectors indexed "subset" hold only the elements of this
// tx-group (txElementsArray row); the rest are full per-element vectors.
struct StimParams {
    double centerFrequencyMHz = 0.0;
    Eigen::VectorXd delaysCycle;      // subset
    Eigen::VectorXd delaysSeconds;    // subset
    Eigen::VectorXd delaysSteering;   // full (focusArrayAtPoint's delaysT)
    Eigen::VectorXd correctionDelays; // full (echoed input)
    Eigen::VectorXd waveformDelays;   // full
    std::vector<int> txElements;      // this tx-group's row
    std::vector<int> txElementsArray; // this tx-group's row
    std::vector<int> rxElements;      // echoed input
    Eigen::VectorXd freqs;            // subset (MATLAB casts to single; kept double here)
    Eigen::VectorXd apods;            // subset
    double c = 0.0;
    Eigen::Vector3d position = Eigen::Vector3d::Zero();  // mm
    int waveform = 3;
};

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/defineStimParams.m.
// txElements / txElementsArray: one inner vector per tx-group (as
// defineArrayTxElements returns). positions: target points in mm, one row
// (xyz) each. freqs / apods / correctionDelays: full per-element vectors
// (freqs in MHz, correctionDelays in seconds). Output order is
// tx-group-major, then position (matching the source's nested loop).
std::vector<StimParams> defineStimParams(const beam::array::ArrayStruct& array, double c,
                                          const std::vector<std::vector<int>>& txElements,
                                          const std::vector<std::vector<int>>& txElementsArray,
                                          const std::vector<int>& rxElements,
                                          const Eigen::MatrixX3d& positions, const Eigen::VectorXd& freqs,
                                          const Eigen::VectorXd& apods,
                                          const Eigen::VectorXd& correctionDelays);

}  // namespace beam::stimulation
