#pragma once

#include <Eigen/Core>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <optional>
#include <functional>
#include <vector>

// The grey window and tone curve the slice panes draw with, shared so the 3D
// scene's planes cannot drift from the 2D panes showing the same slice.
struct WorkflowGreyWindow {
    double lo = 0.0;
    double hi = 1.0;
};
WorkflowGreyWindow workflowGreyWindowFor(const Eigen::MatrixXd& slice);
QImage workflowToneMapSlice(const Eigen::MatrixXd& slice, WorkflowGreyWindow window,
                            double brightness, double contrast, bool flipHorizontal);

struct WorkflowMriMarker {
    QPointF normalizedPosition;
    QString label;
    QColor color;
    bool crosshair = false;
    bool draggable = false;
    int markerIndex = -1;
    // Shown on hover. Last, so the existing aggregate initialisers that stop
    // at markerIndex keep compiling with no tooltip.
    QString tooltip;
    // Ring radius. Sixteen lock positions sit close together and read as
    // blobs at the fiducial size.
    double radius = 5.0;
};

// Native image interaction used by the new workflow UI. Kept independent
// from Beam's work-in-progress MriSliceView so its behavior can be reviewed
// and tested on its own.
class QMenu;

class WorkflowMriView final : public QWidget {
public:
    explicit WorkflowMriView(QWidget* parent = nullptr);

    void setSlice(const Eigen::MatrixXd& slice, bool flipHorizontal = false);
    void setMaskOverlay(const Eigen::MatrixXd& mask, QColor color, double opacity, bool flipHorizontal = false);
    void setSecondaryMaskOverlay(const Eigen::MatrixXd& mask, QColor color, double opacity, bool flipHorizontal = false);
    void setMarkers(std::vector<WorkflowMriMarker> markers);
    void setRasMapping(QString plane, double fixedCoordinateMm,
                       double horizontalMinMm, double horizontalMaxMm,
                       double verticalMinMm, double verticalMaxMm,
                       bool reverseHorizontal, bool reverseVertical);
    void setNavigationCrosshair(QPointF normalizedPosition);
    void setNavigationCrosshairVisible(bool visible);
    void adjustBrightness(double amount);
    void resetBrightness();
    // True when a marker drag was in progress and has been put back.
    bool cancelMarkerDrag();
    void adjustContrast(double amount);
    void resetContrast();
    double brightness() const { return brightness_; }
    double contrast() const { return contrast_; }
    WorkflowGreyWindow greyWindow() const { return {dataMin_, dataMax_}; }
    void resetView();
    // Brings a point into view, doing nothing when it is already comfortably
    // inside it. Zoom is only ever raised, so the operator's own framing and
    // pan survive a marker that is already on screen.
    void revealAt(QPointF normalizedPosition, double minimumZoom = 3.0);
    // Puts a point in the middle of the view, whether or not it was already
    // visible. revealAt leaves a point alone once it is comfortably on
    // screen, which is right for a marker being inspected and wrong for the
    // target: that one should sit under the crosshair in the centre every
    // time it moves. Zoom is still only ever raised.
    void centreOn(QPointF normalizedPosition, double minimumZoom = 3.0);
    void setPointPlacementEnabled(bool enabled);
    void setPointPickedHandler(std::function<void(const Eigen::Vector3d&)> handler);
    // The context menu's "Move fiducial ... to mouse point" is an explicit
    // relocation, not an in-plane refinement, so it goes through its own
    // handler: the caller may want to write all three coordinates here while
    // treating a drag as a measurement of only the two in-plane ones. Falls
    // back to the picked handler when unset.
    void setPointMovedHandler(std::function<void(const Eigen::Vector3d&)> handler);
    void setMarkerPickedHandler(std::function<void(int)> handler);
    void setCoordinatePasteOptions(QStringList options);
    // Adds "Open viewer in a new window" to the context menu. The window
    // itself is the caller's to build -- this view knows nothing about the
    // slider row that drives it.
    void setOpenViewerHandler(std::function<void()> handler);
    // Lets the overlay be turned on and off from a detached viewer too, which
    // is where sixteen labels are easiest to read.
    void setLockOverlayHandler(std::function<void(bool)> handler);
    // Opens the three slices as planes in space, with the array around them.
    void setOpenSceneHandler(std::function<void()> handler);
    void setLockOverlayShown(bool shown);

    // Widget placed at the top of the context menu; nullptr adds nothing.
    void setContextWidgetFactory(std::function<QWidget*(QMenu*)> factory);
    const std::function<QWidget*(QMenu*)>& contextWidgetFactory() const;

