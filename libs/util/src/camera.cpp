#include "util/camera.hpp"

#include <cmath>

#include <Eigen/Dense>

namespace beam::util {

Eigen::MatrixX3d cam2targetSpace(const Eigen::MatrixX3d& points3D, const Eigen::Matrix3d& r,
                                  const Eigen::Vector3d& tvec) {
    // R' * (Points3D' - tvec), then transpose back to N x 3.
    return (r.transpose() * (points3D.transpose().colwise() - tvec)).transpose();
}

Eigen::MatrixXd mm2pixel(const Eigen::MatrixXd& mmCoords, double spacing,
                          const Eigen::RowVectorXd& origin) {
    return (mmCoords / spacing).rowwise() + origin;
}

Eigen::MatrixX3d mm3Dpnp(const Eigen::Matrix3d& r, const Eigen::Vector3d& tvec,
                          const Eigen::MatrixX2d& pixelCoords, const Eigen::Matrix3d& k, double zRel) {
    const Eigen::Vector3d normalVec = r * Eigen::Vector3d(0, 0, 1);
    const Eigen::Vector3d planeOrigin = r * Eigen::Vector3d(0, 0, zRel) + tvec;
    const Eigen::Matrix3d kInv = k.inverse();

    Eigen::MatrixX3d out(pixelCoords.rows(), 3);
    for (Eigen::Index i = 0; i < pixelCoords.rows(); ++i) {
        const Eigen::Vector3d pixelHomog(pixelCoords(i, 0), pixelCoords(i, 1), 1.0);
        Eigen::Vector3d rayCam = kInv * pixelHomog;
        rayCam.normalize();
        const double t = planeOrigin.dot(normalVec) / rayCam.dot(normalVec);
        out.row(i) = (t * rayCam).transpose();
    }
    return out;
}

Eigen::Vector3d rotm2eulSimple(const Eigen::Matrix3d& r) {
    const double sy = -r(2, 0);
    const double cy = std::sqrt(1.0 - sy * sy);

    double yaw, pitch, roll;
    if (cy > 1e-6) {
        yaw = std::atan2(r(1, 0), r(0, 0));
        pitch = std::asin(sy);
        roll = std::atan2(r(2, 1), r(2, 2));
    } else {
        yaw = std::atan2(-r(0, 1), r(1, 1));
        pitch = std::asin(sy);
        roll = 0.0;
    }
    return Eigen::Vector3d(yaw, pitch, roll);
}

}  // namespace beam::util
