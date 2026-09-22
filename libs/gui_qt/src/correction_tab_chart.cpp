#include "gui_qt/correction_tab_chart.hpp"

#include <algorithm>

#include <QColor>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

namespace beam::gui_qt {

QChart* buildTransmissionBarChart(const beam::gui::AvgTransmissionBars& bars) {
    // Two QBarSets, each nonzero only in its own category -- the standard Qt
    // Charts idiom for per-category bar colors within one series.
    auto* currentSet = new QBarSet(QStringLiteral("Current"));
    *currentSet << bars.currentValue << 0.0;
    currentSet->setColor(bars.currentBarIsRed ? QColor(Qt::red) : QColor(Qt::green));

    auto* averageSet = new QBarSet(QStringLiteral("Average"));
    *averageSet << 0.0 << bars.averageValue;
    averageSet->setColor(Qt::green);

    auto* series = new QBarSeries();
    series->append(currentSet);
    series->append(averageSet);

    auto* chart = new QChart();
    chart->addSeries(series);
    chart->setTitle(QStringLiteral("Through-transmit: current vs average"));
    chart->legend()->setVisible(false);

    auto* axisX = new QBarCategoryAxis();
    axisX->append({QStringLiteral("Current"), QStringLiteral("Average")});
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto* axisY = new QValueAxis();
    axisY->setTitleText(QStringLiteral("Transmission"));
    const double maxValue = std::max(bars.currentValue, bars.averageValue);
    axisY->setRange(0.0, maxValue > 0.0 ? maxValue * 1.2 : 1.0);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    return chart;
}

QChart* buildRfWaveformChart(const Eigen::VectorXd& raw, const Eigen::VectorXd& filtered,
                             double yLimit, const QString& title) {
    auto* rawSeries = new QLineSeries();
    rawSeries->setName(QStringLiteral("Received"));
    for (Eigen::Index i = 0; i < raw.size(); ++i) {
        rawSeries->append(static_cast<double>(i + 1), raw(i));
    }

    auto* filteredSeries = new QLineSeries();
    filteredSeries->setName(QStringLiteral("Filtered"));
    for (Eigen::Index i = 0; i < filtered.size(); ++i) {
        filteredSeries->append(static_cast<double>(i + 1), filtered(i));
    }

    auto* chart = new QChart();
    chart->addSeries(rawSeries);
    chart->addSeries(filteredSeries);
    chart->setTitle(title);

    const Eigen::Index n = std::max(raw.size(), filtered.size());

    auto* axisX = new QValueAxis();
    axisX->setTitleText(QStringLiteral("Sample"));
    axisX->setRange(1.0, static_cast<double>(std::max<Eigen::Index>(n, 1)));
    chart->addAxis(axisX, Qt::AlignBottom);
    rawSeries->attachAxis(axisX);
    filteredSeries->attachAxis(axisX);

    auto* axisY = new QValueAxis();
    axisY->setRange(-yLimit, yLimit);
    chart->addAxis(axisY, Qt::AlignLeft);
    rawSeries->attachAxis(axisY);
    filteredSeries->attachAxis(axisY);

    return chart;
}

}  // namespace beam::gui_qt
