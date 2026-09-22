#pragma once

#include <array>

#include <Eigen/Core>

// The subset of a NIfTI-1 header this library needs, matching the field
// names/indexing of MATLAB's nii_hdr struct (Shen's NIfTI toolbox
// convention: nii_hdr.dime.*, nii_hdr.hist.*). dim[i]/pixdim[i] here ==
// MATLAB's dim(i+1)/pixdim(i+1) (1-based -> 0-based).
//
// Copied from Diadem's libs/imaging (diadem::imaging) -- BeamV0's MRI/
// coordinate-math .m files (get_ras_xform_fromHdr, getRASAxisVectorsFromNifti,
// getSliceImage) are byte-identical to DiademV0's, so Diadem's already
// MATLAB-parity-verified port is reused here rather than re-derived.

namespace beam::mri {

struct NiftiHeader {
    std::array<int, 8> dim{};       // dim[1..3] = volume size (nx,ny,nz)
    std::array<double, 8> pixdim{}; // pixdim[0] = qfac; [1..3] = voxel size

    int qformCode = 0;
    double quaternB = 0.0, quaternC = 0.0, quaternD = 0.0;
    double qoffsetX = 0.0, qoffsetY = 0.0, qoffsetZ = 0.0;

    int sformCode = 0;
    Eigen::RowVector4d srowX = Eigen::RowVector4d::Zero();
    Eigen::RowVector4d srowY = Eigen::RowVector4d::Zero();
    Eigen::RowVector4d srowZ = Eigen::RowVector4d::Zero();

    // Fields below are only needed for reading the actual voxel data (not
    // by the RAS-transform math above), added for nifti_file.hpp.
    int datatype = 0;      // NIfTI DT_* code (see nifti_file.hpp)
    int bitpix = 0;
    double voxOffset = 0.0;  // byte offset of voxel data within the file
    double sclSlope = 0.0;   // 0 means "do not scale" per the NIfTI-1 spec,
    double sclInter = 0.0;   // not literally scale-to-zero
};

}  // namespace beam::mri
