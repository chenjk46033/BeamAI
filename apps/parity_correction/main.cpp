// parity_correction -- runs the Beam Correction ports on synthetic
// waveforms + rect, writes the inputs and results so
// matlab_verify/verify_correction.m can run the real BeamV0 Correction/
// functions on the same input. See matlab_verify/README.md.

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/array_data.hpp"
#include "array/array_types.hpp"
#include "correction/attenuation.hpp"
#include "correction/correlation.hpp"
#include "correction/localize.hpp"
#include "correction/peaks.hpp"
#include "correction/receive_elements.hpp"
#include "correction/receive_waveform.hpp"
#include "correction/scan_params.hpp"
#include "correction/signal.hpp"
#include "correction/waveforms.hpp"

using namespace beam::correction;

namespace {

constexpr int kNel = 60;      // >= 56 so every element has a >=28-apart opposite
constexpr int kNsamp = 1200;  // >= 1000: filterTransmitSignal.m does wv(1:1000)

// Per-element Gaussian-modulated sinusoid, burst centre shifting +1 sample
// per element -- so cross-correlation delays are exactly -e.
Eigen::MatrixXd makeWaveforms() {
    Eigen::MatrixXd wv(kNel, kNsamp);
    for (int e = 0; e < kNel; ++e) {
        const double centre = 700.0 + e;
        for (int t = 0; t < kNsamp; ++t) {
            const double dt = t - centre;
            wv(e, t) = 20.0 * std::exp(-(dt * dt) / (2.0 * 8.0 * 8.0)) *
                       std::sin(2.0 * std::numbers::pi * dt / 6.0);
        }
    }
    return wv;
}

Eigen::MatrixXd makeRect() {
    constexpr double spacing = 0.002, h = 0.0003, z = 0.12;
    Eigen::MatrixXd rect = Eigen::MatrixXd::Zero(19, kNel);
    for (int i = 0; i < kNel; ++i) {
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

void writeMatrixCsv(const Eigen::MatrixXd& m, const std::string& path) {
    std::ofstream out(path);
    out << std::setprecision(17);
    for (Eigen::Index r = 0; r < m.rows(); ++r)
        for (Eigen::Index c = 0; c < m.cols(); ++c) out << m(r, c) << (c + 1 < m.cols() ? "," : "\n");
}

struct Results {
    std::ofstream out;
    explicit Results(const std::string& p) : out(p) { out << std::setprecision(17); }
    void v(const std::string& k, double x) { out << k << "," << x << "\n"; }
    void i(const std::string& k, long x) { out << k << "," << x << "\n"; }
};

}  // namespace

int run();

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parity_correction: %s\n", e.what());
        return 1;
    }
}

