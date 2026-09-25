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
    const double triangleWidth = std::min(120.0, std::max(56.0, half - 72.0));
    const double triangleHeight = std::min(78.0, std::max(45.0, height() - 42.0));
    const double top = (height() - triangleHeight) / 2.0 - 2.0;
    const double bottom = top + triangleHeight;
    const double leftCenter = half / 2.0;
    const double rightCenter = half + half / 2.0;
    const double left = leftCenter - triangleWidth / 2.0;
    const double right = rightCenter + triangleWidth / 2.0;
    return {QPointF(left, top), QPointF(left, bottom), QPointF(left + triangleWidth, bottom),
            QPointF(right - triangleWidth, top), QPointF(right - triangleWidth, bottom), QPointF(right, bottom)};
}

void RegistrationFiducialLayout::setMarkerCoordinateLabels(std::array<QString, 6> labels) {
    coordinateLabels_ = std::move(labels);
    update();
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
        QStringLiteral("1. LeftY1Z3"), QStringLiteral("2. LeftY1Z1"), QStringLiteral("3. LeftY4Z1"),
        QStringLiteral("4. RightY1Z3"), QStringLiteral("5. RightY1Z1"), QStringLiteral("6. RightY4Z1")};
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
        if (!coordinateLabels_[index].isEmpty()) {
            QFont coordinateFont = labelFont;
            coordinateFont.setPointSizeF(std::max(7.0, labelFont.pointSizeF() - 1.0));
            painter.setFont(coordinateFont);
            painter.setPen(QColor(95, 111, 120));
            painter.drawText(QRectF(labelX, labelY + 15.0, width() / 2.0 - 55.0, 18.0),
                             Qt::AlignVCenter, coordinateLabels_[index]);
            painter.setFont(labelFont);
        }
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
