#include "gui_qt/total_sonication_view.hpp"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QToolTip>

#include "gui_qt/chart_axis_utils.hpp"

namespace beam::gui_qt {

TotalSonicationView::TotalSonicationView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
    setAutoFillBackground(true);

    // Styles the (x, y) hover tooltip Qt shows via QToolTip::showText(...,
    // this) below -- white background regardless of OS theme, a bit
    // larger/more legible than the system default, with a border/radius
    // matching the bar's own highlight color.
    setStyleSheet(QStringLiteral(
        "QToolTip {"
        "background-color: #ffffff;"
        "color: #1a1a1a;"
        "border: 1px solid #e68c14;"
        "border-radius: 6px;"
        "padding: 6px 10px;"
        "font-size: 11pt;"
        "}"));
}

void TotalSonicationView::setData(const std::vector<beam::stimulation::TimelineSegment>& timeline, double amplitude,
                                  double durationSeconds) {
    timeline_ = timeline;
    amplitude_ = amplitude;
    durationSeconds_ = durationSeconds;

    // X axis: tight to the real duration (no zero-based padding past it)
    // -- ticks at 0/5/10/.../30 for SCC1's default 30s duration, not a
    // "nice max" like 50 that leaves dead space after the last burst.
    xTickStep_ = niceStep(std::max(durationSeconds_, 1.0), 6.0);
    xAxisMax_ = std::ceil(std::max(durationSeconds_, 1.0) / xTickStep_ - 1e-9) * xTickStep_;

    // Y axis: 0 to amplitude + one unit step (e.g. 0.5 -> 0.6), with a
    // separate, coarser tick step targeting ~4 ticks so labels don't
    // crowd together.
    if (amplitude_ > 0.0) {
        const double unitStep = std::pow(10.0, std::floor(std::log10(amplitude_)));
        yAxisMin_ = 0.0;
        yAxisMax_ = amplitude_ + unitStep;
    } else {
        yAxisMin_ = 0.0;
        yAxisMax_ = 0.3;
    }
    yTickStep_ = niceStep(yAxisMax_, 4.0);

    update();
}

QSize TotalSonicationView::minimumSizeHint() const { return QSize(320, 160); }

QRectF TotalSonicationView::plotRect() const {
    const int leftMargin = 56;
    const int rightMargin = 16;
    const int topMargin = 26;
    const int bottomMargin = 40;
    return QRectF(leftMargin, topMargin, std::max(0, width() - leftMargin - rightMargin),
                  std::max(0, height() - topMargin - bottomMargin));
}

QRectF TotalSonicationView::segmentBarRect(const beam::stimulation::TimelineSegment& seg, const QRectF& plot) const {
    const double yRange = std::max(yAxisMax_ - yAxisMin_, 1e-9);
    const auto xToPixel = [&](double t) { return plot.left() + (t / xAxisMax_) * plot.width(); };
    const auto yToPixel = [&](double a) { return plot.bottom() - ((a - yAxisMin_) / yRange) * plot.height(); };
    const double x0 = xToPixel(seg.timeOn);
    const double x1 = std::max(xToPixel(seg.timeOff), x0 + 2.0);
    const double yTop = yToPixel(amplitude_);
    const double yBottom = yToPixel(0.0);
    return QRectF(QPointF(x0, yTop), QPointF(x1, yBottom));
}

