#pragma once

#include <optional>
#include <string>

#include <Eigen/Core>

// Copied from Diadem's libs/infra_dicom/dicom_tags.hpp
// (diadem::infra::dicom). Reads the specific DICOM tags
// MRI/getDicomPixelSpatialReference.m and load_dicom_volume_with_coords.m
// use, via DCMTK. Scoped to uncompressed transfer syntaxes
// (Explicit/Implicit VR Little Endian) -- anything else is rejected with a
// clear error naming the transfer syntax, not silently misread.

namespace beam::infra::dicom {

struct DicomTags {
    int rows = 0;
    int columns = 0;
    Eigen::Vector2d pixelSpacing = Eigen::Vector2d::Zero();          // DICOM order: [row, col]
    Eigen::Vector3d imagePositionPatient = Eigen::Vector3d::Zero();

    std::optional<Eigen::Vector3d> rowDir;
    std::optional<Eigen::Vector3d> colDir;

    std::optional<double> sliceThickness;
    std::optional<double> spacingBetweenSlices;

    double rescaleSlope = 1.0;
    double rescaleIntercept = 0.0;

    int bitsAllocated = 16;
    bool pixelRepresentationSigned = false;

    std::string transferSyntaxUid;  // for diagnostics
};

// Throws std::runtime_error for: file won't open/parse, or a transfer
// syntax other than Explicit/Implicit VR Little Endian.
DicomTags readDicomTags(const std::string& path);

}  // namespace beam::infra::dicom
