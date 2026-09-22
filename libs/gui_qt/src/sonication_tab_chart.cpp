#include "gui_qt/sonication_tab_chart.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QtCharts/QAreaSeries>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include "gui/sonication_tab_presenter.hpp"
#include "gui_qt/chart_axis_utils.hpp"

namespace beam::gui_qt {

QChart* buildPulseWaveformChart(const beam::gui::PulseWaveformPlot& plot, const QString& title,
                                 const QString& xAxisLabel) {
    const double xMaxRaw = plot.x.size() ? plot.x(plot.x.size() - 1) : 1.0;
    const int exponent = chooseExponent(xMaxRaw);
    const double xScale = std::pow(10.0, -exponent);

    auto* upper = new QLineSeries();
    for (Eigen::Index i = 0; i < plot.x.size(); ++i) {
        upper->append(plot.x(i) * xScale, plot.y(i));
    }
    auto* area = new QAreaSeries(upper);

    auto* chart = new QChart();
    chart->addSeries(area);
    chart->setTitle(title);
    chart->legend()->setVisible(false);

    auto* axisX = new QValueAxis();
    axisX->setTitleText(exponent != 0 ? QStringLiteral("%1      x 10^%2").arg(xAxisLabel).arg(exponent) : xAxisLabel);
    const NiceAxisRange xRange = niceAxisRange(xMaxRaw * xScale);
    axisX->setRange(0.0, xRange.max);
    // +1: niceAxisRange's tickCount is an interval count (ticks drawn =
    // tickCount + 1), but QValueAxis::setTickCount wants the total
    // number of tick marks including both endpoints.
    axisX->setTickCount(xRange.tickCount + 1);
    chart->addAxis(axisX, Qt::AlignBottom);
    area->attachAxis(axisX);

    auto* axisY = new QValueAxis();
    axisY->setTitleText(QStringLiteral("Amplitude (MPa)"));
    const double yMaxRaw = plot.y.size() ? plot.y.maxCoeff() : 0.0;
    const NiceAxisRange yRange = niceAxisRange(yMaxRaw);
    axisY->setRange(0.0, yRange.max);
    axisY->setTickCount(yRange.tickCount + 1);
    chart->addAxis(axisY, Qt::AlignLeft);
    area->attachAxis(axisY);

    return chart;
}

QChart* buildSonicationTimelineChart(const std::vector<beam::stimulation::TimelineSegment>& timeline,
                                      const QString& title) {
    auto* upper = new QLineSeries();
    double maxTime = 1.0;
    for (const beam::stimulation::TimelineSegment& seg : timeline) {
        const double v = seg.isBurst ? 1.0 : 0.0;
        upper->append(seg.timeOn, v);
        upper->append(seg.timeOff, v);
        maxTime = std::max(maxTime, seg.timeOff);
    }
    auto* area = new QAreaSeries(upper);

    auto* chart = new QChart();
    chart->addSeries(area);
    chart->setTitle(title);
    chart->legend()->setVisible(false);

    auto* axisX = new QValueAxis();
    axisX->setTitleText(QStringLiteral("Time (s)"));
    axisX->setRange(0.0, maxTime);
    chart->addAxis(axisX, Qt::AlignBottom);
    area->attachAxis(axisX);

    auto* axisY = new QValueAxis();
    axisY->setTitleText(QStringLiteral("Burst on/off"));
    axisY->setRange(0.0, 1.1);
    axisY->setLabelFormat(QStringLiteral("%d"));
    chart->addAxis(axisY, Qt::AlignLeft);
    area->attachAxis(axisY);

    return chart;
}

}  // namespace beam::gui_qt
