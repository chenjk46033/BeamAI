#pragma once

#include <Eigen/Core>

#include "mri/slice.hpp"

// Port of BeamV0/GUIMatlab/BEAM/Registration/ImageBasedModel/apply_affine_3d.m
// -- resample a 3D volume through a world-space (mm) affine, choosing an
// output grid (same voxel size) that fits the transformed bounding box.
// MATLAB's imref3d / affine3d / imwarp are replaced with an explicit
// inverse-map + trilinear sampler (imwarp's 'linear' + FillValues 0).

namespace beam::mri {

struct AffineVolumeResult {
    Volume3D volume;
    Eigen::VectorXd xMm;  // world (mm) coordinate of each output voxel, axis 0/1/2
    Eigen::VectorXd yMm;
    Eigen::VectorXd zMm;
};

// v: input volume, indexed (i, j, k). xMm/yMm/zMm: the world coordinate of
// each input voxel along axes 0/1/2 -- assumed uniformly spaced (the source
// takes mean(diff(...)) for the step). mWorld: 4x4 column-vector affine
// ([x;y;z;1]_out = mWorld * [x;y;z;1]_in), matching apply_affine_3d.m's
// M_world (it internally transposes for affine3d's row convention).
AffineVolumeResult applyAffine3D(const Volume3D& v, const Eigen::VectorXd& xMm,
                                  const Eigen::VectorXd& yMm, const Eigen::VectorXd& zMm,
                                  const Eigen::Matrix4d& mWorld);

}  // namespace beam::mri
