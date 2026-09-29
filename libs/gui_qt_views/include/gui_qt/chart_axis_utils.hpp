#pragma once

namespace beam::gui_qt {

// Shared axis-tick math for this library's hand-painted/QtCharts plots
// (TotalSonicationView, PulseWaveformView, sonication_tab_chart.cpp's
// QtCharts builders) -- kept in one place instead of copied per file.

// Rounds `rawMax` up to a "nice" 1/2/2.5/5/10 x 10^n value and returns
// that step's own natural tick count (ticks drawn = tickCount + 1), so
// evenly-spaced ticks land on round numbers (0.1, 0.2, 0.3, ...) rather
// than the raw data's own max.
struct NiceAxisRange {
    double max;
    int tickCount;
};
NiceAxisRange niceAxisRange(double rawMax);

// Picks a "nice" 1/2/5 x 10^n step so `range` divided by it lands close
// to `targetTicks` intervals (e.g. range=30, targetTicks=6 -> step=5) --
// unlike niceAxisRange() above, doesn't pad the range past `range`
// itself, and lets the caller choose roughly how many ticks to aim for.
double niceStep(double range, double targetTicks);

// MATLAB's own axis "exponent" display: the largest multiple-of-3 power
// of ten that keeps tick values in a readable (engineering-notation)
// range, e.g. 0.01 -> -3, shown as "... x 10^-3" with tick values 1..10.
int chooseExponent(double rawMax);

// How many decimal places a step's own tick values need (step=5 -> 0,
// step=0.1 -> 1, step=0.01 -> 2).
int decimalsForStep(double step);

}  // namespace beam::gui_qt
