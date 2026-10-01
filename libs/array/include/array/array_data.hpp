#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::array {

// Port of BeamV0/GUIMatlab/BEAM/Arrays/defineArrayData.m.
//
// Faithful port, including what looks like unfinished source behavior:
// arrayData.array[0] and array[1] are both full copies of the *same*
// defineArrayStruct(rect, 150000, [0.06,0.06]) result (frequency 150kHz,
// 60mm elements -- Beam's real hardcoded values, not Diadem's), tagged with
// elementMapping 1 and 2 respectively, rather than actually being split
// into two physical sub-arrays. The MATLAB source has a commented-out
// `%reshape(1:126,9,14)` next to the elementMapping assignment suggesting a
// real split was intended but not implemented -- ported as-is, not fixed.
ArrayData defineArrayData(const Eigen::MatrixXd& rect);

// Splits arrayTotal into the two physical panels defineArrayData.m leaves
// as placeholders, so array[0] is the subject-Right half and array[1] the
// subject-Left half (MATLAB designations 1 and 2).
//
// Not a port of any single .m file -- BeamV0 never needs it because its
// sys .mat ships the halves already split. It is required all the same:
// setArrayFiducialMarkers.m centres each marker on
// array(designation).rect, so running it on a bare defineArrayData result
// places all six fiducials around one shared centre, ~90 mm off in LR.
// The midline split below was verified against BeamV0's shipped geometry
// (DefaultSubjectV0/defaultSubjectMNIV1.mat): 80 elements per side and all
// six nominal fiducials identical to the .mat.
//
// Throws std::runtime_error if every element falls on one side of the
// midline, i.e. the geometry has no two distinguishable panels.
void reconstructPhysicalArrayHalves(ArrayData& data);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/defineArrayTxElements.m.
// mode must be one of "first", "second", "firstThenSecond", "both"
// (matching the MATLAB source's strcmp branches exactly); throws
// std::invalid_argument otherwise (MATLAB: error('Tx Elements Not
// Defined')). Returns {txElements, txElementsArray}; each is 1 row for
// "first"/"second"/"both", 2 rows for "firstThenSecond" -- same shape the
// MATLAB source produces (a plain vector vs. a 2-row matrix).
struct TxElements {
    std::vector<std::vector<int>> txElements;
    std::vector<std::vector<int>> txElementsArray;
};
TxElements defineArrayTxElements(const std::string& mode);

// Port of BeamV0/GUIMatlab/BEAM/Arrays/arrayElementsToVSXElements.m.
// arrayElements: 1-based indices into the 252-entry array-to-VSX mapping
// ([1:126, 129:254], matching the MATLAB source exactly -- elements 127/128
// are skipped by this mapping, not this function's choice).
std::vector<int> arrayElementsToVSXElements(const std::vector<int>& arrayElements);

}  // namespace beam::array
