// Copied from Diadem's tests/infra_dicom_tests.cpp (its DicomProbe test is
// omitted -- Beam did not copy dicom_probe). Fixtures are written via
// DCMTK's own writer API, so the readers are checked against
// independently-written files, not hand-guessed bytes.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmdata/dctk.h>

#include "infra_dicom/dicom_series.hpp"
#include "infra_dicom/dicom_slice.hpp"
#include "infra_dicom/dicom_tags.hpp"
#include "infra_dicom/load_mri_ras.hpp"

using namespace beam::infra::dicom;

namespace {

void writeMinimalDicomForTesting(const std::string& path) {
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, "1.2.840.10008.5.1.4.1.1.4");
    ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.3.4.5.6.7.8.9.0");
    ds->putAndInsertUint16(DCM_Rows, 4);
    ds->putAndInsertUint16(DCM_Columns, 3);
    ds->putAndInsertString(DCM_PixelSpacing, "1.5\\2.5");
    ds->putAndInsertString(DCM_ImagePositionPatient, "-100\\-80\\-50");
    ds->putAndInsertString(DCM_ImageOrientationPatient, "1\\0\\0\\0\\1\\0");
    ds->putAndInsertString(DCM_SliceThickness, "2.0");
    ds->putAndInsertString(DCM_RescaleSlope, "2.0");
    ds->putAndInsertString(DCM_RescaleIntercept, "100.0");
    ds->putAndInsertUint16(DCM_BitsAllocated, 16);
    ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
    ff.saveFile(path.c_str(), EXS_LittleEndianExplicit);
}

}  // namespace

TEST(DicomTags, RoundTripsAgainstAnIndependentlyWrittenFile) {
    const std::string path = "test_fixture.dcm";
    writeMinimalDicomForTesting(path);

    const DicomTags tags = readDicomTags(path);
    EXPECT_EQ(tags.rows, 4);
    EXPECT_EQ(tags.columns, 3);
    EXPECT_NEAR(tags.pixelSpacing(0), 1.5, 1e-9);
    EXPECT_NEAR(tags.pixelSpacing(1), 2.5, 1e-9);
    EXPECT_NEAR(tags.imagePositionPatient(0), -100, 1e-9);
    EXPECT_NEAR(tags.imagePositionPatient(2), -50, 1e-9);
    ASSERT_TRUE(tags.rowDir.has_value());
    ASSERT_TRUE(tags.colDir.has_value());
    EXPECT_TRUE(tags.rowDir->isApprox(Eigen::Vector3d(1, 0, 0)));
    EXPECT_TRUE(tags.colDir->isApprox(Eigen::Vector3d(0, 1, 0)));
    ASSERT_TRUE(tags.sliceThickness.has_value());
    EXPECT_NEAR(*tags.sliceThickness, 2.0, 1e-9);
    EXPECT_NEAR(tags.rescaleSlope, 2.0, 1e-9);
    EXPECT_NEAR(tags.rescaleIntercept, 100.0, 1e-9);
    EXPECT_FALSE(tags.spacingBetweenSlices.has_value());
}

TEST(DicomTags, ThrowsOnGarbageFile) {
    std::ofstream out("test_fixture_garbage.dcm");
    out << "not a dicom file";
    out.close();
    EXPECT_THROW(readDicomTags("test_fixture_garbage.dcm"), std::runtime_error);
}

namespace {

void writeDicomSliceForTesting(const std::string& path, int rows, int cols,
                                const Eigen::Vector3d& ipp, const std::vector<Uint16>& pixels,
                                const char* sopInstanceUid) {
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, "1.2.840.10008.5.1.4.1.1.4");
    ds->putAndInsertString(DCM_SOPInstanceUID, sopInstanceUid);
    ds->putAndInsertUint16(DCM_Rows, static_cast<Uint16>(rows));
    ds->putAndInsertUint16(DCM_Columns, static_cast<Uint16>(cols));
    ds->putAndInsertString(DCM_PixelSpacing, "1\\1");
    const std::string ippStr =
        std::to_string(ipp(0)) + "\\" + std::to_string(ipp(1)) + "\\" + std::to_string(ipp(2));
    ds->putAndInsertString(DCM_ImagePositionPatient, ippStr.c_str());
    ds->putAndInsertString(DCM_ImageOrientationPatient, "1\\0\\0\\0\\1\\0");
    ds->putAndInsertUint16(DCM_BitsAllocated, 16);
    ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
    ds->putAndInsertUint16Array(DCM_PixelData, pixels.data(), pixels.size());
    ff.saveFile(path.c_str(), EXS_LittleEndianExplicit);
}

}  // namespace

