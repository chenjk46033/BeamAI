#pragma once

#include <string>
#include <vector>

#include "mri/mri_loader.hpp"

// Copied from Diadem's libs/infra_dicom/dicom_series.hpp. Assembles a
// DICOM series (a folder's worth of same-orientation, uniformly-spaced
// slices) into the same MriVolumeRas shape loadNiftiMriRas produces --
// what replaces dicm2nii's job for MRI/loadMRIRAS.m's DICOM-folder path.
// Common case only: one consistent orientation, uniform slice spacing,
// 16-bit pixel data; anything else throws a clear named error.

namespace beam::infra::dicom {

// filePaths: one file per slice, any order -- sorted internally by
// ImagePositionPatient projected onto the slice normal.
//
// Throws std::runtime_error for: empty filePaths; a slice missing
// ImageOrientationPatient; inconsistent rows/columns or orientation
// across slices; non-uniform slice spacing (beyond ~1%); a single-slice
// series with no SpacingBetweenSlices/SliceThickness; duplicate/
// non-increasing slice positions; or anything readDicomSlice throws.
beam::mri::MriVolumeRas assembleDicomSeriesRas(const std::vector<std::string>& filePaths);

}  // namespace beam::infra::dicom
