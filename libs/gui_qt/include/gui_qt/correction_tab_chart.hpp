#pragma once

#include <QtCharts/QChart>
#include <Eigen/Core>

#include "gui/correction_tab_presenter.hpp"

// The Qt views for BeamV0's CorrectionTab. libs/gui's presenter functions
// are pure and unit-tested without Qt; this turns their output into real
// QChart objects. Caller owns the returned QChart.

namespace beam::gui_qt {

// Port of setAvgTransmissionDataBars.m's plotAttBars: a two-bar
// "Current vs Average" chart. The "Current" bar is red when
// bars.currentBarIsRed, green otherwise; "Average" is always green
// (the source's unconditional i==2 branch). Error-bar whiskers (yStd in
// the source) are not drawn -- Qt Charts has no error-bar series, a
// disclosed simplification carried over from Diadem's same slice.
QChart* buildTransmissionBarChart(const beam::gui::AvgTransmissionBars& bars);

// Port of updateAttenuationPlots.m's per-array plot: the raw received
// waveform and the filterTransmitSignal'd waveform, overlaid, against
// sample index. Y-axis fixed to the symmetric [-yLimit, yLimit] the source
// sets via `ylim(ax, [-ULBound, ULBound])` (yLimit from
// beam::gui::computeRfPlotYLimit).
QChart* buildRfWaveformChart(const Eigen::VectorXd& raw, const Eigen::VectorXd& filtered,
                              double yLimit, const QString& title);

}  // namespace beam::gui_qt
