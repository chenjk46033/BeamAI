#include "gui/correction_tab_presenter.hpp"

#include <algorithm>
#include <cmath>

namespace beam::gui {

AvgTransmissionBars computeAvgTransmissionBars(double att, double transmissionPeak2Peak,
                                               double couplingThreshold) {
    AvgTransmissionBars bars;
    bars.currentValue = att;
    // yMean = [att, 0.2]; yStd = [0, 0.1] -- literals from
    // setAvgTransmissionDataBars.m.
    bars.averageValue = 0.2;
    bars.currentStd = 0.0;
    bars.averageStd = 0.1;
    bars.currentBarIsRed = att < (bars.averageValue - bars.averageStd);
    bars.pass = transmissionPeak2Peak > couplingThreshold;
    return bars;
}

double computeRfPlotYLimit(const Eigen::VectorXd& x0Filtered, const Eigen::VectorXd& x1Filtered) {
    const double m0 = x0Filtered.size() ? x0Filtered.cwiseAbs().maxCoeff() : 0.0;
    const double m1 = x1Filtered.size() ? x1Filtered.cwiseAbs().maxCoeff() : 0.0;
    return std::max(m0, m1) + 10.0;
}

CorrectionInitialState correctionInitialState() { return CorrectionInitialState{}; }

}  // namespace beam::gui
