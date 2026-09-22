// parity_registration -- runs the Beam Registration ports on synthetic
// point sets / fiducials / a synthetic array, writing results for
// matlab_verify/verify_registration.m. See matlab_verify/README.md.

#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/array_data.hpp"
#include "array/array_types.hpp"
#include "registration/affine_registration.hpp"
#include "registration/fiducial_markers.hpp"

using namespace beam::registration;

namespace {

Eigen::Matrix3d rotX(double a) {
    Eigen::Matrix3d m;
    m << 1, 0, 0, 0, std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a);
    return m;
}
Eigen::Matrix3d rotY(double a) {
    Eigen::Matrix3d m;
    m << std::cos(a), 0, std::sin(a), 0, 1, 0, -std::sin(a), 0, std::cos(a);
    return m;
}
Eigen::Matrix3d rotZ(double a) {
    Eigen::Matrix3d m;
    m << std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1;
    return m;
}

constexpr double kDeg = 3.14159265358979311599796346854 / 180.0;

Eigen::Matrix3d rTrue() {
    return rotZ(25.0 * kDeg) * rotY(15.0 * kDeg) * rotX(-10.0 * kDeg);
}

struct Results {
    std::ofstream out;
    explicit Results(const std::string& p) : out(p) { out << std::setprecision(17); }
    void v(const std::string& k, double x) { out << k << "," << x << "\n"; }
    void i(const std::string& k, long x) { out << k << "," << x << "\n"; }
    void mat(const std::string& k, const Eigen::MatrixXd& m) {
        for (Eigen::Index r = 0; r < m.rows(); ++r)
            for (Eigen::Index c = 0; c < m.cols(); ++c)
                v(k + "_" + std::to_string(r) + std::to_string(c), m(r, c));
    }
    void vec(const std::string& k, const Eigen::VectorXd& x) {
        for (Eigen::Index r = 0; r < x.size(); ++r) v(k + "_" + std::to_string(r), x(r));
    }
};

Eigen::Matrix<double, 3, 6> sourcePoints() {
    Eigen::Matrix<double, 3, 6> a;
    a << 0.10, 0.20, -0.15, 0.05, 0.30, -0.20,
         0.05, -0.10, 0.20, -0.25, 0.15, 0.10,
         0.12, 0.08, 0.30, 0.18, -0.05, 0.22;
    return a;
}

Eigen::MatrixXd buildRect(int n) {
    constexpr double spacing = 0.002, h = 0.0003;
    Eigen::MatrixXd rect = Eigen::MatrixXd::Zero(19, n);
    for (int i = 0; i < n; ++i) {
        const Eigen::Vector3d c(static_cast<double>(i + 1) * spacing, 0.001 * i, 0.0);
        rect(0, i) = i + 1;
        rect.block<3, 1>(1, i) = c + Eigen::Vector3d(-h, -h, 0);
        rect.block<3, 1>(4, i) = c + Eigen::Vector3d(h, -h, 0);
        rect.block<3, 1>(7, i) = c + Eigen::Vector3d(h, h, 0);
        rect.block<3, 1>(10, i) = c + Eigen::Vector3d(-h, h, 0);
        rect.block<3, 1>(16, i) = c;
    }
    return rect;
}

std::vector<FiducialMarker> baseFiducials() {
    const Eigen::Matrix3d R = rTrue();
    auto mk = [&](const std::string& name, double x, double y, double z) {
        FiducialMarker f;
        f.name = name;
        f.position = R * Eigen::Vector3d(x, y, z);
        return f;
    };
    return {mk("LeftY1Z3", -0.02, 0.0, 0.02),   mk("LeftY1Z1", -0.02, 0.0, 0.0),
            mk("LeftY4Z1", -0.02, -0.0225, 0.0), mk("RightY1Z3", 0.02, 0.0, 0.02),
            mk("RightY1Z1", 0.02, 0.0, 0.0),     mk("RightY4Z1", 0.02, -0.0225, 0.0)};
}

int run() {
    Results r("parity_registration_cpp.csv");

    const Eigen::Matrix<double, 3, 6> A = sourcePoints();
    const Eigen::Vector3d tTrue(0.010, -0.005, 0.003);
    const Eigen::Matrix<double, 3, 6> B = (rTrue() * A).colwise() + tTrue;

    // --- affineRegistration, unweighted ---
    const AffineRegistrationResult reg = affineRegistration(A, B);
    r.mat("reg_R", reg.r);
    r.vec("reg_t", reg.t);
    r.vec("reg_q", reg.q);

    // --- affineRegistration, weighted (BeamV0's fixed weights) ---
    Eigen::VectorXd w(6);
    w << 0.5, 1, 0.5, 0.5, 1, 0.5;
    const AffineRegistrationResult regW = affineRegistration(A, B, w);
    r.mat("regW_R", regW.r);
    r.vec("regW_t", regW.t);

    // --- getAffineMatrixFromRegistration ---
    const Eigen::Matrix4d m0 = getAffineMatrixFromRegistration(A, B, false);
    r.mat("affMat", m0.topRows<3>());
    const Eigen::Matrix4d m1 = getAffineMatrixFromRegistration(A, B, true);
    r.vec("affMatMm_t", m1.block<3, 1>(0, 3));

    // --- fiducial-basis math ---
    const std::vector<FiducialMarker> fids = baseFiducials();
    const TransducerBasis basis = getTranslationMatrixFromTransducerFiducials(fids);
    r.mat("transBasis", basis.m);
    r.vec("transBasis_x", basis.xVector);
    r.vec("transBasis_y", basis.yVector);
    r.vec("transBasis_z", basis.zVector);
    r.vec("fidByName_RightY1Z1", getFiducialPositionFromName("RightY1Z1", fids));

    // --- applyAffineMatrixToFiducialMarkers ---
    Eigen::Matrix4d aff = Eigen::Matrix4d::Identity();
    aff.topLeftCorner<3, 3>() = rTrue();
    aff.block<3, 1>(0, 3) = Eigen::Vector3d(0.1, -0.2, 0.3);
    const std::vector<FiducialMarker> movedFids = applyAffineMatrixToFiducialMarkers(aff, fids);
    r.vec("movedFid0", movedFids.front().position);
    r.vec("movedFid5", movedFids.back().position);

    // --- setArrayFiducialMarkers (needs a >=81-element array) ---
    const beam::array::ArrayData arrayData = beam::array::defineArrayData(buildRect(90));
    const std::vector<FiducialMarker> arrFids = setArrayFiducialMarkers(arrayData);
    r.i("arrFids_count", static_cast<long>(arrFids.size()));
    for (size_t k = 0; k < arrFids.size(); ++k) r.vec("arrFid_" + arrFids[k].name, arrFids[k].position);

    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parity_registration: %s\n", e.what());
        return 1;
    }
}
