#include "stimulation/stim_params.hpp"

#include <algorithm>
#include <vector>

#include "stimulation/delays.hpp"
#include "util/geometry_math.hpp"

namespace beam::stimulation {

namespace {

double median(const Eigen::VectorXd& v) {
    std::vector<double> s(v.data(), v.data() + v.size());
    std::sort(s.begin(), s.end());
    const size_t n = s.size();
    if (n == 0) {
        return 0.0;
    }
    if (n % 2 == 1) {
        return s[n / 2];
    }
    return 0.5 * (s[n / 2 - 1] + s[n / 2]);
}

Eigen::VectorXd subset(const Eigen::VectorXd& v, const std::vector<int>& idx1Based) {
    Eigen::VectorXd out(static_cast<Eigen::Index>(idx1Based.size()));
    for (size_t k = 0; k < idx1Based.size(); ++k) {
        out(static_cast<Eigen::Index>(k)) = v(idx1Based[k] - 1);
    }
    return out;
}

}  // namespace

std::vector<StimParams> defineStimParams(const beam::array::ArrayStruct& array, double c,
                                          const std::vector<std::vector<int>>& txElements,
                                          const std::vector<std::vector<int>>& txElementsArray,
                                          const std::vector<int>& rxElements,
                                          const Eigen::MatrixX3d& positions, const Eigen::VectorXd& freqs,
                                          const Eigen::VectorXd& apods,
                                          const Eigen::VectorXd& correctionDelays) {
    const double medianFreq = median(freqs);
    const double frequencyHz = medianFreq * 1e6;
    // waveformDelays does not depend on the loop variables in the source.
    const Eigen::VectorXd waveformDelays =
        calculateMultifrequencySuperpositionDelays((freqs.array() * 1e6).matrix(), 0.0);

    std::vector<StimParams> out;
    for (size_t txi = 0; txi < txElements.size(); ++txi) {
        for (Eigen::Index i = 0; i < positions.rows(); ++i) {
            const Eigen::Vector3d pointM = positions.row(i).transpose() / 1000.0;
            const FocusResult focus = focusArrayAtPoint(array, pointM, c);

            Eigen::VectorXd delaysSeconds =
                beam::util::addVectors(focus.delaysT, correctionDelays);
            delaysSeconds = beam::util::addVectors(delaysSeconds, waveformDelays);
            const Eigen::VectorXd delaysCycles =
                beam::util::convertDelaysToCycles(delaysSeconds, frequencyHz);

            StimParams sp;
            sp.centerFrequencyMHz = medianFreq;
            sp.delaysCycle = subset(delaysCycles, txElementsArray[txi]);
            sp.delaysSeconds = subset(delaysSeconds, txElementsArray[txi]);
            sp.delaysSteering = focus.delaysT;
            sp.correctionDelays = correctionDelays;
            sp.waveformDelays = waveformDelays;
            sp.txElements = txElements[txi];
            sp.txElementsArray = txElementsArray[txi];
            sp.rxElements = rxElements;
            sp.freqs = subset(freqs, txElementsArray[txi]);
            sp.apods = subset(apods, txElementsArray[txi]);
            sp.c = c;
            sp.position = positions.row(i).transpose();
            sp.waveform = 3;
            out.push_back(std::move(sp));
        }
    }
    return out;
}

}  // namespace beam::stimulation
