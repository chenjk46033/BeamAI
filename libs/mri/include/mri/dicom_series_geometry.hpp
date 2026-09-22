#pragma once

#include <Eigen/Core>

#include "mri/nifti_header.hpp"

// Copied from Diadem's libs/imaging/dicom_series_geometry.hpp
// (diadem::imaging). New capability, not a MATLAB port: derives a
// NiftiHeader (sform-only) from DICOM series geometry so the existing
// NIfTI RAS-reorientation math (getVoxelRasXform / applyVoxelRasXform3D)
// can process DICOM series too -- both formats reduce to "a 3x4 affine +
// dims".

namespace beam::mri {

// rowDir/colDir: DICOM ImageOrientationPatient direction cosines.
// spacingAlongRowDir/spacingAlongColDir: named by the physical direction
// they apply to (DICOM PixelSpacing[0] is the spacing *between rows*, i.e.
// the step along colDir -- the reverse of what the name suggests).
// sliceSpacing: mm along the slice normal (rowDir x colDir).
// firstSliceIpp: the first (by slice order) slice's ImagePositionPatient.
// dim order (i,j,k) = (numCols, numRows, numSlices).
//
// Uses the standard DICOM patient-LPS -> NIfTI-world-RAS conversion:
// negate the x and y rows of the resulting affine.
NiftiHeader buildNiftiHeaderFromDicomSeries(const Eigen::Vector3d& rowDir,
                                             const Eigen::Vector3d& colDir,
                                             const Eigen::Vector3d& firstSliceIpp,
                                             double spacingAlongRowDir, double spacingAlongColDir,
                                             double sliceSpacing, int numCols, int numRows,
                                             int numSlices);

}  // namespace beam::mri
