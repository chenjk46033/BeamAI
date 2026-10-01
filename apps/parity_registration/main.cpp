// parity_registration -- runs the Beam Registration ports on synthetic
// point sets / fiducials / a synthetic array, writing results for
// matlab_verify/verify_registration.m. See matlab_verify/README.md.

#include <cmath>
#include <array>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/array_data.hpp"
#include "array/array_struct.hpp"
#include "array/array_types.hpp"
#include "registration/affine_registration.hpp"
#include "registration/array_transform.hpp"
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

// The 160-element array both apps load. Resolved relative to the parity
// work directory (build-ai/parity_registration) or the repo root.
std::string findGeometryCsv() {
    const char* candidates[] = {
        "../../../DefaultSubjectV0/defaultSubjectArrayRect.csv",
        "../../DefaultSubjectV0/defaultSubjectArrayRect.csv",
        "../DefaultSubjectV0/defaultSubjectArrayRect.csv",
    };
    for (const char* c : candidates) {
        std::ifstream probe(c);
        if (probe) return c;
    }
    throw std::runtime_error("defaultSubjectArrayRect.csv not found from the parity work directory");
}

Eigen::MatrixXd readRectCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
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
    if (rows.size() != 19 || rows.front().empty()) throw std::runtime_error("invalid rect CSV: " + path);
    Eigen::MatrixXd result(19, static_cast<Eigen::Index>(rows.front().size()));
    for (Eigen::Index i = 0; i < 19; ++i)
        for (Eigen::Index j = 0; j < result.cols(); ++j)
            result(i, j) = rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    return result;
}

// Reads the six measured fiducials (mm, RAS) from the checked-in fixture,
// ordered to match `order` by NAME rather than by row position, so the file
// stays correct even if the two sides ever build their marker lists
// differently. Throws rather than falling back: a parity check that silently
// registers from a partial or missing input is worse than one that stops.
std::vector<Eigen::Vector3d> readFiducialFixture(const std::string& path,
                                                 const std::vector<FiducialMarker>& order) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open fiducial fixture: " + path);

    std::map<std::string, Eigen::Vector3d> byName;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#' || line.rfind("name,", 0) == 0) continue;
        std::stringstream stream(line);
        std::string name, x, y, z, extra;
        if (!std::getline(stream, name, ',') || !std::getline(stream, x, ',') ||
            !std::getline(stream, y, ',') || !std::getline(stream, z, ',') ||
            std::getline(stream, extra, ','))
            throw std::runtime_error("expected name,x,y,z in " + path + ": " + line);
        byName.emplace(name, Eigen::Vector3d(std::stod(x), std::stod(y), std::stod(z)));
    }

    std::vector<Eigen::Vector3d> result;
    result.reserve(order.size());
    for (const FiducialMarker& marker : order) {
        const auto found = byName.find(marker.name);
        if (found == byName.end())
            throw std::runtime_error(path + " has no row for fiducial " + marker.name);
        result.push_back(found->second);
    }
    return result;
}

