#include "gui_qt/zoomable_image_view.hpp"

#include <cmath>

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

namespace beam::gui_qt {

ZoomableImageView::ZoomableImageView(QWidget* parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setRenderHint(QPainter::Antialiasing);
    setFrameShape(QFrame::Box);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void ZoomableImageView::setPixmap(const QPixmap& pixmap) {
    scene_->clear();
    pixmapItem_ = scene_->addPixmap(pixmap);
    scene_->setSceneRect(pixmapItem_->boundingRect());
    manuallyZoomed_ = false;
    fitToView();
}

void ZoomableImageView::fitToView() {
    if (!pixmapItem_) return;
    fitInView(pixmapItem_, Qt::KeepAspectRatio);
}

void ZoomableImageView::clampPan() {
    if (!pixmapItem_) return;
    const QRectF pixmapViewRect = mapFromScene(pixmapItem_->sceneBoundingRect()).boundingRect();
    const QRect viewportRect = viewport()->rect();
    int dx = 0;
    int dy = 0;
    if (pixmapViewRect.width() >= viewportRect.width()) {
        if (pixmapViewRect.left() > viewportRect.left()) {
            dx = static_cast<int>(std::ceil(pixmapViewRect.left() - viewportRect.left()));
        } else if (pixmapViewRect.right() < viewportRect.right()) {
            dx = static_cast<int>(std::floor(pixmapViewRect.right() - viewportRect.right()));
        }
    }
    if (pixmapViewRect.height() >= viewportRect.height()) {
        if (pixmapViewRect.top() > viewportRect.top()) {
            dy = static_cast<int>(std::ceil(pixmapViewRect.top() - viewportRect.top()));
        } else if (pixmapViewRect.bottom() < viewportRect.bottom()) {
            dy = static_cast<int>(std::floor(pixmapViewRect.bottom() - viewportRect.bottom()));
        }
    }
    if (dx != 0) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    if (dy != 0) verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void ZoomableImageView::wheelEvent(QWheelEvent* event) {
    if (!pixmapItem_) {
        event->ignore();
        return;
    }
    constexpr double kStepPerNotch = 1.15;
    constexpr double kMinScale = 0.2;
    constexpr double kMaxScale = 20.0;
    const double notches = event->angleDelta().y() / 120.0;
    const double factor = std::pow(kStepPerNotch, notches);
    const double newScale = transform().m11() * factor;
    if (newScale < kMinScale || newScale > kMaxScale) {
        event->accept();
        return;
    }
    scale(factor, factor);
    manuallyZoomed_ = true;
    clampPan();
    event->accept();
}

void ZoomableImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    manuallyZoomed_ = false;
    fitToView();
    event->accept();
}

void ZoomableImageView::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    clampPan();
}

void ZoomableImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (!manuallyZoomed_) {
        fitToView();
    } else {
        clampPan();
    }
}

}  // namespace beam::gui_qt
