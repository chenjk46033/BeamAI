#include "mri_view.hpp"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

WorkflowMriView::WorkflowMriView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
}

void WorkflowMriView::setNavigationCrosshair(QPointF normalizedPosition) {
    crosshair_.setX(std::clamp(normalizedPosition.x(), 0.0, 1.0));
    crosshair_.setY(std::clamp(normalizedPosition.y(), 0.0, 1.0));
    update();
}

void WorkflowMriView::setMaskOverlay(const Eigen::MatrixXd& mask, QColor color, double opacity,
                                     bool flipHorizontal) {
    if (mask.size() == 0) {
        maskImage_ = {};
        update();
        return;
    }
    color.setAlphaF(std::clamp(opacity, 0.0, 1.0));
    QImage result(static_cast<int>(mask.cols()), static_cast<int>(mask.rows()), QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    for (int y = 0; y < result.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            if (mask(y, x) > 0.0) row[x] = color.rgba();
        }
    }
    maskImage_ = flipHorizontal ? result.mirrored(true, false) : result;
    update();
}

void WorkflowMriView::setMarkers(std::vector<WorkflowMriMarker> markers) {
    markers_ = std::move(markers);
    update();
}

void WorkflowMriView::setRasMapping(QString plane, double fixedCoordinateMm,
                                    double horizontalMinMm, double horizontalMaxMm,
                                    double verticalMinMm, double verticalMaxMm,
                                    bool reverseHorizontal, bool reverseVertical) {
    plane_ = std::move(plane);
    fixedCoordinateMm_ = fixedCoordinateMm;
    horizontalMinMm_ = horizontalMinMm;
    horizontalMaxMm_ = horizontalMaxMm;
    verticalMinMm_ = verticalMinMm;
    verticalMaxMm_ = verticalMaxMm;
    reverseHorizontal_ = reverseHorizontal;
    reverseVertical_ = reverseVertical;
}

void WorkflowMriView::setSlice(const Eigen::MatrixXd& slice, bool flipHorizontal) {
    slice_ = slice;
    flipHorizontal_ = flipHorizontal;
    if (slice_.size() > 0) {
        dataMin_ = slice_.minCoeff();
        dataMax_ = slice_.maxCoeff();
        if (dataMax_ <= dataMin_) dataMax_ = dataMin_ + 1.0;
    }
    rebuildImage();
    update();
}

void WorkflowMriView::resetView() {
    zoom_ = 1.0;
    brightness_ = 1.0;
    contrast_ = 1.0;
    pan_ = {};
    rebuildImage();
    update();
}

void WorkflowMriView::focusOn(QPointF normalizedPosition, double zoom) {
    zoom_ = std::clamp(zoom, 1.0, 10.0);
    pan_ = {};
    const QRectF shown = imageRect();
    const QPointF point(shown.left() + normalizedPosition.x() * shown.width(),
                        shown.top() + normalizedPosition.y() * shown.height());
    pan_ = QPointF(width() / 2.0, height() / 2.0) - point;
    clampPan();
    update();
}

void WorkflowMriView::setPointPlacementEnabled(bool enabled) {
    pointPlacementEnabled_ = enabled;
    setCursor(enabled ? Qt::CrossCursor : Qt::OpenHandCursor);
}

void WorkflowMriView::setPointPickedHandler(std::function<void(const Eigen::Vector3d&)> handler) {
    pointPickedHandler_ = std::move(handler);
}

void WorkflowMriView::rebuildImage() {
    if (slice_.size() == 0) {
        image_ = {};
        return;
    }
    const double range = dataMax_ - dataMin_;
    const double baseHigh = dataMin_ + range * brightness_;
    const double center = (dataMin_ + baseHigh) * 0.5;
    const double width = std::max(range * 0.01, (baseHigh - dataMin_) / contrast_);
    const double windowLow = center - width * 0.5;
    const double windowHigh = center + width * 0.5;
    const double scale = 255.0 / (windowHigh - windowLow);
    QImage result(static_cast<int>(slice_.cols()), static_cast<int>(slice_.rows()), QImage::Format_Grayscale8);
    for (int y = 0; y < result.height(); ++y) {
        auto* row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            row[x] = static_cast<unsigned char>(std::clamp((slice_(y, x) - windowLow) * scale, 0.0, 255.0));
        }
    }
    image_ = flipHorizontal_ ? result.mirrored(true, false) : result;
}

