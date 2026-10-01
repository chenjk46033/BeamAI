#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <Eigen/LU>  // for Matrix3d::determinant()
#include <gtest/gtest.h>

#include "array/affine.hpp"
#include "array/array_data.hpp"
#include "array/array_struct.hpp"
#include "array/geometry.hpp"
#include "registration/fiducial_markers.hpp"

using namespace beam::array;

namespace {

// Builds a synthetic rect (see array_types.hpp for row layout) with `n`
// elements laid out along the x-axis, spaced `spacing` apart, each a small
// square in the local xy-plane (so its normal points along +/-z) --
// synthetic, hand-computable data, not real BeamV0 array geometry.
Eigen::MatrixXd buildSyntheticRect(int n, double spacing = 0.002, double halfSize = 0.0003) {
    Eigen::MatrixXd rect(19, n);
    for (int i = 0; i < n; ++i) {
        const Eigen::Vector3d center(static_cast<double>(i + 1) * spacing, 0, 0);
        const Eigen::Vector3d c1 = center + Eigen::Vector3d(-halfSize, -halfSize, 0);
        const Eigen::Vector3d c2 = center + Eigen::Vector3d(halfSize, -halfSize, 0);
        const Eigen::Vector3d c3 = center + Eigen::Vector3d(halfSize, halfSize, 0);
        const Eigen::Vector3d c4 = center + Eigen::Vector3d(-halfSize, halfSize, 0);
        rect(0, i) = i + 1;  // element number, 1-based
        rect.block<3, 1>(1, i) = c1;
        rect.block<3, 1>(4, i) = c2;
        rect.block<3, 1>(7, i) = c3;
        rect.block<3, 1>(10, i) = c4;
        rect.block<3, 1>(13, i).setZero();
        rect.block<3, 1>(16, i) = center;
    }
    return rect;
}

}  // namespace

TEST(Affine, TranslateMovesPoint) {
    const Eigen::Matrix4d m = translateAffineMatrix(Eigen::Vector3d(1, 2, 3));
    const Eigen::Vector4d p(5, 5, 5, 1);
    const Eigen::Vector4d out = m * p;
    EXPECT_TRUE(out.head<3>().isApprox(Eigen::Vector3d(6, 7, 8)));
}

TEST(Affine, ZRotNinetyDegreesMapsXToY) {
    const Eigen::Matrix4d m = zRotAffineMatrix(std::numbers::pi / 2.0);
    const Eigen::Vector4d p(1, 0, 0, 1);
    const Eigen::Vector4d out = m * p;
    EXPECT_NEAR(out.x(), 0.0, 1e-12);
    EXPECT_NEAR(out.y(), 1.0, 1e-12);
    EXPECT_NEAR(out.z(), 0.0, 1e-12);
}

TEST(Affine, RotationBlocksAreOrthonormalWithUnitDeterminant) {
    for (const Eigen::Matrix4d& m : {xRotAffineMatrix(0.37), yRotAffineMatrix(1.1), zRotAffineMatrix(-0.6)}) {
        const Eigen::Matrix3d r = m.block<3, 3>(0, 0);
        EXPECT_TRUE((r.transpose() * r).isApprox(Eigen::Matrix3d::Identity(), 1e-10));
        EXPECT_NEAR(r.determinant(), 1.0, 1e-10);
    }
}

TEST(Affine, ApplyAffineToRectTranslatesCornersAndCenter) {
    Eigen::MatrixXd rect = buildSyntheticRect(1);
    const Eigen::Vector3d before = rectCenter(rect, 0);
    const Eigen::Matrix4d translate = translateAffineMatrix(Eigen::Vector3d(0.01, 0, 0));

    const Eigen::MatrixXd moved = applyAffineToRect(translate, rect);

    EXPECT_TRUE(rectCenter(moved, 0).isApprox(before + Eigen::Vector3d(0.01, 0, 0)));
    EXPECT_TRUE(rectCorner(moved, 0, 1).isApprox(rectCorner(rect, 0, 1) + Eigen::Vector3d(0.01, 0, 0)));
    EXPECT_DOUBLE_EQ(moved(0, 0), rect(0, 0));  // element number row untouched
}

