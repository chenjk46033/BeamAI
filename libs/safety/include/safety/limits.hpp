#pragma once

// Ported from BeamV0/GUIMatlab/BEAM/GUI/Safety/. The safety limits and the
// acoustic-intensity / mechanical-index / ISPTA math, separated from the
// `app` object and report-string plumbing the GUI wraps them in.

namespace beam::safety {

// checkSonicationSafety.m's hardcoded thresholds.
inline constexpr double kIsptaThresholdWPerCm2 = 0.720;
inline constexpr double kIsppaThresholdWPerCm2 = 190.0;
inline constexpr double kMechanicalIndexThreshold = 1.9;

// Ported from getMaxSonicationAmplitude.m -- a hardcoded 3 (MPa).
inline constexpr double kMaxSonicationAmplitudeMPa = 3.0;

}  // namespace beam::safety
