#pragma once

#include <string>

#include "mri/mri_loader.hpp"

// Copied from Diadem's libs/infra_dicom/load_mri_ras.hpp. Ported from
// MRI/loadMRIRAS.m -- the top-level dispatcher, now that both branches
// exist: assembleDicomSeriesRas (replacing the vendored dicm2nii
// conversion) and loadNiftiMriRas for the direct-NIfTI path.

namespace beam::infra::dicom {

// path may be a directory (a DICOM series, or a folder with an
// already-converted .nii*) or a single file (.nii*, or a single DICOM
// file tried as a one-slice series).
//
// Mirrors loadMRIRAS.m's try/catch: attempts the DICOM path first,
// falling back to loading a .nii* directly if that throws. Two disclosed
// deviations from the literal source: the catch is scoped to
// std::runtime_error (not a blanket catch-all), and the .nii* fallback
// sorts filenames and takes the first alphabetically (throwing a clear
// error if none exist) rather than taking the OS's arbitrary first entry.
//
// Throws std::runtime_error if neither path succeeds.
beam::mri::MriVolumeRas loadMriRas(const std::string& path);

}  // namespace beam::infra::dicom
