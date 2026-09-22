#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "array/array_data.hpp"
#include "array/array_types.hpp"
#include "registration/affine_registration.hpp"
#include "registration/array_transform.hpp"
#include "registration/fiducial_markers.hpp"
#include "registration/image_model.hpp"

using namespace beam::registration;

namespace {

Eigen::Matrix3d rotZ(double rad) {
    Eigen::Matrix3d r;
    r << std::cos(rad), -std::sin(rad), 0, std::sin(rad), std::cos(rad), 0, 0, 0, 1;
    return r;
}

// 6 non-coplanar source points (getAffineMatrixFromRegistration needs 6).
Eigen::Matrix3Xd sixPoints() {
    Eigen::Matrix3Xd a(3, 6);
    a.col(0) = Eigen::Vector3d(1, 0, 0);
    a.col(1) = Eigen::Vector3d(0, 1, 0);
    a.col(2) = Eigen::Vector3d(0, 0, 1);
    a.col(3) = Eigen::Vector3d(1, 1, 0);
    a.col(4) = Eigen::Vector3d(0, 1, 1);
    a.col(5) = Eigen::Vector3d(2, -1, 3);
    return a;
}

}  // namespace

TEST(AffineRegistration, RecoversPureRotation) {
    const Eigen::Matrix3d rTrue = rotZ(0.7);
    const Eigen::Matrix3Xd a = sixPoints();
    const Eigen::Matrix3Xd b = rTrue * a;

    const AffineRegistrationResult reg = affineRegistration(a, b);
    EXPECT_TRUE(reg.r.isApprox(rTrue, 1e-9));
    EXPECT_LT(reg.t.norm(), 1e-9);  // isApprox is relative -> useless vs a zero vector
}

TEST(AffineRegistration, RecoversRotationPlusTranslation) {
    const Eigen::Matrix3d rTrue = rotZ(-0.4);
    const Eigen::Vector3d tTrue(5, -3, 2);
    const Eigen::Matrix3Xd a = sixPoints();
    const Eigen::Matrix3Xd b = (rTrue * a).colwise() + tTrue;

    const AffineRegistrationResult reg = affineRegistration(a, b);
    EXPECT_TRUE(reg.r.isApprox(rTrue, 1e-9));
    EXPECT_TRUE(reg.t.isApprox(tTrue, 1e-9));
}

TEST(AffineRegistration, WeightedStillRecoversPerfectFit) {
    const Eigen::Matrix3d rTrue = rotZ(1.1);
    const Eigen::Matrix3Xd a = sixPoints();
    const Eigen::Matrix3Xd b = rTrue * a;
    Eigen::VectorXd w(6);
    w << 0.5, 1, 0.5, 0.5, 1, 0.5;

    const AffineRegistrationResult reg = affineRegistration(a, b, w);
    EXPECT_TRUE(reg.r.isApprox(rTrue, 1e-9));
}

TEST(GetAffineMatrixFromRegistration, PackagesRotationAndScalesTranslation) {
    const Eigen::Matrix3d rTrue = rotZ(0.3);
    const Eigen::Vector3d tTrue(100, 200, -50);
    const Eigen::Matrix3Xd a = sixPoints();
    const Eigen::Matrix3Xd b = (rTrue * a).colwise() + tTrue;

    const Eigen::Matrix4d mmMatrix = getAffineMatrixFromRegistration(a, b, /*metersToMm=*/false);
    const Eigen::Matrix3d mmRot = mmMatrix.topLeftCorner<3, 3>();
    const Eigen::Vector3d mmTrans = mmMatrix.topRightCorner<3, 1>();
    EXPECT_TRUE(mmRot.isApprox(rTrue, 1e-9));
    EXPECT_TRUE(mmTrans.isApprox(tTrue, 1e-9));
    EXPECT_TRUE(mmMatrix.row(3).isApprox(Eigen::RowVector4d(0, 0, 0, 1)));

    const Eigen::Matrix4d mMatrix = getAffineMatrixFromRegistration(a, b, /*metersToMm=*/true);
    const Eigen::Vector3d mTrans = mMatrix.topRightCorner<3, 1>();
    EXPECT_TRUE(mTrans.isApprox(tTrue / 1000.0, 1e-9));
}