int run() {
    const Eigen::MatrixXd wv = makeWaveforms();
    const Eigen::MatrixXd rect = makeRect();
    writeMatrixCsv(wv, "parity_corr_wv.csv");
    writeMatrixCsv(rect, "parity_corr_rect.csv");
    Results r("parity_correction_cpp.csv");

    const Eigen::VectorXd s1 = wv.row(0).transpose();
    const Eigen::VectorXd s2 = wv.row(3).transpose();

    // --- xcorr ---
    const XcorrResult xc = xcorr(s1, s2);
    r.v("xcorr_len", static_cast<double>(xc.c.size()));
    r.v("xcorr_mid", xc.c(xc.c.size() / 2));            // lag 0
    r.v("xcorr_argmax_lag", static_cast<double>([&] {
        Eigen::Index m = 0;
        xc.c.maxCoeff(&m);
        return xc.lags(m);
    }()));

    r.v("corrcoef_s1s2", corrCoefficient(s1, s2));
    {
        Eigen::VectorXd withNan(4);
        withNan << 1, 2, std::nan(""), 4;
        r.v("nanmean", nanmean(withNan));
    }

    // --- xcorrS1ToS2 / corrSpeedUp ---
    const XcorrS1ToS2Result x12 = xcorrS1ToS2(s1, s2, Eigen::Vector2d(-30, 30));
    r.i("xcorrS1ToS2_d12", x12.d12);
    r.v("xcorrS1ToS2_cc", x12.cc);
    const CorrSpeedUpResult su = corrSpeedUp(s1, s2);
    r.i("corrSpeedUp_speedupsa", su.speedupsa);
    r.v("corrSpeedUp_cc", su.cc);

    // --- computeMaxCorrelationDelays ---
    const MaxCorrelationDelaysResult md = computeMaxCorrelationDelays(wv);
    r.i("compMaxDelays_maxChannel", md.maxChannel);
    r.i("compMaxDelays_delay_e5", md.delaysI(5));
    r.i("compMaxDelays_delay_e20", md.delaysI(20));
    r.v("compMaxDelays_cc_e5", md.cc(5));

    // --- analyticEnvelope / filterTransmitSignal ---
    const Eigen::VectorXd env = analyticEnvelope(s1);
    r.v("envelope_700", env(700));
    r.v("envelope_650", env(650));
    const FilterTransmitResult ft = filterTransmitSignal(s1);
    r.i("filterTransmit_startSample", ft.startSample);
    r.v("filterTransmit_wv_10", ft.filtWave(10));      // outside the blank window
    r.v("filterTransmit_wv_700", ft.filtWave(700));    // inside -> 0

    // --- shiftAndSumWaveforms ---
    Eigen::MatrixXd smallWv(2, 4);
    smallWv << 1, 2, 3, 4, 10, 20, 30, 40;
    Eigen::VectorXi delays(2);
    delays << 1, -1;
    Eigen::VectorXd weights(2);
    weights << 2.0, 0.5;
    const ShiftAndSumResult ss = shiftAndSumWaveforms(smallWv, delays, weights);
    for (int k = 0; k < 4; ++k) r.v("shiftSum_" + std::to_string(k), ss.waveformSum(k));

    // --- setAdjustedAttValues / calculateAttenuation ---
    Eigen::VectorXd att(4);
    att << 2, 5, 0.5, std::numeric_limits<double>::infinity();
    const Eigen::VectorXd adj = setAdjustedAttValues(att, 10.0, 1.0);
    for (int k = 0; k < 4; ++k) r.v("adjAtt_" + std::to_string(k), std::isinf(adj(k)) ? 1e300 : adj(k));
    r.v("calcAtten", calculateAttenuation(s1, s2));

    // --- findPeakNegativeVoltage ---
    r.v("fpnv_lowN", findPeakNegativeVoltage(s1, 1));
    r.v("fpnv_10", findPeakNegativeVoltage(s1, 10));

    // --- defineTxRxScanParams ---
    const TxRxScanParams sp = defineTxRxScanParams();
    r.v("scan_desiredDepth", sp.desiredDepth);
    r.v("scan_sampleRateHz", sp.sampleRateHz);
    r.v("scan_voltageAmplitude", sp.voltageAmplitude);
    r.i("scan_mapping_count", static_cast<long>(sp.arrayToVSXMapping.size()));

    // --- getRecieveWaveformFromSerial.m: butter/filter ---
    {
        constexpr double fs = 1316800.0;
        const IirCoefficients coeffs = butterBandpass(2, 200000.0 / (fs / 2.0), 400000.0 / (fs / 2.0));
        for (int i = 0; i < 5; ++i) r.v("butter_b_" + std::to_string(i), coeffs.b(i));
        for (int i = 0; i < 5; ++i) r.v("butter_a_" + std::to_string(i), coeffs.a(i));

        const Eigen::VectorXd filtered = filterIir(coeffs.b, coeffs.a, s1);
        r.v("filter_s1_0", filtered(0));
        r.v("filter_s1_500", filtered(500));
        r.v("filter_s1_1199", filtered(1199));
    }

    // --- getReceiveElementsUnderAngle ---
    const beam::array::ArrayData data = beam::array::defineArrayData(rect);
    r.i("recvAngle_norm90", static_cast<long>(
        getReceiveElementsUnderAngle(data, 1, std::nullopt, 90.001).size()));
    r.i("recvAngle_explicit", static_cast<long>(
        getReceiveElementsUnderAngle(data, 1, Eigen::Vector3d(0, 0, 500), 90.001).size()));

    return 0;
}
