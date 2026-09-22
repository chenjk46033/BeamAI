#pragma once

#include <vector>

#include <QRectF>
#include <QWidget>

#include "stimulation/events.hpp"

class QMouseEvent;
class QPaintEvent;

namespace beam::gui_qt {

// Hand-painted stand-in for updateSonicationPlots.m's third plot
// (axTotalSonicationPlot). QtCharts' own QValueAxis renders garbled tick
// labels for this specific burst-timeline data (root cause not found
// despite extensive testing) -- painting directly sidesteps its automatic
// tick-label computation entirely. Matches the .mlapp's real axis titles
// ("Time (s)" / "Amplitude (MPa)"). Hovering a burst bar highlights it and
// shows a two-line "X: <cursor time>" / "Y: <amplitude>" tooltip -- Y is
// always the bar's own (fixed) amplitude, not the cursor's vertical
// position, since every point on a given bar represents the same
// amplitude.
class TotalSonicationView : public QWidget {
    Q_OBJECT

public:
    explicit TotalSonicationView(QWidget* parent = nullptr);

    void setData(const std::vector<beam::stimulation::TimelineSegment>& timeline, double amplitude,
                 double durationSeconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    QSize minimumSizeHint() const override;

private:
    QRectF plotRect() const;
    // The exact rectangle a burst segment is drawn as -- shared by
    // paintEvent (drawing) and mouseMoveEvent (hit-testing), so the
    // hover-highlighted area always matches what's actually on screen.
    QRectF segmentBarRect(const beam::stimulation::TimelineSegment& seg, const QRectF& plot) const;

    std::vector<beam::stimulation::TimelineSegment> timeline_;
    double amplitude_ = 0.0;
    double durationSeconds_ = 30.0;
    // X axis: 0..xAxisMax_ (== durationSeconds_ rounded up to a whole
    // xTickStep_, so ticks land on round numbers like 0/5/10/.../30
    // without padding past the real data, unlike a from-zero-padded
    // "nice max"). Y axis: yAxisMin_..yAxisMax_, deliberately NOT
    // starting at 0 -- centered on amplitude_ with amplitude_ itself as
    // the second of 4 ticks (e.g. amplitude 0.5 -> 0.4/0.5/0.6/0.7),
    // matching the real MATLAB app's own autoscaled range and making
    // each burst bar's height meaningful instead of always maxing out
    // the plot.
    double xAxisMax_ = 30.0;
    double xTickStep_ = 5.0;
    double yAxisMin_ = 0.0;
    double yAxisMax_ = 1.0;
    double yTickStep_ = 0.1;

    // Index into timeline_ of the currently-hovered burst bar, or -1.
    int hoveredIndex_ = -1;
};

}  // namespace beam::gui_qt
