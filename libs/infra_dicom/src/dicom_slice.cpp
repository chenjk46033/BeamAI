#include "infra_dicom/dicom_slice.hpp"

#include <stdexcept>
#include <string>

#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmdata/dctk.h>

// Copied from Diadem's libs/infra_dicom/src/dicom_slice.cpp.

namespace beam::infra::dicom {

DicomSlice readDicomSlice(const std::string& path) {
    DicomSlice slice;
    slice.tags = readDicomTags(path);

    if (slice.tags.bitsAllocated != 16) {
        throw std::runtime_error("readDicomSlice: only BitsAllocated == 16 is supported, got " +
                                  std::to_string(slice.tags.bitsAllocated) + " in " + path);
    }

    DcmFileFormat fileFormat;
    if (!fileFormat.loadFile(path.c_str()).good()) {
        throw std::runtime_error("readDicomSlice: could not open/parse " + path);
    }
    DcmDataset* ds = fileFormat.getDataset();

    const unsigned long expectedCount =
        static_cast<unsigned long>(slice.tags.rows) * static_cast<unsigned long>(slice.tags.columns);
    unsigned long count = 0;

    slice.pixels.resize(expectedCount);
    if (slice.tags.pixelRepresentationSigned) {
        const Sint16* data = nullptr;
        if (!ds->findAndGetSint16Array(DCM_PixelData, data, &count).good() || data == nullptr) {
            throw std::runtime_error("readDicomSlice: missing/unreadable PixelData in " + path);
        }
        if (count != expectedCount) {
            throw std::runtime_error("readDicomSlice: PixelData size " + std::to_string(count) +
                                      " != rows*columns " + std::to_string(expectedCount) + " in " + path);
        }
        for (unsigned long i = 0; i < count; ++i) {
            slice.pixels[i] = static_cast<double>(data[i]) * slice.tags.rescaleSlope + slice.tags.rescaleIntercept;
        }
    } else {
        const Uint16* data = nullptr;
        if (!ds->findAndGetUint16Array(DCM_PixelData, data, &count).good() || data == nullptr) {
            throw std::runtime_error("readDicomSlice: missing/unreadable PixelData in " + path);
        }
        if (count != expectedCount) {
            throw std::runtime_error("readDicomSlice: PixelData size " + std::to_string(count) +
                                      " != rows*columns " + std::to_string(expectedCount) + " in " + path);
        }
        for (unsigned long i = 0; i < count; ++i) {
            slice.pixels[i] = static_cast<double>(data[i]) * slice.tags.rescaleSlope + slice.tags.rescaleIntercept;
        }
    }

    return slice;
}

}  // namespace beam::infra::dicom
