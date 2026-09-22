#include "mri/mri_loader.hpp"

#include <array>

#include "mri/nifti_file.hpp"

// Copied from Diadem's libs/imaging/src/mri_loader.cpp (diadem::imaging).

namespace beam::mri {

// Loads a NIfTI MRI file and reorients it into a standard RAS-facing
// volume with labeled physical-position axes.
MriVolumeRas loadNiftiMriRas(const std::string& path) {
    const NiftiVolume vol = readNiftiVolumeScaled(path);
    const std::array<int, 3> dims{vol.header.dim[1], vol.header.dim[2], vol.header.dim[3]};

    MriVolumeRas result;
    result.header = vol.header;
    result.axes = getRasAxisVectors(vol.header);
    const Eigen::Matrix3d voxelXform = getVoxelRasXform(vol.header);
    result.volume = applyVoxelRasXform3D(vol.voxels, dims, voxelXform);
    return result;
}

Volume3D reorientedVolumeToVolume3D(const ReorientedVolume& volume) {
    Volume3D v;
    v.nx = volume.dims[0];
    v.ny = volume.dims[1];
    v.nz = volume.dims[2];
    v.kSlices.resize(static_cast<std::size_t>(v.nz));
    for (Eigen::Index k = 0; k < v.nz; ++k) {
        Eigen::MatrixXd slice(v.nx, v.ny);
        for (Eigen::Index j = 0; j < v.ny; ++j) {
            for (Eigen::Index i = 0; i < v.nx; ++i) {
                const std::size_t idx = static_cast<std::size_t>(i) +
                                        static_cast<std::size_t>(j) * static_cast<std::size_t>(v.nx) +
                                        static_cast<std::size_t>(k) * static_cast<std::size_t>(v.nx) *
                                            static_cast<std::size_t>(v.ny);
                slice(i, j) = volume.voxels[idx];
            }
        }
        v.kSlices[static_cast<std::size_t>(k)] = std::move(slice);
    }
    return v;
}

}  // namespace beam::mri
