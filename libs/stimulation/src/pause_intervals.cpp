#include "stimulation/pause_intervals.hpp"

#include <stdexcept>

namespace beam::stimulation {

std::vector<double> getPauseIntervals(double pauseTime, double burstTimeUnitLength) {
    if (burstTimeUnitLength <= 0.0) {
        throw std::invalid_argument("getPauseIntervals: burstTimeUnitLength must be > 0");
    }
    std::vector<double> intervals;
    double bpt = pauseTime;
    while (bpt != 0.0) {
        if (bpt < burstTimeUnitLength) {
            intervals.push_back(bpt);
            bpt = 0.0;
        } else {
            intervals.push_back(burstTimeUnitLength);
            bpt -= burstTimeUnitLength;
        }
    }
    return intervals;
}

}  // namespace beam::stimulation
