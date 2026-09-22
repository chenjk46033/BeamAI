#pragma once

#include <string>

#include "mri/nifti_header.hpp"
#include "mri/nifti_orientation.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// Copied from Diadem's libs/imaging/mri_loader.hpp (diadem::imaging).
//
// Composes readNiftiVolumeScaled + getRasAxisVectors + getVoxelRasXform +
// applyVoxelRasXform3D, matching BeamV0/GUIMatlab/BEAM/MRI/loadMRIRAS.m's
// non-DICOM branch (nifti_utils.load_untouch_nii_vol_scaled_RAS(path,
// 'double') + getRASAxisVectorsFromNifti(info.hdr)). The DICOM-folder
// branch is not covered here (see docs/known_gaps_mri.md).

namespace beam::mri {

struct MriVolumeRas {
    ReorientedVolume volume;  // voxels reordered/flipped to RAS voxel order
    RasAxisVectors axes;      // physical mm coords, indexed by the file's
                               // ORIGINAL i/j/k axes -- not reindexed to
                               // volume's reordered axes (loadMRIRAS.m has
                               // the same mismatch: dimLR/AP/IS come from
                               // info.hdr directly).
    NiftiHeader header;
};

// Throws std::runtime_error for the same reasons readNiftiVolumeScaled,
// getVoxelRasXform, or getRasXformFromHeader would.
MriVolumeRas loadNiftiMriRas(const std::string& path);

// Converts a ReorientedVolume (loadNiftiMriRas's/MriVolumeRas.volume's
// shape: flat, column-major, `dims`) into the Volume3D shape
// getSliceImage.m's port expects (one nx-by-ny matrix per k-slice). Pure
// reshaping -- this project's own bridge between the two volume
// representations, not a port of any single `.m` file.
Volume3D reorientedVolumeToVolume3D(const ReorientedVolume& volume);

}  // namespace beam::mri
