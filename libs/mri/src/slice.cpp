#include "mri/slice.hpp"

#include <algorithm>
#include <stdexcept>

namespace beam::mri {

namespace {

// One counter-clockwise 90 deg turn (MATLAB rot90): m is r x c, result is
// c x r with result(a, b) = m(b, c-1-a).
Eigen::MatrixXd rot90(const Eigen::MatrixXd& m) {
    const Eigen::Index r = m.rows();
    const Eigen::Index c = m.cols();
    Eigen::MatrixXd out(c, r);
    for (Eigen::Index a = 0; a < c; ++a) {
        for (Eigen::Index b = 0; b < r; ++b) {
            out(a, b) = m(b, c - 1 - a);
        }
    }
    return out;
}

}  // namespace

Eigen::MatrixXd getSliceImage(const Volume3D& img, int sliceIndex, const std::string& plane) {
    const Eigen::Index s = sliceIndex - 1;  // MATLAB 1-based -> 0-based

    Eigen::MatrixXd plane2d;
    if (plane == "sagital") {  // squeeze(img(s, :, :)) -> ny x nz
        plane2d.resize(img.ny, img.nz);
        for (Eigen::Index j = 0; j < img.ny; ++j) {
            for (Eigen::Index k = 0; k < img.nz; ++k) {
                plane2d(j, k) = img(s, j, k);
            }
        }
    } else if (plane == "coronal") {  // squeeze(img(:, s, :)) -> nx x nz
        plane2d.resize(img.nx, img.nz);
        for (Eigen::Index i = 0; i < img.nx; ++i) {
            for (Eigen::Index k = 0; k < img.nz; ++k) {
                plane2d(i, k) = img(i, s, k);
            }
        }
    } else if (plane == "axial") {  // squeeze(img(:, :, s)) -> nx x ny
        plane2d = img.kSlices[static_cast<size_t>(s)];
    } else {
        throw std::invalid_argument("getSliceImage: undefined slice plane '" + plane + "'");
    }
    return rot90(plane2d);
}

DisplayWindow computeDisplayWindow(const Volume3D& img) {
    DisplayWindow w;
    for (const Eigen::MatrixXd& k : img.kSlices) {
        if (k.size() == 0) continue;
        w.hi = std::max(w.hi, k.maxCoeff());
    }
    return w;
}

}  // namespace beam::mri
