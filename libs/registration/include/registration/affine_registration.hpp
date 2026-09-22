#pragma once

#include <Eigen/Core>

// Ported from BeamV0/GUIMatlab/BEAM/Registration/affineRegistration.m and
// getAffineMatrixFromRegistration.m. affineRegistration.m is byte-identical
// to DiademV0's (the well-known public "absor" algorithm -- Horn's
// quaternion method for the best-fit transform between two 3D point sets,
// credited to Matt Jacobson / Xoran Technologies), so this is Diadem's
// libs/registration port with one addition: BeamV0's
// getAffineMatrixFromRegistration.m calls affineRegistration with a
// `weights` vector ([0.5,1,0.5,0.5,1,0.5]), which DiademV0's does not, so
// the weighted centering path is ported here too.
//
// Not ported (matching Diadem's scope): the MATLAB 2D branch (unreachable
// here -- always 3D), and the Bfit / ErrorStats outputs (never consumed).

namespace beam::registration {

struct AffineRegistrationResult {
    Eigen::Matrix3d r = Eigen::Matrix3d::Identity();
    Eigen::Vector3d t = Eigen::Vector3d::Zero();
    double s = 1.0;
    Eigen::Matrix4d m = Eigen::Matrix4d::Identity();
    Eigen::Vector4d q = Eigen::Vector4d(1, 0, 0, 0);
};

// Ported from affineRegistration.m's 3D branch. Finds the rotation (and
// optionally scale/translation) best mapping the columns of `a` onto the
// columns of `b`. `weights`: per-point weights (size N), or an empty vector
// for the unweighted case -- matching the MATLAB `weights` option.
AffineRegistrationResult affineRegistration(const Eigen::Matrix3Xd& a, const Eigen::Matrix3Xd& b,
                                             const Eigen::VectorXd& weights = Eigen::VectorXd(),
                                             bool doScale = false, bool doTrans = true);

// Ported from getAffineMatrixFromRegistration.m. Wraps affineRegistration()
// (with BeamV0's fixed weights [0.5,1,0.5,0.5,1,0.5] -- so `a`/`b` must have
// exactly 6 columns) and packages R/t into a 4x4 homogeneous transform.
// When metersToMm is true the translation is divided by 1000 (the caller
// passed millimetre-scaled array positions in; this converts the result's
// translation back to metres), matching the source's meters2mmFlag branch.
Eigen::Matrix4d getAffineMatrixFromRegistration(const Eigen::Matrix3Xd& a, const Eigen::Matrix3Xd& b,
                                                 bool metersToMm);

}  // namespace beam::registration