TEST(Geometry, NormalVectorFrom3PointsPicksZeroFacingDirection) {
    // p1 at the origin makes dot(-p1, n) == 0 for both candidates, so the
    // ported comparison (strictly-greater) always falls through to the
    // second candidate -- this pins that exact, otherwise easy-to-flip
    // tie-breaking behavior.
    const Eigen::Vector3d p1(0, 0, 0);
    const Eigen::Vector3d p2(1, 0, 0);
    const Eigen::Vector3d p3(0, 1, 0);
    const Eigen::Vector3d n = normalVectorFrom3Points(p1, p2, p3);
    EXPECT_TRUE(n.isApprox(Eigen::Vector3d(0, 0, 1)));
}

TEST(Geometry, CalculateRectNormalVectorIsUnitZForSyntheticElement) {
    Eigen::MatrixXd rect = buildSyntheticRect(1);
    const Eigen::Vector3d n = calculateRectNormalVector(rect, 1);
    EXPECT_NEAR(n.norm(), 1.0, 1e-12);
    EXPECT_NEAR(std::abs(n.z()), 1.0, 1e-9);  // square lies in the xy-plane
}

TEST(Geometry, SpatiallySampleElementCornerMatchesFirstSample) {
    // Corners only -- doesn't need a full ArrayStruct (defineArrayStruct
    // needs >=28 elements for getReceiveElements to find an opposing
    // element, irrelevant to what's under test here).
    Eigen::Matrix<double, 3, 4> corners;
    corners.col(0) = Eigen::Vector3d(-0.0003, -0.0003, 0);
    corners.col(1) = Eigen::Vector3d(0.0003, -0.0003, 0);
    corners.col(2) = Eigen::Vector3d(0.0003, 0.0003, 0);
    corners.col(3) = Eigen::Vector3d(-0.0003, 0.0003, 0);

    const Eigen::MatrixXd points = spatiallySampleElement(corners, 0.0003);
    ASSERT_GT(points.rows(), 0);
    // t=0, r=0 sample equals v1 == corners.col(0), per the ported formula.
    EXPECT_TRUE(points.row(0).transpose().isApprox(corners.col(0)));
}

TEST(Geometry, GetElementPositionsFromArrayStructReturnsRowPerElement) {
    ArrayStruct s;
    s.element.resize(3);
    s.element[0].position = Eigen::Vector3d(1, 2, 3);
    s.element[1].position = Eigen::Vector3d(-4, 5, -6);
    s.element[2].position = Eigen::Vector3d(0.1, 0.2, 0.3);

    const Eigen::MatrixXd p = getElementPositionsFromArrayStruct(s);
    ASSERT_EQ(p.rows(), 3);
    ASSERT_EQ(p.cols(), 3);
    EXPECT_TRUE(p.row(0).transpose().isApprox(Eigen::Vector3d(1, 2, 3)));
    EXPECT_TRUE(p.row(2).transpose().isApprox(Eigen::Vector3d(0.1, 0.2, 0.3)));
}

TEST(ArrayData, GetOpposingElementsReturnsOtherArraysElementNumbers) {
    // Two distinct arrays: numbers 1..3 and 4..6. rect row 0 carries the
    // element numbers (the row getOpposingElements concatenates from).
    ArrayData data;
    auto fill = [](ArrayStruct& a, std::initializer_list<int> numbers) {
        a.rect = Eigen::MatrixXd::Zero(19, static_cast<Eigen::Index>(numbers.size()));
        Eigen::Index c = 0;
        for (int num : numbers) {
            a.rect(0, c) = num;
            ArrayElement el;
            el.number = num;
            a.element.push_back(el);
            ++c;
        }
    };
    fill(data.array[0], {1, 2, 3});
    fill(data.array[1], {4, 5, 6});

    EXPECT_EQ(getOpposingElements(data, 2), (std::vector<int>{4, 5, 6}));
    EXPECT_EQ(getOpposingElements(data, 5), (std::vector<int>{1, 2, 3}));
    EXPECT_THROW(getOpposingElements(data, 99), std::invalid_argument);
}

