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
    void setSelectionHandler(std::function<void(int)> handler);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    std::array<QPointF, 6> markerCenters() const;

    int selectedIndex_ = -1;
    std::array<QString, 6> coordinateLabels_{};
    std::function<void(int)> selectionHandler_;
};
