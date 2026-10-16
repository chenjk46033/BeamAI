#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QWheelEvent>
#include <cmath>

namespace beam::app {

// One of the Targeting Examples, zoomable and pannable where it sits. The tab
// shows three views at once, so each is a few hundred pixels wide -- enough to
// pick the region, not enough to compare against the patient's own anatomy.
// The wheel and a drag make up the difference; a double-click returns to fit.
class ExampleImageView final : public QGraphicsView {
public:
    explicit ExampleImageView(QWidget* parent = nullptr)
        : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
        setScene(scene_);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setRenderHint(QPainter::SmoothPixmapTransform, true);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setMinimumHeight(180);
        setStyleSheet(QStringLiteral(
            "QGraphicsView { background: #101820; border: 1px solid #2b6172; }"));
        setToolTip(QStringLiteral("Wheel to zoom, drag to pan, double-click to fit."));
    }

    // The full-resolution picture, not a thumbnail: zooming in has to reveal
    // detail rather than enlarge pixels.
    void setImage(const QPixmap& image) {
        scene_->clear();
        hasImage_ = !image.isNull();
        if (!hasImage_) return;
        scene_->addPixmap(image);
        scene_->setSceneRect(image.rect());
        fitImage();
    }

    void setPlaceholder(const QString& text) {
        scene_->clear();
        hasImage_ = false;
        QGraphicsSimpleTextItem* label = scene_->addSimpleText(text);
        label->setBrush(QColor(0x9f, 0xd7, 0xe6));
        scene_->setSceneRect(label->boundingRect());
        resetTransform();
        centerOn(label);
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const double steps = event->angleDelta().y() / 120.0;
        if (!hasImage_ || steps == 0.0) {
            event->accept();
            return;
        }
        // Bounded against the fitted size rather than absolutely: the fit
        // scale depends on how wide the three views happen to be.
        const double factor = std::pow(1.2, steps);
        const double next = transform().m11() * factor;
        if (next < fitScale_ * 0.9 || next > fitScale_ * 10.0) {
            event->accept();
            return;
        }
        scale(factor, factor);
        zoomed_ = true;
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (hasImage_) fitImage();
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        // A view that has not been zoomed keeps filling itself as the pane
        // changes width; one the operator has zoomed stays where they put it.
        if (hasImage_ && !zoomed_) fitImage();
    }

private:
    void fitImage() {
        resetTransform();
        fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        fitScale_ = transform().m11();
        zoomed_ = false;
    }

    QGraphicsScene* scene_;
    double fitScale_ = 1.0;
    bool zoomed_ = false;
    bool hasImage_ = false;
};

}  // namespace beam::app
