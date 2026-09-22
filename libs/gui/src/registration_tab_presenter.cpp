#include "gui/registration_tab_presenter.hpp"

#include <stdexcept>

#include <Eigen/Geometry>

#include "array/geometry.hpp"

namespace beam::gui {

Eigen::Matrix4d getAcpcTransform(const Eigen::Vector3d& acXyz, const Eigen::Vector3d& pcXyz,
                                 const Eigen::Vector3d& midSagittalPlaneNormal,
                                 const Eigen::Vector3d& referenceCoordsXyz) {
    Eigen::Vector3d y = (acXyz - pcXyz).normalized();
    y(0) = -y(0);

    Eigen::Vector3d x = midSagittalPlaneNormal.normalized();
    // Source: X = X + dot(X,Y)/(norm(X)*norm(Y)) * Y -- an *addition*, not
    // the subtraction Gram-Schmidt orthogonalization would use. Both X, Y
    // are unit vectors here, so this is just X + dot(X,Y)*Y.
    x = x + x.dot(y) * y;
    if (x(0) < 0.0) {
        x = -x;
    }

    Eigen::Vector3d z = x.cross(y);
    if (z(2) < 0.0) {
        throw std::runtime_error("getAcpcTransform: coordinates in wrong orientation");
    }
    z.normalize();
    z(1) = -z(1);

    const Eigen::Vector3d origin = -referenceCoordsXyz;

    Eigen::Matrix4d t = Eigen::Matrix4d::Identity();
    t.block<3, 1>(0, 0) = x;
    t.block<3, 1>(0, 1) = y;
    t.block<3, 1>(0, 2) = z;
    t.block<3, 1>(0, 3) = origin;
    return t;
}

Eigen::Vector3d applyReferenceCoordinateTransform(const Eigen::Matrix4d& transform,
                                                   const Eigen::Vector3d& xyz,
                                                   ReferenceTransformDirection direction) {
    const Eigen::Matrix3d r = transform.block<3, 3>(0, 0);
    const Eigen::Vector3d t = transform.block<3, 1>(0, 3);

    if (direction == ReferenceTransformDirection::kMriToReference) {
        return r * (xyz + t);
    }
    // kReferenceToMri
    return r.inverse() * xyz - t;
}

ArrayFiducialNormals getArrayFiducialNormals(
    const std::vector<beam::registration::FiducialMarker>& fiducialMarkers) {
    if (fiducialMarkers.size() < 3) {
        throw std::invalid_argument("getArrayFiducialNormals: needs at least 3 fiducial markers");
    }

    const Eigen::Vector3d v2raw = beam::array::normalVectorFrom3Points(
        fiducialMarkers[0].position, fiducialMarkers[1].position, fiducialMarkers[2].position);
    const Eigen::Vector3d v1raw = fiducialMarkers[0].position - fiducialMarkers[1].position;

    // (x,y,z) -> (z,x,y), matching `v1 = [v1(3),v1(1),v1(2)]`.
    ArrayFiducialNormals result;
    result.v1 = Eigen::Vector3d(v1raw(2), v1raw(0), v1raw(1));
    result.v2 = Eigen::Vector3d(v2raw(2), v2raw(0), v2raw(1));
    return result;
}

}  // namespace beam::gui
