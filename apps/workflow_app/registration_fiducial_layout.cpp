#include "registration_fiducial_layout.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QFontMetricsF>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

RegistrationFiducialLayout::RegistrationFiducialLayout(QWidget* parent) : QWidget(parent) {
    // The registration window controls the pane height; do not impose the
    // old 190-pixel minimum, which leaves an empty band above the Restore
    // button when the compact layout is used.
    setMinimumSize(300, 0);
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
    const double budgetWidth = std::min(120.0, std::max(56.0, half - 72.0));
    const double budgetHeight = std::max(45.0, height() - 42.0);
    const double leftCenter = half / 2.0;
    const double rightCenter = half + half / 2.0;
    const double midY = height() / 2.0 - 2.0;

    if (hasPositions_) {
        // Screen axes against patient axes: x grows as AP shrinks (anterior to
        // the left of the panel), y grows as IS shrinks (superior at the top).
        std::array<QPointF, 6> offset{};
        for (int i = 0; i < 6; ++i)
            offset[i] = QPointF(-positionsApIsMm_[i].x(), -positionsApIsMm_[i].y());

        double spanX = 0.0, spanY = 0.0;
        std::array<QPointF, 2> centre{};
        for (int side = 0; side < 2; ++side) {
            const int base = side * 3;
            double loX = offset[base].x(), hiX = loX, loY = offset[base].y(), hiY = loY;
            for (int k = 1; k < 3; ++k) {
                loX = std::min(loX, offset[base + k].x());
                hiX = std::max(hiX, offset[base + k].x());
                loY = std::min(loY, offset[base + k].y());
                hiY = std::max(hiY, offset[base + k].y());
            }
            centre[side] = QPointF((loX + hiX) / 2.0, (loY + hiY) / 2.0);
            spanX = std::max(spanX, hiX - loX);
            spanY = std::max(spanY, hiY - loY);
        }

        // One isotropic scale for both sides: anything else would distort the
        // angles, which is the whole point of drawing the measured shape, and
        // would stop the two triangles being comparable to each other.
        if (spanX > 1e-6 && spanY > 1e-6) {
            const double scale = std::min(budgetWidth / spanX, budgetHeight / spanY);
            std::array<QPointF, 6> points{};
            for (int side = 0; side < 2; ++side) {
                const double cx = side == 0 ? leftCenter : rightCenter;
                for (int k = 0; k < 3; ++k) {
                    const int i = side * 3 + k;
                    points[i] = QPointF(cx + (offset[i].x() - centre[side].x()) * scale,
                                        midY + (offset[i].y() - centre[side].y()) * scale);
                }
            }
            return points;
        }
    }

    // Fallback: the nominal shape, which is a right triangle with a 22.5 mm
    // front-to-back arm and a 20.0 mm top-to-bottom one.
    constexpr double kArmRatio = 20.0 / 22.5;
    const double triangleWidth = budgetWidth;
    const double triangleHeight = std::min(triangleWidth * kArmRatio, budgetHeight);
    const double top = midY - triangleHeight / 2.0;
    const double bottom = top + triangleHeight;
    const double left = leftCenter - triangleWidth / 2.0;
    const double right = rightCenter + triangleWidth / 2.0;
    return {QPointF(left, top), QPointF(left, bottom), QPointF(left + triangleWidth, bottom),
            QPointF(right - triangleWidth, top), QPointF(right - triangleWidth, bottom),
            QPointF(right, bottom)};
}

