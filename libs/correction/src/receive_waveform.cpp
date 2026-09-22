#include "correction/receive_waveform.hpp"

#include <cmath>
#include <complex>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace beam::correction {

namespace {

using Complex = std::complex<double>;

// Expands prod(x - roots[i]) into descending-power polynomial
// coefficients (MATLAB's `poly()` convention: coefficients[0] == 1).
std::vector<Complex> polyFromRoots(const std::vector<Complex>& roots) {
    std::vector<Complex> c = {Complex(1.0, 0.0)};
    for (const Complex& r : roots) {
        std::vector<Complex> next(c.size() + 1, Complex(0.0, 0.0));
        for (std::size_t i = 0; i < c.size(); ++i) {
            next[i] += c[i];
            next[i + 1] -= c[i] * r;
        }
        c = std::move(next);
    }
    return c;
}

Eigen::VectorXd toRealVector(const std::vector<Complex>& v) {
    Eigen::VectorXd out(static_cast<Eigen::Index>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) out(static_cast<Eigen::Index>(i)) = v[i].real();
    return out;
}

std::string trim(const std::string& s) {
    const std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

}  // namespace

std::vector<double> parseCorrectionWaveformLine(const std::string& line) {
    std::vector<double> out;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        const std::string trimmed = trim(field);
        if (trimmed.empty()) continue;
        try {
            std::size_t consumed = 0;
            const double v = std::stod(trimmed, &consumed);
            if (consumed == trimmed.size()) out.push_back(v);
        } catch (const std::exception&) {
            // str2double's NaN-on-failure, then filtered by ~isnan -- skip.
        }
    }
    return out;
}

IirCoefficients butterBandpass(int order, double lowNorm, double highNorm) {
    // 1. buttap(order): analog lowpass Butterworth prototype, cutoff 1
    // rad/s, unit DC gain, no finite zeros.
    std::vector<Complex> protoPoles(static_cast<std::size_t>(order));
    for (int k = 1; k <= order; ++k) {
        const double theta = (2.0 * k - 1.0) * std::numbers::pi / (2.0 * order);
        protoPoles[static_cast<std::size_t>(k - 1)] = Complex(-std::sin(theta), std::cos(theta));
    }

    // 2. Prewarp (butter.m's internal fs=2 convention for pre-normalized Wn).
    constexpr double fs = 2.0;
    const double u1 = 2.0 * fs * std::tan(std::numbers::pi * lowNorm / fs);
    const double u2 = 2.0 * fs * std::tan(std::numbers::pi * highNorm / fs);
    const double bw = u2 - u1;
    const double wo = std::sqrt(u1 * u2);

    // 3. lp2bp: all-pole lowpass -> bandpass. n zeros land at the origin
    // (padding for the zero-count deficit); 2n poles from the quadratic
    // s^2 - (p*Bw)*s + Wo^2 = 0 per lowpass pole p.
    std::vector<Complex> bpPoles;
    bpPoles.reserve(static_cast<std::size_t>(order) * 2);
    for (const Complex& p : protoPoles) {
        const Complex pht = p * (bw / 2.0);
        const Complex root = std::sqrt(pht * pht - Complex(wo * wo, 0.0));
        bpPoles.push_back(pht + root);
        bpPoles.push_back(pht - root);
    }
    const std::vector<Complex> bpZeros(static_cast<std::size_t>(order), Complex(0.0, 0.0));
    const double kBp = std::pow(bw, order);  // original prototype gain is 1

    // 4. Bilinear transform (s -> z): z = (2*fs + s)/(2*fs - s) -- the
    // *doubled* fs (matching MATLAB/scipy's bilinear_zpk convention,
    // distinct from the single-fs prewarp step above). Digital zero
    // count is padded to match pole count with zeros at z=-1 (bilinear's
    // image of s=infinity).
    const double fs2 = 2.0 * fs;
    std::vector<Complex> digPoles;
    digPoles.reserve(bpPoles.size());
    for (const Complex& p : bpPoles) digPoles.push_back((fs2 + p) / (fs2 - p));

    std::vector<Complex> digZeros;
    digZeros.reserve(bpPoles.size());
    for (const Complex& z : bpZeros) digZeros.push_back((fs2 + z) / (fs2 - z));
    while (digZeros.size() < digPoles.size()) digZeros.push_back(Complex(-1.0, 0.0));

    Complex numProd(1.0, 0.0), denProd(1.0, 0.0);
    for (const Complex& z : bpZeros) numProd *= (fs2 - z);
    for (const Complex& p : bpPoles) denProd *= (fs2 - p);
    const double kd = kBp * (numProd / denProd).real();

    std::vector<Complex> bCoeffs = polyFromRoots(digZeros);
    for (Complex& c : bCoeffs) c *= kd;
    const std::vector<Complex> aCoeffs = polyFromRoots(digPoles);

    IirCoefficients out;
    out.b = toRealVector(bCoeffs);
    out.a = toRealVector(aCoeffs);
    return out;
}

