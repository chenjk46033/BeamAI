// parity_arrays -- runs the Beam C++ Arrays/Util ports on a synthetic rect
// and writes both the input (parity_rect.csv) and the results
// (parity_cpp.csv, "label,value") so matlab_verify/verify_arrays.m can run
// the real BeamV0 functions on the same input and matlab_verify/
// compare_parity.ps1 can diff the two. Not a unit test -- a MATLAB-parity
// harness. See matlab_verify/README.md.

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/affine.hpp"
#include "array/array_data.hpp"
#include "array/array_struct.hpp"
#include "array/array_types.hpp"
#include "array/geometry.hpp"
#include "util/camera.hpp"
#include "util/geometry_math.hpp"

using namespace beam::array;
using namespace beam::util;

namespace {

constexpr int kN = 100;

// Synthetic 19 x kN rect: elements along +x at z = 0.12, 2 mm pitch, 0.6 mm
// square. Not real BeamV0 geometry -- a parity check only needs both sides
// to compute the same thing on the same input.
Eigen::MatrixXd makeSyntheticRect() {
    constexpr double spacing = 0.002;
    constexpr double h = 0.0003;
    constexpr double z = 0.12;
    Eigen::MatrixXd rect = Eigen::MatrixXd::Zero(19, kN);
    for (int i = 0; i < kN; ++i) {
        const Eigen::Vector3d c(static_cast<double>(i + 1) * spacing, 0.0, z);
        rect(0, i) = i + 1;
        rect.block<3, 1>(1, i) = c + Eigen::Vector3d(-h, -h, 0);
        rect.block<3, 1>(4, i) = c + Eigen::Vector3d(h, -h, 0);
        rect.block<3, 1>(7, i) = c + Eigen::Vector3d(h, h, 0);
        rect.block<3, 1>(10, i) = c + Eigen::Vector3d(-h, h, 0);
        rect.block<3, 1>(16, i) = c;
    }
    return rect;
}

void writeRectCsv(const Eigen::MatrixXd& rect, const std::string& path) {
    std::ofstream out(path);
    out << std::setprecision(17);
    for (Eigen::Index r = 0; r < rect.rows(); ++r) {
        for (Eigen::Index c = 0; c < rect.cols(); ++c) {
            out << rect(r, c) << (c + 1 < rect.cols() ? "," : "\n");
        }
    }
}

struct Results {
    std::ofstream out;
    explicit Results(const std::string& path) : out(path) { out << std::setprecision(17); }
    void v(const std::string& label, double value) { out << label << "," << value << "\n"; }
    void i(const std::string& label, long value) { out << label << "," << value << "\n"; }
    void vec3(const std::string& label, const Eigen::Vector3d& p) {
        v(label + "_x", p.x());
        v(label + "_y", p.y());
        v(label + "_z", p.z());
    }
};

}  // namespace

