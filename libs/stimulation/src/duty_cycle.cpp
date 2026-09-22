#include "stimulation/duty_cycle.hpp"

#include <algorithm>
#include <cmath>

#include "stimulation/interp.hpp"

namespace beam::stimulation {

double pressureToDutyCycleGivenTransmission(double p, double t, const Eigen::VectorXd& calDuty,
                                             const Eigen::VectorXd& calPressure) {
    constexpr double kTmin = 0.08;
    constexpr double kTmax = 0.4;
    constexpr double kAlpha = 1.3;

    const double s = (p - 0.5) / (0.75 - 0.5);
    const double pMin = 1.0 + s * 0.5;
    const double pMax = 3.0 + s * 0.25;

    double frac = (t - kTmin) / (kTmax - kTmin);
    frac = std::max(std::min(frac, 1.0), 0.0);

    double pAdj = pMin + (pMax - pMin) * std::pow(1.0 - frac, kAlpha);
    pAdj = std::min(3.5, pAdj);

    const double dc = interp1(calPressure, calDuty, std::abs(pAdj)) / 100.0;
    if (std::isnan(dc)) {
        return 0.4;  // MATLAB: max(NaN, 0.4) -> 0.4, then min(0.4, 0.75) -> 0.4
    }
    return std::min(std::max(dc, 0.4), 0.75);
}

}  // namespace beam::stimulation