    // On-image measurement. Line takes two clicks; Contour collects vertices
    // until a double-click, Enter, or a click back on the first one. Escape
    // abandons one in progress, or clears a finished one. A measurement
    // belongs to the slice it was drawn on and is not shown on any other.
    enum class MeasureMode { None, Line, Angle, Contour };
    void beginMeasurement(MeasureMode mode);
    // Makes this view share another view.s measurement instead of owning one,
    // so a detached viewer and its pane are the same canvas.
    void setMeasurementPeer(WorkflowMriView* peer);
    void clearMeasurement();
    bool hasMeasurement() const;
    bool measurementInProgress() const;
    void cancelMeasurementInProgress();
    // Rendered on the image; also readable so callers and tests can check it.
    QString measurementSummary() const;
    QStringList measurementSummaries() const;
    // Copies everything this view renders from another instance: the slice,
    // overlays, markers, RAS mapping and brightness. Used to keep a detached
    // viewer showing exactly what its source pane shows, without duplicating
    // the per-plane assembly in showMriPreviews.
    void mirrorFrom(const WorkflowMriView& source);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void showMarkerTooltipAt(const QPointF& widgetPosition, const QPoint& globalPosition);
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuildImage();
    QRectF imageRect() const;
    void clampPan();
    void updateMouseCoordinate(const QPointF& widgetPosition);
    std::optional<Eigen::Vector3d> rasAtWidgetPosition(const QPointF& widgetPosition) const;
    QPointF normalizedAtWidgetPosition(const QPointF& widgetPosition) const;
    struct Measurement {
        MeasureMode mode = MeasureMode::None;
        QString plane;
        double sliceMm = 0.0;
        std::vector<QPointF> normalized;
        std::vector<Eigen::Vector3d> rasMm;
        bool finished = false;
    };
    static std::size_t pointsNeededFor(MeasureMode mode);
    bool measurementShownHere(const Measurement& measurement) const;
    bool grabMeasurementVertex(const QPointF& widgetPosition);
    void moveGrabbedVertex(const QPointF& widgetPosition);
    bool draggingMeasurementVertex() const;
    void releaseMeasurementVertex();
    void setMeasurementHover(const QPointF& widgetPosition);
    std::vector<Eigen::Vector3d> pointsForSummary(const Measurement& measurement) const;
    QString summaryFor(const Measurement& measurement) const;
    WorkflowMriView* measurementOwner();
    const WorkflowMriView* measurementOwner() const;
    bool addMeasurementPoint(const QPointF& widgetPosition);
    void finishMeasurement();
    void paintMeasurement(QPainter& painter, const QRectF& displayed) const;

    Eigen::MatrixXd slice_;
    QImage image_;
    QImage maskImage_;
    QImage secondaryMaskImage_;
    std::vector<WorkflowMriMarker> markers_;
    bool flipHorizontal_ = false;
    double dataMin_ = 0.0;
    double dataMax_ = 1.0;
    double zoom_ = 1.0;
    double brightness_ = 1.0;
    double contrast_ = 1.0;
    QPointF pan_;
    QPoint lastMousePosition_;
    bool panning_ = false;
    bool pendingPlacement_ = false;
    QPoint pressPosition_;
    bool draggingMarker_ = false;
    // Where a dragged marker was picked up, so Escape can put it back.
    std::optional<Eigen::Vector3d> dragOriginRas_;
    QPointF crosshair_ = QPointF(0.5, 0.5);
    bool navigationCrosshairVisible_ = false;
    QString plane_;
    double fixedCoordinateMm_ = 0.0;
    double horizontalMinMm_ = 0.0;
    double horizontalMaxMm_ = 0.0;
    double verticalMinMm_ = 0.0;
    double verticalMaxMm_ = 0.0;
    bool reverseHorizontal_ = false;
    bool reverseVertical_ = false;
    std::optional<Eigen::Vector3d> mouseRasMm_;
    QPointF mouseWidgetPosition_;
    bool pointPlacementEnabled_ = false;
    std::function<void(const Eigen::Vector3d&)> pointPickedHandler_;
    std::function<void(const Eigen::Vector3d&)> pointMovedHandler_;
    std::function<void(int)> markerPickedHandler_;
    QStringList coordinatePasteOptions_;
    std::vector<Measurement> measurements_;
    int activeMeasurement_ = -1;
    int dragMeasurement_ = -1;
    int dragVertex_ = -1;
    std::optional<QPointF> hoverNormalized_;
    std::optional<Eigen::Vector3d> hoverRasMm_;
    QPointer<WorkflowMriView> measurementPeer_;
    std::function<void()> openViewerHandler_;
    std::function<void(bool)> lockOverlayHandler_;
    std::function<void()> openSceneHandler_;
    bool lockOverlayShown_ = false;
    std::function<QWidget*(QMenu*)> contextWidgetFactory_;
};
