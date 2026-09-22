#pragma once

#include <QGraphicsView>
#include <QPixmap>

class QGraphicsPixmapItem;
class QGraphicsScene;
class QWheelEvent;
class QMouseEvent;
class QResizeEvent;

namespace beam::gui_qt {

// Generic zoomable/pannable static-image viewer (Targeting Examples'
// reference photos -- not MRI slice data, so MriSliceView's own
// zoom/pan doesn't apply here). Mouse wheel zooms centered on the
// cursor; left-drag pans (QGraphicsView's built-in ScrollHandDrag);
// double-click resets to fit-the-view.
class ZoomableImageView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ZoomableImageView(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pixmap);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void fitToView();
    // Drag-panning (QGraphicsView's own ScrollHandDrag) can scroll past
    // the pixmap's own edges, revealing blank scene background -- MATLAB
    // never does this, it keeps real image content in view. Snaps the
    // scroll position back so the pixmap always covers the viewport
    // whenever it's zoomed in enough to be able to.
    void clampPan();

    QGraphicsScene* scene_;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    // Resize re-fits automatically until the user zooms by hand -- past
    // that point, a resize (e.g. this window's own proportional rescale)
    // shouldn't silently discard their zoom/pan.
    bool manuallyZoomed_ = false;
};

}  // namespace beam::gui_qt
