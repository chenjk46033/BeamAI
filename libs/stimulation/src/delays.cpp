#include "stimulation/delays.hpp"

namespace beam::stimulation {

Eigen::VectorXd calculateMultifrequencySuperpositionDelays(const Eigen::VectorXd& freqs, double ncycles) {
    const Eigen::VectorXd delays = (ncycles / freqs.array()) + (0.25 / freqs.array());
    return 2.0 * delays.maxCoeff() - delays.array();
}

FocusResult focusArrayAtPoint(const beam::array::ArrayStruct& array, const Eigen::Vector3d& point, double c) {
    const Eigen::Index n = static_cast<Eigen::Index>(array.element.size());
    Eigen::VectorXd raw(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        raw(i) = (array.element[static_cast<size_t>(i)].position - point).norm() / c;
    }

    Eigen::Index miIdx = 0;
    const double maxRaw = raw.maxCoeff(&miIdx);

    FocusResult result;
    result.delaysTRaw = raw;
    result.delaysT = (maxRaw - raw.array()).matrix();
    result.mi = static_cast<int>(miIdx) + 1;
    return result;
}

}  // namespace beam::stimulation
