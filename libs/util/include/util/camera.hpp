#pragma once

#include <Eigen/Core>

// Ported from BeamV0/GUIMatlab/BEAM/Util/: cam2targetSpace.m, mm2pixel.m,
// mm3Dpnp.m, rotm2eul_simple.m -- camera / pixel <-> 3D coordinate math.
// Their one consumer is Registration/ImageBasedModel/getImageData.m.

namespace beam::util {

// Port of cam2targetSpace.m:  localXYZ = (R' * (Points3D' - tvec))'
// points3D: N x 3 points in camera space. Returns N x 3 in target space.
Eigen::MatrixX3d cam2targetSpace(const Eigen::MatrixX3d& points3D, const Eigen::Matrix3d& r,
                                  const Eigen::Vector3d& tvec);

// Port of mm2pixel.m:  pixel_coords = mm_coords / spacing + origin
// mmCoords: N x D. origin: 1 x D (added to every row).
Eigen::MatrixXd mm2pixel(const Eigen::MatrixXd& mmCoords, double spacing,
                          const Eigen::RowVectorXd& origin);

// Port of mm3Dpnp.m. For each image pixel, intersects its back-projected
// camera ray with the plane z = zRel (in the frame R defines). pixelCoords:
// N x 2 (u, v). k: 3x3 camera intrinsics. Returns N x 3 intersection points
// in camera coordinates.
Eigen::MatrixX3d mm3Dpnp(const Eigen::Matrix3d& r, const Eigen::Vector3d& tvec,
                          const Eigen::MatrixX2d& pixelCoords, const Eigen::Matrix3d& k, double zRel);

// Port of rotm2eul_simple.m. 3x3 rotation matrix -> ZYX Euler angles
// [yaw, pitch, roll] in radians, with the same gimbal-lock branch
// (|cos(pitch)| <= 1e-6 -> roll = 0).
Eigen::Vector3d rotm2eulSimple(const Eigen::Matrix3d& r);

}  // namespace beam::util
