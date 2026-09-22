#pragma once

#include <vector>

namespace beam::stimulation {

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/GeneralSonication/getPauseIntervals.m.
// Splits pauseTime into consecutive chunks of at most burstTimeUnitLength
// (the last chunk holds the remainder). Returns an empty vector for
// pauseTime == 0.
//
// burstTimeUnitLength must be > 0 -- the MATLAB source would loop forever
// otherwise; this throws std::invalid_argument.
std::vector<double> getPauseIntervals(double pauseTime, double burstTimeUnitLength);

}  // namespace beam::stimulation
