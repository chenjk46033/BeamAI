#include "registration/affine_registration.hpp"

#include <Eigen/Eigenvalues>

// Ported from Diadem's libs/registration/src/affine_registration.cpp, with
// the `weights` path added (BeamV0's getAffineMatrixFromRegistration.m
// passes weights; DiademV0's does not).

namespace beam::registration {

AffineRegistrationResult affineRegistration(const Eigen::Matrix3Xd& a, const Eigen::Matrix3Xd& b,
                                             const Eigen::VectorXd& weights, bool doScale, bool doTrans) {
    Eigen::Vector3d lc;
    Eigen::Vector3d rc;
    Eigen::Matrix3Xd left;
    Eigen::Matrix3Xd right;

    if (weights.size() == 0) {
        lc = a.rowwise().mean();
        rc = b.rowwise().mean();
        left = a.colwise() - lc;
        right = b.colwise() - rc;
    } else {
        // MATLAB: weights = weights/sum(weights); sqrtwts = sqrt(weights)';
        // lc = A*weights; left = (A - lc) .* sqrtwts (per column).
        const Eigen::VectorXd w = weights / weights.sum();
        const Eigen::VectorXd sqrtw = w.array().sqrt();
        lc = a * w;
        rc = b * w;
        left = (a.colwise() - lc) * sqrtw.asDiagonal();
        right = (b.colwise() - rc) * sqrtw.asDiagonal();
    }

    const Eigen::Matrix3d m = left * right.transpose();
    const double sxx = m(0, 0), syx = m(1, 0), szx = m(2, 0);
    const double sxy = m(0, 1), syy = m(1, 1), szy = m(2, 1);
    const double sxz = m(0, 2), syz = m(1, 2), szz = m(2, 2);

    Eigen::Matrix4d n;
    // clang-format off
    n << (sxx + syy + szz),  (syz - szy),          (szx - sxz),          (sxy - syx),
         (syz - szy),        (sxx - syy - szz),    (sxy + syx),          (szx + sxz),
         (szx - sxz),        (sxy + syx),          (-sxx + syy - szz),   (syz + szy),
         (sxy - syx),        (szx + sxz),          (syz + szy),          (-sxx - syy + szz);
    // clang-format on

    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix4d> solver(n);
    Eigen::Index maxIdx = 0;
    solver.eigenvalues().maxCoeff(&maxIdx);
    Eigen::Vector4d q = solver.eigenvectors().col(maxIdx);

    Eigen::Index maxAbsIdx = 0;
    q.array().abs().maxCoeff(&maxAbsIdx);
    if (q(maxAbsIdx) < 0.0) q = -q;
    q.normalize();

    const double q0 = q(0), qx = q(1), qy = q(2), qz = q(3);
    Eigen::Matrix3d z;
    z << q0, -qz, qy, qz, q0, -qx, -qy, qx, q0;
    const Eigen::Vector3d v = q.tail<3>();
    const Eigen::Matrix3d r = v * v.transpose() + z * z;

    AffineRegistrationResult result;
    result.r = r;
    result.q = q;

    if (doScale) {
        result.s = right.cwiseProduct(r * left).sum() / left.array().square().sum();
    }
    if (doTrans) {
        result.t = rc - r * (lc * result.s);
    }

    result.m.setIdentity();
    result.m.topLeftCorner<3, 3>() = result.s * r;
    result.m.topRightCorner<3, 1>() = result.t;
    return result;
}

Eigen::Matrix4d getAffineMatrixFromRegistration(const Eigen::Matrix3Xd& a, const Eigen::Matrix3Xd& b,
                                                 bool metersToMm) {
    Eigen::VectorXd weights(6);
    weights << 0.5, 1.0, 0.5, 0.5, 1.0, 0.5;  // BeamV0's fixed weights
    const AffineRegistrationResult reg = affineRegistration(a, b, weights);

    Eigen::Matrix4d affineMatrix = Eigen::Matrix4d::Identity();
    affineMatrix.topLeftCorner<3, 3>() = reg.r;
    affineMatrix.topRightCorner<3, 1>() = metersToMm ? (reg.t / 1000.0).eval() : reg.t;
    return affineMatrix;
}

}  // namespace beam::registration
