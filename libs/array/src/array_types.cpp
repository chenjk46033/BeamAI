#include "array/array_types.hpp"

#include <stdexcept>

namespace beam::array {

Eigen::Vector3d rectCorner(const Eigen::MatrixXd& rect, int col, int k) {
    if (k < 1 || k > 4) {
        throw std::invalid_argument("rectCorner: k must be in [1,4]");
    }
    if (rect.rows() < kRectMinRows) {
        throw std::invalid_argument("rectCorner: rect has fewer than kRectMinRows rows");
    }
    const int row = kRectCornerStartRow + 3 * (k - 1);
    return rect.block<3, 1>(row, col);
}

Eigen::Vector3d rectCenter(const Eigen::MatrixXd& rect, int col) {
    if (rect.rows() < kRectMinRows) {
        throw std::invalid_argument("rectCenter: rect has fewer than kRectMinRows rows");
    }
    return rect.block<3, 1>(kRectCenterStartRow, col);
}

}  // namespace beam::array