TEST(GetArrayFiducialMarkerNames, EightElNames) {
    EXPECT_EQ(getArrayFiducialMarkerNames(),
              (std::vector<std::string>{"EL1", "EL9", "EL118", "EL126", "EL127", "EL135", "EL244", "EL252"}));
}

namespace {

// ArrayData with 100 elements positioned so setArrayFiducialMarkers'
// x/y/z basis (elements 1/81, 24/1, 31/40) comes out as identity, and both
// array halves' rect centres are the origin -- so the marker positions
// reduce to just the fixed offsets.
beam::array::ArrayData makeArrayDataForFiducials() {
    beam::array::ArrayData d;
    d.arrayTotal.element.resize(100);
    d.arrayTotal.element[0].position = Eigen::Vector3d(0, 0, 0);    // element 1
    d.arrayTotal.element[80].position = Eigen::Vector3d(-1, 0, 0);  // element 81 -> xVector +x
    d.arrayTotal.element[23].position = Eigen::Vector3d(0, 1, 0);   // element 24 -> yVector +y
    d.arrayTotal.element[30].position = Eigen::Vector3d(0, 0, 1);   // element 31 -> zVector +z
    d.arrayTotal.element[39].position = Eigen::Vector3d(0, 0, 0);   // element 40

    d.array[0].rect = Eigen::MatrixXd::Zero(19, 4);  // centre rows (16..18) all zero
    d.array[1].rect = Eigen::MatrixXd::Zero(19, 4);
    return d;
}

}  // namespace

TEST(SetArrayFiducialMarkers, SixMarkersAtFixedOffsets) {
    const std::vector<FiducialMarker> markers = setArrayFiducialMarkers(makeArrayDataForFiducials());
    ASSERT_EQ(markers.size(), 6u);
    EXPECT_EQ(markers[0].name, "LeftY1Z3");
    EXPECT_EQ(markers[4].name, "RightY1Z1");

    // Identity basis + zero centre -> position == the spec's relative offset.
    EXPECT_TRUE(markers[0].position.isApprox(Eigen::Vector3d(-0.019, 0.0, 0.020), 1e-12));
    EXPECT_TRUE(markers[2].position.isApprox(Eigen::Vector3d(-0.019, -0.0225, 0.0), 1e-12));
    EXPECT_TRUE(markers[3].position.isApprox(Eigen::Vector3d(0.019, 0.0, 0.020), 1e-12));
    EXPECT_TRUE(markers[5].position.isApprox(Eigen::Vector3d(0.019, -0.0225, 0.0), 1e-12));
}

TEST(ApplyAffineMatrixToFiducialMarkers, TranslatesEveryMarker) {
    std::vector<FiducialMarker> markers = {{"a", Eigen::Vector3d(1, 2, 3)}, {"b", Eigen::Vector3d(-1, 0, 4)}};
    Eigen::Matrix4d affine = Eigen::Matrix4d::Identity();
    affine.topRightCorner<3, 1>() = Eigen::Vector3d(10, 20, 30);

    const std::vector<FiducialMarker> out = applyAffineMatrixToFiducialMarkers(affine, markers);
    EXPECT_TRUE(out[0].position.isApprox(Eigen::Vector3d(11, 22, 33)));
    EXPECT_TRUE(out[1].position.isApprox(Eigen::Vector3d(9, 20, 34)));
    EXPECT_EQ(out[1].name, "b");
}

TEST(GetTransducerFiducialMarkersPositionFromFrame, MovesMarkersBySliderDelta) {
    const std::vector<FiducialMarker> baseline = {{"m", Eigen::Vector3d(0, 0, 0)}};
    // dV = 3-1 = 2, dH = 5-2 = 3; dY = 2*10/1000 = 0.02; dZ = -3*7.5/1000 = -0.0225.
    const std::vector<FiducialMarker> out = getTransducerFiducialMarkersPositionFromFrame(
        baseline, Eigen::Matrix3d::Identity(), /*baseV=*/1, /*baseH=*/2, /*vDelta=*/10, /*hDelta=*/7.5,
        /*curV=*/3, /*curH=*/5);
    EXPECT_TRUE(out[0].position.isApprox(Eigen::Vector3d(0.0, 0.02, -0.0225), 1e-12));
}

