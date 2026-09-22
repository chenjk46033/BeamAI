#pragma once

#include <Eigen/Core>

namespace beam::correction {

// Port of BeamV0/GUIMatlab/BEAM/Correction/setAdjustedAttValues.m.
// att: per-element attenuation factors (may contain +Inf). vAmplitudeToMPa,
// attThreshold: scalars (the source's misspelled `VAmpltiudeToMPa`).
//
// Clamps every att below attThreshold up to attThreshold, then walks the
// result in descending order and caps entries above vAmplitudeToMPa/30 at
// that value, stopping once the running "deficit" it tracks goes negative.
// The source's `disp(...)` progress prints and its unused `maxTotal` local
// are dropped. Returns the adjusted vector (`attReduced`).
Eigen::VectorXd setAdjustedAttValues(const Eigen::VectorXd& att, double vAmplitudeToMPa,
                                      double attThreshold);

// Port of BeamV0/GUIMatlab/BEAM/Correction/calculateAttenuation.m. Returns
// the ratio of the subject signal's amplitude to the free-field signal's
// after removing each one's mean. usePeakNegativeVoltage selects the
// MATLAB source's `varargin` branch: false (default) uses peak-to-peak
// (max - min); true uses findPeakNegativeVoltage(sig, 10). The commented-out
// 'noise' branch in the source is not ported (it's dead).
double calculateAttenuation(const Eigen::VectorXd& subjectSignal, const Eigen::VectorXd& freeFieldSignal,
                             bool usePeakNegativeVoltage = false);

}  // namespace beam::correction