QRectF WorkflowMriView::imageRect() const {
    if (image_.isNull()) return {};
    const QSizeF available = size();
    const double fit = std::min(available.width() / image_.width(), available.height() / image_.height());
    const QSizeF displayed(image_.width() * fit * zoom_, image_.height() * fit * zoom_);
    return QRectF(QPointF((width() - displayed.width()) / 2.0, (height() - displayed.height()) / 2.0) + pan_, displayed);
}

void WorkflowMriView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(16, 23, 27));
    if (image_.isNull()) {
        painter.setPen(QColor(159, 176, 186));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF displayed = imageRect();
    painter.drawImage(displayed, image_);
    if (!maskImage_.isNull()) painter.drawImage(displayed, maskImage_);
    const double x = displayed.left() + crosshair_.x() * displayed.width();
    const double y = displayed.top() + crosshair_.y() * displayed.height();
    painter.setPen(QPen(QColor(40, 225, 245, 235), 2.0));
    painter.drawLine(QPointF(x, displayed.top()), QPointF(x, displayed.bottom()));
    painter.drawLine(QPointF(displayed.left(), y), QPointF(displayed.right(), y));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(x, y), 5.0, 5.0);

    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const WorkflowMriMarker& marker : markers_) {
        const QPointF point(displayed.left() + marker.normalizedPosition.x() * displayed.width(),
                            displayed.top() + marker.normalizedPosition.y() * displayed.height());
        painter.setPen(QPen(marker.color, marker.crosshair ? 2.0 : 1.5));
        if (marker.crosshair) {
            painter.drawLine(point + QPointF(-13, 0), point + QPointF(13, 0));
            painter.drawLine(point + QPointF(0, -13), point + QPointF(0, 13));
        } else {
            painter.setBrush(marker.color);
            painter.drawEllipse(point, 4.0, 4.0);
            if (!marker.label.isEmpty()) {
                const QRectF labelRect(point + QPointF(7, -17), QSizeF(105, 18));
                painter.fillRect(labelRect, QColor(150, 25, 35, 205));
                painter.setPen(Qt::white);
                painter.drawText(labelRect.adjusted(4, 0, -2, 0), Qt::AlignVCenter, marker.label);
            }
        }
    }
    if (mouseRasMm_.has_value()) {
        const QString text = QStringLiteral("LR %1   AP %2   IS %3 mm")
                                 .arg(mouseRasMm_->x(), 0, 'f', 1)
                                 .arg(mouseRasMm_->y(), 0, 'f', 1)
                                 .arg(mouseRasMm_->z(), 0, 'f', 1);
        const QFontMetrics metrics(painter.font());
        QRectF badge(QPointF(mouseWidgetPosition_.x() + 14, mouseWidgetPosition_.y() + 14),
                     QSizeF(metrics.horizontalAdvance(text) + 14, metrics.height() + 8));
        if (badge.right() > width() - 4) badge.moveRight(width() - 4);
        if (badge.bottom() > height() - 4) badge.moveBottom(mouseWidgetPosition_.y() - 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 20, 27, 220));
        painter.drawRoundedRect(badge, 4, 4);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, text);
    }
}

void WorkflowMriView::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) return;
    const QRectF before = imageRect();
    const QPointF cursor = event->position();
    const QPointF relative((cursor.x() - before.left()) / before.width(), (cursor.y() - before.top()) / before.height());
    const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
    const double previousZoom = zoom_;
    zoom_ = std::clamp(zoom_ * factor, 1.0, 10.0);
    if (zoom_ != previousZoom) {
        const QRectF after = imageRect();
        const QPointF mapped(after.left() + relative.x() * after.width(), after.top() + relative.y() * after.height());
        pan_ += cursor - mapped;
        clampPan();
        update();
    }
    event->accept();
}

void WorkflowMriView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && pointPlacementEnabled_) {
        if (const auto ras = rasAtWidgetPosition(event->position()); ras && pointPickedHandler_)
            pointPickedHandler_(*ras);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !image_.isNull()) {
        panning_ = true;
        lastMousePosition_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void WorkflowMriView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        pan_ += event->pos() - lastMousePosition_;
        lastMousePosition_ = event->pos();
        clampPan();
    }
    updateMouseCoordinate(event->position());
    update();
}

