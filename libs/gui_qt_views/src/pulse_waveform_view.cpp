#include "gui_qt/pulse_waveform_view.hpp"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>

#include "gui_qt/chart_axis_utils.hpp"

namespace beam::gui_qt {

PulseWaveformView::PulseWaveformView(QWidget* parent) : QWidget(parent) {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    setAutoFillBackground(true);
}

void PulseWaveformView::setData(const beam::gui::PulseWaveformPlot& plot, const QString& title,
                                const QString& xAxisLabel) {
    x_ = plot.x;
    y_ = plot.y;
    title_ = title;
    xAxisLabel_ = xAxisLabel;

    const double xMaxRaw = x_.size() ? x_(x_.size() - 1) : 1.0;
    const NiceAxisRange xRange = niceAxisRange(xMaxRaw);
    xAxisMax_ = xRange.max;
    xTickStep_ = xRange.max / std::max(xRange.tickCount, 1);

    const double yMaxRaw = y_.size() ? y_.maxCoeff() : 0.0;
    const NiceAxisRange yRange = niceAxisRange(yMaxRaw);
    yAxisMax_ = yRange.max;
    yTickStep_ = yRange.max / std::max(yRange.tickCount, 1);

    update();
}

QSize PulseWaveformView::minimumSizeHint() const { return QSize(240, 100); }

QRectF PulseWaveformView::plotRect() const {
    const int leftMargin = 50;
    const int rightMargin = 12;
    const int topMargin = 22;
    const int bottomMargin = 36;
    return QRectF(leftMargin, topMargin, std::max(0, width() - leftMargin - rightMargin),
                  std::max(0, height() - topMargin - bottomMargin));
}

void PulseWaveformView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), Qt::white);

    const QRectF plot = plotRect();
    if (plot.width() <= 0 || plot.height() <= 0) return;

    painter.setPen(QPen(QColor(60, 60, 60)));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(rect().adjusted(0, 2, 0, 0), Qt::AlignHCenter | Qt::AlignTop, title_);

    QFont axisFont = painter.font();
    axisFont.setBold(false);
    axisFont.setPointSizeF(std::max(7.0, axisFont.pointSizeF() - 1.5));
    painter.setFont(axisFont);

    const auto xToPixel = [&](double t) { return plot.left() + (t / xAxisMax_) * plot.width(); };
    const auto yToPixel = [&](double a) { return plot.bottom() - (a / yAxisMax_) * plot.height(); };

    // Y gridlines + tick labels + small axis tick marks.
    const int yTicks = static_cast<int>(std::lround(yAxisMax_ / yTickStep_));
    const int yDecimals = decimalsForStep(yTickStep_);
    painter.setPen(QPen(QColor(225, 225, 225)));
    for (int i = 0; i <= yTicks; ++i) {
        const double y = yToPixel(yTickStep_ * i);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QPen(QColor(80, 80, 80)));
    for (int i = 0; i <= yTicks; ++i) {
        const double value = yTickStep_ * i;
        const double y = yToPixel(value);
        painter.drawLine(QPointF(plot.left() - 3, y), QPointF(plot.left(), y));
        painter.drawText(QRectF(0, y - 7, plot.left() - 6, 14), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(value, 'f', yDecimals));
    }

    // X tick labels + small axis tick marks.
    const int xTicks = static_cast<int>(std::lround(xAxisMax_ / xTickStep_));
    const int xDecimals = decimalsForStep(xTickStep_);
    for (int i = 0; i <= xTicks; ++i) {
        const double value = xTickStep_ * i;
        const double x = plot.left() + (value / xAxisMax_) * plot.width();
        painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 3));
        painter.drawText(QRectF(x - 20, plot.bottom() + 4, 40, 14), Qt::AlignHCenter | Qt::AlignTop,
                         QString::number(value, 'f', xDecimals));
    }

    painter.setPen(QPen(QColor(80, 80, 80)));
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.drawLine(plot.bottomLeft(), plot.topLeft());

    painter.drawText(QRectF(plot.left(), plot.bottom() + 18, plot.width(), 14), Qt::AlignHCenter | Qt::AlignTop,
                     xAxisLabel_);
    painter.save();
    painter.translate(12, plot.top() + plot.height() / 2.0);
    painter.rotate(-90);
    painter.drawText(QRectF(-plot.height() / 2.0, -7, plot.height(), 14), Qt::AlignHCenter | Qt::AlignTop,
                     QStringLiteral("Amplitude (MPa)"));
    painter.restore();

    // The waveform itself -- a filled area under the step curve, clipped
    // to the plot rect (matches the old QAreaSeries look).
    if (x_.size() >= 2) {
        QPolygonF poly;
        poly.reserve(static_cast<int>(x_.size()) + 2);
        poly << QPointF(xToPixel(x_(0)), yToPixel(0.0));
        for (Eigen::Index i = 0; i < x_.size(); ++i) {
            poly << QPointF(xToPixel(x_(i)), yToPixel(y_(i)));
        }
        poly << QPointF(xToPixel(x_(x_.size() - 1)), yToPixel(0.0));

        painter.setClipRect(plot);
        painter.setPen(QPen(QColor(14, 124, 134), 1.0));
        painter.setBrush(QColor(14, 124, 134, 90));
        painter.drawPolygon(poly);
        painter.setClipping(false);
    }
}

}  // namespace beam::gui_qt
