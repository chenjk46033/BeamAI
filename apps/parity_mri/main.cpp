// parity_mri -- runs the Beam MRI coordinate-math ports (getRasXformFromHeader,
// getRasAxisVectors, getSliceImage) on synthetic NIfTI headers + a synthetic
// volume, writing results for matlab_verify/verify_mri.m. See
// matlab_verify/README.md.

#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <string>

#include <Eigen/Core>

#include "mri/nifti_header.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

using namespace beam::mri;

namespace {

struct Results {
    std::ofstream out;
    explicit Results(const std::string& p) : out(p) { out << std::setprecision(17); }
    void v(const std::string& k, double x) { out << k << "," << x << "\n"; }
    void i(const std::string& k, long x) { out << k << "," << x << "\n"; }
};

NiftiHeader qformHeader() {
    NiftiHeader h;
    h.dim = {3, 4, 5, 6, 0, 0, 0, 0};
    h.pixdim = {1.0, 0.9, 1.1, 1.3, 0, 0, 0, 0};
    h.qformCode = 1;
    h.quaternB = 0.1;
    h.quaternC = -0.2;
    h.quaternD = 0.3;
    h.qoffsetX = -90.0;
    h.qoffsetY = -126.0;
    h.qoffsetZ = -72.0;
    return h;
}

NiftiHeader sformHeader() {
    NiftiHeader h;
    h.dim = {3, 4, 5, 6, 0, 0, 0, 0};
    h.pixdim = {1.0, 0.9, 1.1, 1.3, 0, 0, 0, 0};
    h.sformCode = 1;
    h.srowX = Eigen::RowVector4d(0.9, 0.02, -0.01, -90.0);
    h.srowY = Eigen::RowVector4d(-0.02, 1.1, 0.03, -126.0);
    h.srowZ = Eigen::RowVector4d(0.01, -0.03, 1.3, -72.0);
    return h;
}

void writeXform(Results& r, const std::string& prefix, const RasXform& x) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) r.v(prefix + "_r" + std::to_string(i) + "c" + std::to_string(j), x(i, j));
}

void writeAxes(Results& r, const std::string& prefix, const RasAxisVectors& a) {
    r.v(prefix + "_LR_first", a.dimLR(0));
    r.v(prefix + "_LR_last", a.dimLR(a.dimLR.size() - 1));
    r.v(prefix + "_AP_first", a.dimAP(0));
    r.v(prefix + "_AP_last", a.dimAP(a.dimAP.size() - 1));
    r.v(prefix + "_IS_first", a.dimIS(0));
    r.v(prefix + "_IS_last", a.dimIS(a.dimIS.size() - 1));
}

Volume3D rampVolume(Eigen::Index nx, Eigen::Index ny, Eigen::Index nz) {
    Volume3D v;
    v.nx = nx;
    v.ny = ny;
    v.nz = nz;
    v.kSlices.resize(static_cast<size_t>(nz));
    for (Eigen::Index k = 0; k < nz; ++k) {
        Eigen::MatrixXd s(nx, ny);
        for (Eigen::Index i = 0; i < nx; ++i)
            for (Eigen::Index j = 0; j < ny; ++j)
                s(i, j) = static_cast<double>(i) * 10000.0 + static_cast<double>(j) * 100.0 +
                          static_cast<double>(k);
        v.kSlices[static_cast<size_t>(k)] = s;
    }
    return v;
}

void writeSlice(Results& r, const std::string& prefix, const Eigen::MatrixXd& s) {
    r.i(prefix + "_rows", s.rows());
    r.i(prefix + "_cols", s.cols());
    r.v(prefix + "_00", s(0, 0));
    r.v(prefix + "_0last", s(0, s.cols() - 1));
    r.v(prefix + "_last0", s(s.rows() - 1, 0));
    r.v(prefix + "_lastlast", s(s.rows() - 1, s.cols() - 1));
    r.v(prefix + "_11", s(1, 1));
}

int run() {
    Results r("parity_mri_cpp.csv");

    const NiftiHeader hq = qformHeader();
    writeXform(r, "qform", getRasXformFromHeader(hq));
    writeAxes(r, "qaxes", getRasAxisVectors(hq));

    NiftiHeader hqn = qformHeader();
    hqn.pixdim[0] = -1.0;  // qfac
    writeXform(r, "qformNeg", getRasXformFromHeader(hqn));

    const NiftiHeader hs = sformHeader();
    writeXform(r, "sform", getRasXformFromHeader(hs));
    writeAxes(r, "saxes", getRasAxisVectors(hs));

    const Volume3D vol = rampVolume(4, 5, 6);
    writeSlice(r, "sagital", getSliceImage(vol, 2, "sagital"));
    writeSlice(r, "coronal", getSliceImage(vol, 3, "coronal"));
    writeSlice(r, "axial", getSliceImage(vol, 4, "axial"));

    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parity_mri: %s\n", e.what());
        return 1;
    }
}
