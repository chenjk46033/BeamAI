#include "gui_qt/chart_axis_utils.hpp"

#include <algorithm>
#include <cmath>

namespace beam::gui_qt {

NiceAxisRange niceAxisRange(double rawMax) {
    if (rawMax <= 0.0) return {1.0, 4};
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawMax)));
    const double normalized = rawMax / magnitude;
    double niceNormalized;
    int steps;
    if (normalized <= 1.0) {
        niceNormalized = 1.0;
        steps = 5;
    } else if (normalized <= 2.0) {
        niceNormalized = 2.0;
        steps = 4;
    } else if (normalized <= 2.5) {
        niceNormalized = 2.5;
        steps = 5;
    } else if (normalized <= 5.0) {
        niceNormalized = 5.0;
        steps = 5;
    } else {
        niceNormalized = 10.0;
        steps = 5;
    }
    return {niceNormalized * magnitude, steps};
}

double niceStep(double range, double targetTicks) {
    if (range <= 0.0) return 1.0;
    const double roughStep = range / targetTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(roughStep)));
    // +eps: roughStep/magnitude can land just under an exact threshold
    // (e.g. 1.5) due to binary rounding, tipping into the wrong bucket.
    const double normalized = roughStep / magnitude * (1.0 + 1e-9);
    double niceNormalized;
    if (normalized < 1.5) {
        niceNormalized = 1.0;
    } else if (normalized < 3.0) {
        niceNormalized = 2.0;
    } else if (normalized < 7.0) {
        niceNormalized = 5.0;
    } else {
        niceNormalized = 10.0;
    }
    return niceNormalized * magnitude;
}

int chooseExponent(double rawMax) {
    if (rawMax <= 0.0) return 0;
    return 3 * static_cast<int>(std::floor(std::log10(rawMax) / 3.0));
}

int decimalsForStep(double step) {
    if (step <= 0.0) return 2;
    const int decimals = static_cast<int>(-std::floor(std::log10(step) + 1e-9));
    return std::max(0, decimals);
}

}  // namespace beam::gui_qt
