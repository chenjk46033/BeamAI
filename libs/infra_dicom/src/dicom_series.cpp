#include "infra_dicom/dicom_series.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>

#include <Eigen/Geometry>

#include "infra_dicom/dicom_slice.hpp"
#include "mri/dicom_series_geometry.hpp"

// Copied from Diadem's libs/infra_dicom/src/dicom_series.cpp.

namespace beam::infra::dicom {

using beam::mri::MriVolumeRas;
using beam::mri::NiftiHeader;

namespace {
constexpr double kOrientationTolerance = 1e-3;
constexpr double kSpacingRelativeTolerance = 1e-2;
}  // namespace

MriVolumeRas assembleDicomSeriesRas(const std::vector<std::string>& filePaths) {
    if (filePaths.empty()) {
        throw std::runtime_error("assembleDicomSeriesRas: no files given");
    }

    std::vector<DicomSlice> slices;
    slices.reserve(filePaths.size());
    for (const auto& path : filePaths) {
        slices.push_back(readDicomSlice(path));
    }

    const DicomSlice& first = slices.front();
    if (!first.tags.rowDir.has_value() || !first.tags.colDir.has_value()) {
        throw std::runtime_error("assembleDicomSeriesRas: first slice has no ImageOrientationPatient");
    }
    const Eigen::Vector3d rowDir = *first.tags.rowDir;
    const Eigen::Vector3d colDir = *first.tags.colDir;
    const Eigen::Vector3d sliceDir = rowDir.cross(colDir);

    for (const auto& s : slices) {
        if (s.tags.rows != first.tags.rows || s.tags.columns != first.tags.columns) {
            throw std::runtime_error("assembleDicomSeriesRas: inconsistent rows/columns across slices");
        }
        if (!s.tags.rowDir.has_value() || !s.tags.colDir.has_value() ||
            !s.tags.rowDir->isApprox(rowDir, kOrientationTolerance) ||
            !s.tags.colDir->isApprox(colDir, kOrientationTolerance)) {
            throw std::runtime_error(
                "assembleDicomSeriesRas: inconsistent (or missing) ImageOrientationPatient across "
                "slices -- multi-orientation series are out of scope");
        }
    }

    std::vector<std::size_t> order(slices.size());
    std::iota(order.begin(), order.end(), 0);
    std::vector<double> projected(slices.size());
    for (std::size_t i = 0; i < slices.size(); ++i) {
        projected[i] = slices[i].tags.imagePositionPatient.dot(sliceDir);
    }
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return projected[a] < projected[b]; });

    double sliceSpacing = 0.0;
    if (slices.size() > 1) {
        std::vector<double> deltas(slices.size() - 1);
        for (std::size_t i = 0; i + 1 < order.size(); ++i) {
            deltas[i] = projected[order[i + 1]] - projected[order[i]];
        }
        sliceSpacing = std::accumulate(deltas.begin(), deltas.end(), 0.0) /
                       static_cast<double>(deltas.size());
        if (sliceSpacing <= 0.0) {
            throw std::runtime_error(
                "assembleDicomSeriesRas: could not determine a positive slice spacing "
                "(duplicate or non-increasing slice positions)");
        }
        for (double d : deltas) {
            if (std::abs(d - sliceSpacing) > kSpacingRelativeTolerance * sliceSpacing) {
                throw std::runtime_error(
                    "assembleDicomSeriesRas: non-uniform slice spacing -- interleaved or "
                    "gantry-tilted acquisitions are out of scope");
            }
        }
    } else if (first.tags.spacingBetweenSlices.has_value()) {
        sliceSpacing = *first.tags.spacingBetweenSlices;
    } else if (first.tags.sliceThickness.has_value()) {
        sliceSpacing = *first.tags.sliceThickness;
    } else {
        throw std::runtime_error(
            "assembleDicomSeriesRas: single-slice series needs SpacingBetweenSlices or "
            "SliceThickness to establish a slice spacing");
    }

    const int numRows = first.tags.rows;
    const int numCols = first.tags.columns;
    const int numSlices = static_cast<int>(slices.size());

    const NiftiHeader header = beam::mri::buildNiftiHeaderFromDicomSeries(
        rowDir, colDir, slices[order.front()].tags.imagePositionPatient,
        first.tags.pixelSpacing(1),  // spacing along rowDir (DICOM "column spacing")
        first.tags.pixelSpacing(0),  // spacing along colDir (DICOM "row spacing")
        sliceSpacing, numCols, numRows, numSlices);

    std::vector<double> voxels(static_cast<std::size_t>(numCols) * static_cast<std::size_t>(numRows) *
                                static_cast<std::size_t>(numSlices));
    for (int k = 0; k < numSlices; ++k) {
        const std::vector<double>& px = slices[order[static_cast<std::size_t>(k)]].pixels;
        for (int j = 0; j < numRows; ++j) {
            for (int i = 0; i < numCols; ++i) {
                const std::size_t srcIdx = static_cast<std::size_t>(j) * static_cast<std::size_t>(numCols) +
                                            static_cast<std::size_t>(i);
                const std::size_t dstIdx =
                    static_cast<std::size_t>(i) + static_cast<std::size_t>(j) * static_cast<std::size_t>(numCols) +
                    static_cast<std::size_t>(k) * static_cast<std::size_t>(numCols) * static_cast<std::size_t>(numRows);
                voxels[dstIdx] = px[srcIdx];
            }
        }
    }

    MriVolumeRas result;
    result.header = header;
    result.axes = beam::mri::getRasAxisVectors(header);
    const Eigen::Matrix3d voxelXform = beam::mri::getVoxelRasXform(header);
    result.volume = beam::mri::applyVoxelRasXform3D(voxels, {numCols, numRows, numSlices}, voxelXform);
    return result;
}

}  // namespace beam::infra::dicom