void WorkflowMriView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        panning_ = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void WorkflowMriView::mouseDoubleClickEvent(QMouseEvent*) { resetView(); }

void WorkflowMriView::leaveEvent(QEvent*) {
    mouseRasMm_.reset();
    update();
}

void WorkflowMriView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* container = new QWidget(&menu);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->addWidget(new QLabel(QStringLiteral("Brightness"), container));
    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(25, 250);
    slider->setValue(static_cast<int>(std::lround(brightness_ * 100.0)));
    slider->setMinimumWidth(220);
    layout->addWidget(slider);
    layout->addWidget(new QLabel(QStringLiteral("Contrast"), container));
    auto* contrastSlider = new QSlider(Qt::Horizontal, container);
    contrastSlider->setRange(50, 300);
    contrastSlider->setValue(static_cast<int>(std::lround(contrast_ * 100.0)));
    contrastSlider->setMinimumWidth(220);
    layout->addWidget(contrastSlider);
    auto* sliderAction = new QWidgetAction(&menu);
    sliderAction->setDefaultWidget(container);
    menu.addAction(sliderAction);
    connect(slider, &QSlider::valueChanged, this, [this](int value) {
        brightness_ = static_cast<double>(value) / 100.0;
        rebuildImage();
        update();
    });
    connect(contrastSlider, &QSlider::valueChanged, this, [this](int value) {
        contrast_ = static_cast<double>(value) / 100.0;
        rebuildImage();
        update();
    });
    menu.addSeparator();
    QAction* resetBrightness = menu.addAction(QStringLiteral("Reset brightness and contrast"));
    QAction* resetAll = menu.addAction(QStringLiteral("Reset zoom, pan, brightness, and contrast"));
    QAction* selected = menu.exec(event->globalPos());
    if (selected == resetBrightness) {
        brightness_ = 1.0;
        contrast_ = 1.0;
        rebuildImage();
        update();
    } else if (selected == resetAll) {
        resetView();
    }
}

void WorkflowMriView::clampPan() {
    const QRectF shown = imageRect();
    // At fit zoom the full image is visible, but the operator may still
    // reposition it for comparison with another plane. Retain a bounded
    // quarter-viewport travel area; once zoomed, expand the bound to cover
    // the portion of the image extending beyond the viewport.
    const double maxX = std::max(width() * 0.25, std::abs(shown.width() - width()) / 2.0);
    const double maxY = std::max(height() * 0.25, std::abs(shown.height() - height()) / 2.0);
    pan_.setX(std::clamp(pan_.x(), -maxX, maxX));
    pan_.setY(std::clamp(pan_.y(), -maxY, maxY));
}

void WorkflowMriView::updateMouseCoordinate(const QPointF& widgetPosition) {
    mouseRasMm_ = rasAtWidgetPosition(widgetPosition);
    mouseWidgetPosition_ = widgetPosition;
}

std::optional<Eigen::Vector3d> WorkflowMriView::rasAtWidgetPosition(const QPointF& widgetPosition) const {
    const QRectF displayed = imageRect();
    if (image_.isNull() || plane_.isEmpty() || !displayed.contains(widgetPosition)) {
        return std::nullopt;
    }
    double horizontal = (widgetPosition.x() - displayed.left()) / displayed.width();
    double vertical = (widgetPosition.y() - displayed.top()) / displayed.height();
    if (reverseHorizontal_) horizontal = 1.0 - horizontal;
    if (reverseVertical_) vertical = 1.0 - vertical;
    const double horizontalMm = horizontalMinMm_ + horizontal * (horizontalMaxMm_ - horizontalMinMm_);
    const double verticalMm = verticalMinMm_ + vertical * (verticalMaxMm_ - verticalMinMm_);

    if (plane_ == QStringLiteral("sagittal"))
        return Eigen::Vector3d(fixedCoordinateMm_, horizontalMm, verticalMm);
    else if (plane_ == QStringLiteral("coronal"))
        return Eigen::Vector3d(horizontalMm, fixedCoordinateMm_, verticalMm);
    else if (plane_ == QStringLiteral("axial"))
        return Eigen::Vector3d(horizontalMm, verticalMm, fixedCoordinateMm_);
    return std::nullopt;
}
