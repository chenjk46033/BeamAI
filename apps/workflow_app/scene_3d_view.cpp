#include "scene_3d_view.hpp"

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainterPath>
#include <QWidgetAction>
#include <QPolygonF>
#include <QWheelEvent>

namespace beam::app {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// Drawn back to front, so each item carries the depth it is sorted on.
struct Drawable {
    double depth = 0.0;
    int kind = 0;  // 0 plane, 1 quad, 2 point, 3 ellipsoid
    std::size_t index = 0;
};

// Orthographic projection takes an ellipsoid to an ellipse exactly, so
// diagonalising a 2x2 gives its screen axes -- no tessellation.
struct ProjectedEllipse {
    double semiA = 0.0;
    double semiB = 0.0;
    double tiltRad = 0.0;
};

ProjectedEllipse projectEllipsoid(const Eigen::Vector3d& semiAxesMm,
                                  const Eigen::Vector3d& right,
                                  const Eigen::Vector3d& up) {
    const Eigen::Vector3d a = semiAxesMm.cwiseProduct(right);
    const Eigen::Vector3d b = semiAxesMm.cwiseProduct(up);
    const double maa = a.squaredNorm();
    const double mbb = b.squaredNorm();
    const double mab = a.dot(b);

    const double mean = 0.5 * (maa + mbb);
    const double diff = 0.5 * (maa - mbb);
    const double root = std::sqrt(std::max(0.0, diff * diff + mab * mab));
    ProjectedEllipse out;
    out.semiA = std::sqrt(std::max(0.0, mean + root));
    out.semiB = std::sqrt(std::max(0.0, mean - root));
    // Screen y grows downward, so the tilt is negated to match.
    out.tiltRad = -0.5 * std::atan2(2.0 * mab, maa - mbb);
    return out;
}

}  // namespace

Scene3DView::Scene3DView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 300);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
}

void Scene3DView::setPlanes(std::vector<ScenePlane> planes) {
    planes_ = std::move(planes);
    update();
}

void Scene3DView::setQuads(std::vector<SceneQuad> quads) {
    quads_ = std::move(quads);
    update();
}

void Scene3DView::setPoints(std::vector<ScenePoint> points) {
    points_ = std::move(points);
    update();
}

void Scene3DView::setEllipsoids(std::vector<SceneEllipsoid> ellipsoids) {
    ellipsoids_ = std::move(ellipsoids);
    update();
}

void Scene3DView::setContextWidgetFactory(std::function<QWidget*(QMenu*)> factory) {
    contextWidgetFactory_ = std::move(factory);
}

void Scene3DView::contextMenuEvent(QContextMenuEvent* event) {
    if (!contextWidgetFactory_) return;
    QMenu menu(this);
    QWidget* supplied = contextWidgetFactory_(&menu);
    if (supplied == nullptr) return;
    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(supplied);
    menu.addAction(action);
    menu.exec(event->globalPos());
}

void Scene3DView::setCentreMm(const Eigen::Vector3d& centreMm) {
    centreMm_ = centreMm;
    update();
}

Eigen::Matrix3d Scene3DView::basis() const {
    const double a = azimuthDeg_ * kDegToRad;
    const double e = elevationDeg_ * kDegToRad;
    // Toward the viewer, from the two angles.
    const Eigen::Vector3d toward(std::cos(e) * std::sin(a), -std::cos(e) * std::cos(a),
                                 std::sin(e));
    const Eigen::Vector3d worldUp(0.0, 0.0, 1.0);
    Eigen::Vector3d right = worldUp.cross(toward);
    if (right.norm() < 1e-9) right = Eigen::Vector3d(1.0, 0.0, 0.0);
    right.normalize();
    const Eigen::Vector3d up = toward.cross(right).normalized();
    Eigen::Matrix3d m;
    m.col(0) = right;
    m.col(1) = up;
    m.col(2) = toward;
    return m;
}

double Scene3DView::scale() const {
    // The head is roughly 200 mm across; fit that in the smaller side.
    const double side = std::min(width(), height());
    return zoom_ * side / 220.0;
}

QPointF Scene3DView::project(const Eigen::Vector3d& worldMm, const Eigen::Matrix3d& m) const {
    const Eigen::Vector3d local = worldMm - centreMm_;
    const double s = scale();
    // Screen y grows downward; the camera's up is the other way.
    return QPointF(width() / 2.0 + local.dot(m.col(0)) * s,
                   height() / 2.0 - local.dot(m.col(1)) * s);
}

double Scene3DView::depthOf(const Eigen::Vector3d& worldMm, const Eigen::Matrix3d& m) const {
    return (worldMm - centreMm_).dot(m.col(2));
}

