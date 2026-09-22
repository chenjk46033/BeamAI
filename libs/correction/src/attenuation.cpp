#include "correction/attenuation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include "correction/correlation.hpp"  // nanmean
#include "correction/peaks.hpp"

namespace beam::correction {

Eigen::VectorXd setAdjustedAttValues(const Eigen::VectorXd& att, double vAmplitudeToMPa,
                                      double attThreshold) {
    const Eigen::Index n = att.size();
    constexpr double kInf = std::numeric_limits<double>::infinity();

    // P0 = ones(size(att)); sum(P0) == n.
    Eigen::VectorXd p = Eigen::VectorXd::Ones(n);
    for (Eigen::Index k = 0; k < n; ++k) {
        if (att(k) == kInf) {
            p(k) = 0.0;
        } else if (att(k) < attThreshold) {
            p(k) *= att(k) / attThreshold;
        }
    }
    double deficit = p.sum() / static_cast<double>(n);

    Eigen::VectorXd attReduced = att;
    for (Eigen::Index k = 0; k < n; ++k) {
        if (att(k) < attThreshold) {
            attReduced(k) = attThreshold;
        }
    }

    constexpr double kMaxAdjustVoltage = 30.0;
    const double maxAdjustAtt = vAmplitudeToMPa / kMaxAdjustVoltage;

    // [~, i] = sort(attReduced, 'descend') -- index permutation, computed
    // once before the loop mutates attReduced. MATLAB's sort is stable, so
    // ties keep ascending original order.
    std::vector<Eigen::Index> order(static_cast<size_t>(n));
    std::iota(order.begin(), order.end(), Eigen::Index{0});
    std::stable_sort(order.begin(), order.end(),
                     [&attReduced](Eigen::Index a, Eigen::Index b) { return attReduced(a) > attReduced(b); });

    for (Eigen::Index idx : order) {
        const double valAtt = attReduced(idx);
        if (valAtt == kInf) {
            continue;
        }
        if (valAtt > maxAdjustAtt) {
            const double pVal = p(idx);
            p(idx) = p(idx) * att(idx) / maxAdjustAtt;
            deficit -= (pVal - p(idx));
            if (deficit < 0.0) {
                break;
            }
            attReduced(idx) = maxAdjustAtt;
        }
    }
    return attReduced;
}

double calculateAttenuation(const Eigen::VectorXd& subjectSignal, const Eigen::VectorXd& freeFieldSignal,
                             bool usePeakNegativeVoltage) {
    const Eigen::VectorXd s = subjectSignal.array() - nanmean(subjectSignal);
    const Eigen::VectorXd n = freeFieldSignal.array() - nanmean(freeFieldSignal);

    double sPnv;
    double nPnv;
    if (usePeakNegativeVoltage) {
        constexpr int kNCycles = 10;
        sPnv = findPeakNegativeVoltage(s, kNCycles);
        nPnv = findPeakNegativeVoltage(n, kNCycles);
    } else {
        sPnv = s.maxCoeff() - s.minCoeff();  // peak2peak
        nPnv = n.maxCoeff() - n.minCoeff();
    }
    return sPnv / nPnv;
}

}  // namespace beam::correction
