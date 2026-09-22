#include "util/geometry_math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <Eigen/Geometry>  // for MatrixBase::cross()

namespace beam::util {

double vectorRange(const Eigen::VectorXd& vec) {
    return vec.maxCoeff() - vec.minCoeff();
}

double distancePointToLine(const Eigen::Vector3d& lineV, const Eigen::Vector3d& lineQ,
                            const Eigen::Vector3d& point) {
    const Eigen::Vector3d diff = lineQ - point;
    return diff.cross(lineV).norm() / lineV.norm();
}

Eigen::VectorXd addVectors(const Eigen::VectorXd& u1, const Eigen::VectorXd& u2) {
    return u1 + u2;
}

double angleBetweenTwoVectors(const Eigen::Vector3d& u, const Eigen::Vector3d& v) {
    double cosTheta = u.dot(v) / (u.norm() * v.norm());
    // MATLAB: max(min(x, 1), -1). MATLAB's min/max ignore NaN, so a 0/0
    // ratio (a zero-length input) resolves to min(NaN, 1) == 1 -> angle 0,
    // not NaN. std::clamp does not do that, so handle NaN explicitly.
    if (std::isnan(cosTheta)) {
        cosTheta = 1.0;
    }
    cosTheta = std::clamp(cosTheta, -1.0, 1.0);
    return std::acos(cosTheta) * 180.0 / std::numbers::pi;
}

Eigen::VectorXd convertDelaysToCycles(const Eigen::VectorXd& delaysSeconds, double frequencyHz) {
    return delaysSeconds * frequencyHz;
}

}  // namespace beam::util
