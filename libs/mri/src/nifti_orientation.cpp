#include "mri/nifti_orientation.hpp"

#include <stdexcept>
#include <utility>

// Copied from Diadem's libs/imaging/src/nifti_orientation.cpp.

namespace beam::mri {

namespace {

// Reverses the voxel order along one axis of a 3D volume.
std::vector<double> flipAxis3D(const std::vector<double>& buf, int nx, int ny, int nz, int axis) {
    std::vector<double> out(buf.size());
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                int si = i, sj = j, sk = k;
                if (axis == 0) si = nx - 1 - i;
                else if (axis == 1) sj = ny - 1 - j;
                else sk = nz - 1 - k;
                const long dstIdx = i + j * static_cast<long>(nx) + k * static_cast<long>(nx) * ny;
                const long srcIdx = si + sj * static_cast<long>(nx) + sk * static_cast<long>(nx) * ny;
                out[dstIdx] = buf[srcIdx];
            }
        }
    }
    return out;
}

}  // namespace

// Flips and reorders a volume's axes so it's stored in standard
// right/anterior/superior-facing orientation.
ReorientedVolume applyVoxelRasXform3D(const std::vector<double>& voxelsIn,
                                       const std::array<int, 3>& dimsIn,
                                       const Eigen::Matrix3d& voxelXform) {
    std::vector<double> buf = voxelsIn;
    const std::array<int, 3> dims = dimsIn;

    // Flip: for each source axis, any negative entry in its column.
    for (int axis = 0; axis < 3; ++axis) {
        bool negative = false;
        for (int row = 0; row < 3; ++row) {
            if (voxelXform(row, axis) < 0.0) negative = true;
        }
        if (negative) {
            buf = flipAxis3D(buf, dims[0], dims[1], dims[2], axis);
        }
    }

    // Permute: idx_permute(row) = the source axis whose nonzero entry is
    // in this row (0-based here; MATLAB's find(xform(i,:)) is 1-based).
    std::array<int, 3> idxPermute{};
    for (int row = 0; row < 3; ++row) {
        int found = -1;
        for (int col = 0; col < 3; ++col) {
            if (voxelXform(row, col) != 0.0) {
                found = col;
                break;
            }
        }
        if (found < 0) {
            throw std::runtime_error("applyVoxelRasXform3D: row has no nonzero entry");
        }
        idxPermute[row] = found;
    }

    const std::array<int, 3> newDims = {dims[idxPermute[0]], dims[idxPermute[1]], dims[idxPermute[2]]};
    std::vector<double> out(buf.size());
    for (int o2 = 0; o2 < newDims[2]; ++o2) {
        for (int o1 = 0; o1 < newDims[1]; ++o1) {
            for (int o0 = 0; o0 < newDims[0]; ++o0) {
                const std::array<int, 3> outPos = {o0, o1, o2};
                std::array<int, 3> inPos{};
                inPos[idxPermute[0]] = outPos[0];
                inPos[idxPermute[1]] = outPos[1];
                inPos[idxPermute[2]] = outPos[2];
                const long srcIdx = inPos[0] + inPos[1] * static_cast<long>(dims[0]) +
                                     inPos[2] * static_cast<long>(dims[0]) * dims[1];
                const long dstIdx = o0 + o1 * static_cast<long>(newDims[0]) +
                                     o2 * static_cast<long>(newDims[0]) * newDims[1];
                out[dstIdx] = buf[srcIdx];
            }
        }
    }

    ReorientedVolume result;
    result.voxels = std::move(out);
    result.dims = newDims;
    return result;
}

}  // namespace beam::mri
