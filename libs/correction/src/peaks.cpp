#include "correction/peaks.hpp"

#include <algorithm>
#include <cmath>

namespace beam::correction {

std::vector<Eigen::Index> findPeaks(const Eigen::VectorXd& v) {
    std::vector<Eigen::Index> idx;
    for (Eigen::Index i = 1; i + 1 < v.size(); ++i) {
        if (v(i - 1) < v(i) && v(i) > v(i + 1)) {
            idx.push_back(i);
        }
    }
    return idx;
}

namespace {

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

}  // namespace

double findPeakNegativeVoltage(const Eigen::VectorXd& v, int nCycles) {
    if (nCycles < 2) {
        return -v.minCoeff();
    }

    const Eigen::VectorXd a = (v.array() - v.mean()).abs();
    std::vector<Eigen::Index> peakIdx = findPeaks(a);

    std::vector<double> peaks;
    peaks.reserve(peakIdx.size());
    for (Eigen::Index i : peakIdx) {
        peaks.push_back(a(i));
    }
    std::sort(peaks.begin(), peaks.end(), std::greater<double>());

    if (static_cast<size_t>(nCycles) > peaks.size()) {
        return -a.minCoeff();
    }
    peaks.resize(static_cast<size_t>(nCycles));
    return median(std::move(peaks));
}

}  // namespace beam::correction
