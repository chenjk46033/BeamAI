#include "mri/affine_volume.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <Eigen/Dense>

namespace beam::mri {

namespace {

double meanDiff(const Eigen::VectorXd& v) {
    if (v.size() < 2) {
        return 1.0;
    }
    return (v(v.size() - 1) - v(0)) / static_cast<double>(v.size() - 1);
}

// Trilinear sample of `v` at fractional index (fi, fj, fk); 0 outside the
// voxel-centre grid (imwarp FillValues 0).
double sampleTrilinear(const Volume3D& v, double fi, double fj, double fk) {
    if (fi < 0.0 || fi > static_cast<double>(v.nx - 1) || fj < 0.0 || fj > static_cast<double>(v.ny - 1) ||
        fk < 0.0 || fk > static_cast<double>(v.nz - 1)) {
        return 0.0;
    }
    const auto lerpAxis = [](double f, Eigen::Index n, Eigen::Index& i0, Eigen::Index& i1, double& w) {
        i0 = static_cast<Eigen::Index>(std::floor(f));
        w = f - static_cast<double>(i0);
        i1 = (i0 + 1 <= n - 1) ? i0 + 1 : i0;
    };
    Eigen::Index i0, i1, j0, j1, k0, k1;
    double wi, wj, wk;
    lerpAxis(fi, v.nx, i0, i1, wi);
    lerpAxis(fj, v.ny, j0, j1, wj);
    lerpAxis(fk, v.nz, k0, k1, wk);

    const auto at = [&v](Eigen::Index i, Eigen::Index j, Eigen::Index k) { return v(i, j, k); };
    const double c00 = at(i0, j0, k0) * (1 - wi) + at(i1, j0, k0) * wi;
    const double c01 = at(i0, j0, k1) * (1 - wi) + at(i1, j0, k1) * wi;
    const double c10 = at(i0, j1, k0) * (1 - wi) + at(i1, j1, k0) * wi;
    const double c11 = at(i0, j1, k1) * (1 - wi) + at(i1, j1, k1) * wi;
    const double c0 = c00 * (1 - wj) + c10 * wj;
    const double c1 = c01 * (1 - wj) + c11 * wj;
    return c0 * (1 - wk) + c1 * wk;
}

}  // namespace

AffineVolumeResult applyAffine3D(const Volume3D& v, const Eigen::VectorXd& xMm, const Eigen::VectorXd& yMm,
                                  const Eigen::VectorXd& zMm, const Eigen::Matrix4d& mWorld) {
    const double dx = meanDiff(xMm);
    const double dy = meanDiff(yMm);
    const double dz = meanDiff(zMm);

    // Input world limits (half a voxel past the outer centres).
    const double xLo = xMm(0) - dx / 2.0, xHi = xMm(xMm.size() - 1) + dx / 2.0;
    const double yLo = yMm(0) - dy / 2.0, yHi = yMm(yMm.size() - 1) + dy / 2.0;
    const double zLo = zMm(0) - dz / 2.0, zHi = zMm(zMm.size() - 1) + dz / 2.0;

    // Transform the 8 corners to find the output bounding box.
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300, zmin = 1e300, zmax = -1e300;
    for (double cx : {xLo, xHi}) {
        for (double cy : {yLo, yHi}) {
            for (double cz : {zLo, zHi}) {
                const Eigen::Vector4d w = mWorld * Eigen::Vector4d(cx, cy, cz, 1.0);
                xmin = std::min(xmin, w(0)); xmax = std::max(xmax, w(0));
                ymin = std::min(ymin, w(1)); ymax = std::max(ymax, w(1));
                zmin = std::min(zmin, w(2)); zmax = std::max(zmax, w(2));
            }
        }
    }
    xmin -= dx / 2.0; xmax += dx / 2.0;
    ymin -= dy / 2.0; ymax += dy / 2.0;
    zmin -= dz / 2.0; zmax += dz / 2.0;

    const auto nCells = [](double lo, double hi, double step) {
        return std::max<Eigen::Index>(1, static_cast<Eigen::Index>(std::ceil((hi - lo) / step)));
    };
    const Eigen::Index nx = nCells(xmin, xmax, dx);
    const Eigen::Index ny = nCells(ymin, ymax, dy);
    const Eigen::Index nz = nCells(zmin, zmax, dz);

    const Eigen::Matrix4d inv = mWorld.inverse();

    Volume3D out;
    out.nx = nx;
    out.ny = ny;
    out.nz = nz;
    out.kSlices.assign(static_cast<std::size_t>(nz), Eigen::MatrixXd::Zero(nx, ny));

    for (Eigen::Index k = 0; k < nz; ++k) {
        const double wz = zmin + (static_cast<double>(k) + 0.5) * dz;
        for (Eigen::Index j = 0; j < ny; ++j) {
            const double wy = ymin + (static_cast<double>(j) + 0.5) * dy;
            for (Eigen::Index i = 0; i < nx; ++i) {
                const double wx = xmin + (static_cast<double>(i) + 0.5) * dx;
                const Eigen::Vector4d in = inv * Eigen::Vector4d(wx, wy, wz, 1.0);
                const double fi = (in(0) - xMm(0)) / dx;
                const double fj = (in(1) - yMm(0)) / dy;
                const double fk = (in(2) - zMm(0)) / dz;
                out.kSlices[static_cast<std::size_t>(k)](i, j) = sampleTrilinear(v, fi, fj, fk);
            }
        }
    }

    AffineVolumeResult result;
    result.volume = std::move(out);
    result.xMm.resize(nx);
    result.yMm.resize(ny);
    result.zMm.resize(nz);
    for (Eigen::Index i = 0; i < nx; ++i) result.xMm(i) = xmin + dx / 2.0 + static_cast<double>(i) * dx;
    for (Eigen::Index j = 0; j < ny; ++j) result.yMm(j) = ymin + dy / 2.0 + static_cast<double>(j) * dy;
    for (Eigen::Index k = 0; k < nz; ++k) result.zMm(k) = zmin + dz / 2.0 + static_cast<double>(k) * dz;
    return result;
}

}  // namespace beam::mri