TEST(GetFiducialPositionFromName, FindsAndThrows) {
    const std::vector<FiducialMarker> markers = {{"x", Eigen::Vector3d(1, 1, 1)},
                                                 {"y", Eigen::Vector3d(2, 2, 2)}};
    EXPECT_TRUE(getFiducialPositionFromName("y", markers).isApprox(Eigen::Vector3d(2, 2, 2)));
    EXPECT_THROW(getFiducialPositionFromName("z", markers), std::invalid_argument);
}

TEST(GetTranslationMatrixFromTransducerFiducials, IdentityBasisFromCleanAxes) {
    const std::vector<FiducialMarker> markers = {
        {"LeftY1Z1", Eigen::Vector3d(0, 0, 0)},   {"RightY1Z1", Eigen::Vector3d(2, 0, 0)},
        {"LeftY1Z3", Eigen::Vector3d(0, 0, 3)},   {"RightY1Z3", Eigen::Vector3d(2, 0, 3)},
        {"LeftY4Z1", Eigen::Vector3d(0, -1, 0)},  {"RightY4Z1", Eigen::Vector3d(2, -1, 0)},
    };
    const TransducerBasis basis = getTranslationMatrixFromTransducerFiducials(markers);
    EXPECT_TRUE(basis.xVector.isApprox(Eigen::Vector3d(1, 0, 0), 1e-12));
    EXPECT_TRUE(basis.yVector.isApprox(Eigen::Vector3d(0, 1, 0), 1e-12));
    EXPECT_TRUE(basis.zVector.isApprox(Eigen::Vector3d(0, 0, 1), 1e-12));
    EXPECT_TRUE(basis.m.isApprox(Eigen::Matrix3d::Identity(), 1e-12));
}

namespace {

// 100 elements along x (same idea as array_tests' buildSyntheticRect).
Eigen::MatrixXd buildRect(int n) {
    Eigen::MatrixXd rect(19, n);
    const double spacing = 0.002, h = 0.0003;
    for (int i = 0; i < n; ++i) {
        const Eigen::Vector3d c(static_cast<double>(i + 1) * spacing, 0, 0);
        rect(0, i) = i + 1;
        rect.block<3, 1>(1, i) = c + Eigen::Vector3d(-h, -h, 0);
        rect.block<3, 1>(4, i) = c + Eigen::Vector3d(h, -h, 0);
        rect.block<3, 1>(7, i) = c + Eigen::Vector3d(h, h, 0);
        rect.block<3, 1>(10, i) = c + Eigen::Vector3d(-h, h, 0);
        rect.block<3, 1>(13, i).setZero();
        rect.block<3, 1>(16, i) = c;
    }
    return rect;
}

}  // namespace

TEST(ApplyAffineToArrayData, IdentityIsNoOpAndTranslationShiftsElements) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildRect(100));

    const AffineArrayResult same = applyAffineToArrayData(Eigen::Matrix4d::Identity(), data);
    EXPECT_TRUE(same.arrayData.arrayTotal.element[0].position.isApprox(
        data.arrayTotal.element[0].position, 1e-9));
    EXPECT_EQ(same.fiducialMarkers.size(), 6u);

    Eigen::Matrix4d translate = Eigen::Matrix4d::Identity();
    translate.topRightCorner<3, 1>() = Eigen::Vector3d(0.01, 0, 0);
    const AffineArrayResult moved = applyAffineToArrayData(translate, data);
    const Eigen::Vector3d before = data.arrayTotal.element[10].position;
    const Eigen::Vector3d after = moved.arrayData.arrayTotal.element[10].position;
    EXPECT_TRUE(after.isApprox(before + Eigen::Vector3d(0.01, 0, 0), 1e-9));
}

// --- ImageBasedModel ---

TEST(OrientationAngleFromExif, StandardTags) {
    EXPECT_DOUBLE_EQ(orientationAngleFromExif(1), 0.0);
    EXPECT_DOUBLE_EQ(orientationAngleFromExif(3), 180.0);
    EXPECT_DOUBLE_EQ(orientationAngleFromExif(6), 90.0);
    EXPECT_DOUBLE_EQ(orientationAngleFromExif(8), -90.0);
    EXPECT_THROW(orientationAngleFromExif(99), std::invalid_argument);
}

