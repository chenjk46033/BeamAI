#include "array/affine.hpp"

#include <cmath>
#include <stdexcept>

#include "array/array_types.hpp"

namespace beam::array {

Eigen::Matrix4d translateAffineMatrix(const Eigen::Vector3d& xyzTranslate) {
    Eigen::Matrix4d m = Eigen::Matrix4d::Identity();
    m.block<3, 1>(0, 3) = xyzTranslate;
    return m;
}

Eigen::Matrix4d xRotAffineMatrix(double angle) {
    const double sa = std::sin(angle);
    const double ca = std::cos(angle);
    Eigen::Matrix4d m;
    m << 1, 0, 0, 0,
         0, ca, -sa, 0,
         0, sa, ca, 0,
         0, 0, 0, 1;
    return m;
}

Eigen::Matrix4d yRotAffineMatrix(double angle) {
    const double sa = std::sin(angle);
    const double ca = std::cos(angle);
    Eigen::Matrix4d m;
    m << ca, 0, sa, 0,
         0, 1, 0, 0,
         -sa, 0, ca, 0,
         0, 0, 0, 1;
    return m;
}

Eigen::Matrix4d zRotAffineMatrix(double angle) {
    const double sa = std::sin(angle);
    const double ca = std::cos(angle);
    Eigen::Matrix4d m;
    m << ca, -sa, 0, 0,
         sa, ca, 0, 0,
         0, 0, 1, 0,
         0, 0, 0, 1;
    return m;
}

Eigen::MatrixXd applyAffineToRect(const Eigen::Matrix4d& affineMatrix, Eigen::MatrixXd rect) {
    if (rect.rows() < kRectMinRows) {
        throw std::invalid_argument("applyAffineToRect: rect has fewer than kRectMinRows rows");
    }
    const Eigen::Index n = rect.cols();
    for (Eigen::Index col = 0; col < n; ++col) {
        for (int k = 1; k <= 4; ++k) {
            const Eigen::Vector4d inVec(rect(kRectCornerStartRow + 3 * (k - 1), col),
                                         rect(kRectCornerStartRow + 3 * (k - 1) + 1, col),
                                         rect(kRectCornerStartRow + 3 * (k - 1) + 2, col), 1.0);
            const Eigen::Vector4d outVec = affineMatrix * inVec;
            rect.block<3, 1>(kRectCornerStartRow + 3 * (k - 1), col) = outVec.head<3>();
        }
        const Eigen::Vector4d centerIn(rect(kRectCenterStartRow, col), rect(kRectCenterStartRow + 1, col),
                                        rect(kRectCenterStartRow + 2, col), 1.0);
        const Eigen::Vector4d centerOut = affineMatrix * centerIn;
        rect.block<3, 1>(kRectCenterStartRow, col) = centerOut.head<3>();
    }
    return rect;
}

}  // namespace beam::array
