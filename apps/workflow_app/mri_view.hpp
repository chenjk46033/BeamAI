#pragma once

#include <Eigen/Core>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <optional>
#include <functional>
#include <vector>

struct WorkflowMriMarker {
    QPointF normalizedPosition;
    QString label;
    QColor color;
    bool crosshair = false;
    bool draggable = false;
    int markerIndex = -1;
};

// Native image interaction used by the new workflow UI. Kept independent
// from Beam's work-in-progress MriSliceView so its behavior can be reviewed
// and tested on its own.
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
    void resetView();
    void focusOn(QPointF normalizedPosition, double zoom = 3.0);
    void setPointPlacementEnabled(bool enabled);
    void setPointPickedHandler(std::function<void(const Eigen::Vector3d&)> handler);
    void setMarkerPickedHandler(std::function<void(int)> handler);
    void setCoordinatePasteOptions(QStringList options);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void rebuildImage();
    QRectF imageRect() const;
    void clampPan();
    void updateMouseCoordinate(const QPointF& widgetPosition);
    std::optional<Eigen::Vector3d> rasAtWidgetPosition(const QPointF& widgetPosition) const;

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
    bool draggingMarker_ = false;
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
    std::function<void(int)> markerPickedHandler_;
    QStringList coordinatePasteOptions_;
};
