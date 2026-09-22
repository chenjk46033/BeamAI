#pragma once

#include <array>
#include <vector>

#include <Eigen/Core>

namespace beam::array {

// Row layout of a BeamV0 "rect" matrix (one column per element), ported
// from how BeamV0/GUIMatlab/BEAM/Arrays/*.m index into it (fieldii
// convention). MATLAB rows are 1-based; these are the 0-based Eigen
// row indices for the same fields.
//   row 0        : element number               (MATLAB row 1)
//   rows 1..12   : 4 corners, xyz each           (MATLAB rows 2:13)
//   rows 13..15  : unused by any ported function (MATLAB rows 14:16)
//   rows 16..18  : element center, xyz           (MATLAB rows 17:19)
constexpr int kRectElementNumberRow = 0;
constexpr int kRectCornerStartRow = 1;    // corner k (1-based, k=1..4) at rows 3*(k-1)+1 .. +3
constexpr int kRectCenterStartRow = 16;
constexpr int kRectMinRows = 19;

// Returns corner k (1-based, k in [1,4]) of element column `col`.
Eigen::Vector3d rectCorner(const Eigen::MatrixXd& rect, int col, int k);

// Returns the center xyz of element column `col`.
Eigen::Vector3d rectCenter(const Eigen::MatrixXd& rect, int col);

struct ArrayElement {
    int number = 0;
    Eigen::Vector3d position = Eigen::Vector3d::Zero();
    // 4 corners, one per column, xyz per row -- port of array.element(i).corners
    // (rect(2:13,i) in MATLAB, reshaped here into a 3x4 matrix instead of a
    // flat 12-vector; same data, easier to index).
    Eigen::Matrix<double, 3, 4> corners = Eigen::Matrix<double, 3, 4>::Zero();

    // Populated by getReceiveElements (called from defineArrayStruct).
    int opposingElement = 0;              // 1-based element index, matches MATLAB
    std::vector<int> receiveElements;     // 1-based element indices, matches MATLAB

    // Populated by calculateArrayNormalVectors.
    Eigen::Vector3d normalVector = Eigen::Vector3d::Zero();
    Eigen::Vector3d center = Eigen::Vector3d::Zero();
    Eigen::Vector3d xNorm = Eigen::Vector3d::Zero();
    Eigen::Vector3d yNorm = Eigen::Vector3d::Zero();
};

struct ArrayStruct {
    Eigen::MatrixXd rect;
    double frequency = 0.0;                    // Hz
    std::array<double, 2> elementDimensions{};  // meters, [width, height]
    std::vector<ArrayElement> element;

    // Only set on arrayData.array(1)/(2) by defineArrayData, not on
    // arrayTotal or on a bare defineArrayStruct result. -1 = unset, matching
    // "field doesn't exist" since MATLAB doesn't set it in that case either.
    int elementMapping = -1;
};

struct ArrayData {
    ArrayStruct arrayTotal;
    std::array<ArrayStruct, 2> array;
};

}  // namespace beam::array