TEST(ArrayData, DefineArrayTxElementsModes) {
    EXPECT_EQ(defineArrayTxElements("first").txElements[0].size(), 126u);
    EXPECT_EQ(defineArrayTxElements("second").txElements[0].front(), 129);
    EXPECT_EQ(defineArrayTxElements("both").txElements[0].size(), 252u);
    EXPECT_EQ(defineArrayTxElements("firstThenSecond").txElements.size(), 2u);
    EXPECT_THROW(defineArrayTxElements("nonsense"), std::invalid_argument);
}

TEST(ArrayData, ArrayElementsToVSXElementsSkipsElements127And128) {
    const std::vector<int> mapped = arrayElementsToVSXElements({1, 126, 127});
    EXPECT_EQ(mapped, (std::vector<int>{1, 126, 129}));
}

TEST(ArrayData, DefineArrayDataUsesBeamsRealConstants) {
    Eigen::MatrixXd rect = buildSyntheticRect(60);
    const ArrayData data = defineArrayData(rect);

    EXPECT_DOUBLE_EQ(data.arrayTotal.frequency, 150000.0);
    EXPECT_DOUBLE_EQ(data.arrayTotal.elementDimensions[0], 0.06);
    EXPECT_DOUBLE_EQ(data.arrayTotal.elementDimensions[1], 0.06);
    EXPECT_EQ(data.array[0].elementMapping, 1);
    EXPECT_EQ(data.array[1].elementMapping, 2);
    // Ported quirk (see docs/known_gaps.md): both sub-arrays are full
    // copies of the same struct, not actually split.
    EXPECT_TRUE(data.array[0].rect.isApprox(data.arrayTotal.rect));
    EXPECT_EQ(data.arrayTotal.element.size(), 60u);

    for (const ArrayElement& el : data.arrayTotal.element) {
        EXPECT_NE(el.opposingElement, 0);
        EXPECT_GE(el.opposingElement, 1);
        EXPECT_LE(el.opposingElement, 60);
        EXPECT_NEAR(el.normalVector.norm(), 1.0, 1e-9);
    }
}

// --- reconstructPhysicalArrayHalves ------------------------------------
//
// defineArrayData.m leaves array(1) and array(2) as copies of the whole
// array. setArrayFiducialMarkers.m centres each marker on
// array(designation).rect, so the halves must be split first or all six
// fiducials collapse around one shared centre. BeamV0 never hits this --
// its sys .mat ships the halves -- which is exactly why nothing in the
// MATLAB source performs the split and why it is easy to omit here.

namespace {

// BeamV0's own nominal fiducial positions, read out of a running BeamV0
// (app.sys.originArrayData.fiducialMarkers, loaded from
// DefaultSubjectV0/defaultSubjectMNIV1.mat) and converted to millimetres.
// These are the reference BeamAI's reconstructed halves must reproduce.
struct NamedPointMm {
    const char* name;
    double x, y, z;
};
constexpr NamedPointMm kBeamV0NominalFiducialsMm[] = {
    {"LeftY1Z3", -108.73400115966791, 33.995002746582031, 13.499999999999984},
    {"LeftY1Z1", -108.73400115966791, 33.995002746582031, -6.5000000000000169},
    {"LeftY4Z1", -108.73400115966791, 11.495002746582029, -6.500000000000008},
    {"RightY1Z3", 109.26599884033189, 33.995002746582031, 13.499999999999995},
    {"RightY1Z1", 109.26599884033189, 33.995002746582031, -6.5000000000000053},
    {"RightY4Z1", 109.26599884033189, 11.495002746582029, -6.4999999999999973},
};

// Reads the 19-row rect CSV, or returns an empty matrix when the sibling
// DefaultSubjectV0 checkout is not present.
Eigen::MatrixXd readDefaultSubjectRect() {
    std::ifstream file(BEAM_DEFAULT_SUBJECT_RECT_CSV);
    if (!file) return {};
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::string cell;
        std::vector<double> row;
        while (std::getline(stream, cell, ',')) row.push_back(std::stod(cell));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 19 || rows.front().empty()) return {};
    Eigen::MatrixXd rect(19, static_cast<Eigen::Index>(rows.front().size()));
    for (Eigen::Index r = 0; r < 19; ++r)
        for (Eigen::Index c = 0; c < rect.cols(); ++c)
            rect(r, c) = rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
    return rect;
}

}  // namespace

