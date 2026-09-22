#pragma once

#include <string>
#include <vector>

#include "infra_dicom/dicom_tags.hpp"

// Copied from Diadem's libs/infra_dicom/dicom_slice.hpp. Extends
// dicom_tags.hpp's tag-only reading with actual pixel data.

namespace beam::infra::dicom {

struct DicomSlice {
    DicomTags tags;
    std::vector<double> pixels;  // row-major [row][col], rows*columns
                                  // values, RescaleSlope/Intercept applied
};

// Throws everything readDicomTags does, plus: BitsAllocated other than 16,
// or a PixelData element whose size doesn't match rows*columns.
DicomSlice readDicomSlice(const std::string& path);

}  // namespace beam::infra::dicom
