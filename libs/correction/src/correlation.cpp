#include "correction/correlation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace beam::correction {

namespace {

// MATLAB circshift convention: a positive shift moves values toward higher
// indices, wrapping around.
Eigen::VectorXd circshiftVector(const Eigen::VectorXd& v, int shift) {
    const Eigen::Index n = v.size();
    if (n == 0) {
        return v;
    }
    Eigen::VectorXd out(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        const Eigen::Index src = ((i - shift) % n + n) % n;
        out(i) = v(src);
    }
    return out;
}

// MATLAB's `x(1:count) = 0` is a no-op for count<=0 and errors if count
// exceeds length(x) -- ported the same way (explicit throw where MATLAB
// hits its own index error), not silently clamped.
void zeroPrefix(Eigen::VectorXd& v, int count, const char* caller) {
    if (count <= 0) {
        return;
    }
    if (count > v.size()) {
        throw std::out_of_range(std::string(caller) + ": prefix-zero index exceeds vector length");
    }
    v.head(count).setZero();
}

}  // namespace

XcorrResult xcorr(const Eigen::VectorXd& x, const Eigen::VectorXd& y) {
    const Eigen::Index n = std::max(x.size(), y.size());
    Eigen::VectorXd xp = Eigen::VectorXd::Zero(n);
    Eigen::VectorXd yp = Eigen::VectorXd::Zero(n);
    xp.head(x.size()) = x;
    yp.head(y.size()) = y;

    XcorrResult result;
    result.c.resize(2 * n - 1);
    result.lags.resize(2 * n - 1);
    for (Eigen::Index idx = 0; idx < 2 * n - 1; ++idx) {
        const int m = static_cast<int>(idx) - static_cast<int>(n - 1);
        const Eigen::Index kStart = std::max<Eigen::Index>(0, -m);
        const Eigen::Index kEnd = std::min<Eigen::Index>(n, n - m);
        double sum = 0.0;
        for (Eigen::Index k = kStart; k < kEnd; ++k) {
            sum += xp(k + m) * yp(k);
        }
        result.c(idx) = sum;
        result.lags(idx) = m;
    }
    return result;
}

double corrCoefficient(const Eigen::VectorXd& a, const Eigen::VectorXd& b) {
    const Eigen::VectorXd da = a.array() - a.mean();
    const Eigen::VectorXd db = b.array() - b.mean();
    return da.dot(db) / std::sqrt(da.squaredNorm() * db.squaredNorm());
}

double nanmean(const Eigen::VectorXd& v) {
    double sum = 0.0;
    Eigen::Index count = 0;
    for (Eigen::Index i = 0; i < v.size(); ++i) {
        if (!std::isnan(v(i))) {
            sum += v(i);
            ++count;
        }
    }
    if (count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / static_cast<double>(count);
}

XcorrS1ToS2Result xcorrS1ToS2(Eigen::VectorXd s1, Eigen::VectorXd s2, const Eigen::Vector2d& bounds) {
    s1.array() -= nanmean(s1);
    s2.array() -= nanmean(s2);

    XcorrResult xc = xcorr(s2, s1);
    for (Eigen::Index i = 0; i < xc.c.size(); ++i) {
        if (xc.lags(i) < bounds(0) || xc.lags(i) > bounds(1)) {
            xc.c(i) = 0.0;
        }
    }

    Eigen::Index mi = 0;
    xc.c.maxCoeff(&mi);
    const int d12 = xc.lags(mi);

    s1 = circshiftVector(s1, d12);
    zeroPrefix(s1, d12, "xcorrS1ToS2");
    zeroPrefix(s2, d12, "xcorrS1ToS2");
    const double cc = corrCoefficient(s2, s1);

    XcorrS1ToS2Result result;
    if (d12 > bounds(1) || d12 < bounds(0)) {
        result.d12 = 0;
        result.cc = 0.0;
    } else {
        result.d12 = d12;
        result.cc = cc;
    }
    return result;
}

CorrSpeedUpResult corrSpeedUp(Eigen::VectorXd ssignal, Eigen::VectorXd nssignal) {
    ssignal.array() -= nanmean(ssignal);
    nssignal.array() -= nanmean(nssignal);

    XcorrResult xc = xcorr(ssignal, nssignal);
    constexpr double kLb = 0.0;
    constexpr double kUb = -150.0;
    for (Eigen::Index i = 0; i < xc.c.size(); ++i) {
        if (xc.lags(i) >= kLb || xc.lags(i) < kUb) {
            xc.c(i) = 0.0;
        }
    }

    Eigen::Index mi = 0;
    xc.c.maxCoeff(&mi);
    const int speedupsa = static_cast<int>(ssignal.size()) - static_cast<int>(mi) - 1;

    ssignal = circshiftVector(ssignal, speedupsa);
    zeroPrefix(ssignal, speedupsa, "corrSpeedUp");
    zeroPrefix(nssignal, speedupsa, "corrSpeedUp");
    const double cc = corrCoefficient(nssignal, ssignal);

    return {speedupsa, cc};
}

MaxCorrelationDelaysResult computeMaxCorrelationDelays(const Eigen::MatrixXd& wvData,
                                                        std::optional<int> maxChannelIn, bool ccFlag) {
    const Eigen::Index nElements = wvData.rows();

    int maxChannel = 0;
    if (maxChannelIn) {
        maxChannel = *maxChannelIn;
    } else {
        // MATLAB: [row,~]=find(abs(wvData)==maxAbs); maxChannel=row(1) --
        // first match in column-major scan order.
        const double maxAbs = wvData.array().abs().maxCoeff();
        bool found = false;
        for (Eigen::Index col = 0; col < wvData.cols() && !found; ++col) {
            for (Eigen::Index row = 0; row < wvData.rows(); ++row) {
                if (std::abs(wvData(row, col)) == maxAbs) {
                    maxChannel = static_cast<int>(row) + 1;
                    found = true;
                    break;
                }
            }
        }
    }

    const Eigen::VectorXd wv0 = wvData.row(maxChannel - 1).transpose();
    Eigen::VectorXi delaysI = Eigen::VectorXi::Zero(nElements);
    Eigen::VectorXd cc = Eigen::VectorXd::Zero(nElements);

    for (Eigen::Index i = 0; i < nElements; ++i) {
        if (i == maxChannel - 1) {
            continue;
        }
        const Eigen::VectorXd wvi = wvData.row(i).transpose();
        const XcorrResult xc = xcorr(wv0, wvi);
        Eigen::Index maxI = 0;
        xc.c.maxCoeff(&maxI);
        const int delay = xc.lags(maxI);
        delaysI(i) = delay;
        if (ccFlag) {
            cc(i) = corrCoefficient(wv0, circshiftVector(wvi, delay));
        } else {
            cc(i) = 1.0;
        }
    }

    MaxCorrelationDelaysResult result;
    result.delaysI = delaysI;
    result.maxChannel = maxChannel;
    result.cc = cc;
    return result;
}

}  // namespace beam::correction
