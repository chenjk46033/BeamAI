#include "correction/signal.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <vector>

#include <unsupported/Eigen/FFT>

#include "correction/peaks.hpp"

namespace beam::correction {

namespace {

double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return std::numeric_limits<double>::quiet_NaN();  // MATLAB median([])
    }
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

Eigen::VectorXd analyticEnvelope(const Eigen::VectorXd& x) {
    const Eigen::Index n = x.size();
    if (n == 0) {
        return Eigen::VectorXd();
    }

    Eigen::FFT<double> fft;
    const Eigen::VectorXcd xc = x.cast<std::complex<double>>();
    Eigen::VectorXcd spectrum;
    fft.fwd(spectrum, xc);  // full-length complex spectrum

    // MATLAB hilbert() weighting: h(1)=1; even n -> h(n/2+1)=1, h(2..n/2)=2;
    // odd n -> h(2..(n+1)/2)=2.
    Eigen::VectorXcd h = Eigen::VectorXcd::Zero(n);
    h(0) = 1.0;
    if (n % 2 == 0) {
        h(n / 2) = 1.0;
        for (Eigen::Index i = 1; i < n / 2; ++i) h(i) = 2.0;
    } else {
        for (Eigen::Index i = 1; i <= (n - 1) / 2; ++i) h(i) = 2.0;
    }

    const Eigen::VectorXcd weighted = spectrum.cwiseProduct(h);
    Eigen::VectorXcd analytic;
    fft.inv(analytic, weighted);
    return analytic.cwiseAbs();
}

FilterTransmitResult filterTransmitSignal(const Eigen::VectorXd& wv) {
    const Eigen::Index n = wv.size();
    const Eigen::VectorXd env = analyticEnvelope(wv);

    constexpr double kThreshold = 5.0;
    constexpr int kContinuousLength = 5;
    constexpr int kTransmitLengthSamples = 175;

    std::vector<Eigen::Index> idx;  // 0-based positions where env > threshold
    for (Eigen::Index i = 0; i < env.size(); ++i) {
        if (env(i) > kThreshold) idx.push_back(i);
    }

    int startSample1 = 1;  // MATLAB default (1-based)
    for (std::size_t i0 = 0; i0 + kContinuousLength < idx.size(); ++i0) {
        if (idx[i0 + kContinuousLength] - idx[i0] <= kContinuousLength) {
            startSample1 = static_cast<int>(idx[i0]) + 1;
            break;
        }
    }

    Eigen::VectorXd filtWave = wv;
    const int a1 = startSample1 - kTransmitLengthSamples;  // 1-based, inclusive
    const int b1 = startSample1 + kTransmitLengthSamples;
    if (a1 >= 1 && b1 <= static_cast<int>(n)) {  // else: MATLAB's catch -> leave unblanked
        filtWave.segment(a1 - 1, b1 - a1 + 1).setZero();
    }

    FilterTransmitResult result;
    result.filtWave = std::move(filtWave);
    result.startSample = startSample1 + kTransmitLengthSamples;
    return result;
}

TxRxSignalAmplitude txRxSignalAmplitude(const Eigen::VectorXd& x) {
    // y = x(20:end-200), 1-based -> 0-based x.segment(19, n - 219).
    const Eigen::Index n = x.size();
    if (n <= 219) {
        throw std::invalid_argument("txRxSignalAmplitude: signal too short (needs > 219 samples)");
    }
    const Eigen::VectorXd y = x.segment(19, n - 219);
    const Eigen::VectorXd absY = y.cwiseAbs();

    // min_height = mean([abs(min(y)), max(y)]) / 2   ==   (|min| + max) / 4
    const double minHeight = (std::abs(y.minCoeff()) + y.maxCoeff()) / 4.0;

    std::vector<double> peakVals;
    for (Eigen::Index idx : findPeaks(absY)) {
        if (absY(idx) > minHeight) {  // MATLAB findpeaks 'MinPeakHeight': strictly higher
            peakVals.push_back(absY(idx));
        }
    }

    TxRxSignalAmplitude r;
    r.peak2peak = medianOf(peakVals);  // median(abs(peak_values))
    r.rms = std::sqrt(y.array().square().mean());
    r.envelope = analyticEnvelope(y).maxCoeff();
    return r;
}

ThroughTransmitAmplitude throughTransmitAmplitude(const Eigen::VectorXd& ch0rcv,
                                                   const Eigen::VectorXd& ch1rcv) {
    constexpr double kNormValAmp = 1200.0;
    constexpr double kNormValRms = 90.0;

    const TxRxSignalAmplitude a0 = txRxSignalAmplitude(filterTransmitSignal(ch0rcv).filtWave);
    const TxRxSignalAmplitude a1 = txRxSignalAmplitude(filterTransmitSignal(ch1rcv).filtWave);

    ThroughTransmitAmplitude r;
    r.amp = 0.5 * (std::sqrt(a0.envelope / kNormValAmp) + std::sqrt(a1.envelope / kNormValAmp));
    r.rms = 0.5 * (std::sqrt(a0.rms / kNormValRms) + std::sqrt(a1.rms / kNormValRms));
    return r;
}

}  // namespace beam::correction
