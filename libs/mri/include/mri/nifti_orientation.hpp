#pragma once

#include <array>
#include <vector>

#include <Eigen/Core>

// Copied from Diadem's libs/imaging/nifti_orientation.hpp (diadem::imaging).

namespace beam::mri {

struct ReorientedVolume {
    std::vector<double> voxels;  // flat, column-major, newDims order
    std::array<int, 3> dims{};
};

// Ported from nifti_utils/+nifti_utils/vol_apply_xform.m, restricted to
// 3D volumes -- matches loadMRIRAS.m's usage. 4D time-series reorientation
// is out of scope for now (disclosed, not silently dropped).
//
// voxelXform is a getVoxelRasXform() result: flips axes with a negative
// entry in their column, then permutes axis i to wherever row i's nonzero
// entry points.
ReorientedVolume applyVoxelRasXform3D(const std::vector<double>& voxels,
                                       const std::array<int, 3>& dims,
                                       const Eigen::Matrix3d& voxelXform);

}  // namespace beam::mri