TEST(PerimdistanceImage, ScaledStepNorms) {
    Eigen::MatrixX2i p(3, 2);
    p << 0, 0,
         0, 3,
         4, 3;
    const Eigen::VectorXd d = perimdistanceImage(p, 1.0, 1.0);
    ASSERT_EQ(d.size(), 2);
    EXPECT_DOUBLE_EQ(d(0), 3.0);  // step [0,3]
    EXPECT_DOUBLE_EQ(d(1), 4.0);  // step [4,0]
    EXPECT_DOUBLE_EQ(perimdistanceImage(p, 2.0, 1.0)(1), 8.0);  // row delta 4 scaled by hRatio 2
}

TEST(PerimwalkImage2, OrdersConnectedPixelsFromStart) {
    Eigen::MatrixX2i p(5, 2);
    p << 0, 0,
         1, 0,
         2, 0,
         2, 1,
         2, 2;
    const Eigen::MatrixX2i walked = perimwalkImage2(p, Eigen::Vector2i(0, 0));
    ASSERT_EQ(walked.rows(), 5);
    EXPECT_EQ(walked.row(0).transpose(), Eigen::Vector2i(0, 0));  // starts at startpoint
    // every input pixel is present exactly once
    std::vector<std::pair<int, int>> got, want;
    for (int i = 0; i < 5; ++i) {
        got.emplace_back(walked(i, 0), walked(i, 1));
        want.emplace_back(p(i, 0), p(i, 1));
    }
    std::sort(got.begin(), got.end());
    std::sort(want.begin(), want.end());
    EXPECT_EQ(got, want);
    EXPECT_THROW(perimwalkImage2(p, Eigen::Vector2i(9, 9)), std::invalid_argument);
}

TEST(GetDiscreteFromImage, SamplesSevenMarkersAlongAVerticalContour) {
    Eigen::MatrixXi contour = Eigen::MatrixXi::Zero(50, 10);
    contour.col(5).setOnes();  // vertical "scalp" line at 0-based col 5 == 1-based col 6

    // crop keeps rows 1..30; nasion at top, inion at interior row 20.
    const Eigen::MatrixX2i markers = getDiscreteFromImage(contour, /*spacings=*/2.0,
                                                          /*nzImg=*/Eigen::Vector2i(4, 0),
                                                          /*izImg=*/Eigen::Vector2i(4, 19));
    ASSERT_EQ(markers.rows(), 7);
    EXPECT_EQ(markers.row(0).transpose(), Eigen::Vector2i(1, 6));    // 0%
    EXPECT_EQ(markers.row(3).transpose(), Eigen::Vector2i(10, 6));   // 50%
    EXPECT_EQ(markers.row(6).transpose(), Eigen::Vector2i(20, 6));   // 100%
}

TEST(OrganizeTransducerSlots, SortsSlotsAndPairsWithArrayMarkers) {
    Eigen::MatrixX3d slots(4, 3);
    slots << 10, 1, 1,
             10, 4, 1,
             10, 1, 4,
             10, 4, 4;  // (x, Y, Z)

    std::vector<FiducialMarker> fm(6);
    for (int i = 0; i < 6; ++i) {
        fm[i].name = "m" + std::to_string(i + 1);
        fm[i].position = Eigen::Vector3d(0.01 * (i + 1), 0, 0);  // x in metres
    }

    const std::vector<FiducialMarker> out = organizeTransducerSlots(slots, fm);
    ASSERT_EQ(out.size(), 6u);
    EXPECT_EQ(out[0].name, "m1");
    EXPECT_TRUE(out[0].position.isApprox(Eigen::Vector3d(10, 1, 4)));   // Y1Z4
    EXPECT_TRUE(out[1].position.isApprox(Eigen::Vector3d(20, 4, 1)));   // Y1Z1
    EXPECT_TRUE(out[2].position.isApprox(Eigen::Vector3d(30, 1, 4)));   // Y4Z1
    EXPECT_EQ(out[3].name, "m4");
    EXPECT_TRUE(out[3].position.isApprox(Eigen::Vector3d(40, 1, 4)));   // reuses out[0]'s Y,Z
}

