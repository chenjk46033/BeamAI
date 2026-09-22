#pragma once

#include <optional>

#include <Eigen/Core>

// Copied from Diadem's libs/imaging/dicom_coords.hpp (diadem::imaging).
// Coordinate math from MRI/getDicomPixelSpatialReference.m and
// load_dicom_volume_with_coords.m; the dir()/dicominfo() file I/O is in
// infra_dicom, not here.

namespace beam::mri {

// --- Ported from MRI/getDicomPixelSpatialReference.m's coordinate math ---
// Preserves the source's exact index mapping verbatim, including an
// apparent inconsistency with its own header comment (comment says LR
// relates to columns and AP to rows, but the code computes dimAP from
// numCols and dimIS from numRows). Not corrected here.
struct PixelSpatialReference {
    Eigen::VectorXd dimAP;
    Eigen::VectorXd dimIS;
    Eigen::VectorXd dimLR;
};

// Throws std::runtime_error if sliceThickness has no value -- the MATLAB
// source's else-branch assigns a local `dimS`, not the declared output
// `dimLR`, so MATLAB itself would error there.
PixelSpatialReference getDicomPixelSpatialReferenceMath(
    int numRows, int numCols, int numFiles, const Eigen::Vector2d& pixelSpacing,
    const Eigen::Vector3d& imagePositionPatient, std::optional<double> sliceThickness);

// --- Ported from MRI/load_dicom_volume_with_coords.m's coordinate math ---
struct DicomOrientationInfo {
    std::optional<Eigen::Vector3d> rowDir;
    std::optional<Eigen::Vector3d> colDir;
    std::optional<Eigen::Vector2d> pixelSpacing;  // [row; col], DICOM order
    std::optional<double> spacingBetweenSlices;
    std::optional<double> sliceThickness;
    std::optional<Eigen::Vector3d> imagePositionPatient;
};

struct ResolvedDicomOrientation {
    Eigen::Vector3d rowDir, colDir, sliceDir;
    Eigen::Vector2d pixelSpacing;
    double sliceSpacing = 0.0;
    Eigen::Vector3d origin;
};

// Identity axes / 1mm spacing / zero origin when a field is absent,
// matching the source's isfield(...)/else fallbacks.
ResolvedDicomOrientation resolveDicomOrientationDefaults(const DicomOrientationInfo& info);

struct VolumeCoords {
    Eigen::VectorXd dimLR, dimAP, dimIS;
};

VolumeCoords getDicomVolumeCoords(int nRows, int nCols, int nSlices,
                                   const ResolvedDicomOrientation& orientation);

}  // namespace beam::mri