int main() {
    const Eigen::MatrixXd rect = makeSyntheticRect();
    writeRectCsv(rect, "parity_rect.csv");
    Results r("parity_arrays_cpp.csv");

    // --- affine matrices ---
    const Eigen::Matrix4d zr = zRotAffineMatrix(std::numbers::pi / 6.0);
    r.v("zrot_00", zr(0, 0));
    r.v("zrot_10", zr(1, 0));
    const Eigen::Matrix4d tr = translateAffineMatrix(Eigen::Vector3d(0.001, 0.002, 0.003));
    r.v("translate_03", tr(0, 3));
    r.v("translate_13", tr(1, 3));

    const Eigen::MatrixXd moved = applyAffineToRect(zr, rect);
    r.vec3("applyaffine_elem1_center", rectCenter(moved, 0));
    r.vec3("applyaffine_elem50_corner1", rectCorner(moved, 49, 1));

    // --- defineArrayStruct ---
    const ArrayStruct array = defineArrayStruct(rect, 150000.0, {0.06, 0.06});
    for (int idx : {0, 49, kN - 1}) {
        const std::string p = "elem" + std::to_string(idx + 1);
        r.vec3(p + "_pos", array.element[static_cast<size_t>(idx)].position);
        r.vec3(p + "_normal", array.element[static_cast<size_t>(idx)].normalVector);
        r.i(p + "_opposing", array.element[static_cast<size_t>(idx)].opposingElement);
        r.i(p + "_receive_count",
            static_cast<long>(array.element[static_cast<size_t>(idx)].receiveElements.size()));
    }

    const Eigen::MatrixXd positions = getElementPositionsFromArrayStruct(array);
    r.vec3("positions_row1", positions.row(0).transpose());
    r.vec3("positions_rowN", positions.row(kN - 1).transpose());

    r.vec3("normal_3points",
           normalVectorFrom3Points(Eigen::Vector3d(0.01, 0.02, 0.03), Eigen::Vector3d(0.05, 0.01, 0.02),
                                    Eigen::Vector3d(0.02, 0.06, 0.01)));
    r.vec3("calc_rect_normal", calculateRectNormalVector(rect, 1));

    const Eigen::Matrix<double, 3, 4> corners = array.element[0].corners;
    const Eigen::MatrixXd samples = spatiallySampleElement(corners, 0.0001);
    r.i("spatial_sample_count", static_cast<long>(samples.rows()));
    if (samples.rows() > 0) r.vec3("spatial_sample_first", samples.row(0).transpose());

    const auto [oppEl, recvEls] = getReceiveElements(rect, 1, 1490.0 / 150000.0, 0.06);
    r.i("getreceive_opposing", oppEl);
    r.i("getreceive_count", static_cast<long>(recvEls.size()));

    // --- defineArrayData / getOpposingElements ---
    const ArrayData data = defineArrayData(rect);
    const std::vector<int> opp1 = getOpposingElements(data, 1);
    r.i("opposing1_count", static_cast<long>(opp1.size()));
    if (!opp1.empty()) {
        r.i("opposing1_first", opp1.front());
        r.i("opposing1_last", opp1.back());
    }

    // --- defineArrayTxElements / arrayElementsToVSXElements ---
    const TxElements tx = defineArrayTxElements("firstThenSecond");
    r.i("tx_rows", static_cast<long>(tx.txElements.size()));
    r.i("tx_row0_first", tx.txElements[0].front());
    r.i("tx_row0_last", tx.txElements[0].back());
    r.i("tx_row1_first", tx.txElements[1].front());
    r.i("tx_row1_last", tx.txElements[1].back());
    const std::vector<int> vsx = arrayElementsToVSXElements({1, 126, 127});
    r.i("vsx_0", vsx[0]);
    r.i("vsx_1", vsx[1]);
    r.i("vsx_2", vsx[2]);

    // --- util ---
    r.vec3("add_vectors", addVectors(Eigen::Vector3d(1, 2, 3), Eigen::Vector3d(10, 20, 30)));
    r.v("angle_between", angleBetweenTwoVectors(Eigen::Vector3d(1, 2, 2), Eigen::Vector3d(2, 3, 6)));
    Eigen::VectorXd rangeVec(5);
    rangeVec << 1, 5, -3, 2, 8;
    r.v("vector_range", vectorRange(rangeVec));
    r.v("distance_point_to_line",
        distancePointToLine(Eigen::Vector3d(1, 0, 0), Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0.5, 0.3, 0.4)));
    Eigen::VectorXd delays(3);
    delays << 1e-6, 2e-6, 3e-6;
    const Eigen::VectorXd cyc = convertDelaysToCycles(delays, 650000.0);
    r.v("cycles_0", cyc(0));
    r.v("cycles_2", cyc(2));

    // --- util/camera ---
    Eigen::Matrix3d rmat;
    rmat << 0.936293, -0.289629, 0.198669, 0.312992, 0.944703, -0.0978434, -0.159345, 0.153792, 0.975170;
    Eigen::MatrixX3d pts(2, 3);
    pts << 0.1, 0.2, 0.3, -0.05, 0.15, 0.4;
    const Eigen::MatrixX3d local = cam2targetSpace(pts, rmat, Eigen::Vector3d(0.01, 0.02, 0.03));
    r.vec3("cam2target_row0", local.row(0).transpose());
    r.vec3("cam2target_row1", local.row(1).transpose());

    Eigen::MatrixXd mm(1, 2);
    mm << 12.5, 33.0;
    Eigen::RowVectorXd origin(2);
    origin << 1.0, 2.0;
    const Eigen::MatrixXd px = mm2pixel(mm, 0.5, origin);
    r.v("mm2pixel_0", px(0, 0));
    r.v("mm2pixel_1", px(0, 1));

    Eigen::Matrix3d k;
    k << 800, 0, 320, 0, 800, 240, 0, 0, 1;
    Eigen::MatrixX2d pix(2, 2);
    pix << 320, 240, 400, 300;
    const Eigen::MatrixX3d hit = mm3Dpnp(rmat, Eigen::Vector3d(0, 0, 0.5), pix, k, 0.0);
    r.vec3("mm3dpnp_row0", hit.row(0).transpose());
    r.vec3("mm3dpnp_row1", hit.row(1).transpose());

    r.vec3("rotm2eul", rotm2eulSimple(rmat));

    return 0;
}