TEST(RegisterArrayToFiducials, PureTranslationRecoveredAndFiducialsMatchTarget) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildRect(100));
    const std::vector<FiducialMarker> originFiducials = setArrayFiducialMarkers(data);
    ASSERT_EQ(originFiducials.size(), 6u);

    const Eigen::Vector3d shiftM(0.005, -0.003, 0.002);  // meters
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const FiducialMarker& m : originFiducials) {
        mriFiducialsMm.push_back((m.position + shiftM) * 1000.0);
    }

    const AffineArrayResult result = registerArrayToFiducials(data, mriFiducialsMm);

    const Eigen::Vector3d before = data.arrayTotal.element[10].position;
    const Eigen::Vector3d after = result.arrayData.arrayTotal.element[10].position;
    EXPECT_TRUE(after.isApprox(before + shiftM, 1e-6));

    ASSERT_EQ(result.fiducialMarkers.size(), 6u);
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_TRUE((result.fiducialMarkers[i].position * 1000.0).isApprox(mriFiducialsMm[i], 1e-4));
    }
}

TEST(RegisterArrayToFiducials, WrongFiducialCountThrows) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildRect(100));
    const std::vector<Eigen::Vector3d> tooFew(5, Eigen::Vector3d::Zero());
    EXPECT_THROW(registerArrayToFiducials(data, tooFew), std::invalid_argument);
}

TEST(RegisterCurrentTransducerPosition, NoSliderDeltaMatchesPlainFiducialFit) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildRect(100));
    const std::vector<FiducialMarker> originFiducials = setArrayFiducialMarkers(data);
    const Eigen::Vector3d shiftM(0.005, -0.003, 0.002);
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const FiducialMarker& m : originFiducials) mriFiducialsMm.push_back((m.position + shiftM) * 1000.0);

    const AffineArrayResult plain = registerArrayToFiducials(data, mriFiducialsMm);
    const AffineArrayResult withSliders = registerCurrentTransducerPosition(data, mriFiducialsMm, /*h=*/1.0, /*v=*/1.0);

    EXPECT_TRUE(withSliders.arrayData.arrayTotal.element[10].position.isApprox(
        plain.arrayData.arrayTotal.element[10].position, 1e-9));
}

TEST(RegisterCurrentTransducerPosition, VerticalSliderShiftsByExactDeltaMagnitude) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildRect(100));
    const std::vector<FiducialMarker> originFiducials = setArrayFiducialMarkers(data);
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const FiducialMarker& m : originFiducials) mriFiducialsMm.push_back(m.position * 1000.0);

    const AffineArrayResult base = registerCurrentTransducerPosition(data, mriFiducialsMm, 1.0, 1.0);
    const AffineArrayResult shifted = registerCurrentTransducerPosition(data, mriFiducialsMm, 1.0, 2.0);

    const Eigen::Vector3d delta =
        shifted.arrayData.arrayTotal.element[10].position - base.arrayData.arrayTotal.element[10].position;
    EXPECT_NEAR(delta.norm(), 0.010, 1e-9);  // 10mm verticalDelta / 1000
}

TEST(RegisterCurrentTransducerPosition, HorizontalSliderShiftsByExactDeltaMagnitude) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildRect(100));
    const std::vector<FiducialMarker> originFiducials = setArrayFiducialMarkers(data);
    std::vector<Eigen::Vector3d> mriFiducialsMm;
    for (const FiducialMarker& m : originFiducials) mriFiducialsMm.push_back(m.position * 1000.0);

    const AffineArrayResult base = registerCurrentTransducerPosition(data, mriFiducialsMm, 1.0, 1.0);
    const AffineArrayResult shifted = registerCurrentTransducerPosition(data, mriFiducialsMm, 2.0, 1.0);

    const Eigen::Vector3d delta =
        shifted.arrayData.arrayTotal.element[10].position - base.arrayData.arrayTotal.element[10].position;
    EXPECT_NEAR(delta.norm(), 0.0075, 1e-9);  // 7.5mm horizontalDelta / 1000
}
