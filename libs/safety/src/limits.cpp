#include "safety/limits.hpp"

namespace beam::safety {

Eigen::Matrix<double, 3, 2> maxSteeringRangeDegrees() {
    Eigen::Matrix<double, 3, 2> r;
    r << -45, 45,
         -28, 28,
         -15, 15;
    return r;
}

}  // namespace beam::safety
