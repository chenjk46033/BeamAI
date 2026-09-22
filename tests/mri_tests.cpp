// libs/mri tests. The NIfTI header/transform/file/loader tests are copied
// from Diadem's tests/imaging_tests.cpp (its DICOM tests are omitted --
// Beam has not brought over the DICOM path). The getSliceImage tests are
// Beam's own.

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mri/affine_volume.hpp"
#include "mri/dicom_coords.hpp"
#include "mri/dicom_series_geometry.hpp"
#include "mri/mri_loader.hpp"
#include "mri/nifti_file.hpp"
#include "mri/nifti_header.hpp"
#include "mri/nifti_orientation.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

using namespace beam::mri;

namespace {
constexpr double kEps = 1e-9;

NiftiHeader makeHeader() {
    NiftiHeader h;
    h.dim = {3, 5, 4, 3, 0, 0, 0, 0};
    h.pixdim = {1.0, 2.0, 2.0, 2.0, 0, 0, 0, 0};
    return h;
}
}  // namespace

TEST(GetRasXformFromHeader, IdentityQuaternionGivesScaledIdentityRotation) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    h.qoffsetX = 10; h.qoffsetY = 20; h.qoffsetZ = 30;

    const RasXform xform = getRasXformFromHeader(h);
    const Eigen::Matrix3d rot = xform.block<3, 3>(0, 0);
    const Eigen::Matrix3d expectedRot = Eigen::Vector3d(2.0, 2.0, 2.0).asDiagonal();
    EXPECT_TRUE(rot.isApprox(expectedRot, kEps));
    EXPECT_TRUE(xform.col(3).isApprox(Eigen::Vector3d(10, 20, 30), kEps));
}

TEST(GetRasXformFromHeader, QfacMinusOneNegatesThirdAxisScale) {
    NiftiHeader h = makeHeader();
    h.pixdim[0] = -1.0;  // qfac
    h.qformCode = 1;
    const RasXform xform = getRasXformFromHeader(h);
    EXPECT_NEAR(xform(2, 2), -2.0, kEps);
}

TEST(GetRasXformFromHeader, SformBranchUsesRowsDirectlyWhenNoQform) {
    NiftiHeader h = makeHeader();
    h.sformCode = 1;
    h.srowX = Eigen::RowVector4d(1, 0, 0, 5);
    h.srowY = Eigen::RowVector4d(0, 1, 0, 6);
    h.srowZ = Eigen::RowVector4d(0, 0, 1, 7);
    const RasXform xform = getRasXformFromHeader(h);
    EXPECT_TRUE(xform.row(0).isApprox(h.srowX, kEps));
    EXPECT_TRUE(xform.row(1).isApprox(h.srowY, kEps));
    EXPECT_TRUE(xform.row(2).isApprox(h.srowZ, kEps));
}

TEST(GetRasXformFromHeader, NoQformNoSformThrows) {
    EXPECT_THROW(getRasXformFromHeader(makeHeader()), std::runtime_error);
}

TEST(GetRasXformFromHeader, OutOfRangeQuaternionClampsInsteadOfNaN) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    h.quaternB = 1.0; h.quaternC = 1.0; h.quaternD = 1.0;
    EXPECT_TRUE(getRasXformFromHeader(h).allFinite());
}

TEST(GetRasAxisVectors, LinearRelationshipHandComputed) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    h.qoffsetX = 10; h.qoffsetY = 20; h.qoffsetZ = 30;

    const RasAxisVectors axes = getRasAxisVectors(h);
    ASSERT_EQ(axes.dimLR.size(), 5);
    for (int i = 0; i < 5; ++i) EXPECT_NEAR(axes.dimLR(i), 10 + i * 2.0, kEps);
    ASSERT_EQ(axes.dimAP.size(), 4);
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(axes.dimAP(i), 20 + i * 2.0, kEps);
    ASSERT_EQ(axes.dimIS.size(), 3);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(axes.dimIS(i), 30 + i * 2.0, kEps);
}