TEST(DicomSlice, RoundTripsPixelDataWithRescaleApplied) {
    const std::string path = "test_fixture_slice.dcm";
    writeDicomSliceForTesting(path, 2, 3, Eigen::Vector3d(-10, -20, 0),
                               {10, 20, 30, 40, 50, 60}, "1.2.3.4.5.6.7.8.9.1");
    const DicomSlice slice = readDicomSlice(path);
    ASSERT_EQ(slice.pixels.size(), 6u);
    const std::vector<double> expected = {10, 20, 30, 40, 50, 60};
    for (size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(slice.pixels[i], expected[i], 1e-9);
}

TEST(AssembleDicomSeriesRas, ThreeAxialSlicesProduceHandComputedVolume) {
    std::vector<std::string> files;
    for (int k = 0; k < 3; ++k) {
        std::vector<Uint16> px(6);
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 3; ++i)
                px[static_cast<size_t>(j * 3 + i)] = static_cast<Uint16>(100 * k + 10 * j + i);
        const std::string path = "test_fixture_series_slice" + std::to_string(k) + ".dcm";
        writeDicomSliceForTesting(path, 2, 3, Eigen::Vector3d(-10, -20, 2.0 * k), px,
                                   ("1.2.3.4.5.6.7.8.9." + std::to_string(k + 2)).c_str());
        files.push_back(path);
    }

    const auto mri = assembleDicomSeriesRas(files);
    ASSERT_EQ(mri.volume.dims[0], 3);
    ASSERT_EQ(mri.volume.dims[1], 2);
    ASSERT_EQ(mri.volume.dims[2], 3);
    const std::vector<double> expectedK0 = {12, 11, 10, 2, 1, 0};
    const std::vector<double> expectedK1 = {112, 111, 110, 102, 101, 100};
    const std::vector<double> expectedK2 = {212, 211, 210, 202, 201, 200};
    ASSERT_EQ(mri.volume.voxels.size(), 18u);
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_NEAR(mri.volume.voxels[i], expectedK0[i], 1e-9);
        EXPECT_NEAR(mri.volume.voxels[6 + i], expectedK1[i], 1e-9);
        EXPECT_NEAR(mri.volume.voxels[12 + i], expectedK2[i], 1e-9);
    }
}

TEST(AssembleDicomSeriesRas, ThrowsOnNonUniformSliceSpacing) {
    std::vector<std::string> files;
    const double zPositions[3] = {0.0, 2.0, 10.0};
    for (int k = 0; k < 3; ++k) {
        const std::vector<Uint16> px(6, static_cast<Uint16>(k));
        const std::string path = "test_fixture_nonuniform_slice" + std::to_string(k) + ".dcm";
        writeDicomSliceForTesting(path, 2, 3, Eigen::Vector3d(-10, -20, zPositions[k]), px,
                                   ("1.2.3.4.5.6.7.8.9." + std::to_string(k + 10)).c_str());
        files.push_back(path);
    }
    EXPECT_THROW(assembleDicomSeriesRas(files), std::runtime_error);
}

TEST(AssembleDicomSeriesRas, EmptyInputThrows) {
    EXPECT_THROW(assembleDicomSeriesRas({}), std::runtime_error);
}

