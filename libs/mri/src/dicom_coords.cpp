#include "mri/dicom_coords.hpp"

#include <stdexcept>

#include <Eigen/Geometry>

// Copied from Diadem's libs/imaging/src/dicom_coords.cpp (diadem::imaging).

namespace beam::mri {

PixelSpatialReference getDicomPixelSpatialReferenceMath(
    int numRows, int numCols, int numFiles, const Eigen::Vector2d& pixelSpacing,
    const Eigen::Vector3d& imagePositionPatient, std::optional<double> sliceThickness) {
    PixelSpatialReference result;

    result.dimAP.resize(numCols);
    for (int i = 0; i < numCols; ++i) {
        result.dimAP(i) = pixelSpacing(0) * i + imagePositionPatient(0);
    }

    result.dimIS.resize(numRows);
    for (int i = 0; i < numRows; ++i) {
        result.dimIS(i) = pixelSpacing(1) * i + imagePositionPatient(1);
    }

    if (!sliceThickness.has_value()) {
        throw std::runtime_error(
            "getDicomPixelSpatialReferenceMath: sliceThickness unavailable -- "
            "the MATLAB source would itself error here (its fallback branch "
            "assigns `dimS`, not the declared output `dimLR`)");
    }
    result.dimLR.resize(numFiles);
    for (int i = 0; i < numFiles; ++i) {
        result.dimLR(i) = *sliceThickness * i + imagePositionPatient(2);
    }

    return result;
}

ResolvedDicomOrientation resolveDicomOrientationDefaults(const DicomOrientationInfo& info) {
    ResolvedDicomOrientation r;

    if (info.rowDir.has_value() && info.colDir.has_value()) {
        r.rowDir = *info.rowDir;
        r.colDir = *info.colDir;
    } else {
        r.rowDir = Eigen::Vector3d(1, 0, 0);
        r.colDir = Eigen::Vector3d(0, 1, 0);
    }
    r.sliceDir = r.rowDir.cross(r.colDir);

    r.pixelSpacing = info.pixelSpacing.value_or(Eigen::Vector2d(1, 1));

    if (info.spacingBetweenSlices.has_value()) {
        r.sliceSpacing = *info.spacingBetweenSlices;
    } else if (info.sliceThickness.has_value()) {
        r.sliceSpacing = *info.sliceThickness;
    } else {
        r.sliceSpacing = 1.0;
    }

    r.origin = info.imagePositionPatient.value_or(Eigen::Vector3d(0, 0, 0));
    return r;
}

VolumeCoords getDicomVolumeCoords(int nRows, int nCols, int nSlices,
                                   const ResolvedDicomOrientation& o) {
    VolumeCoords c;

    c.dimLR.resize(nCols);
    for (int i = 0; i < nCols; ++i) c.dimLR(i) = o.origin(0) + i * o.pixelSpacing(1) * o.rowDir(0);

    c.dimAP.resize(nRows);
    for (int i = 0; i < nRows; ++i) c.dimAP(i) = o.origin(1) + i * o.pixelSpacing(0) * o.colDir(1);

    c.dimIS.resize(nSlices);
    for (int i = 0; i < nSlices; ++i) c.dimIS(i) = o.origin(2) + i * o.sliceSpacing * o.sliceDir(2);

    return c;
}

}  // namespace beam::mri
