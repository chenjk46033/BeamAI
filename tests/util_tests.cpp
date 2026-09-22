#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "util/camera.hpp"
#include "util/geometry_math.hpp"

using beam::util::addVectors;
using beam::util::angleBetweenTwoVectors;
using beam::util::cam2targetSpace;
using beam::util::convertDelaysToCycles;
using beam::util::distancePointToLine;
using beam::util::mm2pixel;
using beam::util::mm3Dpnp;
using beam::util::rotm2eulSimple;
using beam::util::vectorRange;

TEST(VectorRange, KnownValue) {
    Eigen::VectorXd v(4);
    v << 1.0, 5.0, 3.0, -2.0;
    EXPECT_DOUBLE_EQ(vectorRange(v), 7.0);
}

TEST(DistancePointToLine, PointOffAxisAlignedLine) {
    // Line along the x-axis through the origin; point (0,5,0) is 5 away.
    const Eigen::Vector3d lineV(1, 0, 0);
    const Eigen::Vector3d lineQ(0, 0, 0);
    const Eigen::Vector3d point(0, 5, 0);
    EXPECT_DOUBLE_EQ(distancePointToLine(lineV, lineQ, point), 5.0);
}

TEST(DistancePointToLine, PointOnLine) {
    const Eigen::Vector3d lineV(1, 0, 0);
    const Eigen::Vector3d lineQ(0, 0, 0);
    const Eigen::Vector3d point(3, 0, 0);
    EXPECT_NEAR(distancePointToLine(lineV, lineQ, point), 0.0, 1e-12);
}

TEST(AddVectors, ElementwiseSum) {
    Eigen::VectorXd a(3);
    a << 1.0, 2.0, 3.0;
    Eigen::VectorXd b(3);
    b << 10.0, 20.0, 30.0;
    Eigen::VectorXd expected(3);
    expected << 11.0, 22.0, 33.0;
    EXPECT_TRUE(addVectors(a, b).isApprox(expected));
}

TEST(AngleBetweenTwoVectors, OrthogonalIsNinetyDegrees) {
    EXPECT_NEAR(angleBetweenTwoVectors(Eigen::Vector3d(1, 0, 0), Eigen::Vector3d(0, 1, 0)), 90.0, 1e-12);
}

TEST(AngleBetweenTwoVectors, ParallelIsZeroDespiteRoundoff) {
    // dot/(norm*norm) can land just above 1.0 for parallel vectors; the
    // clamp must keep acos real (MATLAB's max(min(...,1),-1) guard).
    const Eigen::Vector3d v(0.1, 0.2, 0.3);
    EXPECT_NEAR(angleBetweenTwoVectors(v, 2.0 * v), 0.0, 1e-9);
    EXPECT_NEAR(angleBetweenTwoVectors(v, -v), 180.0, 1e-9);
}

TEST(AngleBetweenTwoVectors, ZeroVectorGivesZeroLikeMatlab) {
    // 0/0 -> MATLAB min(NaN, 1) == 1 -> acos(1) == 0 (not NaN).
    EXPECT_DOUBLE_EQ(angleBetweenTwoVectors(Eigen::Vector3d::Zero(), Eigen::Vector3d(1, 2, 3)), 0.0);
}

TEST(ConvertDelaysToCycles, ScalesBySingleFrequency) {
    Eigen::VectorXd delays(3);
    delays << 1e-6, 2e-6, 5e-6;
    Eigen::VectorXd expected(3);
    expected << 0.15, 0.30, 0.75;  // * 150 kHz
    EXPECT_TRUE(convertDelaysToCycles(delays, 150000.0).isApprox(expected));
}

namespace {
Eigen::Matrix3d rotZ(double rad) {
    Eigen::Matrix3d r;
    r << std::cos(rad), -std::sin(rad), 0, std::sin(rad), std::cos(rad), 0, 0, 0, 1;
    return r;
}
}  // namespace

TEST(Rotm2EulSimple, IdentityAndYaw) {
    EXPECT_TRUE(rotm2eulSimple(Eigen::Matrix3d::Identity()).isApprox(Eigen::Vector3d::Zero()) ||
                rotm2eulSimple(Eigen::Matrix3d::Identity()).norm() < 1e-12);
    const Eigen::Vector3d eul = rotm2eulSimple(rotZ(0.6));
    EXPECT_NEAR(eul(0), 0.6, 1e-12);  // yaw
    EXPECT_NEAR(eul(1), 0.0, 1e-12);  // pitch
    EXPECT_NEAR(eul(2), 0.0, 1e-12);  // roll
}

TEST(Cam2TargetSpace, IdentityAndTranslation) {
    Eigen::MatrixX3d pts(2, 3);
    pts << 1, 2, 3, 4, 5, 6;
    EXPECT_TRUE(cam2targetSpace(pts, Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero()).isApprox(pts));

    const Eigen::MatrixX3d shifted =
        cam2targetSpace(pts, Eigen::Matrix3d::Identity(), Eigen::Vector3d(1, 0, 0));
    Eigen::MatrixX3d expected(2, 3);
    expected << 0, 2, 3, 3, 5, 6;
    EXPECT_TRUE(shifted.isApprox(expected));
}

TEST(Mm2Pixel, ScaleAndOffset) {
    Eigen::MatrixXd mm(1, 2);
    mm << 10, 20;
    Eigen::RowVectorXd origin(2);
    origin << 1, 1;
    Eigen::MatrixXd expected(1, 2);
    expected << 6, 11;
    EXPECT_TRUE(mm2pixel(mm, 2.0, origin).isApprox(expected));
}

TEST(Mm3Dpnp, RaysHitPlane) {
    Eigen::MatrixX2d px(2, 2);
    px << 0, 0, 1, 0;
    const Eigen::MatrixX3d out = mm3Dpnp(Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero(), px,
                                          Eigen::Matrix3d::Identity(), /*zRel=*/1.0);
    EXPECT_TRUE(out.row(0).transpose().isApprox(Eigen::Vector3d(0, 0, 1), 1e-12));
    EXPECT_TRUE(out.row(1).transpose().isApprox(Eigen::Vector3d(1, 0, 1), 1e-12));
}
