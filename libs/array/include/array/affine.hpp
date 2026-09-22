#pragma once

#include <Eigen/Core>

namespace beam::array {

// Port of BeamV0/GUIMatlab/BEAM/Arrays/defineTranslateAffineMatrix.m
Eigen::Matrix4d translateAffineMatrix(const Eigen::Vector3d& xyzTranslate);

// Ports of BeamV0/GUIMatlab/BEAM/Arrays/{x,y,z}RotAffineMatrix.m
// (angle in radians, matching the MATLAB source's sin/cos usage directly).
Eigen::Matrix4d xRotAffineMatrix(double angle);
Eigen::Matrix4d yRotAffineMatrix(double angle);
Eigen::Matrix4d zRotAffineMatrix(double angle);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/applyAffineToRect.m -- applies the
// affine transform to each element column's 4 corners and center in place
// (rect must have at least kRectMinRows rows; see array_types.hpp).
Eigen::MatrixXd applyAffineToRect(const Eigen::Matrix4d& affineMatrix, Eigen::MatrixXd rect);

}  // namespace beam::array
