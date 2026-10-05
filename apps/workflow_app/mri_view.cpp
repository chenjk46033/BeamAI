#include "mri_view.hpp"

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QAction>
#include <QContextMenuEvent>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

WorkflowMriView::WorkflowMriView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::StrongFocus);
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
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void WorkflowMriView::setMarkerPickedHandler(std::function<void(int)> handler) {
    markerPickedHandler_ = std::move(handler);
}

void WorkflowMriView::setCoordinatePasteOptions(QStringList options) {
    coordinatePasteOptions_ = std::move(options);
}

void WorkflowMriView::setPointPickedHandler(std::function<void(const Eigen::Vector3d&)> handler) {
    pointPickedHandler_ = std::move(handler);
}

void WorkflowMriView::setPointMovedHandler(std::function<void(const Eigen::Vector3d&)> handler) {
    pointMovedHandler_ = std::move(handler);
}

void WorkflowMriView::setOpenViewerHandler(std::function<void()> handler) {
    openViewerHandler_ = std::move(handler);
}

void WorkflowMriView::mirrorFrom(const WorkflowMriView& source) {
    slice_ = source.slice_;
    image_ = source.image_;
    maskImage_ = source.maskImage_;
    secondaryMaskImage_ = source.secondaryMaskImage_;
    markers_ = source.markers_;
    flipHorizontal_ = source.flipHorizontal_;
    dataMin_ = source.dataMin_;
    dataMax_ = source.dataMax_;
    brightness_ = source.brightness_;
    contrast_ = source.contrast_;
    crosshair_ = source.crosshair_;
    navigationCrosshairVisible_ = source.navigationCrosshairVisible_;
    plane_ = source.plane_;
    fixedCoordinateMm_ = source.fixedCoordinateMm_;
    horizontalMinMm_ = source.horizontalMinMm_;
    horizontalMaxMm_ = source.horizontalMaxMm_;
    verticalMinMm_ = source.verticalMinMm_;
    verticalMaxMm_ = source.verticalMaxMm_;
    reverseHorizontal_ = source.reverseHorizontal_;
    reverseVertical_ = source.reverseVertical_;

    // The interaction wiring comes across too, so a detached viewer is a full
    // peer of its pane rather than a picture of it: fiducials can be dragged
    // in it, and its context menu offers the same "Move fiducial ... to mouse
    // point". These are copied on every mirror, not once at creation, because
    // coordinatePasteOptions_ is refilled with the current marker names each
    // time showMriPreviews runs.
    pointPlacementEnabled_ = source.pointPlacementEnabled_;
    pointPickedHandler_ = source.pointPickedHandler_;
    pointMovedHandler_ = source.pointMovedHandler_;
    markerPickedHandler_ = source.markerPickedHandler_;
    coordinatePasteOptions_ = source.coordinatePasteOptions_;
    // openViewerHandler_ is deliberately not copied: a detached viewer should
    // not offer to open another copy of itself.

    // Zoom and pan stay the detached window's own: it exists to show the
    // slice larger, so inheriting the pane's framing would defeat that.
    update();
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
    paintMeasurement(painter, displayed);
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
    // Placing a point wins over grabbing a handle: measurements are often
    // started from a corner of an existing one, and grabbing there would
    // silently drag that instead of beginning the new shape.
    if (measurementInProgress() && event->button() == Qt::LeftButton) {
        if (addMeasurementPoint(event->position())) {
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && grabMeasurementVertex(event->position())) {
        event->accept();
        return;
    }
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
                if (markerPickedHandler_ && marker.markerIndex >= 0)
                    markerPickedHandler_(marker.markerIndex);
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
        setCursor(Qt::SizeAllCursor);
        event->accept();
    }
}

