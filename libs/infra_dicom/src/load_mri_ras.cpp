#include "infra_dicom/load_mri_ras.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "infra_dicom/dicom_series.hpp"
#include "mri/mri_loader.hpp"

// Copied from Diadem's libs/infra_dicom/src/load_mri_ras.cpp.

namespace beam::infra::dicom {

namespace {

// MATLAB: dir(fullfile(mrPath,'*.nii*')) -- any filename containing
// ".nii" (so this also matches .nii.gz).
bool looksLikeNifti(const std::filesystem::path& p) {
    return p.filename().string().find(".nii") != std::string::npos;
}

// A real patient folder (e.g. this port's own BeamExampleDataDICOMandMatFiles)
// mixes actual DICOM slice files (often extensionless, PACS-style) several
// directories deep with things that are never DICOM: a companion .mat
// export, a DICOMDIR index file, macOS's .DS_Store, an already-converted
// .nii*. readDicomSlice has no per-file recovery -- it throws on the
// first file that fails to parse -- so these need filtering out before
// assembleDicomSeriesRas ever sees them, not caught after the fact.
bool looksLikeNonDicomCompanion(const std::filesystem::path& p) {
    const std::string name = p.filename().string();
    if (name == "DICOMDIR" || name == ".DS_Store") return true;
    const std::string ext = p.extension().string();
    return ext == ".mat" || ext == ".zip" || ext == ".txt" || looksLikeNifti(p);
}

// Recursive: a real acquisition's DICOM files typically sit several
// directories below whatever folder the operator actually picks (the
// study/patient folder, not the exact series leaf) -- user: "any depth
// of folder should work. program should go in the search for it."
std::vector<std::string> findFilesRecursive(const std::string& root,
                                             bool (*keep)(const std::filesystem::path&)) {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && keep(entry.path())) found.push_back(entry.path().string());
    }
    return found;
}

}  // namespace

beam::mri::MriVolumeRas loadMriRas(const std::string& path) {
    namespace fs = std::filesystem;
    const bool isDir = fs::is_directory(path);

    std::vector<std::string> dicomCandidates;
    if (isDir) {
        dicomCandidates =
            findFilesRecursive(path, [](const fs::path& p) { return !looksLikeNonDicomCompanion(p); });
    } else {
        dicomCandidates.push_back(path);
    }

    if (!dicomCandidates.empty()) {
        try {
            return assembleDicomSeriesRas(dicomCandidates);
        } catch (const std::runtime_error&) {
            // Fall through to the NIfTI path, matching loadMRIRAS.m's try/catch.
        }
    }

    std::string niftiPath = path;
    if (isDir) {
        std::vector<std::string> niiFiles = findFilesRecursive(path, looksLikeNifti);
        if (niiFiles.empty()) {
            throw std::runtime_error("loadMriRas: '" + path +
                                      "' is not a readable DICOM series and contains no .nii* "
                                      "file to fall back to");
        }
        std::sort(niiFiles.begin(), niiFiles.end());
        niftiPath = niiFiles.front();
    }

    return beam::mri::loadNiftiMriRas(niftiPath);
}

}  // namespace beam::infra::dicom
