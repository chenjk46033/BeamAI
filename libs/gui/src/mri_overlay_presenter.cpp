#include "gui/mri_overlay_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "array/geometry.hpp"

namespace beam::gui {

namespace {

Eigen::Index nearestIndex(const Eigen::VectorXd& axis, double value) {
    Eigen::Index best = 0;
    double bestDist = std::numeric_limits<double>::infinity();
    for (Eigen::Index n = 0; n < axis.size(); ++n) {
        const double dist = std::abs(value - axis(n));
        if (dist < bestDist) {
            bestDist = dist;
            best = n;
        }
    }
    return best;
}

bool inBounds(Eigen::Index idx, Eigen::Index extent) { return idx >= 0 && idx < extent; }

// setFiducialTemplate.m: R = outerD/2 + height/2, outerD=15, height=50.
// `innerD` (10) is declared in the source but never read -- dropped.
constexpr double kEllipsoidR = 15.0 / 2.0 + 50.0 / 2.0;  // 32.5

// The ellipsoid indicator `(x/30)^2+(y/5)^2+(z/5)^2 < 1`, evaluated at a
// point already on the dec=4 fine lattice (fiducialRotation == 1, i.e. no
// rotation, matching drawFocusOnMRI.m's call).
double ellipsoidIndicator(double x, double y, double z) {
    const double r = (x / 30.0) * (x / 30.0) + (y / 5.0) * (y / 5.0) + (z / 5.0) * (z / 5.0);
    return (r < 1.0) ? 1.0 : 0.0;
}

// Number of voxel-resolution samples spanning [-R, R] with step `res`
// (MATLAB's `length(-R:res:R)`).
Eigen::Index stampExtent(double res) {
    if (!(res > 0.0)) return 0;
    return static_cast<Eigen::Index>(std::floor(2.0 * kEllipsoidR / res + 1e-9)) + 1;
}

// Trilinear interpolation of ellipsoidIndicator from the dec=4 fine
// lattice (spacing res/4) onto the continuous point (x, y, z) -- MATLAB's
// interp3 downsampling step. Since the indicator is evaluated
// analytically rather than read from a materialized array, there is no
// real "out of range" case to guard: a query exactly at the +/-R boundary
// just evaluates one ghost lattice point past the nominal edge with zero
// interpolation weight.
double interpolatedEllipsoidCoverage(double x, double y, double z, double resX, double resY, double resZ) {
    const double stepX = resX / 4.0, stepY = resY / 4.0, stepZ = resZ / 4.0;

    const double fx = (x + kEllipsoidR) / stepX;
    const double fy = (y + kEllipsoidR) / stepY;
    const double fz = (z + kEllipsoidR) / stepZ;
    const Eigen::Index ix0 = static_cast<Eigen::Index>(std::floor(fx));
    const Eigen::Index iy0 = static_cast<Eigen::Index>(std::floor(fy));
    const Eigen::Index iz0 = static_cast<Eigen::Index>(std::floor(fz));
    const double tx = fx - static_cast<double>(ix0);
    const double ty = fy - static_cast<double>(iy0);
    const double tz = fz - static_cast<double>(iz0);

    double acc = 0.0;
    for (int dx = 0; dx <= 1; ++dx) {
        const double wx = dx ? tx : (1.0 - tx);
        const double px = -kEllipsoidR + static_cast<double>(ix0 + dx) * stepX;
        for (int dy = 0; dy <= 1; ++dy) {
            const double wy = dy ? ty : (1.0 - ty);
            const double py = -kEllipsoidR + static_cast<double>(iy0 + dy) * stepY;
            for (int dz = 0; dz <= 1; ++dz) {
                const double wz = dz ? tz : (1.0 - tz);
                const double pz = -kEllipsoidR + static_cast<double>(iz0 + dz) * stepZ;
                acc += ellipsoidIndicator(px, py, pz) * wx * wy * wz;
            }
        }
    }
    return acc;
}

beam::mri::Volume3D makeZeroMask(Eigen::Index nx, Eigen::Index ny, Eigen::Index nz) {
    beam::mri::Volume3D mask;
    mask.nx = nx;
    mask.ny = ny;
    mask.nz = nz;
    mask.kSlices.assign(static_cast<size_t>(std::max<Eigen::Index>(nz, 0)), Eigen::MatrixXd::Zero(nx, ny));
    return mask;
}

}  // namespace

VoxelIndex imagePositionToVoxelIndex(const Eigen::Vector3d& positionMm, const beam::mri::RasAxisVectors& axes) {
    VoxelIndex idx;
    idx.i = nearestIndex(axes.dimLR, positionMm.x());
    idx.j = nearestIndex(axes.dimAP, positionMm.y());
    idx.k = nearestIndex(axes.dimIS, positionMm.z());
    return idx;
}

beam::mri::Volume3D rasterizeArrayOntoMriGrid(const beam::array::ArrayStruct& arrayTotal,
                                               const beam::mri::RasAxisVectors& axes, Eigen::Index nx,
                                               Eigen::Index ny, Eigen::Index nz) {
    beam::mri::Volume3D mask = makeZeroMask(nx, ny, nz);
    if (axes.dimLR.size() < 2 || axes.dimAP.size() < 2 || axes.dimIS.size() < 2) {
        return mask;
    }
    const beam::mri::VoxelResolution res = beam::mri::computeVoxelResolution(axes);
    const double fs = std::min({res.lr, res.ap, res.is}) / 2.0;
    if (!(fs > 0.0)) {
        return mask;
    }

    for (const beam::array::ArrayElement& element : arrayTotal.element) {
        const Eigen::Matrix<double, 3, 4> cornersMm = element.corners * 1000.0;  // m -> mm
        const Eigen::MatrixXd points = beam::array::spatiallySampleElement(cornersMm, fs);
        for (Eigen::Index row = 0; row < points.rows(); ++row) {
            const VoxelIndex idx = imagePositionToVoxelIndex(points.row(row).transpose(), axes);
            if (inBounds(idx.i, nx) && inBounds(idx.j, ny) && inBounds(idx.k, nz)) {
                mask.kSlices[static_cast<size_t>(idx.k)](idx.i, idx.j) = 1.0;
            }
        }
    }
    return mask;
}

beam::mri::Volume3D rasterizeFiducialMarkersOntoMriGrid(const std::vector<Eigen::Vector3d>& positionsMm,
                                                          const beam::mri::RasAxisVectors& axes, Eigen::Index nx,
                                                          Eigen::Index ny, Eigen::Index nz,
                                                          Eigen::Index radiusVoxels) {
    beam::mri::Volume3D mask = makeZeroMask(nx, ny, nz);
    for (const Eigen::Vector3d& position : positionsMm) {
        const VoxelIndex center = imagePositionToVoxelIndex(position, axes);
        for (Eigen::Index di = -radiusVoxels; di <= radiusVoxels; ++di) {
            for (Eigen::Index dj = -radiusVoxels; dj <= radiusVoxels; ++dj) {
                for (Eigen::Index dk = -radiusVoxels; dk <= radiusVoxels; ++dk) {
                    const Eigen::Index i = center.i + di;
                    const Eigen::Index j = center.j + dj;
                    const Eigen::Index k = center.k + dk;
                    if (inBounds(i, nx) && inBounds(j, ny) && inBounds(k, nz)) {
                        mask.kSlices[static_cast<size_t>(k)](i, j) = 1.0;
                    }
                }
            }
        }
    }
    return mask;
}

beam::mri::Volume3D rasterizeFocusEllipsoidOntoMriGrid(const Eigen::Vector3d& focusPositionMm,
                                                        const beam::mri::RasAxisVectors& axes, Eigen::Index nx,
                                                        Eigen::Index ny, Eigen::Index nz) {
    beam::mri::Volume3D mask = makeZeroMask(nx, ny, nz);
    if (axes.dimLR.size() < 2 || axes.dimAP.size() < 2 || axes.dimIS.size() < 2) {
        return mask;
    }
    const double resX = std::abs(axes.dimLR(1) - axes.dimLR(0));
    const double resY = std::abs(axes.dimAP(1) - axes.dimAP(0));
    const double resZ = std::abs(axes.dimIS(1) - axes.dimIS(0));
    if (!(resX > 0.0) || !(resY > 0.0) || !(resZ > 0.0)) {
        return mask;
    }

    const Eigen::Index sx = stampExtent(resX);
    const Eigen::Index sy = stampExtent(resY);
    const Eigen::Index sz = stampExtent(resZ);
    if (sx <= 0 || sy <= 0 || sz <= 0) {
        return mask;
    }

    const VoxelIndex center = imagePositionToVoxelIndex(focusPositionMm, axes);
    const Eigen::Index startI = center.i - sx / 2;
    const Eigen::Index startJ = center.j - sy / 2;
    const Eigen::Index startK = center.k - sz / 2;

    for (Eigen::Index dk = 0; dk < sz; ++dk) {
        const Eigen::Index gk = startK + dk;
        if (!inBounds(gk, nz)) continue;
        const double z = -kEllipsoidR + static_cast<double>(dk) * resZ;
        for (Eigen::Index dj = 0; dj < sy; ++dj) {
            const Eigen::Index gj = startJ + dj;
            if (!inBounds(gj, ny)) continue;
            const double y = -kEllipsoidR + static_cast<double>(dj) * resY;
            for (Eigen::Index di = 0; di < sx; ++di) {
                const Eigen::Index gi = startI + di;
                if (!inBounds(gi, nx)) continue;
                const double x = -kEllipsoidR + static_cast<double>(di) * resX;
                mask.kSlices[static_cast<size_t>(gk)](gi, gj) =
                    interpolatedEllipsoidCoverage(x, y, z, resX, resY, resZ);
            }
        }
    }
    return mask;
}

}  // namespace beam::gui