void WorkflowMriView::mouseMoveEvent(QMouseEvent* event) {
    if (draggingMeasurementVertex()) {
        moveGrabbedVertex(event->position());
        event->accept();
        return;
    }
    if (measurementInProgress()) setMeasurementHover(event->position());
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
    if (draggingMeasurementVertex()) {
        releaseMeasurementVertex();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        draggingMarker_ = false;
        panning_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void WorkflowMriView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (measurementInProgress()) {
        finishMeasurement();
        event->accept();
        return;
    }
    resetView();
}

void WorkflowMriView::leaveEvent(QEvent*) {
    mouseRasMm_.reset();
    update();
}

void WorkflowMriView::contextMenuEvent(QContextMenuEvent* event) {
    updateMouseCoordinate(event->pos());
    // Capture the point before the menu opens.  Opening a popup can cause
    // the view to receive leaveEvent(), so reading mouseRasMm_ later would
    // otherwise produce an empty or changed coordinate.
    const auto coordinateAtContext = rasAtWidgetPosition(event->pos());
    QMenu menu(this);
    if (markerPickedHandler_) {
        const QString coordinateText = coordinateAtContext
            ? QStringLiteral("Mouse point: X %1 mm, Y %2 mm, Z %3 mm")
                  .arg(coordinateAtContext->x(), 0, 'f', 3)
                  .arg(coordinateAtContext->y(), 0, 'f', 3)
                  .arg(coordinateAtContext->z(), 0, 'f', 3)
            : QStringLiteral("Mouse point is outside the MRI image");
        // Use the same embedded-widget margins as the Marker selector below.
        // A normal QAction receives Qt's extra menu indentation, which made
        // this coordinate line start noticeably farther to the right.
        auto* coordinateWidget = new QWidget(&menu);
        auto* coordinateLayout = new QHBoxLayout(coordinateWidget);
        coordinateLayout->setContentsMargins(10, 5, 10, 5);
        auto* coordinateLabel = new QLabel(coordinateText, coordinateWidget);
        coordinateLabel->setStyleSheet(QStringLiteral(
            "QLabel { background: #d8f1f6; color: #123d4b; border: 1px solid #63b7c9; "
            "border-radius: 3px; padding: 3px 0px; font-weight: 600; }"));
        coordinateLayout->addWidget(coordinateLabel);
        auto* coordinateAction = new QWidgetAction(&menu);
        coordinateAction->setDefaultWidget(coordinateWidget);
        menu.addAction(coordinateAction);
        if (coordinateAtContext && pointPickedHandler_ && !coordinatePasteOptions_.isEmpty()) {
            auto* targetWidget = new QWidget(&menu);
            auto* targetLayout = new QHBoxLayout(targetWidget);
            // Slightly indent the action beneath the highlighted coordinate.
            targetLayout->setContentsMargins(18, 5, 10, 5);
            auto* targetLabel = new QLabel(QStringLiteral("Move fiducial"), targetWidget);
            auto* targetCombo = new QComboBox(targetWidget);
            targetCombo->addItems(coordinatePasteOptions_);
            targetCombo->setMinimumWidth(100);
            auto* targetSuffix = new QLabel(QStringLiteral("to mouse point."), targetWidget);
            auto* moveButton = new QPushButton(QStringLiteral("Move"), targetWidget);
            moveButton->setMinimumWidth(54);
            targetLayout->addWidget(targetLabel);
            targetLayout->addWidget(targetCombo, 1);
            targetLayout->addWidget(targetSuffix);
            targetLayout->addWidget(moveButton);
            auto* targetAction = new QWidgetAction(&menu);
            targetAction->setDefaultWidget(targetWidget);
            menu.addAction(targetAction);
            connect(moveButton, &QPushButton::clicked, this, [this, targetCombo, coordinateAtContext, &menu] {
                if (!coordinateAtContext || !pointPickedHandler_) return;
                if (markerPickedHandler_) markerPickedHandler_(targetCombo->currentIndex());
                // Explicit relocation: the operator pointed at a place and
                // named a fiducial, so it belongs at that place -- including
                // on this view's own slice. Routing it through the drag
                // handler instead kept the fiducial's old out-of-plane value
                // and dropped it on a different slice than the one clicked.
                if (pointMovedHandler_)
                    pointMovedHandler_(*coordinateAtContext);
                else
                    pointPickedHandler_(*coordinateAtContext);
                menu.close();
            });
        }
    }
    if (!plane_.isEmpty() && !image_.isNull()) {
        menu.addSeparator();
        QAction* line = menu.addAction(QStringLiteral("Measure distance"));
        connect(line, &QAction::triggered, this,
                [this] { beginMeasurement(MeasureMode::Line); });
        QAction* contour = menu.addAction(QStringLiteral("Measure area"));
        connect(contour, &QAction::triggered, this,
                [this] { beginMeasurement(MeasureMode::Contour); });
        if (hasMeasurement()) {
            QAction* clear = menu.addAction(QStringLiteral("Clear measurements"));
            connect(clear, &QAction::triggered, this, [this] { clearMeasurement(); });
        }
    }
    if (openViewerHandler_) {
        menu.addSeparator();
        QAction* openViewer = menu.addAction(QStringLiteral("Open viewer in a new window"));
        connect(openViewer, &QAction::triggered, this, [this] { openViewerHandler_(); });
    }
    menu.addSeparator();
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
// A detached viewer shows the same plane as its pane, so it shares the pane's
// measurements rather than keeping its own: one owner, drawn in both windows,
// editable from either.
void WorkflowMriView::setMeasurementPeer(WorkflowMriView* peer) { measurementPeer_ = peer; }

WorkflowMriView* WorkflowMriView::measurementOwner() {
    return measurementPeer_ ? measurementPeer_.data() : this;
}

const WorkflowMriView* WorkflowMriView::measurementOwner() const {
    return measurementPeer_ ? measurementPeer_.data() : this;
}

std::size_t WorkflowMriView::pointsNeededFor(MeasureMode mode) {
    switch (mode) {
        case MeasureMode::Line: return 2;
        case MeasureMode::Contour: return 3;
        case MeasureMode::None: break;
    }
    return 0;
}

void WorkflowMriView::beginMeasurement(MeasureMode mode) {
    WorkflowMriView* owner = measurementOwner();
    owner->cancelMeasurementInProgress();
    Measurement started;
    started.mode = mode;
    started.plane = plane_;
    started.sliceMm = fixedCoordinateMm_;
    owner->measurements_.push_back(std::move(started));
    owner->activeMeasurement_ = static_cast<int>(owner->measurements_.size()) - 1;
    owner->hoverNormalized_.reset();
    owner->hoverRasMm_.reset();
    owner->update();
    setFocus(Qt::OtherFocusReason);
    update();
}

// Abandons the one being drawn, leaving every completed measurement alone.
void WorkflowMriView::cancelMeasurementInProgress() {
    WorkflowMriView* owner = measurementOwner();
    if (owner->activeMeasurement_ < 0) return;
    owner->measurements_.erase(owner->measurements_.begin() + owner->activeMeasurement_);
    owner->activeMeasurement_ = -1;
    owner->hoverNormalized_.reset();
    owner->hoverRasMm_.reset();
    owner->update();
    update();
}

void WorkflowMriView::clearMeasurement() {
    WorkflowMriView* owner = measurementOwner();
    owner->measurements_.clear();
    owner->activeMeasurement_ = -1;
    owner->dragMeasurement_ = -1;
    owner->dragVertex_ = -1;
    owner->hoverNormalized_.reset();
    owner->hoverRasMm_.reset();
    owner->update();
    update();
}

bool WorkflowMriView::hasMeasurement() const {
    return !measurementOwner()->measurements_.empty();
}

bool WorkflowMriView::measurementInProgress() const {
    return measurementOwner()->activeMeasurement_ >= 0;
}

QPointF WorkflowMriView::normalizedAtWidgetPosition(const QPointF& widgetPosition) const {
    const QRectF displayed = imageRect();
    if (displayed.width() <= 0.0 || displayed.height() <= 0.0) return QPointF();
    return QPointF((widgetPosition.x() - displayed.left()) / displayed.width(),
                   (widgetPosition.y() - displayed.top()) / displayed.height());
}

// A measurement belongs to the slice it was drawn on; showing it over any
// other slice would imply the structure is there too.
bool WorkflowMriView::measurementShownHere(const Measurement& measurement) const {
    return !measurement.normalized.empty() && measurement.plane == plane_ &&
           std::abs(measurement.sliceMm - fixedCoordinateMm_) < 1e-6;
}

bool WorkflowMriView::addMeasurementPoint(const QPointF& widgetPosition) {
    const auto ras = rasAtWidgetPosition(widgetPosition);
    if (!ras) return false;
    WorkflowMriView* owner = measurementOwner();
    if (owner->activeMeasurement_ < 0) return false;
    Measurement& active = owner->measurements_[static_cast<std::size_t>(owner->activeMeasurement_)];

    // Clicking back on the first vertex closes a contour.
    if (active.mode == MeasureMode::Contour && active.normalized.size() >= 3) {
        const QRectF displayed = imageRect();
        const QPointF first(displayed.left() + active.normalized.front().x() * displayed.width(),
                            displayed.top() + active.normalized.front().y() * displayed.height());
        if (QLineF(first, widgetPosition).length() < 10.0) {
            finishMeasurement();
            return true;
        }
    }

    active.normalized.push_back(normalizedAtWidgetPosition(widgetPosition));
    active.rasMm.push_back(*ras);
    // A line takes a fixed number of points and completes itself.
    if (active.mode != MeasureMode::Contour && active.normalized.size() >= pointsNeededFor(active.mode)) {
        finishMeasurement();
        return true;
    }
    owner->update();
    update();
    return true;
}

void WorkflowMriView::setMeasurementHover(const QPointF& widgetPosition) {
    WorkflowMriView* owner = measurementOwner();
    owner->hoverNormalized_ = normalizedAtWidgetPosition(widgetPosition);
    owner->hoverRasMm_ = rasAtWidgetPosition(widgetPosition);
    owner->update();
}

void WorkflowMriView::finishMeasurement() {
    WorkflowMriView* owner = measurementOwner();
    if (owner->activeMeasurement_ < 0) return;
    Measurement& active = owner->measurements_[static_cast<std::size_t>(owner->activeMeasurement_)];
    if (active.normalized.size() < pointsNeededFor(active.mode)) return;
    active.finished = true;
    owner->activeMeasurement_ = -1;
    owner->hoverNormalized_.reset();
    owner->hoverRasMm_.reset();
    owner->update();
    update();
}

// A finished measurement stays editable: its vertices are grab handles.
bool WorkflowMriView::grabMeasurementVertex(const QPointF& widgetPosition) {
    WorkflowMriView* owner = measurementOwner();
    const QRectF displayed = imageRect();
    for (std::size_t m = 0; m < owner->measurements_.size(); ++m) {
        const Measurement& measurement = owner->measurements_[m];
        if (!measurement.finished || !measurementShownHere(measurement)) continue;
        for (std::size_t v = 0; v < measurement.normalized.size(); ++v) {
            const QPointF point(displayed.left() + measurement.normalized[v].x() * displayed.width(),
                                displayed.top() + measurement.normalized[v].y() * displayed.height());
            if (QLineF(point, widgetPosition).length() <= 9.0) {
                owner->dragMeasurement_ = static_cast<int>(m);
                owner->dragVertex_ = static_cast<int>(v);
                return true;
            }
        }
    }
    return false;
}

void WorkflowMriView::moveGrabbedVertex(const QPointF& widgetPosition) {
    WorkflowMriView* owner = measurementOwner();
    if (owner->dragMeasurement_ < 0 || owner->dragVertex_ < 0) return;
    const auto ras = rasAtWidgetPosition(widgetPosition);
    if (!ras) return;
    Measurement& measurement = owner->measurements_[static_cast<std::size_t>(owner->dragMeasurement_)];
    const auto vertex = static_cast<std::size_t>(owner->dragVertex_);
    if (vertex >= measurement.normalized.size()) return;
    measurement.normalized[vertex] = normalizedAtWidgetPosition(widgetPosition);
    measurement.rasMm[vertex] = *ras;
    owner->update();
    update();
}

bool WorkflowMriView::draggingMeasurementVertex() const {
    return measurementOwner()->dragMeasurement_ >= 0;
}

void WorkflowMriView::releaseMeasurementVertex() {
    WorkflowMriView* owner = measurementOwner();
    owner->dragMeasurement_ = -1;
    owner->dragVertex_ = -1;
}

// The placed points, plus the one under the cursor while still drawing, so
// the number updates as the shape is pulled out.
std::vector<Eigen::Vector3d> WorkflowMriView::pointsForSummary(const Measurement& measurement) const {
    const WorkflowMriView* owner = measurementOwner();
    std::vector<Eigen::Vector3d> points = measurement.rasMm;
    if (!measurement.finished && owner->hoverRasMm_) points.push_back(*owner->hoverRasMm_);
    return points;
}

QString WorkflowMriView::summaryFor(const Measurement& measurement) const {
    const std::vector<Eigen::Vector3d> points = pointsForSummary(measurement);
    if (points.size() < 2) return QString();

    if (measurement.mode == MeasureMode::Line)
        return QStringLiteral("%1 mm").arg((points[1] - points[0]).norm(), 0, 'f', 1);

    if (points.size() < 3)
        return QStringLiteral("%1 mm").arg((points[1] - points[0]).norm(), 0, 'f', 1);

    // Shoelace over the plane's two varying axes; the third is constant, so
    // the in-plane projection loses nothing.
    const int a = plane_ == QStringLiteral("sagittal") ? 1 : 0;
    const int b = plane_ == QStringLiteral("axial") ? 1 : 2;
    double twiceArea = 0.0;
    double perimeter = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Eigen::Vector3d& p = points[i];
        const Eigen::Vector3d& q = points[(i + 1) % points.size()];
        twiceArea += p(a) * q(b) - q(a) * p(b);
        perimeter += (q - p).norm();
    }
    const double area = std::abs(twiceArea) / 2.0;
    return area >= 100.0 ? QStringLiteral("%1 mm2  (%2 cm2)   perimeter %3 mm")
                               .arg(area, 0, 'f', 1)
                               .arg(area / 100.0, 0, 'f', 2)
                               .arg(perimeter, 0, 'f', 1)
                         : QStringLiteral("%1 mm2   perimeter %2 mm")
                               .arg(area, 0, 'f', 1)
                               .arg(perimeter, 0, 'f', 1);
}

QStringList WorkflowMriView::measurementSummaries() const {
    QStringList all;
    for (const Measurement& measurement : measurementOwner()->measurements_) {
        const QString summary = summaryFor(measurement);
        if (!summary.isEmpty()) all << summary;
    }
    return all;
}

QString WorkflowMriView::measurementSummary() const {
    const QStringList all = measurementSummaries();
    return all.isEmpty() ? QString() : all.last();
}

void WorkflowMriView::paintMeasurement(QPainter& painter, const QRectF& displayed) const {
    const WorkflowMriView* owner = measurementOwner();
    const auto toWidget = [&displayed](const QPointF& normalized) {
        return QPointF(displayed.left() + normalized.x() * displayed.width(),
                       displayed.top() + normalized.y() * displayed.height());
    };
    const QColor ink(255, 214, 64);
    const QFontMetricsF metrics(painter.font());

    for (const Measurement& measurement : owner->measurements_) {
        if (!measurementShownHere(measurement)) continue;
        QVector<QPointF> points;
        points.reserve(static_cast<int>(measurement.normalized.size()) + 1);
        for (const QPointF& normalized : measurement.normalized) points.push_back(toWidget(normalized));
        const bool drawing = !measurement.finished && owner->hoverNormalized_.has_value();
        if (drawing) points.push_back(toWidget(*owner->hoverNormalized_));
        if (points.isEmpty()) continue;

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ink, 1.8));
        if (measurement.mode == MeasureMode::Contour &&
            (measurement.finished || points.size() > 2)) {
            painter.drawPolygon(points);
        } else {
            painter.drawPolyline(points);
        }
        painter.setPen(QPen(ink, 1.4));
        for (const QPointF& point : points) painter.drawEllipse(point, 3.0, 3.0);

        const QString summary = summaryFor(measurement);
        if (summary.isEmpty()) continue;
        // While drawing the number rides with the cursor; once finished it
        // settles on the measurement's own anchor so it stops moving.
        const QPointF anchorPoint = drawing ? points.back() : points.front();
        const QRectF box(anchorPoint + QPointF(10, -24),
                         QSizeF(metrics.horizontalAdvance(summary) + 12, 19));
        painter.fillRect(box, QColor(60, 45, 0, 225));
        painter.setPen(QPen(ink, 1.0));
        painter.drawRect(box);
        painter.setPen(QColor(255, 240, 190));
        painter.drawText(box.adjusted(6, 0, -2, 0), Qt::AlignVCenter, summary);
    }
}

void WorkflowMriView::keyPressEvent(QKeyEvent* event) {
    // Escape abandons what is being drawn. Finished measurements persist until
    // "Clear measurements", so Escape never silently discards them.
    if (event->key() == Qt::Key_Escape && measurementInProgress()) {
        cancelMeasurementInProgress();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && measurementInProgress()) {
        finishMeasurement();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
