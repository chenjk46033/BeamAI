#pragma once

#include <QRectF>
#include <QString>
#include <QWidget>

#include "gui/sonication_tab_presenter.hpp"

class QPaintEvent;

namespace beam::gui_qt {

// Hand-painted stand-in for updateSonicationPlots.m's pulse/burst plots
// (axPulseWaveformPlot/axBurstWaveformPlot). Reused by both -- same data
// shape (beam::gui::PulseWaveformPlot), just different data/title/
// xAxisLabel. QtCharts renders these fine at their old ~220px height, but
// renders NOTHING but the bare title at the shorter height needed to fit
// all 3 Pulse Details charts on screen without scrolling (the same class
// of QtCharts fragility hit with Total Sonication) -- painting directly
// sidesteps that entirely, like TotalSonicationView.
class PulseWaveformView : public QWidget {
public:
    explicit PulseWaveformView(QWidget* parent = nullptr);

    void setData(const beam::gui::PulseWaveformPlot& plot, const QString& title, const QString& xAxisLabel);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize minimumSizeHint() const override;

private:
    QRectF plotRect() const;

    Eigen::VectorXd x_;
    Eigen::VectorXd y_;
    QString title_;
    QString xAxisLabel_;

    // Y axis is a plain 0-based nice range (unlike TotalSonicationView's
    // amplitude-centered one -- here the 0-vs-amplitude transition
    // itself is what the chart is showing).
    double xAxisMax_ = 1.0;
    double xTickStep_ = 0.2;
    double yAxisMax_ = 1.0;
    double yTickStep_ = 0.2;
};

}  // namespace beam::gui_qt
