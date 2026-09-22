#include "mri/ras_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <Eigen/LU>

// Copied from Diadem's libs/imaging/src/ras_transform.cpp (diadem::imaging).

namespace beam::mri {

// The affine transform mapping voxel indices to real-world RAS
// (right/anterior/superior) positions, from a NIfTI header's qform/sform.
RasXform getRasXformFromHeader(const NiftiHeader& header) {
    RasXform xform;

    if (header.qformCode > 0) {
        const double b = header.quaternB;
        const double c = header.quaternC;
        const double d = header.quaternD;
        // MATLAB: a = sqrt(1-b^2-c^2-d^2), then real(xform_RAS) at the end.
        // sqrt() of a negative real in MATLAB yields an imaginary result
        // whose real() part is 0, so clamping to 0 here before sqrt is the
        // equivalent recovery for near-unit quaternions with float noise.
        const double a = std::sqrt(std::max(0.0, 1.0 - b * b - c * c - d * d));

        Eigen::Matrix3d rot;
        rot << a * a + b * b - c * c - d * d, 2 * (b * c - a * d), 2 * (b * d + a * c),
               2 * (b * c + a * d), a * a + c * c - b * b - d * d, 2 * (c * d - a * b),
               2 * (b * d - a * c), 2 * (c * d + a * b), a * a + d * d - b * b - c * c;

        const double q = header.pixdim[0];
        const Eigen::Vector3d scale(header.pixdim[1], header.pixdim[2], q * header.pixdim[3]);
        rot = rot * scale.asDiagonal();

        xform.block<3, 3>(0, 0) = rot;
        xform.col(3) << header.qoffsetX, header.qoffsetY, header.qoffsetZ;
    } else if (header.sformCode > 0) {
        xform.row(0) = header.srowX;
        xform.row(1) = header.srowY;
        xform.row(2) = header.srowZ;
    } else {
        throw std::runtime_error("getRasXformFromHeader: Analyze format not supported");
    }

    return xform;
}

// The real-world position of every voxel index along each of the three
// axes, for labeling/plotting.
// getRASAxisVectorsFromNifti.m's own assumption -- voxel axis i varies
// physical LR, j varies AP, k varies IS -- only holds for a header whose
// affine is (close to) diagonal, e.g. a "canonical"-oriented NIfTI. A
// 90-degree-rotated acquisition (a sagittal-plane DICOM series, where
// physical LR instead varies with the *slice* index k, not i) breaks
// that: varying i while holding j=k=0 fixed lands on a row of `xform`
// that's actually all-zero for LR, producing a degenerate (constant,
// zero-resolution) dimLR -- exactly what made every axis tick and
// slider value disappear for real sagittal DICOM data (this port's own
// bug, not a faithful reproduction of the source's -- the disclosed
// "not reindexed to volume's reordered axes" mismatch in mri_loader.hpp
// is about a different, already-accepted quirk, not this one). Using
// getVoxelRasXform's own dominant-axis permutation to find which voxel
// index actually drives each physical axis fixes it for any
// axis-aligned orientation, not just the canonical one.
RasAxisVectors getRasAxisVectors(const NiftiHeader& header) {
    const RasXform xform = getRasXformFromHeader(header);
    const std::array<int, 3> voxelSize = {header.dim[1], header.dim[2], header.dim[3]};
    const Eigen::Matrix3d voxelRasXform = getVoxelRasXform(header);

    // voxelRasXform(rasAxis, voxelAxis) is nonzero exactly where voxel
    // index `voxelAxis` drives physical axis `rasAxis` -- find that
    // column for each of LR(0)/AP(1)/IS(2).
    std::array<int, 3> voxelAxisForRas = {-1, -1, -1};
    for (int rasAxis = 0; rasAxis < 3; ++rasAxis) {
        for (int voxelAxis = 0; voxelAxis < 3; ++voxelAxis) {
            if (voxelRasXform(rasAxis, voxelAxis) != 0.0) {
                voxelAxisForRas[static_cast<std::size_t>(rasAxis)] = voxelAxis;
                break;
            }
        }
    }

    const auto buildAxis = [&](int rasAxis) {
        const int voxelAxis = voxelAxisForRas[static_cast<std::size_t>(rasAxis)];
        const int n = voxelSize[static_cast<std::size_t>(voxelAxis)];
        Eigen::VectorXd v(n);
        for (int t = 0; t < n; ++t) {
            Eigen::Vector4d voxelIndex = Eigen::Vector4d(0, 0, 0, 1);
            voxelIndex(voxelAxis) = t;
            v(t) = (xform * voxelIndex)(rasAxis);
        }
        return v;
    };

    RasAxisVectors axes;
    axes.dimLR = buildAxis(0);
    axes.dimAP = buildAxis(1);
    axes.dimIS = buildAxis(2);
    return axes;
}

// Reduces the header's affine to just which way each voxel axis points
// (a flip/permute matrix), for reorienting a volume without resampling it.
// Ported from GUI/updateSysWithMRI.m: `res(i) = abs(diff(dim(1:2)))` for
// each axis. Defensively returns 0 for an axis with fewer than 2 points
// (the source would error on `diff` there) -- matching the same guard
// beam::gui::rasterizeArrayOntoMriGrid already applies to this same math.
VoxelResolution computeVoxelResolution(const RasAxisVectors& axes) {
    VoxelResolution res;
    if (axes.dimLR.size() >= 2) res.lr = std::abs(axes.dimLR(1) - axes.dimLR(0));
    if (axes.dimAP.size() >= 2) res.ap = std::abs(axes.dimAP(1) - axes.dimAP(0));
    if (axes.dimIS.size() >= 2) res.is = std::abs(axes.dimIS(1) - axes.dimIS(0));
    return res;
}

Eigen::Matrix3d getVoxelRasXform(const NiftiHeader& header) {
    const RasXform xformRas = getRasXformFromHeader(header);

    Eigen::Matrix4d xformScale = Eigen::Matrix4d::Identity();
    xformScale(0, 0) = header.pixdim[1];
    xformScale(1, 1) = header.pixdim[2];
    xformScale(2, 2) = header.pixdim[3];

    Eigen::Matrix4d xform4 = Eigen::Matrix4d::Identity();
    xform4.block<3, 4>(0, 0) = xformRas;  // vertcat(xform_RAS, [0 0 0 1])

    const Eigen::Matrix4d voxelXform4 = xform4 * xformScale.inverse();
    Eigen::Matrix3d voxelXform = voxelXform4.block<3, 3>(0, 0);

    // Zero all but the 3 largest-|value| entries (MATLAB: idx_sort(1:end-3)
    // over the flattened, ascending-sorted 3x3 -> zero the smallest 6).
    std::array<std::pair<double, int>, 9> entries;
    for (int idx = 0; idx < 9; ++idx) {
        const int row = idx % 3;
        const int col = idx / 3;
        entries[idx] = {std::abs(voxelXform(row, col)), idx};
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (int k = 0; k < 6; ++k) {
        const int idx = entries[k].second;
        voxelXform(idx % 3, idx / 3) = 0.0;
    }

    Eigen::Matrix3d signMatrix;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const double v = voxelXform(r, c);
            signMatrix(r, c) = (v > 0.0) - (v < 0.0);
        }
    }

    if (std::abs(signMatrix.determinant()) < 1e-9) {
        throw std::runtime_error("getVoxelRasXform: RAS voxel orientation matrix is singular");
    }
    return signMatrix;
}

}  // namespace beam::mri