Eigen::VectorXd filterIir(const Eigen::VectorXd& b, const Eigen::VectorXd& a, const Eigen::VectorXd& x) {
    const double a0 = a(0);
    const Eigen::VectorXd bn = b / a0;
    const Eigen::VectorXd an = a / a0;
    const Eigen::Index order = std::max(bn.size(), an.size()) - 1;

    Eigen::VectorXd z = Eigen::VectorXd::Zero(order);
    Eigen::VectorXd y(x.size());
    for (Eigen::Index n = 0; n < x.size(); ++n) {
        const double xn = x(n);
        const double yn = bn(0) * xn + (order > 0 ? z(0) : 0.0);
        for (Eigen::Index i = 0; i < order; ++i) {
            const double bi1 = (i + 1 < bn.size()) ? bn(i + 1) : 0.0;
            const double ai1 = (i + 1 < an.size()) ? an(i + 1) : 0.0;
            const double zNext = bi1 * xn - ai1 * yn + (i + 1 < order ? z(i + 1) : 0.0);
            z(i) = zNext;
        }
        y(n) = yn;
    }
    return y;
}

ReceiveWaveformSplit splitAndFilterReceiveWaveform(const std::vector<double>& floatData) {
    const Eigen::Index n = static_cast<Eigen::Index>(floatData.size()) / 2;
    if (n < 2) {
        throw std::invalid_argument(
            "splitAndFilterReceiveWaveform: fewer than 2 samples per channel -- the source's own N<2 "
            "fallback is broken (a typo'd variable name leaves ch0rcv a scalar, then errors indexing it)");
    }

    Eigen::VectorXd ch0raw(n);
    for (Eigen::Index i = 0; i < n; ++i) ch0raw(i) = floatData[static_cast<std::size_t>(i)];
    // MATLAB's floatData(N:end), 1-based inclusive of N: this project's
    // 0-based equivalent keeps index (n-1) through the end -- the
    // 1-sample overlap with ch0raw at that index is the source's own
    // off-by-one, reproduced as-is.
    const Eigen::Index ch1Len = static_cast<Eigen::Index>(floatData.size()) - n + 1;
    Eigen::VectorXd ch1raw(ch1Len);
    for (Eigen::Index i = 0; i < ch1Len; ++i) ch1raw(i) = floatData[static_cast<std::size_t>(n - 1 + i)];

    constexpr double kFs = 1316800.0;
    const IirCoefficients coeffs = butterBandpass(2, 200000.0 / (kFs / 2.0), 400000.0 / (kFs / 2.0));

    const Eigen::VectorXd ch0filt = filterIir(coeffs.b, coeffs.a, ch0raw);
    const Eigen::VectorXd ch1filt = filterIir(coeffs.b, coeffs.a, ch1raw);

    constexpr Eigen::Index kThreshCut = 100;  // MATLAB's ch0rcv(threshCut:end), 1-based
    ReceiveWaveformSplit result;
    result.ch0rcv = ch0filt.tail(ch0filt.size() - (kThreshCut - 1));
    result.ch1rcv = ch1filt.tail(ch1filt.size() - (kThreshCut - 1));
    return result;
}

}  // namespace beam::correction
