#pragma once

#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"

namespace beam::correction {

// Ports of BeamV0/GUIMatlab/BEAM/Correction/localizeArrays/ helpers. Only
// the driver (localizeArraysMaster.m, drives MATLAB's lsqnonlin) is still
// deferred -- see docs/known_gaps_correction.md.

// Port of localizeArrays/distance.m for the single-point case (its only
// real use, from nonlinRelativeDistanceFun.m). The MATLAB source's
// row-wise-over-a-matrix generality is not carried over.
double distance(const Eigen::Vector3d& v1, const Eigen::Vector3d& v2);

// Port of localizeArrays/updateArrayPositions.m. rs: new element-center
// positions, one row (xyz) per element, length must match
// array.element.size(). Shifts each element's 4 rect corners by
// (newCenter - oldCenter) and writes the new center into rect rows 17:19
// and element.position.
beam::array::ArrayStruct updateArrayPositions(beam::array::ArrayStruct array,
                                               const Eigen::MatrixX3d& rs);

// Port of localizeArrays/nonlinRelativeDistanceFun.m -- the residual vector
// lsqnonlin minimizes. x: [c; positions], where positions is
// 3*nElements long, column-major (x(1)..x(nEl) = all x's, then all y's,
// then all z's), matching MATLAB's reshape(positions, nEl, 3). mData: one
// row [i j k] of 1-based element indices per equation. dData: measured
// relative arrival-time differences (seconds). fs: sample rate (params.fs).
Eigen::VectorXd nonlinRelativeDistanceFun(const Eigen::MatrixXi& mData, const Eigen::VectorXd& dData,
                                           int nElements, double fs, const Eigen::VectorXd& x);

// Port of localizeArrays/getLocalizeArraysSystemOfEquations.m -- builds the
// (MData, dData) system for the array-localization least-squares fit from
// through-transmit receive waveforms.
//
// wvData: one matrix per transmit element (outer index = transmit element,
// 0-based), each matrix nSamples x nElements, so MATLAB's wvData(:,k,i)
// is wvData[i-1].col(k-1). params.f = transmit frequency (Hz),
// params.fs = sample rate (Hz).
//
// arrayData.arrayTotal.element[*].receiveElements must be populated (they
// are, by defineArrayStruct). Returns MData (M x 3, 1-based [i j k] per
// row) and dData (M, seconds -- already divided by fs).
struct LocalizeSystem {
    Eigen::MatrixXi mData;
    Eigen::VectorXd dData;
};
LocalizeSystem getLocalizeArraysSystemOfEquations(const std::vector<Eigen::MatrixXd>& wvData,
                                                   const beam::array::ArrayData& arrayData,
                                                   double f, double fs);

// Port of the solver core of localizeArrays/localizeArraysMaster.m -- the
// `lsqnonlin(nonlinRelativeDistanceFun, initial, lb, ub)` fit. The script's
// hardcoded-path .mat load/save, plotting, and transmitReceiveScan calls
// are not ported (excluded).
//
// initialPositions: nElements x 3, the pre-localization element centres
// (metres). c0: initial speed-of-sound guess (m/s). Bounds are built the
// way the MATLAB source does: c in [c0-20, c0+20]; every element on the
// top or bottom row of its 9-per-column layout (MATLAB 1:9:end and
// 9:9:end) is pinned to its initial position; every other element's x/y/z
// may move +/- 2.3 mm.
//
// Runs a bounded Levenberg-Marquardt (box projection) for at most
// maxIterations steps. Returns the optimised speed of sound and element
// positions -- feed positions to updateArrayPositions to get the new rect.
struct LocalizeResult {
    double c = 0.0;
    Eigen::MatrixX3d positions;
};
LocalizeResult localizeArrays(const LocalizeSystem& system, const Eigen::MatrixX3d& initialPositions,
                               double c0, double fs, int maxIterations = 7);

}  // namespace beam::correction
