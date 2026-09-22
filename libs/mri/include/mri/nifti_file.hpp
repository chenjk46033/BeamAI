#pragma once

#include <string>
#include <vector>

#include "mri/nifti_header.hpp"

// Copied from Diadem's libs/imaging/nifti_file.hpp (diadem::imaging).
//
// A minimal, dependency-free NIfTI-1 reader for the common case: a
// single-file ("n+1") .nii, uncompressed, little-endian, one of a
// handful of common voxel datatypes. NOT a general NIfTI/DICOM library
// replacement -- .nii.gz, big-endian files, exotic datatypes, and
// two-file "ni1" Analyze-style pairs are out of scope.

namespace beam::mri {

// Throws std::runtime_error for: wrong magic/sizeof_hdr (not a valid
// little-endian NIfTI-1 file), or a file too short to hold a header.
NiftiHeader readNiftiHeader(const std::string& path);

struct NiftiVolume {
    NiftiHeader header;
    std::vector<double> voxels;  // flat, file (column-major/Fortran) order,
                                  // scl_slope/scl_inter already applied
};

// Throws std::runtime_error for the same header problems as
// readNiftiHeader, plus an unsupported datatype code.
NiftiVolume readNiftiVolumeScaled(const std::string& path);

// Writes a single-file ("n+1") little-endian NIfTI-1: the 348-byte header
// (from `header`), 4 pad bytes, then `voxels` written in the on-disk type
// `header.datatype` names (each double cast to that type). This is the
// portable core of BeamV0/GUIMatlab/BEAM/MRI/setNiftiFromSys.m -- its
// `sys` bookkeeping and nifti_utils dependency are not carried over; the
// caller supplies the header + volume.
//
// vox_offset is set to 352 and scl_slope/scl_inter to 0 ("no scaling"):
// voxels are written as-is. Throws std::runtime_error for an unsupported
// datatype code or if the file can't be opened.
void writeNiftiVolume(const std::string& path, const NiftiHeader& header,
                       const std::vector<double>& voxels);

}  // namespace beam::mri
