#include "registration_fiducial_layout.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

RegistrationFiducialLayout::RegistrationFiducialLayout(QWidget* parent) : QWidget(parent) {
    setMinimumSize(470, 190);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Select a fiducial from the left or right array layout"));
}

void RegistrationFiducialLayout::setSelectedIndex(int index) {
    selectedIndex_ = index >= 0 && index < 6 ? index : -1;
    update();
}

void RegistrationFiducialLayout::setSelectionHandler(std::function<void(int)> handler) {
    selectionHandler_ = std::move(handler);
}

std::array<QPointF, 6> RegistrationFiducialLayout::markerCenters() const {
    const double half = width() / 2.0;
    const double top = 22.0;
    const double bottom = height() - 38.0;
    return {QPointF(32.0, top), QPointF(32.0, bottom), QPointF(half - 45.0, bottom),
            QPointF(half + 32.0, top), QPointF(half + 32.0, bottom), QPointF(width() - 45.0, bottom)};
}

void RegistrationFiducialLayout::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(248, 250, 251));
    painter.setPen(QPen(QColor(210, 220, 225), 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const auto points = markerCenters();
    painter.setPen(QPen(QColor(115, 135, 145), 2.0));
    painter.setBrush(QColor(228, 237, 241, 110));
    for (int base : {0, 3}) {
        QPainterPath triangle;
        triangle.moveTo(points[base]);
        triangle.lineTo(points[base + 1]);
        triangle.lineTo(points[base + 2]);
        triangle.closeSubpath();
        painter.drawPath(triangle);
    }

    static const std::array<QString, 6> names = {
        QStringLiteral("LeftY1Z3"), QStringLiteral("LeftY1Z1"), QStringLiteral("LeftY4Z1"),
        QStringLiteral("RightY1Z3"), QStringLiteral("RightY1Z1"), QStringLiteral("RightY4Z1")};
    QFont labelFont = painter.font();
    labelFont.setBold(false);
    labelFont.setPointSizeF(std::max(8.0, labelFont.pointSizeF() - 1.0));
    painter.setFont(labelFont);
    for (int index = 0; index < 6; ++index) {
        const bool selected = index == selectedIndex_;
        painter.setPen(QPen(selected ? QColor(20, 112, 142) : QColor(190, 38, 52), selected ? 3.0 : 2.0));
        painter.setBrush(selected ? QColor(92, 205, 229) : QColor(255, 245, 246));
        painter.drawEllipse(points[index], selected ? 8.0 : 6.0, selected ? 8.0 : 6.0);
        painter.setPen(selected ? QColor(13, 85, 109) : QColor(75, 48, 52));
        const double labelX = points[index].x() + 10.0;
        const double labelY = points[index].y() - 10.0;
        painter.drawText(QRectF(labelX, labelY, width() / 2.0 - 55.0, 22.0), Qt::AlignVCenter, names[index]);
    }
}

void RegistrationFiducialLayout::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const auto points = markerCenters();
    int nearest = -1;
    double nearestDistance = 18.0;
    for (int index = 0; index < 6; ++index) {
        const double distance = std::hypot(event->position().x() - points[index].x(),
                                           event->position().y() - points[index].y());
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    if (nearest >= 0) {
        setSelectedIndex(nearest);
        if (selectionHandler_) selectionHandler_(nearest);
    }
}
