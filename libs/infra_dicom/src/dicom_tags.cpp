#include "infra_dicom/dicom_tags.hpp"

#include <stdexcept>
#include <string>

#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmdata/dctk.h>

// Copied from Diadem's libs/infra_dicom/src/dicom_tags.cpp.

namespace beam::infra::dicom {

namespace {

double getFloat64OrThrow(DcmDataset* ds, const DcmTagKey& tag, unsigned long pos,
                          const char* fieldName) {
    Float64 v = 0;
    if (!ds->findAndGetFloat64(tag, v, pos).good()) {
        throw std::runtime_error(std::string("readDicomTags: missing required field ") + fieldName);
    }
    return v;
}

std::optional<double> getFloat64Optional(DcmDataset* ds, const DcmTagKey& tag) {
    Float64 v = 0;
    if (ds->findAndGetFloat64(tag, v).good()) return v;
    return std::nullopt;
}

}  // namespace

DicomTags readDicomTags(const std::string& path) {
    DcmFileFormat fileFormat;
    const OFCondition loadStatus = fileFormat.loadFile(path.c_str());
    if (!loadStatus.good()) {
        throw std::runtime_error("readDicomTags: could not open/parse " + path + ": " +
                                  loadStatus.text());
    }

    DcmDataset* ds = fileFormat.getDataset();

    const E_TransferSyntax xfer = ds->getOriginalXfer();
    if (xfer != EXS_LittleEndianExplicit && xfer != EXS_LittleEndianImplicit) {
        DcmXfer xferInfo(xfer);
        throw std::runtime_error(
            std::string("readDicomTags: unsupported transfer syntax '") + xferInfo.getXferName() +
            "' (" + xferInfo.getXferID() +
            ") -- only Explicit/Implicit VR Little Endian are supported");
    }

    DicomTags tags;
    tags.transferSyntaxUid = DcmXfer(xfer).getXferID();

    Uint16 rows = 0, cols = 0;
    if (!ds->findAndGetUint16(DCM_Rows, rows).good() ||
        !ds->findAndGetUint16(DCM_Columns, cols).good()) {
        throw std::runtime_error("readDicomTags: missing Rows/Columns in " + path);
    }
    tags.rows = rows;
    tags.columns = cols;

    tags.pixelSpacing(0) = getFloat64OrThrow(ds, DCM_PixelSpacing, 0, "PixelSpacing[0]");
    tags.pixelSpacing(1) = getFloat64OrThrow(ds, DCM_PixelSpacing, 1, "PixelSpacing[1]");

    tags.imagePositionPatient(0) = getFloat64OrThrow(ds, DCM_ImagePositionPatient, 0, "ImagePositionPatient[0]");
    tags.imagePositionPatient(1) = getFloat64OrThrow(ds, DCM_ImagePositionPatient, 1, "ImagePositionPatient[1]");
    tags.imagePositionPatient(2) = getFloat64OrThrow(ds, DCM_ImagePositionPatient, 2, "ImagePositionPatient[2]");

    Float64 iop[6];
    bool haveOrientation = true;
    for (unsigned long i = 0; i < 6 && haveOrientation; ++i) {
        haveOrientation = ds->findAndGetFloat64(DCM_ImageOrientationPatient, iop[i], i).good();
    }
    if (haveOrientation) {
        tags.rowDir = Eigen::Vector3d(iop[0], iop[1], iop[2]);
        tags.colDir = Eigen::Vector3d(iop[3], iop[4], iop[5]);
    }

    tags.sliceThickness = getFloat64Optional(ds, DCM_SliceThickness);
    tags.spacingBetweenSlices = getFloat64Optional(ds, DCM_SpacingBetweenSlices);

    Float64 slope = 1.0, intercept = 0.0;
    if (ds->findAndGetFloat64(DCM_RescaleSlope, slope).good()) tags.rescaleSlope = slope;
    if (ds->findAndGetFloat64(DCM_RescaleIntercept, intercept).good()) tags.rescaleIntercept = intercept;

    Uint16 bitsAllocated = 16, pixelRep = 0;
    if (ds->findAndGetUint16(DCM_BitsAllocated, bitsAllocated).good()) tags.bitsAllocated = bitsAllocated;
    if (ds->findAndGetUint16(DCM_PixelRepresentation, pixelRep).good()) {
        tags.pixelRepresentationSigned = (pixelRep != 0);
    }

    return tags;
}

}  // namespace beam::infra::dicom
