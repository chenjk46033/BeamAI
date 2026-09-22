#include "registration/fiducial_markers.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "array/array_types.hpp"

namespace beam::registration {

using beam::array::ArrayData;

namespace {

// Column-wise median of the rows of `rows` (each entry a 3-vector) --
// MATLAB median(X, 1) for an Nx3 X. Even N -> mean of the two middle.
Eigen::Vector3d columnwiseMedian(std::vector<Eigen::Vector3d> rows) {
    Eigen::Vector3d out;
    for (int c = 0; c < 3; ++c) {
        std::vector<double> vals;
        vals.reserve(rows.size());
        for (const auto& r : rows) vals.push_back(r(c));
        std::sort(vals.begin(), vals.end());
        const std::size_t n = vals.size();
        out(c) = (n % 2 == 1) ? vals[n / 2] : 0.5 * (vals[n / 2 - 1] + vals[n / 2]);
    }
    return out;
}

// mean of rect rows 17:19 (0-based 16..18) over all columns -> the array
// half's centre, in metres.
Eigen::Vector3d rectCentreMean(const beam::array::ArrayStruct& a) {
    return a.rect.middleRows(beam::array::kRectCenterStartRow, 3).rowwise().mean();
}

}  // namespace

std::vector<std::string> getArrayFiducialMarkerNames() {
    return {"EL1", "EL9", "EL118", "EL126", "EL127", "EL135", "EL244", "EL252"};
}

std::vector<FiducialMarker> setArrayFiducialMarkers(const ArrayData& arrayData) {
    constexpr double absX = 19.0 / 1000.0;
    constexpr double absY = 22.5 / 1000.0;
    constexpr double absZ = 20.0 / 1000.0;

    struct Spec {
        const char* name;
        Eigen::Vector3d rel;
        int designation;  // 1-based array half (1 or 2)
    };
    const std::array<Spec, 6> specs = {{
        {"LeftY1Z3", {-absX, 0.0, absZ}, 2},
        {"LeftY1Z1", {-absX, 0.0, 0.0}, 2},
        {"LeftY4Z1", {-absX, -absY, 0.0}, 2},
        {"RightY1Z3", {absX, 0.0, absZ}, 1},
        {"RightY1Z1", {absX, 0.0, 0.0}, 1},
        {"RightY4Z1", {absX, -absY, 0.0}, 1},
    }};

    const auto& el = arrayData.arrayTotal.element;
    const auto pos = [&el](int n1Based) { return el[static_cast<std::size_t>(n1Based - 1)].position; };
    Eigen::Vector3d xVector = (pos(1) - pos(81)).normalized();
    Eigen::Vector3d zVector = (pos(31) - pos(40)).normalized();
    Eigen::Vector3d yVector = (pos(24) - pos(1)).normalized();
    Eigen::Matrix3d m;
    m.col(0) = xVector;
    m.col(1) = yVector;
    m.col(2) = zVector;

    // Loop 1: initial positions = rel + array-half centre.
    std::vector<FiducialMarker> markers(6);
    for (std::size_t i = 0; i < 6; ++i) {
        markers[i].name = specs[i].name;
        const Eigen::Vector3d centre = rectCentreMean(arrayData.array[static_cast<std::size_t>(specs[i].designation - 1)]);
        markers[i].position = specs[i].rel + centre;
    }

    // Loop 2: reference centre per array half (each half's Y1Z1 marker).
    Eigen::Vector3d center1 = Eigen::Vector3d::Zero();  // Right (designation 1)
    Eigen::Vector3d center2 = Eigen::Vector3d::Zero();  // Left  (designation 2)
    for (const auto& mk : markers) {
        if (mk.name == "LeftY1Z1") center2 = mk.position;
        else if (mk.name == "RightY1Z1") center1 = mk.position;
    }

    // Loop 3: rotate each marker about its half's reference centre by M.
    for (std::size_t i = 0; i < 6; ++i) {
        const Eigen::Vector3d centreTrans = (specs[i].designation == 1) ? center1 : center2;
        markers[i].position = centreTrans + m * (markers[i].position - centreTrans);
    }
    return markers;
}

std::vector<FiducialMarker> applyAffineMatrixToFiducialMarkers(
    const Eigen::Matrix4d& affineMatrix, const std::vector<FiducialMarker>& markers) {
    std::vector<FiducialMarker> result = markers;
    for (auto& marker : result) {
        const Eigen::Vector4d h(marker.position.x(), marker.position.y(), marker.position.z(), 1.0);
        marker.position = (affineMatrix * h).head<3>();
    }
    return result;
}

std::vector<FiducialMarker> getTransducerFiducialMarkersPositionFromFrame(
    const std::vector<FiducialMarker>& baselineMarkers, const Eigen::Matrix3d& regRotation,
    double baselineVerticalPosition, double baselineHorizontalPosition, double verticalDelta,
    double horizontalDelta, double currentVerticalPosition, double currentHorizontalPosition) {
    const double dV = currentVerticalPosition - baselineVerticalPosition;
    const double dH = currentHorizontalPosition - baselineHorizontalPosition;
    const double dY = dV * verticalDelta / 1000.0;
    const double dZ = -dH * horizontalDelta / 1000.0;
    const Eigen::Vector3d regDXYZ = regRotation * Eigen::Vector3d(0.0, dY, dZ);

    std::vector<FiducialMarker> result = baselineMarkers;
    for (auto& marker : result) marker.position += regDXYZ;
    return result;
}

Eigen::Vector3d getFiducialPositionFromName(const std::string& name,
                                             const std::vector<FiducialMarker>& markers) {
    bool found = false;
    Eigen::Vector3d position = Eigen::Vector3d::Zero();
    for (const auto& mk : markers) {
        if (mk.name == name) {
            position = mk.position;  // last match wins, matching the MATLAB loop
            found = true;
        }
    }
    if (!found) {
        throw std::invalid_argument("getFiducialPositionFromName: no marker named '" + name + "'");
    }
    return position;
}

TransducerBasis getTranslationMatrixFromTransducerFiducials(const std::vector<FiducialMarker>& markers) {
    const auto p = [&markers](const std::string& n) { return getFiducialPositionFromName(n, markers); };

    const auto medianUnit = [](std::vector<Eigen::Vector3d> diffs) {
        for (auto& d : diffs) d.normalize();
        const Eigen::Vector3d med = columnwiseMedian(std::move(diffs));
        return med.normalized();
    };

    const Eigen::Vector3d xVector = medianUnit({p("RightY1Z3") - p("LeftY1Z3"),
                                                p("RightY1Z1") - p("LeftY1Z1"),
                                                p("RightY4Z1") - p("LeftY4Z1")});
    const Eigen::Vector3d zVector = medianUnit({p("LeftY1Z3") - p("LeftY1Z1"),
                                                p("RightY1Z3") - p("RightY1Z1")});
    const Eigen::Vector3d yVector = medianUnit({p("LeftY1Z1") - p("LeftY4Z1"),
                                                p("RightY1Z1") - p("RightY4Z1")});

    TransducerBasis basis;
    basis.xVector = xVector;
    basis.yVector = yVector;
    basis.zVector = zVector;
    basis.m.row(0) = xVector.transpose();
    basis.m.row(1) = yVector.transpose();
    basis.m.row(2) = zVector.transpose();
    return basis;
}

}  // namespace beam::registration
