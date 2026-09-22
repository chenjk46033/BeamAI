#pragma once

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::stimulation {

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/calculateMultifrequencySuperpositionDelays.m.
// freqs: per-element frequency (Hz -- the MATLAB caller passes freqs*1e6).
// ncycles: which cycle to line up on. Returns per-element waveform delays
// (seconds) that bring the different-frequency waveforms into phase.
Eigen::VectorXd calculateMultifrequencySuperpositionDelays(const Eigen::VectorXd& freqs, double ncycles);

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/focusArrayAtPoint.m.
// point: focal point in the same units as element positions (meters).
// c: speed of sound. Returns {delaysT, delaysTRaw, mi}:
//   delaysTRaw = |element - point| / c   (raw propagation time)
//   delaysT    = max(delaysTRaw) - delaysTRaw   (steering delays: farthest
//                element fires first)
//   mi         = 1-based index of the farthest element (argmax of raw).
// The source's dead `delays = max(delays)-delays` line (delays is all
// zeros) is not carried over.
struct FocusResult {
    Eigen::VectorXd delaysT;
    Eigen::VectorXd delaysTRaw;
    int mi = 0;
};
FocusResult focusArrayAtPoint(const beam::array::ArrayStruct& array, const Eigen::Vector3d& point, double c);

}  // namespace beam::stimulation