void Scene3DView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x0d, 0x13, 0x17));
    if (planes_.empty() && quads_.empty() && points_.empty() && ellipsoids_.empty()) {
        painter.setPen(QColor(0x7e, 0x8c, 0x93));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Register the array to see it in space."));
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const Eigen::Matrix3d m = basis();
    const auto centroid = [](const std::array<Eigen::Vector3d, 4>& corners) {
        return ((corners[0] + corners[1] + corners[2] + corners[3]) / 4.0).eval();
    };

    std::vector<Drawable> order;
    order.reserve(planes_.size() + quads_.size() + ellipsoids_.size() + points_.size());
    for (std::size_t i = 0; i < planes_.size(); ++i)
        order.push_back({depthOf(centroid(planes_[i].cornersMm), m), 0, i});
    for (std::size_t i = 0; i < quads_.size(); ++i)
        order.push_back({depthOf(centroid(quads_[i].cornersMm), m), 1, i});
    // Furthest first: a painter has no depth buffer.
    std::sort(order.begin(), order.end(),
              [](const Drawable& a, const Drawable& b) { return a.depth < b.depth; });

    // After the sort: the focus sits where the slices cross, so sorting against
    // them ties on depth and a near-opaque plane paints it out as the view turns.
    for (std::size_t i = 0; i < ellipsoids_.size(); ++i)
        order.push_back({depthOf(ellipsoids_[i].centreMm, m), 3, i});

    // Markers go in a pass of their own, over everything: a slice is a sheet the width of the
    for (std::size_t i = 0; i < points_.size(); ++i)
        order.push_back({depthOf(points_[i].positionMm, m), 2, i});

    for (const Drawable& item : order) {
        if (item.kind == 0) {
            const ScenePlane& plane = planes_[item.index];
            if (plane.image.isNull()) continue;
            const QPointF tl = project(plane.cornersMm[0], m);
            const QPointF tr = project(plane.cornersMm[1], m);
            const QPointF bl = project(plane.cornersMm[3], m);
            // Orthographic: the plane's map to the screen is affine, so three corners fix it
            const double w = std::max(1, plane.image.width());
            const double h = std::max(1, plane.image.height());
            const QTransform to(
                (tr.x() - tl.x()) / w, (tr.y() - tl.y()) / w,
                (bl.x() - tl.x()) / h, (bl.y() - tl.y()) / h,
                tl.x(), tl.y());
            painter.save();
            painter.setTransform(to, true);
            painter.setOpacity(0.92);
            painter.drawImage(QPointF(0, 0), plane.image);
            painter.restore();
            painter.setOpacity(1.0);
            QPolygonF edge;
            for (const Eigen::Vector3d& corner : plane.cornersMm) edge << project(corner, m);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0x2f, 0x6f, 0x87), 1.0));
            painter.drawPolygon(edge);
            continue;
        }
        if (item.kind == 1) {
            const SceneQuad& quad = quads_[item.index];
            QPolygonF face;
            for (const Eigen::Vector3d& corner : quad.cornersMm) face << project(corner, m);
            painter.setPen(Qt::NoPen);
            painter.setBrush(quad.color);
            painter.drawPolygon(face);
            continue;
        }
        if (item.kind == 3) {
            const SceneEllipsoid& blob = ellipsoids_[item.index];
            const ProjectedEllipse shape =
                projectEllipsoid(blob.semiAxesMm, m.col(0), m.col(1));
            const double s = scale();
            QColor edge = blob.color;
            edge.setAlphaF(std::min(1.0, blob.opacity * 2.2));
            painter.save();
            painter.translate(project(blob.centreMm, m));
            painter.rotate(shape.tiltRad / kDegToRad);
            painter.setOpacity(blob.opacity);
            painter.setBrush(blob.color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPointF(0, 0), shape.semiA * s, shape.semiB * s);
            painter.setOpacity(1.0);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(edge, 1.2));
            painter.drawEllipse(QPointF(0, 0), shape.semiA * s, shape.semiB * s);
            painter.restore();
            continue;
        }
        const ScenePoint& point = points_[item.index];
        const QPointF at = project(point.positionMm, m);
        painter.setBrush(point.color);
        painter.setPen(QPen(QColor(0x10, 0x16, 0x1a, 200), 1.2));
        painter.drawEllipse(at, point.radius, point.radius);
        if (!point.label.isEmpty()) {
            painter.setPen(point.color);
            painter.drawText(at + QPointF(point.radius + 3.0, 4.0), point.label);
        }
    }

    painter.setPen(QColor(0x9f, 0xb0, 0xba));
    painter.drawText(QRectF(8, height() - 26.0, width() - 16.0, 20.0), Qt::AlignLeft,
                     QStringLiteral("drag to rotate  ·  wheel to zoom  ·  R resets"));
    painter.drawText(QRectF(8, 6, width() - 16.0, 20.0), Qt::AlignRight,
                     QStringLiteral("az %1°  el %2°")
                         .arg(azimuthDeg_, 0, 'f', 0)
                         .arg(elevationDeg_, 0, 'f', 0));
}

void Scene3DView::mousePressEvent(QMouseEvent* event) {
    dragFrom_ = event->position();
    dragging_ = true;
    setCursor(Qt::ClosedHandCursor);
}

void Scene3DView::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPointF delta = event->position() - dragFrom_;
    dragFrom_ = event->position();
    azimuthDeg_ += delta.x() * 0.4;
    // Stopping short of straight up avoids the basis collapsing.
    elevationDeg_ = std::clamp(elevationDeg_ + delta.y() * 0.4, -85.0, 85.0);
    update();
}

void Scene3DView::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = false;
    unsetCursor();
}

void Scene3DView::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    zoom_ = std::clamp(zoom_ * std::pow(1.12, steps), 0.3, 6.0);
    update();
}

void Scene3DView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) {
        azimuthDeg_ = 35.0;
        elevationDeg_ = 18.0;
        zoom_ = 1.0;
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace beam::app