namespace {

void writeMinimalNiftiForTesting(const std::string& path, int nx, int ny, int nz,
                                  const std::vector<float>& voxels) {
    constexpr float kVoxOffset = 352.0f;
    std::vector<char> buf(static_cast<std::size_t>(kVoxOffset) + voxels.size() * sizeof(float), 0);
    auto putI32 = [&](int off, int32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto putI16 = [&](int off, int16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    auto putF32 = [&](int off, float v) { std::memcpy(buf.data() + off, &v, 4); };

    putI32(0, 348);
    const int16_t dim[8] = {3, static_cast<int16_t>(nx), static_cast<int16_t>(ny),
                             static_cast<int16_t>(nz), 1, 0, 0, 0};
    for (int i = 0; i < 8; ++i) putI16(40 + 2 * i, dim[i]);
    putI16(70, 16);
    putI16(72, 32);
    const float pixdim[8] = {1, 1, 1, 1, 0, 0, 0, 0};
    for (int i = 0; i < 8; ++i) putF32(76 + 4 * i, pixdim[i]);
    putF32(108, kVoxOffset);
    putF32(112, 1.0f);
    putF32(116, 0.0f);
    putI16(252, 1);
    putF32(256, 0);
    putF32(260, 0);
    putF32(264, 0);
    putF32(268, 0);
    putF32(272, 0);
    putF32(276, 0);
    buf[344] = 'n';
    buf[345] = '+';
    buf[346] = '1';
    buf[347] = '\0';
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        std::memcpy(buf.data() + static_cast<std::size_t>(kVoxOffset) + i * 4, &voxels[i], 4);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}

}  // namespace

TEST(LoadMriRas, DirectoryOfDicomFilesUsesAssembler) {
    const std::string dir = "test_fixture_mriras_dicom_dir";
    std::filesystem::create_directories(dir);
    std::vector<std::string> files;
    for (int k = 0; k < 3; ++k) {
        std::vector<Uint16> px(6);
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 3; ++i)
                px[static_cast<size_t>(j * 3 + i)] = static_cast<Uint16>(100 * k + 10 * j + i);
        const std::string path = dir + "/slice" + std::to_string(k) + ".dcm";
        writeDicomSliceForTesting(path, 2, 3, Eigen::Vector3d(-10, -20, 2.0 * k), px,
                                   ("1.2.3.4.5.6.7.8.9." + std::to_string(k + 20)).c_str());
        files.push_back(path);
    }

    const auto mri = loadMriRas(dir);
    ASSERT_EQ(mri.volume.dims[0], 3);
    ASSERT_EQ(mri.volume.dims[1], 2);
    ASSERT_EQ(mri.volume.dims[2], 3);
    const std::vector<double> expectedK0 = {12, 11, 10, 2, 1, 0};
    for (size_t i = 0; i < 6; ++i) EXPECT_NEAR(mri.volume.voxels[i], expectedK0[i], 1e-9);
}

TEST(LoadMriRas, DirectoryWithOnlyNiftiFallsBackAndLoadsIt) {
    const std::string dir = "test_fixture_mriras_nifti_dir";
    std::filesystem::create_directories(dir);
    writeMinimalNiftiForTesting(dir + "/scan.nii", 2, 1, 1, {1.0f, 2.0f});
    const auto mri = loadMriRas(dir);
    ASSERT_EQ(mri.volume.voxels.size(), 2u);
    EXPECT_NEAR(mri.volume.voxels[0], 1.0, 1e-9);
    EXPECT_NEAR(mri.volume.voxels[1], 2.0, 1e-9);
}

TEST(LoadMriRas, PlainNiftiFilePathLoadsDirectly) {
    const std::string path = "test_fixture_mriras_plain.nii";
    writeMinimalNiftiForTesting(path, 2, 1, 1, {5.0f, 6.0f});
    const auto mri = loadMriRas(path);
    ASSERT_EQ(mri.volume.voxels.size(), 2u);
    EXPECT_NEAR(mri.volume.voxels[0], 5.0, 1e-9);
    EXPECT_NEAR(mri.volume.voxels[1], 6.0, 1e-9);
}

TEST(LoadMriRas, EmptyDirectoryThrows) {
    const std::string dir = "test_fixture_mriras_empty_dir";
    std::filesystem::create_directories(dir);
    EXPECT_THROW(loadMriRas(dir), std::runtime_error);
}

TEST(LoadMriRas, DirectoryWithNeitherDicomNorNiftiThrows) {
    const std::string dir = "test_fixture_mriras_garbage_dir";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + "/notes.txt");
    out << "not an image";
    out.close();
    EXPECT_THROW(loadMriRas(dir), std::runtime_error);
}