void writeRect(const std::string& path, const Eigen::MatrixXd& rect) {
    std::ofstream out(path);
    out << std::setprecision(17);
    for (Eigen::Index i = 0; i < rect.rows(); ++i) {
        for (Eigen::Index j = 0; j < rect.cols(); ++j) out << (j ? "," : "") << rect(i, j);
        out << "\n";
    }
}

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

    // --- end-to-end registration, the two buttons an operator presses ---
    // Six measured fiducials in millimetres, read from the checked-in fixture
    // testdata/beamai_fiducials_F040_T1_MRI.csv -- a real BeamAI session on
    // BEAM MRIs/F040/T1_MRI. verify_registration.m reads the SAME file, so the
    // comparison has one source of truth for its input and no literals on
    // either side to drift apart. (The app also writes beamai_fiducials.csv
    // into its working directory on every Confirm; that one is deliberately
    // not used here, because it changes meaning whenever someone measures a
    // different subject.)
    //
    // It drives BeamV0's own registerArrayToFiducials.m and the MRI-Based
    // branch of registerCurrentTransducerPostion.m from the identical six
    // points and the identical array, so these labels compare the whole chain
    // rather than its pieces.
    const std::vector<Eigen::Vector3d> mriFiducialsMm =
        readFiducialFixture(BEAM_FIDUCIAL_FIXTURE_CSV, arrFids);
    const auto centreMm = [](const beam::array::ArrayData& d) {
        return Eigen::Vector3d(d.arrayTotal.rect.block(beam::array::kRectCenterStartRow, 0, 3,
                                                        d.arrayTotal.rect.cols())
                                   .rowwise()
                                   .mean() *
                               1000.0);
    };

    const AffineArrayResult fitted = registerArrayToFiducials(arrayData, mriFiducialsMm);
    r.vec("e2e_centreAfterFitMm", centreMm(fitted.arrayData));
    for (const FiducialMarker& m : fitted.fiducialMarkers)
        r.vec("e2e_fittedFid_" + m.name, m.position);

    // Same lock-position readings on both sides (BeamV0's sliders are 1-based,
    // so 1,1 is "no offset").
    constexpr double kH = 3.0;
    constexpr double kV = 3.0;
    const AffineArrayResult locked = registerCurrentTransducerPosition(arrayData, mriFiducialsMm, kH, kV);
    r.vec("e2e_centreAfterLockMm", centreMm(locked.arrayData));


    // --- end-to-end on the REAL transducer geometry ---------------------
    // Everything above uses the synthetic buildRect(90). This block repeats
    // the two registration steps on DefaultSubjectV0/defaultSubjectArrayRect.csv
    // -- the 160-element array the apps actually load -- so the comparison
    // covers the geometry an operator really registers. The fit is rigid and
    // maps the array's own fiducials onto the measured ones, so the array's
    // initial placement in the MRI cancels out and is not needed here.
    const std::string geometryPath = findGeometryCsv();
    const Eigen::MatrixXd realRect = readRectCsv(geometryPath);
    beam::array::ArrayData realArray = beam::array::defineArrayData(realRect);
    beam::array::reconstructPhysicalArrayHalves(realArray);
    const Eigen::Index nElements = realArray.arrayTotal.rect.cols();
    r.i("real_nElements", static_cast<long>(nElements));

    const AffineArrayResult realFit = registerArrayToFiducials(realArray, mriFiducialsMm);
    r.vec("real_centreAfterFitMm", centreMm(realFit.arrayData));
    for (const FiducialMarker& m : realFit.fiducialMarkers)
        r.vec("real_fittedFid_" + m.name, m.position);

    const AffineArrayResult realLock =
        registerCurrentTransducerPosition(realArray, mriFiducialsMm, kH, kV);
    r.vec("real_centreAfterLockMm", centreMm(realLock.arrayData));

    // Every element centre, both stages -- the whole registered geometry,
    // not just its mean.
    for (Eigen::Index c = 0; c < nElements; ++c) {
        const std::string tag = std::to_string(c);
        for (int k = 0; k < 3; ++k) {
            r.v("real_fitElem_" + tag + "_" + std::to_string(k),
                realFit.arrayData.arrayTotal.rect(beam::array::kRectCenterStartRow + k, c));
            r.v("real_lockElem_" + tag + "_" + std::to_string(k),
                realLock.arrayData.arrayTotal.rect(beam::array::kRectCenterStartRow + k, c));
        }
    }

    // Full 19xN rects for the overlay renderer (corners included, so the
    // panels can be drawn rather than scattered).
    writeRect("beamai_rect_fit.csv", realFit.arrayData.arrayTotal.rect);
    writeRect("beamai_rect_lock.csv", realLock.arrayData.arrayTotal.rect);

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