TEST(ReconstructPhysicalArrayHalves, SplitsTheRealGeometryIntoTwoEqualPanels) {
    const Eigen::MatrixXd rect = readDefaultSubjectRect();
    if (rect.size() == 0) GTEST_SKIP() << "DefaultSubjectV0 geometry not available";

    ArrayData data = defineArrayData(rect);
    // defineArrayData alone duplicates the whole array into both slots.
    ASSERT_EQ(data.array[0].rect.cols(), rect.cols());
    ASSERT_EQ(data.array[1].rect.cols(), rect.cols());

    reconstructPhysicalArrayHalves(data);
    EXPECT_EQ(data.array[0].rect.cols() + data.array[1].rect.cols(), rect.cols());
    EXPECT_EQ(data.array[0].rect.cols(), data.array[1].rect.cols());
    EXPECT_EQ(data.array[0].elementMapping, 1);
    EXPECT_EQ(data.array[1].elementMapping, 2);

    // Designation 1 is subject-Right, so its panel centre must sit at the
    // greater LR coordinate.
    const double rightX = data.array[0].rect.row(kRectCenterStartRow).mean();
    const double leftX = data.array[1].rect.row(kRectCenterStartRow).mean();
    EXPECT_GT(rightX, leftX);
}

TEST(ReconstructPhysicalArrayHalves, ReproducesBeamV0sNominalFiducialsFromTheRectCsv) {
    const Eigen::MatrixXd rect = readDefaultSubjectRect();
    if (rect.size() == 0) GTEST_SKIP() << "DefaultSubjectV0 geometry not available";

    ArrayData data = defineArrayData(rect);
    reconstructPhysicalArrayHalves(data);
    const std::vector<beam::registration::FiducialMarker> markers =
        beam::registration::setArrayFiducialMarkers(data);

    ASSERT_EQ(markers.size(), std::size(kBeamV0NominalFiducialsMm));
    for (std::size_t i = 0; i < markers.size(); ++i) {
        const NamedPointMm& expected = kBeamV0NominalFiducialsMm[i];
        EXPECT_EQ(markers[i].name, expected.name);
        const Eigen::Vector3d actualMm = markers[i].position * 1000.0;
        // 1e-9 mm = 1 picometre; the two paths should agree to rounding.
        EXPECT_NEAR(actualMm.x(), expected.x, 1e-9) << "marker " << expected.name;
        EXPECT_NEAR(actualMm.y(), expected.y, 1e-9) << "marker " << expected.name;
        EXPECT_NEAR(actualMm.z(), expected.z, 1e-9) << "marker " << expected.name;
    }
}

TEST(ReconstructPhysicalArrayHalves, OmittingItCollapsesTheTwoPanelsOntoOneCentre) {
    // Guards the reason the split exists: skipping it is not a cosmetic
    // difference, it moves every fiducial by ~90 mm in LR.
    const Eigen::MatrixXd rect = readDefaultSubjectRect();
    if (rect.size() == 0) GTEST_SKIP() << "DefaultSubjectV0 geometry not available";

    const ArrayData unsplit = defineArrayData(rect);
    const std::vector<beam::registration::FiducialMarker> wrong =
        beam::registration::setArrayFiducialMarkers(unsplit);

    ArrayData split = defineArrayData(rect);
    reconstructPhysicalArrayHalves(split);
    const std::vector<beam::registration::FiducialMarker> right =
        beam::registration::setArrayFiducialMarkers(split);

    const auto spanLr = [](const std::vector<beam::registration::FiducialMarker>& m) {
        return std::abs(m[3].position.x() - m[0].position.x()) * 1000.0;  // RightY1Z3 - LeftY1Z3
    };
    EXPECT_NEAR(spanLr(wrong), 38.0, 0.5);    // ~2 * absX, both panels on one centre
    EXPECT_NEAR(spanLr(right), 218.0, 0.5);   // the real panel separation
    EXPECT_GT(spanLr(right) - spanLr(wrong), 150.0);
}
