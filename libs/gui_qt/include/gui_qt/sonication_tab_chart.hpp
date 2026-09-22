#pragma once

#include <vector>

#include <QtCharts/QChart>
#include <QString>

#include "gui/sonication_tab_presenter.hpp"
#include "stimulation/events.hpp"

namespace beam::gui_qt {

// updateSonicationPlots.m's pulse/burst plots (the .mlapp's own axPulseWaveformPlot/
// axBurstWaveformPlot, xlabel "Pulse Interval (s)"/"Burst Interval (s)",
// ylabel "Amplitude (MPa)"). `xAxisLabel` is the caller-supplied X label;
// seconds, auto-scaled to MATLAB's own engineering-notation exponent
// (e.g. "... x 10^-3") when the data is small.
QChart* buildPulseWaveformChart(const beam::gui::PulseWaveformPlot& plot, const QString& title,
                                 const QString& xAxisLabel);

// Burst/pause step chart (1 = on, 0 = pause). Kept for BeamMainWindow's
// own "Sonication timeline" chart. DesignerStartupWindow's equivalent
// (axTotalSonicationPlot) uses TotalSonicationView instead -- QtCharts'
// own QValueAxis renders garbled tick labels for that chart's real
// burst-timeline data (root cause never found despite extensive
// testing), so it's hand-painted instead of built here.
QChart* buildSonicationTimelineChart(const std::vector<beam::stimulation::TimelineSegment>& timeline,
                                      const QString& title);

}  // namespace beam::gui_qt
