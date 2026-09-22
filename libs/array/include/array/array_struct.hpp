#pragma once

#include <utility>
#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::array {

// Port of BeamV0/GUIMatlab/BEAM/Arrays/getReceiveElements.m.
// elIndex1Based: transmitting element index, 1-based (matches MATLAB).
// Returns {opposingElement, receiveElements}, both 1-based element indices.
// Throws std::runtime_error if no opposing element is found (rect has too
// few columns for the ">= 28 apart" exclusion to leave any candidate) --
// the MATLAB source has the same implicit assumption: it would index
// rect(:,0) and error if this happened there.
std::pair<int, std::vector<int>> getReceiveElements(const Eigen::MatrixXd& rect, int elIndex1Based,
                                                      double lambda, double diameter);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/defineArrayStruct.m.
ArrayStruct defineArrayStruct(const Eigen::MatrixXd& rect, double frequency,
                               std::array<double, 2> elementDimensions);

// Port of BeamV0/GUIMatlab/BEAM/Util/getOpposingElements.m. Lives here
// rather than in libs/util because it needs the ArrayData type (util cannot
// depend on array). Finds which of arrayData's arrays contains an element
// whose .number equals elementNumber, then returns the element numbers
// (row 0 of each rect -- a data value, not a container index) of every
// element in the *other* array(s), concatenated in array order, matching
// the MATLAB source's `horzcat(..., arrayData.array(k).rect(1,:))` loop.
//
// Throws std::invalid_argument if elementNumber isn't found in any array.
// The MATLAB source has no such guard: `currentArray` would be left
// unassigned and it would error at runtime the moment it's used. Failing
// loudly here is the equivalent behavior, not a silent wrong answer.
std::vector<int> getOpposingElements(const ArrayData& arrayData, int elementNumber);

}  // namespace beam::array
