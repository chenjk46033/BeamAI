#include "stimulation/interp.hpp"

#include <limits>

namespace beam::stimulation {

double interp1(const Eigen::VectorXd& x, const Eigen::VectorXd& y, double xq) {
    const Eigen::Index n = x.size();
    if (n == 0 || xq < x(0) || xq > x(n - 1)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // Find the interval [x(k), x(k+1)] containing xq.
    Eigen::Index k = 0;
    while (k + 1 < n && x(k + 1) < xq) {
        ++k;
    }
    if (k + 1 >= n) {
        return y(n - 1);
    }
    const double t = (xq - x(k)) / (x(k + 1) - x(k));
    return y(k) + t * (y(k + 1) - y(k));
}

}  // namespace beam::stimulation