void TotalSonicationView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), Qt::white);

    const QRectF plot = plotRect();
    if (plot.width() <= 0 || plot.height() <= 0) return;

    painter.setPen(QPen(QColor(60, 60, 60)));
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(rect().adjusted(0, 4, 0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("Total sonication"));

    QFont axisFont = painter.font();
    axisFont.setBold(false);
    axisFont.setPointSizeF(axisFont.pointSizeF() - 1.0);
    painter.setFont(axisFont);

    const double yRange = std::max(yAxisMax_ - yAxisMin_, 1e-9);
    const auto xToPixel = [&](double t) { return plot.left() + (t / xAxisMax_) * plot.width(); };
    const auto yToPixel = [&](double a) { return plot.bottom() - ((a - yAxisMin_) / yRange) * plot.height(); };

    // Y gridlines + tick labels + small axis tick marks.
    const int yTickCount = static_cast<int>(std::lround(yRange / yTickStep_));
    const int yDecimals = decimalsForStep(yTickStep_);
    painter.setPen(QPen(QColor(225, 225, 225)));
    for (int i = 0; i <= yTickCount; ++i) {
        const double value = yAxisMin_ + yTickStep_ * i;
        const double y = yToPixel(value);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QPen(QColor(80, 80, 80)));
    for (int i = 0; i <= yTickCount; ++i) {
        const double value = yAxisMin_ + yTickStep_ * i;
        const double y = yToPixel(value);
        painter.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
        const QString label = QString::number(value, 'f', yDecimals);
        painter.drawText(QRectF(0, y - 8, plot.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // X tick labels + small axis tick marks, at 0/xTickStep_/.../xAxisMax_.
    const int xTickCount = static_cast<int>(std::lround(xAxisMax_ / xTickStep_));
    const int xDecimals = decimalsForStep(xTickStep_);
    for (int i = 0; i <= xTickCount; ++i) {
        const double value = xTickStep_ * i;
        const double x = xToPixel(value);
        painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        const QString label = QString::number(value, 'f', xDecimals);
        painter.drawText(QRectF(x - 24, plot.bottom() + 6, 48, 16), Qt::AlignHCenter | Qt::AlignTop, label);
    }

    // Axis lines.
    painter.setPen(QPen(QColor(80, 80, 80)));
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.drawLine(plot.bottomLeft(), plot.topLeft());

    // Axis titles -- the .mlapp's own real xlabel/ylabel for
    // axTotalSonicationPlot.
    painter.drawText(QRectF(plot.left(), plot.bottom() + 22, plot.width(), 16), Qt::AlignHCenter | Qt::AlignTop,
                     QStringLiteral("Time (s)"));
    painter.save();
    painter.translate(14, plot.top() + plot.height() / 2.0);
    painter.rotate(-90);
    painter.drawText(QRectF(-plot.height() / 2.0, -8, plot.height(), 16), Qt::AlignHCenter | Qt::AlignTop,
                     QStringLiteral("Amplitude (MPa)"));
    painter.restore();

    // Burst bars -- floor a minimum pixel width so very short bursts
    // (e.g. 0.03s in a 30s window) stay visible, with real gaps between
    // adjacent bars at SCC1's 0.7s burst interval. Clipped to the plot
    // rect since each bar's true bottom (amplitude 0) sits below
    // yAxisMin_ -- off screen now that the Y axis is centered on
    // amplitude_ instead of starting at 0 -- so only the sliver from
    // yAxisMin_ up to amplitude_ should actually render.
    painter.setClipRect(plot);
    for (std::size_t i = 0; i < timeline_.size(); ++i) {
        const beam::stimulation::TimelineSegment& seg = timeline_[i];
        if (!seg.isBurst) continue;
        const bool hovered = (static_cast<int>(i) == hoveredIndex_);
        painter.setPen(QPen(hovered ? QColor(230, 140, 20) : QColor(14, 124, 134), hovered ? 1.2 : 0.6));
        painter.setBrush(hovered ? QColor(230, 140, 20, 160) : QColor(14, 124, 134, 90));
        painter.drawRect(segmentBarRect(seg, plot));
    }
    painter.setClipping(false);
}

void TotalSonicationView::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
    const QRectF plot = plotRect();
    const QPointF pos = event->position();

    int newHoveredIndex = -1;
    if (plot.contains(pos)) {
        for (std::size_t i = 0; i < timeline_.size(); ++i) {
            const beam::stimulation::TimelineSegment& seg = timeline_[i];
            if (!seg.isBurst) continue;
            if (segmentBarRect(seg, plot).contains(pos)) {
                newHoveredIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (newHoveredIndex != hoveredIndex_) {
        hoveredIndex_ = newHoveredIndex;
        update();
    }

    if (newHoveredIndex < 0 || xAxisMax_ <= 0.0) {
        QToolTip::hideText();
        return;
    }
    const double t = ((pos.x() - plot.left()) / plot.width()) * xAxisMax_;
    const int yDecimals = decimalsForStep(yTickStep_);
    QToolTip::showText(event->globalPosition().toPoint(),
                       QStringLiteral("X: %1\nY: %2").arg(t, 0, 'f', 1).arg(amplitude_, 0, 'f', yDecimals), this);
}

void TotalSonicationView::leaveEvent(QEvent* /*event*/) {
    QToolTip::hideText();
    if (hoveredIndex_ != -1) {
        hoveredIndex_ = -1;
        update();
    }
}

}  // namespace beam::gui_qt
