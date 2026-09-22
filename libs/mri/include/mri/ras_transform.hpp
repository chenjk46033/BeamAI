#pragma once

#include <Eigen/Core>

#include "mri/nifti_header.hpp"

// Copied from Diadem's libs/imaging/ras_transform.hpp. Ports of
// BeamV0/GUIMatlab/BEAM/MRI/get_ras_xform_fromHdr.m and
// getRASAxisVectorsFromNifti.m (byte-identical to DiademV0's), plus
// getVoxelRasXform from nifti_utils/get_voxel_RAS_xform.m.

namespace beam::mri {

using RasXform = Eigen::Matrix<double, 3, 4>;

// Ported from MRI/get_ras_xform_fromHdr.m. Throws std::runtime_error for
// the no-qform/no-sform case, matching the source's error(...) call.
RasXform getRasXformFromHeader(const NiftiHeader& header);

struct RasAxisVectors {
    Eigen::VectorXd dimLR;
    Eigen::VectorXd dimAP;
    Eigen::VectorXd dimIS;
};

// Ported from MRI/getRASAxisVectorsFromNifti.m.
RasAxisVectors getRasAxisVectors(const NiftiHeader& header);

// Ported from GUI/updateSysWithMRI.m's `res` computation: each axis's
// voxel spacing, `abs(diff(dim(1:2)))` -- app.sys.aRes. Already used inline
// (duplicated, not shared) by beam::gui::rasterizeArrayOntoMriGrid's `fs`
// calculation; this is the standalone, named, tested version of that same
// math, matching the source's own separately-callable function.
struct VoxelResolution {
    double lr = 0.0;
    double ap = 0.0;
    double is = 0.0;
};
VoxelResolution computeVoxelResolution(const RasAxisVectors& axes);

// Ported from nifti_utils/+nifti_utils/get_voxel_RAS_xform.m. A 3x3
// sign/permutation matrix (entries in {-1,0,+1}) describing how to
// reorder+flip voxel array axes into RAS order, without resampling --
// distinct from getRasXformFromHeader's continuous affine. Derived by
// scaling out voxel size, keeping only the 3 dominant-magnitude entries
// of the resulting 3x3, and taking their sign.
//
// Throws std::runtime_error if the result is singular (source: `error('RAS
// voxel orientation matrix is singular.')`), e.g. for a genuinely oblique,
// non-axis-aligned acquisition this heuristic can't reduce to a clean
// permutation. Uses a small tolerance on the determinant rather than
// MATLAB's exact `== 0`.
Eigen::Matrix3d getVoxelRasXform(const NiftiHeader& header);

}  // namespace beam::mri
