#include "mri_view.hpp"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
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

void WorkflowMriView::setNavigationCrosshairVisible(bool visible) {
    navigationCrosshairVisible_ = visible;
    update();
}

void WorkflowMriView::adjustBrightness(double amount) {
    brightness_ = std::clamp(brightness_ + amount, 0.25, 2.5);
    rebuildImage();
    update();
}

void WorkflowMriView::resetBrightness() {
    brightness_ = 1.0;
    rebuildImage();
    update();
}

void WorkflowMriView::setMaskOverlay(const Eigen::MatrixXd& mask, QColor color, double opacity,
                                     bool flipHorizontal) {
    if (mask.size() == 0) {
        maskImage_ = {};
        update();
        return;
    }
    QImage result(static_cast<int>(mask.cols()), static_cast<int>(mask.rows()), QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    for (int y = 0; y < result.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            if (mask(y, x) > 0.0) {
                QColor pixel = color;
                pixel.setAlphaF(std::clamp(mask(y, x), 0.0, 1.0) * std::clamp(opacity, 0.0, 1.0));
                row[x] = pixel.rgba();
            }
        }
    }
    maskImage_ = flipHorizontal ? result.mirrored(true, false) : result;
    update();
}

void WorkflowMriView::setSecondaryMaskOverlay(const Eigen::MatrixXd& mask, QColor color, double opacity,
                                              bool flipHorizontal) {
    if (mask.size() == 0) {
        secondaryMaskImage_ = {};
        update();
        return;
    }
    QImage result(static_cast<int>(mask.cols()), static_cast<int>(mask.rows()), QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    for (int y = 0; y < result.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            if (mask(y, x) > 0.0) {
                QColor pixel = color;
                pixel.setAlphaF(std::clamp(mask(y, x), 0.0, 1.0) * std::clamp(opacity, 0.0, 1.0));
                row[x] = pixel.rgba();
            }
        }
    }
    secondaryMaskImage_ = flipHorizontal ? result.mirrored(true, false) : result;
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
    const double range = std::max(dataMax_ - dataMin_, 1e-12);
    // Brightness is an output-level offset centered at 1.0. Therefore moving
    // the slider right always raises displayed gray levels. Contrast remains
    // centered around mid-gray and grows as its slider moves right.
    const double brightnessOffset = (brightness_ - 1.0) * 0.5;
    QImage result(static_cast<int>(slice_.cols()), static_cast<int>(slice_.rows()), QImage::Format_Grayscale8);
    for (int y = 0; y < result.height(); ++y) {
        auto* row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            const double normalized = (slice_(y, x) - dataMin_) / range;
            const double displayed = (normalized - 0.5) * contrast_ + 0.5 + brightnessOffset;
            row[x] = static_cast<unsigned char>(std::clamp(displayed * 255.0, 0.0, 255.0));
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
    if (!secondaryMaskImage_.isNull()) painter.drawImage(displayed, secondaryMaskImage_);
    if (navigationCrosshairVisible_) {
        const double x = displayed.left() + crosshair_.x() * displayed.width();
        const double y = displayed.top() + crosshair_.y() * displayed.height();
        painter.setPen(QPen(QColor(40, 225, 245, 235), 2.0));
        painter.drawLine(QPointF(x, displayed.top()), QPointF(x, displayed.bottom()));
        painter.drawLine(QPointF(displayed.left(), y), QPointF(displayed.right(), y));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(x, y), 5.0, 5.0);
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const WorkflowMriMarker& marker : markers_) {
        const QPointF point(displayed.left() + marker.normalizedPosition.x() * displayed.width(),
                            displayed.top() + marker.normalizedPosition.y() * displayed.height());
        painter.setPen(QPen(marker.color, marker.crosshair ? 2.0 : 1.5));
        if (marker.crosshair) {
            painter.drawLine(QPointF(displayed.left(), point.y()),
                             QPointF(displayed.right(), point.y()));
            painter.drawLine(QPointF(point.x(), displayed.top()),
                             QPointF(point.x(), displayed.bottom()));
        } else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(marker.color, marker.draggable ? 2.5 : 1.5));
            painter.drawEllipse(point, marker.draggable ? 8.0 : 5.0, marker.draggable ? 8.0 : 5.0);
            if (marker.draggable) {
                painter.drawLine(point + QPointF(-11, 0), point + QPointF(-4, 0));
                painter.drawLine(point + QPointF(4, 0), point + QPointF(11, 0));
                painter.drawLine(point + QPointF(0, -11), point + QPointF(0, -4));
                painter.drawLine(point + QPointF(0, 4), point + QPointF(0, 11));
            }
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
        const QRectF displayed = imageRect();
        for (const WorkflowMriMarker& marker : markers_) {
            if (!marker.draggable) continue;
            const QPointF point(displayed.left() + marker.normalizedPosition.x() * displayed.width(),
                                displayed.top() + marker.normalizedPosition.y() * displayed.height());
            if (QLineF(point, event->position()).length() <= 14.0) {
                draggingMarker_ = true;
                setCursor(Qt::CrossCursor);
                if (const auto ras = rasAtWidgetPosition(event->position()); ras && pointPickedHandler_)
                    pointPickedHandler_(*ras);
                event->accept();
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton && !image_.isNull()) {
        panning_ = true;
        lastMousePosition_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void WorkflowMriView::mouseMoveEvent(QMouseEvent* event) {
    if (draggingMarker_) {
        if (const auto ras = rasAtWidgetPosition(event->position()); ras && pointPickedHandler_)
            pointPickedHandler_(*ras);
    } else if (panning_) {
        pan_ += event->pos() - lastMousePosition_;
        lastMousePosition_ = event->pos();
        clampPan();
    }
    updateMouseCoordinate(event->position());
    update();
}

void WorkflowMriView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        draggingMarker_ = false;
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
    const QString buttonStyle = QStringLiteral(
        "QToolButton { background: #f7fafb; color: #17313f; border: 1px solid #9eacb4; "
        "border-radius: 3px; min-width: 24px; max-width: 24px; min-height: 22px; max-height: 22px; "
        "font-family: 'Segoe UI Symbol'; font-weight: 700; } "
        "QToolButton:hover { background: #e3f2f6; border-color: #2888a2; }");
    const auto makeButton = [container, &buttonStyle](QChar symbol, const QString& toolTip) {
        auto* button = new QToolButton(container);
        button->setText(QString(symbol));
        button->setToolTip(toolTip);
        button->setStyleSheet(buttonStyle);
        return button;
    };

    auto* brightnessRow = new QHBoxLayout;
    auto* brightnessLabel = new QLabel(QStringLiteral("Brightness"), container);
    brightnessLabel->setMinimumWidth(72);
    brightnessRow->addWidget(brightnessLabel);
    brightnessRow->addSpacing(6);
    auto* brightnessDown = makeButton(QChar(0x2193), QStringLiteral("Decrease brightness"));
    auto* brightnessUp = makeButton(QChar(0x2191), QStringLiteral("Increase brightness"));
    auto* brightnessReset = makeButton(QChar(0x21BA), QStringLiteral("Reset brightness"));
    brightnessRow->addWidget(brightnessUp);
    brightnessRow->addWidget(brightnessDown);
    brightnessRow->addWidget(brightnessReset);
    layout->addLayout(brightnessRow);

    auto* contrastRow = new QHBoxLayout;
    auto* contrastLabel = new QLabel(QStringLiteral("Contrast"), container);
    contrastLabel->setMinimumWidth(72);
    contrastRow->addWidget(contrastLabel);
    contrastRow->addSpacing(6);
    auto* contrastDown = makeButton(QChar(0x2193), QStringLiteral("Decrease contrast"));
    auto* contrastUp = makeButton(QChar(0x2191), QStringLiteral("Increase contrast"));
    auto* contrastReset = makeButton(QChar(0x21BA), QStringLiteral("Reset contrast"));
    contrastRow->addWidget(contrastUp);
    contrastRow->addWidget(contrastDown);
    contrastRow->addWidget(contrastReset);
    layout->addLayout(contrastRow);

    auto* controlsAction = new QWidgetAction(&menu);
    controlsAction->setDefaultWidget(container);
    menu.addAction(controlsAction);
    const auto redraw = [this] { rebuildImage(); update(); };
    connect(brightnessDown, &QToolButton::clicked, this, [this, redraw] {
        brightness_ = std::clamp(brightness_ - 0.1, 0.25, 2.5); redraw();
    });
    connect(brightnessUp, &QToolButton::clicked, this, [this, redraw] {
        brightness_ = std::clamp(brightness_ + 0.1, 0.25, 2.5); redraw();
    });
    connect(brightnessReset, &QToolButton::clicked, this, [this, redraw] { brightness_ = 1.0; redraw(); });
    connect(contrastDown, &QToolButton::clicked, this, [this, redraw] {
        contrast_ = std::clamp(contrast_ - 0.1, 0.5, 3.0); redraw();
    });
    connect(contrastUp, &QToolButton::clicked, this, [this, redraw] {
        contrast_ = std::clamp(contrast_ + 0.1, 0.5, 3.0); redraw();
    });
    connect(contrastReset, &QToolButton::clicked, this, [this, redraw] { contrast_ = 1.0; redraw(); });
    menu.addSeparator();
    QAction* resetAll = menu.addAction(QStringLiteral("Reset zoom, pan, brightness, and contrast"));
    QAction* selected = menu.exec(event->globalPos());
    if (selected == resetAll) resetView();
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
