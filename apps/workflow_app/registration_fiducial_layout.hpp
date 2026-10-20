#pragma once

#include <QPointF>
#include <QWidget>
#include <array>

#include <functional>

class RegistrationFiducialLayout final : public QWidget {
public:
    explicit RegistrationFiducialLayout(QWidget* parent = nullptr);

    void setSelectedIndex(int index);
    void setMarkerCoordinateLabels(std::array<QString, 6> labels);
    // Each entry is one marker's (AP, IS) position in millimetres. Supplying
    // them draws the two triangles in their measured shape; without them the
    // drawing falls back to a right triangle, which is the nominal shape only.
    void setMarkerPositions(std::array<QPointF, 6> positionsApIsMm);
    // LR for each marker, so the side lengths are the real 3-D distance
    // rather than the distance in the projection the diagram draws.
    void setMarkerDepthsMm(std::array<double, 6> lrMm);
    void setSelectionHandler(std::function<void(int)> handler);
    // Back to the nominal right-triangle drawing with no coordinates against
    // it. Nothing else can un-set the measured shape, and a diagram still
    // holding the last patient's marker positions is a drawing of their head.
    void clearMarkers();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    std::array<QPointF, 6> markerCenters() const;
    double sideLengthMm(int from, int to) const;

    int selectedIndex_ = -1;
    std::array<QString, 6> coordinateLabels_{};
    std::array<QPointF, 6> positionsApIsMm_{};
    bool hasPositions_ = false;
    std::array<double, 6> depthsMm_{};
    bool hasDepths_ = false;
    std::function<void(int)> selectionHandler_;
};
