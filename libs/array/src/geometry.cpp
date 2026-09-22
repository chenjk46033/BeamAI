#include "array/geometry.hpp"

#include <cmath>

#include <Eigen/Geometry>  // for MatrixBase::cross()

#include "util/geometry_math.hpp"

namespace beam::array {

Eigen::Vector3d normalVectorFrom3Points(const Eigen::Vector3d& p1, const Eigen::Vector3d& p2,
                                         const Eigen::Vector3d& p3) {
    const Eigen::Vector3d a = p2 - p1;
    const Eigen::Vector3d b = p3 - p1;
    const Eigen::Vector3d n = b.cross(a);
    const Eigen::Vector3d n1 = a.cross(b);  // == -n
    if ((-p1).dot(n) > (-p1).dot(n1)) {
        return n;
    }
    return n1;
}

Eigen::Vector3d calculateRectNormalVector(const Eigen::MatrixXd& rect, int col1Based) {
    const int col = col1Based - 1;
    const Eigen::Vector3d c1 = rectCorner(rect, col, 1);
    const Eigen::Vector3d c2 = rectCorner(rect, col, 2);
    const Eigen::Vector3d c3 = rectCorner(rect, col, 3);
    Eigen::Vector3d n = normalVectorFrom3Points(c1, c2, c3);
    return n / n.norm();
}

void calculateArrayNormalVectors(ArrayStruct& arrayStruct) {
    const Eigen::Index n = arrayStruct.rect.cols();
    for (Eigen::Index i = 0; i < n; ++i) {
        const int col1Based = static_cast<int>(i) + 1;
        const Eigen::Vector3d normal = calculateRectNormalVector(arrayStruct.rect, col1Based);
        const Eigen::Vector3d c1 = rectCorner(arrayStruct.rect, static_cast<int>(i), 1);
        const Eigen::Vector3d c2 = rectCorner(arrayStruct.rect, static_cast<int>(i), 2);
        const Eigen::Vector3d c3 = rectCorner(arrayStruct.rect, static_cast<int>(i), 3);

        ArrayElement& element = arrayStruct.element.at(static_cast<size_t>(i));
        element.normalVector = normal;
        element.center = rectCenter(arrayStruct.rect, static_cast<int>(i));
        element.xNorm = (c2 - c3) / (c2 - c3).norm();
        element.yNorm = (c1 - c2) / (c1 - c2).norm();
    }
}

Eigen::MatrixXd getElementPositionsFromArrayStruct(const ArrayStruct& arrayStruct) {
    Eigen::MatrixXd positions(static_cast<Eigen::Index>(arrayStruct.element.size()), 3);
    for (size_t e = 0; e < arrayStruct.element.size(); ++e) {
        positions.row(static_cast<Eigen::Index>(e)) = arrayStruct.element[e].position.transpose();
    }
    return positions;
}

namespace {
// Port of MATLAB's linspace(0, 1, n): n evenly spaced points in [0,1]
// (n==1 returns just {1}, n==0 returns empty -- matching MATLAB exactly).
std::vector<double> linspaceZeroToOne(int n) {
    std::vector<double> out;
    if (n <= 0) {
        return out;
    }
    if (n == 1) {
        out.push_back(1.0);
        return out;
    }
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(static_cast<double>(i) / static_cast<double>(n - 1));
    }
    return out;
}
}  // namespace

Eigen::MatrixXd spatiallySampleElement(const Eigen::Matrix<double, 3, 4>& corners, double fs) {
    const double maxRange = std::max({util::vectorRange(corners.row(0)), util::vectorRange(corners.row(1)),
                                       util::vectorRange(corners.row(2))});
    const int nSamples = static_cast<int>(std::lround(maxRange / fs));

    const Eigen::Vector3d v1 = corners.col(0);
    const Eigen::Vector3d v2 = corners.col(1);
    const Eigen::Vector3d v3 = corners.col(3);  // matches MATLAB's corners(:,4), not corners(:,3)

    const std::vector<double> t = linspaceZeroToOne(nSamples);
    const std::vector<double> r = linspaceZeroToOne(nSamples);

    Eigen::MatrixXd points(static_cast<Eigen::Index>(nSamples) * nSamples, 3);
    Eigen::Index count = 0;
    for (double ti : t) {
        for (double ri : r) {
            points.row(count) = ((v3 - v1) * ri + (v2 - v1) * ti + v1).transpose();
            ++count;
        }
    }
    return points;
}

}  // namespace beam::array
