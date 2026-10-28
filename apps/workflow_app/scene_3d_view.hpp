#pragma once

// The three MRI slices standing as planes in space, with the array, the
// fiducials and the lock-position targets around them. Orthographic, so a
// textured plane maps to the screen by an affine transform -- which QPainter
// does exactly, and which keeps the application free of any GPU dependency.

#include <array>
#include <functional>
#include <vector>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QMenu;
QT_END_NAMESPACE

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace beam::app {

// One MRI slice placed in the patient: the image, and the mm positions of its
// four corners in the order top-left, top-right, bottom-right, bottom-left.
struct ScenePlane {
    QImage image;
    std::array<Eigen::Vector3d, 4> cornersMm{};
};

// A quad of the transducer, or any other flat patch drawn as geometry.
struct SceneQuad {
    std::array<Eigen::Vector3d, 4> cornersMm{};
    QColor color;
};

struct ScenePoint {
    Eigen::Vector3d positionMm = Eigen::Vector3d::Zero();
    QColor color;
    QString label;
    double radius = 4.0;
};

// An axis-aligned ellipsoid, for the acoustic focus.
struct SceneEllipsoid {
    Eigen::Vector3d centreMm = Eigen::Vector3d::Zero();
    Eigen::Vector3d semiAxesMm = Eigen::Vector3d::Ones();
    QColor color;
    double opacity = 0.35;
};

class Scene3DView final : public QWidget {
    Q_OBJECT

public:
    explicit Scene3DView(QWidget* parent = nullptr);

    void setPlanes(std::vector<ScenePlane> planes);
    void setQuads(std::vector<SceneQuad> quads);
    void setPoints(std::vector<ScenePoint> points);
    void setEllipsoids(std::vector<SceneEllipsoid> ellipsoids);
    // The lock-position popup, the same one the slice panes carry.
    void setContextWidgetFactory(std::function<QWidget*(QMenu*)> factory);
    // Everything is drawn about this, and the view rotates around it.
    void setCentreMm(const Eigen::Vector3d& centreMm);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Camera basis from the two angles: columns are right, up and toward the
    // viewer, so a world point's depth is the third component.
    Eigen::Matrix3d basis() const;
    QPointF project(const Eigen::Vector3d& worldMm, const Eigen::Matrix3d& m) const;
    double depthOf(const Eigen::Vector3d& worldMm, const Eigen::Matrix3d& m) const;
    double scale() const;

    std::vector<ScenePlane> planes_;
    std::vector<SceneQuad> quads_;
    std::vector<ScenePoint> points_;
    std::vector<SceneEllipsoid> ellipsoids_;
    std::function<QWidget*(QMenu*)> contextWidgetFactory_;
    Eigen::Vector3d centreMm_ = Eigen::Vector3d::Zero();

    // Opens coronal: the camera in front of the patient looking back, superior
    // up, so the head stands straight and screen right is the patient's left.
    double azimuthDeg_ = 180.0;
    double elevationDeg_ = 0.0;
    double zoom_ = 1.0;
    QPointF dragFrom_;
    bool dragging_ = false;
};

}  // namespace beam::app
