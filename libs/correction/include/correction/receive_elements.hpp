#pragma once

#include <optional>
#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::correction {

// Port of BeamV0/GUIMatlab/BEAM/Correction/getReceiveElementsUnderAngle.m.
// eli1Based: 1-based element index into arrayData.arrayTotal.element.
// targetPosMm: the target position in mm, or std::nullopt for the MATLAB
// source's 'norm' string case (use the element's own normal as the
// element->target direction). angleThreshold: degrees.
//
// Returns the subset of getOpposingElements(arrayData, eli) whose
// element->element direction sits within angleThreshold of the
// element->target direction (taking whichever of +/-target is closer, as
// the source does). Values are element numbers, same as getOpposingElements.
std::vector<int> getReceiveElementsUnderAngle(const beam::array::ArrayData& arrayData, int eli1Based,
                                               std::optional<Eigen::Vector3d> targetPosMm,
                                               double angleThreshold);

}  // namespace beam::correction