void RegistrationFiducialLayout::setMarkerPositions(std::array<QPointF, 6> positionsApIsMm) {
    positionsApIsMm_ = positionsApIsMm;
    hasPositions_ = true;
    update();
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

    // The corner angles, drawn just inside each vertex. Worth showing because
    // the nominal array is a right triangle and a measured one is not: the
    // number is where that difference becomes readable.
    {
        QFont angleFont = painter.font();
        angleFont.setPointSizeF(std::max(7.5, angleFont.pointSizeF() - 1.5));
        angleFont.setBold(true);
        painter.setFont(angleFont);
        painter.setPen(QColor(96, 118, 128));
        for (int base : {0, 3}) {
            const QPointF centroid =
                (points[base] + points[base + 1] + points[base + 2]) / 3.0;
            for (int k = 0; k < 3; ++k) {
                const QPointF at = points[base + k];
                const QPointF armA = points[base + (k + 1) % 3] - at;
                const QPointF armB = points[base + (k + 2) % 3] - at;
                const double lengths = std::hypot(armA.x(), armA.y()) * std::hypot(armB.x(), armB.y());
                if (lengths <= 0.0) continue;
                const double cosine =
                    std::clamp((armA.x() * armB.x() + armA.y() * armB.y()) / lengths, -1.0, 1.0);
                constexpr double kPi = 3.14159265358979323846;
                const QString text =
                    QStringLiteral("%1°").arg(std::acos(cosine) * 180.0 / kPi, 0, 'f', 0);
                // Step in from the vertex towards the centre, so the number sits
                // inside the corner it belongs to and clear of the name label.
                QPointF inward = centroid - at;
                const double reach = std::hypot(inward.x(), inward.y());
                if (reach > 1.0) inward *= 24.0 / reach;
                const QPointF textAt = at + inward;
                painter.drawText(QRectF(textAt.x() - 18.0, textAt.y() - 8.0, 36.0, 16.0),
                                 Qt::AlignCenter, text);
            }
        }
    }

    static const std::array<QString, 6> names = {
        QStringLiteral("1. LeftY1Z3"), QStringLiteral("2. LeftY1Z1"), QStringLiteral("3. LeftY4Z1"),
        QStringLiteral("4. RightY1Z3"), QStringLiteral("5. RightY1Z1"), QStringLiteral("6. RightY4Z1")};
    QFont labelFont = painter.font();
    labelFont.setBold(false);
    labelFont.setPointSizeF(std::max(8.0, labelFont.pointSizeF() - 1.0));
    QFont coordinateFont = labelFont;
    coordinateFont.setPointSizeF(std::max(7.0, labelFont.pointSizeF() - 1.0));
    const QFontMetricsF labelMetrics(labelFont);
    const QFontMetricsF coordinateMetrics(coordinateFont);
    painter.setFont(labelFont);

    // Place every label before drawing any, so overlaps can be resolved. Each
    // one starts outward from its own triangle's centre and inside that
    // triangle's half of the pane; without the half, the two inner vertices
    // both write into the gap between the triangles.
    struct Placement {
        QRectF box;
        Qt::Alignment alignment = Qt::AlignVCenter;
        double nameHeight = 0.0;
    };
    std::array<Placement, 6> placements{};
    for (int index = 0; index < 6; ++index) {
        double textWidth = labelMetrics.horizontalAdvance(names[index]);
        if (!coordinateLabels_[index].isEmpty())
            textWidth = std::max(
                textWidth, coordinateMetrics.horizontalAdvance(coordinateLabels_[index]));
        const double nameHeight = labelMetrics.height();
        const double blockHeight =
            nameHeight +
            (coordinateLabels_[index].isEmpty() ? 0.0 : coordinateMetrics.height());
        const int base = (index / 3) * 3;
        const QPointF centroid = (points[base] + points[base + 1] + points[base + 2]) / 3.0;
        const double xLo = base == 0 ? 3.0 : width() / 2.0 + 3.0;
        const double xHi = base == 0 ? width() / 2.0 - 3.0 : width() - 3.0;
        bool toLeft = points[index].x() < centroid.x();
        if (toLeft && points[index].x() - 10.0 - textWidth < xLo) toLeft = false;
        if (!toLeft && points[index].x() + 10.0 + textWidth > xHi) toLeft = true;
        const double labelX = std::clamp(toLeft ? points[index].x() - 10.0 - textWidth
                                                : points[index].x() + 10.0,
                                         xLo, std::max(xLo, xHi - textWidth));
        const double labelY = points[index].y() < centroid.y()
                                  ? points[index].y() - 9.0 - blockHeight
                                  : points[index].y() + 9.0;
        placements[index] = {QRectF(labelX, labelY, textWidth, blockHeight),
                             Qt::AlignVCenter | (toLeft ? Qt::AlignRight : Qt::AlignLeft),
                             nameHeight};
    }

    // Two vertices at nearly the same height -- markers 5 and 6 are 2 mm apart
    // on some scans -- would otherwise land in one strip. Move the later block
    // clear of the earlier, below it when the pane has room and above it when
    // it does not.
    for (int index = 1; index < 6; ++index) {
        for (int pass = 0; pass < 3; ++pass) {
            bool moved = false;
            for (int other = 0; other < index; ++other) {
                if (index / 3 != other / 3) continue;
                if (!placements[index].box.intersects(placements[other].box)) continue;
                const double below = placements[other].box.bottom() + 2.0;
                if (below + placements[index].box.height() <= height() - 2.0)
                    placements[index].box.moveTop(below);
                else
                    placements[index].box.moveBottom(placements[other].box.top() - 2.0);
                moved = true;
            }
            if (!moved) break;
        }
        placements[index].box.moveTop(
            std::clamp(placements[index].box.top(), 2.0,
                       std::max(2.0, height() - placements[index].box.height() - 2.0)));
    }

    for (int index = 0; index < 6; ++index) {
        const bool selected = index == selectedIndex_;
        painter.setPen(QPen(selected ? QColor(20, 112, 142) : QColor(190, 38, 52), selected ? 3.0 : 2.0));
        painter.setBrush(selected ? QColor(92, 205, 229) : QColor(255, 245, 246));
        painter.drawEllipse(points[index], selected ? 8.0 : 6.0, selected ? 8.0 : 6.0);
        painter.setPen(selected ? QColor(13, 85, 109) : QColor(75, 48, 52));

        const Placement& at = placements[index];
        painter.drawText(QRectF(at.box.left(), at.box.top(), at.box.width(), at.nameHeight),
                         at.alignment, names[index]);
        if (!coordinateLabels_[index].isEmpty()) {
            painter.setFont(coordinateFont);
            painter.setPen(QColor(95, 111, 120));
            painter.drawText(QRectF(at.box.left(), at.box.top() + at.nameHeight, at.box.width(),
                                    coordinateMetrics.height()),
                             at.alignment, coordinateLabels_[index]);
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