TEST(GetRasAxisVectors, SizesMatchHeaderDims) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    const RasAxisVectors axes = getRasAxisVectors(h);
    EXPECT_EQ(axes.dimLR.size(), h.dim[1]);
    EXPECT_EQ(axes.dimAP.size(), h.dim[2]);
    EXPECT_EQ(axes.dimIS.size(), h.dim[3]);
}

TEST(ComputeVoxelResolution, MatchesAbsDiffOfFirstTwoAxisPoints) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    h.qoffsetX = 10; h.qoffsetY = 20; h.qoffsetZ = 30;
    const RasAxisVectors axes = getRasAxisVectors(h);
    const VoxelResolution res = computeVoxelResolution(axes);
    EXPECT_NEAR(res.lr, 2.0, kEps);
    EXPECT_NEAR(res.ap, 2.0, kEps);
    EXPECT_NEAR(res.is, 2.0, kEps);
}

TEST(ComputeVoxelResolution, FewerThanTwoPointsGivesZeroForThatAxis) {
    RasAxisVectors axes;
    axes.dimLR.resize(1);
    axes.dimLR << 5.0;
    axes.dimAP.resize(3);
    axes.dimAP << 0.0, 1.5, 3.0;
    axes.dimIS.resize(0);
    const VoxelResolution res = computeVoxelResolution(axes);
    EXPECT_DOUBLE_EQ(res.lr, 0.0);
    EXPECT_NEAR(res.ap, 1.5, kEps);
    EXPECT_DOUBLE_EQ(res.is, 0.0);
}

TEST(GetVoxelRasXform, AxisAlignedIdentityQuaternionQfacPlusOneGivesIdentity) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    h.pixdim[0] = 1.0;
    EXPECT_TRUE(getVoxelRasXform(h).isApprox(Eigen::Matrix3d::Identity(), kEps));
}

TEST(GetVoxelRasXform, QfacMinusOneFlipsThirdAxisSign) {
    NiftiHeader h = makeHeader();
    h.qformCode = 1;
    h.pixdim[0] = -1.0;
    const Eigen::Matrix3d expected = Eigen::Vector3d(1, 1, -1).asDiagonal();
    EXPECT_TRUE(getVoxelRasXform(h).isApprox(expected, kEps));
}

TEST(ApplyVoxelRasXform3D, PureFlipOfSingleAxis) {
    const Eigen::Matrix3d xform = Eigen::Vector3d(-1, 1, 1).asDiagonal();
    const ReorientedVolume out = applyVoxelRasXform3D({5.0, 9.0}, {2, 1, 1}, xform);
    EXPECT_EQ(out.dims[0], 2);
    EXPECT_NEAR(out.voxels[0], 9.0, kEps);
    EXPECT_NEAR(out.voxels[1], 5.0, kEps);
}

