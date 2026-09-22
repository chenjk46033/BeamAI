#include "mri/dicom_series_geometry.hpp"

#include <Eigen/Geometry>

// Copied from Diadem's libs/imaging/src/dicom_series_geometry.cpp.

namespace beam::mri {

NiftiHeader buildNiftiHeaderFromDicomSeries(const Eigen::Vector3d& rowDir,
                                             const Eigen::Vector3d& colDir,
                                             const Eigen::Vector3d& firstSliceIpp,
                                             double spacingAlongRowDir, double spacingAlongColDir,
                                             double sliceSpacing, int numCols, int numRows,
                                             int numSlices) {
    const Eigen::Vector3d sliceDir = rowDir.cross(colDir);

    NiftiHeader header;
    header.dim = {3, numCols, numRows, numSlices, 0, 0, 0, 0};
    header.pixdim = {1.0, spacingAlongRowDir, spacingAlongColDir, sliceSpacing, 0, 0, 0, 0};
    header.sformCode = 1;

    const Eigen::Vector3d colI = rowDir * spacingAlongRowDir;
    const Eigen::Vector3d colJ = colDir * spacingAlongColDir;
    const Eigen::Vector3d colK = sliceDir * sliceSpacing;

    // LPS -> RAS: negate x (Left -> Right) and y (Posterior -> Anterior).
    header.srowX << -colI(0), -colJ(0), -colK(0), -firstSliceIpp(0);
    header.srowY << -colI(1), -colJ(1), -colK(1), -firstSliceIpp(1);
    header.srowZ << colI(2), colJ(2), colK(2), firstSliceIpp(2);

    return header;
}

}  // namespace beam::mri
