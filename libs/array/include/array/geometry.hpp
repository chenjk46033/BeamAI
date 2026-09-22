#pragma once

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::array {

// Port of BeamV0/GUIMatlab/BEAM/Arrays/normalVectorFrom3Points.m --
// normal to the plane through p1,p2,p3, oriented "toward zero" (picks
// between the two possible normal directions by which one has the larger
// dot product with -p1, exactly matching the MATLAB source's comparison,
// ties included).
Eigen::Vector3d normalVectorFrom3Points(const Eigen::Vector3d& p1, const Eigen::Vector3d& p2,
                                         const Eigen::Vector3d& p3);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/calculateRectNormalVector.m --
// unit normal of element column `col` (1-based, matching MATLAB's `i`).
Eigen::Vector3d calculateRectNormalVector(const Eigen::MatrixXd& rect, int col1Based);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/calculateArrayNormalVectors.m --
// fills normalVector/center/xNorm/yNorm on every element already present in
// arrayStruct.element, from arrayStruct.rect. arrayStruct.element must
// already be sized to match arrayStruct.rect's column count (as
// defineArrayStruct's ports it).
void calculateArrayNormalVectors(ArrayStruct& arrayStruct);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/getElementPositionsFromArrayStruct.m
Eigen::MatrixXd getElementPositionsFromArrayStruct(const ArrayStruct& arrayStruct);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/spatiallySampleElement.m --
// corners: the 4 element corners (columns), xyz per row, same layout as
// ArrayElement::corners. fs: sample spacing. Returns an (n_samples^2 x 3)
// matrix of sampled points (0 rows if the element is smaller than one
// sample spacing, matching MATLAB's round(max_range/fs) == 0 case).
Eigen::MatrixXd spatiallySampleElement(const Eigen::Matrix<double, 3, 4>& corners, double fs);

}  // namespace beam::array