TEST(ApplyVoxelRasXform3D, PurePermuteIsATranspose) {
    Eigen::Matrix3d xform = Eigen::Matrix3d::Zero();
    xform(0, 1) = 1;
    xform(1, 0) = 1;
    xform(2, 2) = 1;
    const std::vector<double> voxels = {0, 1, 10, 11, 20, 21};
    const ReorientedVolume out = applyVoxelRasXform3D(voxels, {2, 3, 1}, xform);
    ASSERT_EQ(out.dims[0], 3);
    ASSERT_EQ(out.dims[1], 2);
    const std::vector<double> expected = {0, 10, 20, 1, 11, 21};
    for (size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(out.voxels[i], expected[i], kEps);
}

// --- NIfTI file reader: minimal valid .nii built byte-for-byte here,
// independent of nifti_file.cpp's own offset constants. ---

namespace {

std::vector<char> makeMinimalNiftiBytes(const std::array<int16_t, 8>& dim,
                                         const std::array<float, 8>& pixdim, int16_t datatype,
                                         float sclSlope, float sclInter, int16_t qformCode,
                                         float quaternB, float quaternC, float quaternD,
                                         float qoffsetX, float qoffsetY, float qoffsetZ,
                                         const std::vector<float>& voxelData) {
    constexpr float kVoxOffset = 352.0f;
    std::vector<char> buf(static_cast<std::size_t>(kVoxOffset) + voxelData.size() * sizeof(float), 0);
    auto putI32 = [&](int off, int32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto putI16 = [&](int off, int16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    auto putF32 = [&](int off, float v) { std::memcpy(buf.data() + off, &v, 4); };

    putI32(0, 348);
    for (int i = 0; i < 8; ++i) putI16(40 + 2 * i, dim[i]);
    putI16(70, datatype);
    putI16(72, 32);
    for (int i = 0; i < 8; ++i) putF32(76 + 4 * i, pixdim[i]);
    putF32(108, kVoxOffset);
    putF32(112, sclSlope);
    putF32(116, sclInter);
    putI16(252, qformCode);
    putI16(254, 0);
    putF32(256, quaternB);
    putF32(260, quaternC);
    putF32(264, quaternD);
    putF32(268, qoffsetX);
    putF32(272, qoffsetY);
    putF32(276, qoffsetZ);
    buf[344] = 'n'; buf[345] = '+'; buf[346] = '1'; buf[347] = '\0';
    for (std::size_t i = 0; i < voxelData.size(); ++i) {
        std::memcpy(buf.data() + static_cast<std::size_t>(kVoxOffset) + i * 4, &voxelData[i], 4);
    }
    return buf;
}

void writeFile(const std::string& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST(NiftiFile, RoundTripsHeaderAndAppliesScaling) {
    const std::string path = "test_fixture_scaled.nii";
    writeFile(path, makeMinimalNiftiBytes({3, 2, 3, 1, 0, 0, 0, 0}, {1, 1.5f, 1.5f, 1.5f, 0, 0, 0, 0},
                                          16, 2.0f, 100.0f, 1, 0, 0, 0, -10, -20, -30,
                                          {1, 2, 3, 4, 5, 6}));
    const NiftiVolume vol = readNiftiVolumeScaled(path);
    EXPECT_EQ(vol.header.dim[1], 2);
    EXPECT_EQ(vol.header.dim[2], 3);
    EXPECT_NEAR(vol.header.pixdim[1], 1.5, kEps);
    EXPECT_NEAR(vol.header.qoffsetX, -10, kEps);
    ASSERT_EQ(vol.voxels.size(), 6u);
    for (int i = 0; i < 6; ++i) EXPECT_NEAR(vol.voxels[i], (i + 1) * 2.0 + 100.0, kEps);
}

TEST(NiftiFile, ZeroSclSlopeMeansNoScalingNotScaleToZero) {
    const std::string path = "test_fixture_noscale.nii";
    writeFile(path, makeMinimalNiftiBytes({3, 2, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0}, 16,
                                          0.0f, 999.0f, 1, 0, 0, 0, 0, 0, 0, {5, 7}));
    const NiftiVolume vol = readNiftiVolumeScaled(path);
    EXPECT_NEAR(vol.voxels[0], 5.0, kEps);
    EXPECT_NEAR(vol.voxels[1], 7.0, kEps);
}

TEST(NiftiFile, RejectsWrongMagic) {
    const std::string path = "test_fixture_badmagic.nii";
    auto bytes = makeMinimalNiftiBytes({3, 1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0}, 16, 1, 0,
                                        1, 0, 0, 0, 0, 0, 0, {1});
    bytes[344] = 'x';
    writeFile(path, bytes);
    EXPECT_THROW(readNiftiHeader(path), std::runtime_error);
}

TEST(NiftiFile, RejectsCorruptSizeofHdr) {
    const std::string path = "test_fixture_badsize.nii";
    auto bytes = makeMinimalNiftiBytes({3, 1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0}, 16, 1, 0,
                                        1, 0, 0, 0, 0, 0, 0, {1});
    const int32_t badSize = 999;
    std::memcpy(bytes.data(), &badSize, 4);
    writeFile(path, bytes);
    EXPECT_THROW(readNiftiHeader(path), std::runtime_error);
}

TEST(WriteNiftiVolume, RoundTripsThroughTheReader) {
    NiftiHeader h;
    h.dim = {3, 2, 3, 1, 0, 0, 0, 0};
    h.pixdim = {1, 1.5, 1.5, 1.5, 0, 0, 0, 0};
    h.datatype = 16;  // DT_FLOAT32
    h.bitpix = 32;
    h.qformCode = 1;
    h.qoffsetX = -10;
    h.qoffsetY = -20;
    h.qoffsetZ = -30;
    const std::vector<double> voxels = {1, 2, 3, 4, 5, 6};

    const std::string path = "test_fixture_write.nii";
    writeNiftiVolume(path, h, voxels);

    const NiftiVolume back = readNiftiVolumeScaled(path);
    EXPECT_EQ(back.header.dim[1], 2);
    EXPECT_EQ(back.header.dim[2], 3);
    EXPECT_NEAR(back.header.pixdim[1], 1.5, kEps);
    EXPECT_NEAR(back.header.qoffsetX, -10, kEps);
    ASSERT_EQ(back.voxels.size(), 6u);
    for (int i = 0; i < 6; ++i) EXPECT_NEAR(back.voxels[i], i + 1, kEps);
}

TEST(WriteNiftiVolume, Int16DatatypeRoundTrips) {
    NiftiHeader h;
    h.dim = {3, 3, 1, 1, 0, 0, 0, 0};
    h.pixdim = {1, 1, 1, 1, 0, 0, 0, 0};
    h.datatype = 4;  // DT_INT16
    h.bitpix = 16;
    h.sformCode = 1;
    h.srowX = Eigen::RowVector4d(1, 0, 0, 0);
    h.srowY = Eigen::RowVector4d(0, 1, 0, 0);
    h.srowZ = Eigen::RowVector4d(0, 0, 1, 0);
    const std::vector<double> voxels = {-5, 0, 12345};

    const std::string path = "test_fixture_write_i16.nii";
    writeNiftiVolume(path, h, voxels);

    const NiftiVolume back = readNiftiVolumeScaled(path);
    ASSERT_EQ(back.voxels.size(), 3u);
    EXPECT_NEAR(back.voxels[0], -5, kEps);
    EXPECT_NEAR(back.voxels[2], 12345, kEps);
}

TEST(ReorientedVolumeToVolume3D, ReshapesColumnMajorFlatVoxelsPerKSlice) {
    // dims = {nx=2, ny=3, nz=2}; flat column-major (i fastest, then j, then
    // k) -- voxel value == its flat index, so slice(i,j) at k should equal
    // i + j*nx + k*nx*ny.
    ReorientedVolume rv;
    rv.dims = {2, 3, 2};
    rv.voxels.resize(12);
    for (int idx = 0; idx < 12; ++idx) rv.voxels[static_cast<std::size_t>(idx)] = idx;

    const Volume3D v = reorientedVolumeToVolume3D(rv);
    EXPECT_EQ(v.nx, 2);
    EXPECT_EQ(v.ny, 3);
    EXPECT_EQ(v.nz, 2);
    EXPECT_DOUBLE_EQ(v(0, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(v(1, 0, 0), 1.0);
    EXPECT_DOUBLE_EQ(v(0, 1, 0), 2.0);
    EXPECT_DOUBLE_EQ(v(1, 2, 0), 5.0);
    EXPECT_DOUBLE_EQ(v(0, 0, 1), 6.0);
    EXPECT_DOUBLE_EQ(v(1, 2, 1), 11.0);
}

TEST(LoadNiftiMriRas, IdentityQformPassesVoxelsAndAxesThroughUnchanged) {
    const std::string path = "test_fixture_mri_loader.nii";
    writeFile(path, makeMinimalNiftiBytes({3, 2, 3, 2, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0}, 16,
                                          1.0f, 0.0f, 1, 0, 0, 0, 0, 0, 0,
                                          {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
    const MriVolumeRas mri = loadNiftiMriRas(path);
    ASSERT_EQ(mri.volume.dims[0], 2);
    ASSERT_EQ(mri.volume.dims[1], 3);
    ASSERT_EQ(mri.volume.dims[2], 2);
    ASSERT_EQ(mri.volume.voxels.size(), 12u);
    for (int i = 0; i < 12; ++i) EXPECT_NEAR(mri.volume.voxels[static_cast<size_t>(i)], i + 1, kEps);
    ASSERT_EQ(mri.axes.dimLR.size(), 2);
    EXPECT_NEAR(mri.axes.dimLR(0), 0.0, kEps);
    EXPECT_NEAR(mri.axes.dimLR(1), 1.0, kEps);
}

// --- DICOM coordinate math (copied from Diadem imaging_tests.cpp) ---

TEST(GetDicomPixelSpatialReferenceMath, HandComputedLinearSequences) {
    const auto r = getDicomPixelSpatialReferenceMath(
        4, 5, 3, Eigen::Vector2d(1.5, 2.5), Eigen::Vector3d(-100, -80, -50), 2.0);
    ASSERT_EQ(r.dimAP.size(), 5);
    for (int i = 0; i < 5; ++i) EXPECT_NEAR(r.dimAP(i), -100 + i * 1.5, kEps);
    ASSERT_EQ(r.dimIS.size(), 4);
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(r.dimIS(i), -80 + i * 2.5, kEps);
    ASSERT_EQ(r.dimLR.size(), 3);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(r.dimLR(i), -50 + i * 2.0, kEps);
}

TEST(GetDicomPixelSpatialReferenceMath, MissingSliceThicknessThrows) {
    EXPECT_THROW(getDicomPixelSpatialReferenceMath(4, 5, 3, Eigen::Vector2d(1, 1),
                                                   Eigen::Vector3d::Zero(), std::nullopt),
                 std::runtime_error);
}

TEST(ResolveDicomOrientationDefaults, MissingOrientationUsesIdentityAxes) {
    const DicomOrientationInfo info;
    const auto r = resolveDicomOrientationDefaults(info);
    EXPECT_TRUE(r.rowDir.isApprox(Eigen::Vector3d(1, 0, 0), kEps));
    EXPECT_TRUE(r.colDir.isApprox(Eigen::Vector3d(0, 1, 0), kEps));
    EXPECT_TRUE(r.sliceDir.isApprox(Eigen::Vector3d(0, 0, 1), kEps));
    EXPECT_TRUE(r.pixelSpacing.isApprox(Eigen::Vector2d(1, 1), kEps));
    EXPECT_NEAR(r.sliceSpacing, 1.0, kEps);
    EXPECT_TRUE(r.origin.isApprox(Eigen::Vector3d::Zero(), kEps));
}

TEST(ResolveDicomOrientationDefaults, AllFieldsPresentUsesGivenValuesAndDerivesSliceDir) {
    DicomOrientationInfo info;
    info.rowDir = Eigen::Vector3d(0, 1, 0);
    info.colDir = Eigen::Vector3d(0, 0, 1);
    info.pixelSpacing = Eigen::Vector2d(1.5, 2.5);
    info.spacingBetweenSlices = 3.0;
    info.sliceThickness = 4.0;
    info.imagePositionPatient = Eigen::Vector3d(5, 6, 7);
    const auto r = resolveDicomOrientationDefaults(info);
    EXPECT_TRUE(r.sliceDir.isApprox(Eigen::Vector3d(1, 0, 0), kEps));  // cross(y,z) = x
    EXPECT_NEAR(r.sliceSpacing, 3.0, kEps);
    EXPECT_TRUE(r.origin.isApprox(Eigen::Vector3d(5, 6, 7), kEps));
}

TEST(ResolveDicomOrientationDefaults, FallsBackToSliceThicknessWhenSpacingBetweenSlicesAbsent) {
    DicomOrientationInfo info;
    info.sliceThickness = 4.0;
    EXPECT_NEAR(resolveDicomOrientationDefaults(info).sliceSpacing, 4.0, kEps);
}

TEST(GetDicomVolumeCoords, HandComputedLinearSequencesWithIdentityAxes) {
    ResolvedDicomOrientation o;
    o.rowDir = Eigen::Vector3d(1, 0, 0);
    o.colDir = Eigen::Vector3d(0, 1, 0);
    o.sliceDir = Eigen::Vector3d(0, 0, 1);
    o.pixelSpacing = Eigen::Vector2d(2, 3);
    o.sliceSpacing = 4;
    o.origin = Eigen::Vector3d(10, 20, 30);
    const auto c = getDicomVolumeCoords(3, 2, 2, o);
    ASSERT_EQ(c.dimLR.size(), 2);
    EXPECT_NEAR(c.dimLR(1), 13, kEps);
    ASSERT_EQ(c.dimAP.size(), 3);
    EXPECT_NEAR(c.dimAP(2), 24, kEps);
    ASSERT_EQ(c.dimIS.size(), 2);
    EXPECT_NEAR(c.dimIS(1), 34, kEps);
}

TEST(BuildNiftiHeaderFromDicomSeries, HandComputedAxialCase) {
    const NiftiHeader header = buildNiftiHeaderFromDicomSeries(
        Eigen::Vector3d(1, 0, 0), Eigen::Vector3d(0, 1, 0), Eigen::Vector3d(-10, -20, -30),
        /*spacingAlongRowDir=*/1.0, /*spacingAlongColDir=*/1.0, /*sliceSpacing=*/2.0,
        /*numCols=*/5, /*numRows=*/4, /*numSlices=*/3);
    EXPECT_EQ(header.dim[1], 5);
    EXPECT_EQ(header.dim[2], 4);
    EXPECT_EQ(header.dim[3], 3);
    EXPECT_EQ(header.sformCode, 1);
    EXPECT_TRUE(header.srowX.isApprox(Eigen::RowVector4d(-1, 0, 0, 10), kEps));
    EXPECT_TRUE(header.srowY.isApprox(Eigen::RowVector4d(0, -1, 0, 20), kEps));
    EXPECT_TRUE(header.srowZ.isApprox(Eigen::RowVector4d(0, 0, 2, -30), kEps));
}

// --- getSliceImage (Beam's own) ---

TEST(GetSliceImage, AxialIsRot90OfKSlice) {
    Volume3D vol;
    vol.nx = 2; vol.ny = 2; vol.nz = 1;
    Eigen::MatrixXd s(2, 2);
    s << 1, 2, 3, 4;
    vol.kSlices = {s};
    const Eigen::MatrixXd out = getSliceImage(vol, 1, "axial");
    Eigen::MatrixXd expected(2, 2);
    expected << 2, 4, 1, 3;
    EXPECT_TRUE(out.isApprox(expected));
}

TEST(GetSliceImage, SagitalPlaneExtractionAndRotation) {
    Volume3D vol;
    vol.nx = 2; vol.ny = 2; vol.nz = 2;
    Eigen::MatrixXd k0(2, 2);
    k0 << 1, 2, 3, 4;
    Eigen::MatrixXd k1(2, 2);
    k1 << 5, 6, 7, 8;
    vol.kSlices = {k0, k1};
    const Eigen::MatrixXd out = getSliceImage(vol, 1, "sagital");
    Eigen::MatrixXd expected(2, 2);
    expected << 5, 6, 1, 2;
    EXPECT_TRUE(out.isApprox(expected));
}

TEST(GetSliceImage, UnknownPlaneThrows) {
    Volume3D vol;
    vol.nx = 1; vol.ny = 1; vol.nz = 1;
    vol.kSlices = {Eigen::MatrixXd::Zero(1, 1)};
    EXPECT_THROW(getSliceImage(vol, 1, "oblique"), std::invalid_argument);
}

TEST(ComputeDisplayWindow, LoIsZeroHiIsVolumeMax) {
    Volume3D vol;
    vol.nx = 2; vol.ny = 2; vol.nz = 2;
    Eigen::MatrixXd k0(2, 2);
    k0 << 1, -5, 3, 2;
    Eigen::MatrixXd k1(2, 2);
    k1 << 0, 9, -100, 4;
    vol.kSlices = {k0, k1};
    const DisplayWindow w = computeDisplayWindow(vol);
    EXPECT_DOUBLE_EQ(w.lo, 0.0);
    EXPECT_DOUBLE_EQ(w.hi, 9.0);
}

TEST(ComputeDisplayWindow, EmptyVolumeGivesZeroZero) {
    const DisplayWindow w = computeDisplayWindow(Volume3D{});
    EXPECT_DOUBLE_EQ(w.lo, 0.0);
    EXPECT_DOUBLE_EQ(w.hi, 0.0);
}

// --- applyAffine3D ---

namespace {
Volume3D constantVolume(Eigen::Index n, double value) {
    Volume3D v;
    v.nx = v.ny = v.nz = n;
    v.kSlices.assign(static_cast<std::size_t>(n), Eigen::MatrixXd::Constant(n, n, value));
    return v;
}
Eigen::VectorXd axis012() {
    Eigen::VectorXd a(3);
    a << 0, 1, 2;
    return a;
}
}  // namespace

TEST(ApplyAffine3D, IdentityGivesOneVoxelLargerGridWithHalfVoxelShift) {
    const beam::mri::AffineVolumeResult r = beam::mri::applyAffine3D(
        constantVolume(3, 7.0), axis012(), axis012(), axis012(), Eigen::Matrix4d::Identity());
    // input world limits [-0.5, 2.5], padded by dx/2 -> [-1, 3], dx = 1 -> 4 cells.
    EXPECT_EQ(r.volume.nx, 4);
    EXPECT_EQ(r.volume.ny, 4);
    EXPECT_EQ(r.volume.nz, 4);
    EXPECT_NEAR(r.xMm(0), -0.5, 1e-12);  // first output voxel centre
    EXPECT_NEAR(r.xMm(3), 2.5, 1e-12);
}

TEST(ApplyAffine3D, ConstantVolumeInteriorPreservedEdgesZero) {
    const beam::mri::AffineVolumeResult r = beam::mri::applyAffine3D(
        constantVolume(3, 7.0), axis012(), axis012(), axis012(), Eigen::Matrix4d::Identity());
    // interior output voxels (1..2) sample inside the input -> 7; edges -> 0.
    EXPECT_DOUBLE_EQ(r.volume(1, 1, 1), 7.0);
    EXPECT_DOUBLE_EQ(r.volume(2, 2, 2), 7.0);
    EXPECT_DOUBLE_EQ(r.volume(0, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(r.volume(3, 3, 3), 0.0);
}

TEST(ApplyAffine3D, LinearRampIsInterpolatedAtSampledPositions) {
    Volume3D ramp;
    ramp.nx = ramp.ny = ramp.nz = 3;
    ramp.kSlices.assign(3, Eigen::MatrixXd::Zero(3, 3));
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) ramp.kSlices[static_cast<std::size_t>(k)](i, j) = i;  // V(i,j,k) = i

    const beam::mri::AffineVolumeResult r =
        beam::mri::applyAffine3D(ramp, axis012(), axis012(), axis012(), Eigen::Matrix4d::Identity());
    // output voxel i centre samples input fi = -0.5 + i; V there = fi.
    EXPECT_NEAR(r.volume(1, 1, 1), 0.5, 1e-12);
    EXPECT_NEAR(r.volume(2, 1, 1), 1.5, 1e-12);
}

TEST(ApplyAffine3D, TranslationShiftsTheOutputAxes) {
    Eigen::Matrix4d translate = Eigen::Matrix4d::Identity();
    translate(0, 3) = 10.0;  // world x += 10
    const beam::mri::AffineVolumeResult r = beam::mri::applyAffine3D(
        constantVolume(3, 1.0), axis012(), axis012(), axis012(), translate);
    EXPECT_NEAR(r.xMm(0), -0.5 + 10.0, 1e-9);  // same grid, shifted by +10
    EXPECT_NEAR(r.yMm(0), -0.5, 1e-9);         // y untouched
}
