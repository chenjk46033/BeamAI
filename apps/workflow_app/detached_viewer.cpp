// "Open viewer in a new window" -- a larger, resizable window showing one MRI plane, with the

#include "workflow_window.hpp"
#include "workflow_internal.hpp"

#include <array>

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QShortcut>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "mri_view.hpp"
#include "scene_3d_view.hpp"

#include "array/array_types.hpp"
#include "mri/slice.hpp"

#include "ui_workflow_shell.h"

namespace beam::app {

namespace {

// Beam's own factor for the same feature, relative to the source pane.
constexpr double kViewerScale = 2.0;
constexpr int kMinViewerSide = 520;

}  // namespace


// The scene the 3D window shows: the three slices where they really sit, the transducer around
void WorkflowWindow::buildScene(Scene3DView* view) {
    if (view == nullptr || !mriLoaded_) return;
    const auto& lr = mriAxes_.dimLR;
    const auto& ap = mriAxes_.dimAP;
    const auto& is = mriAxes_.dimIS;
    if (lr.size() < 2 || ap.size() < 2 || is.size() < 2) return;
    const double lr0 = lr(0), lr1 = lr(lr.size() - 1);
    const double ap0 = ap(0), ap1 = ap(ap.size() - 1);
    const double is0 = is(0), is1 = is(is.size() - 1);

    const int sagittal = ui_->sagittalSlider->value() + 1;
    const int coronal = ui_->coronalSlider->value() + 1;
    const int axial = ui_->axialSlider->value() + 1;
    const double atLr = lr(std::clamp(sagittal - 1, 0, int(lr.size()) - 1));
    const double atAp = ap(std::clamp(coronal - 1, 0, int(ap.size()) - 1));
    const double atIs = is(std::clamp(axial - 1, 0, int(is.size()) - 1));

    // Greyscale straight from the same slice the 2D panes draw.
    const auto toImage = [](const Eigen::MatrixXd& slice) {
        QImage out(int(slice.cols()), int(slice.rows()), QImage::Format_Grayscale8);
        double lo = slice.minCoeff(), hi = slice.maxCoeff();
        const double range = std::max(hi - lo, 1e-12);
        for (int y = 0; y < out.height(); ++y) {
            uchar* row = out.scanLine(y);
            for (int x = 0; x < out.width(); ++x)
                row[x] = uchar(std::clamp((slice(y, x) - lo) / range, 0.0, 1.0) * 255.0);
        }
        return out;
    };

    std::vector<ScenePlane> planes;
    ScenePlane sag;
    sag.image = toImage(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"));
    sag.cornersMm = {Eigen::Vector3d(atLr, ap0, is1), Eigen::Vector3d(atLr, ap1, is1),
                     Eigen::Vector3d(atLr, ap1, is0), Eigen::Vector3d(atLr, ap0, is0)};
    planes.push_back(std::move(sag));
    ScenePlane cor;
    cor.image = toImage(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    cor.cornersMm = {Eigen::Vector3d(lr0, atAp, is1), Eigen::Vector3d(lr1, atAp, is1),
                     Eigen::Vector3d(lr1, atAp, is0), Eigen::Vector3d(lr0, atAp, is0)};
    planes.push_back(std::move(cor));
    ScenePlane axi;
    axi.image = toImage(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    axi.cornersMm = {Eigen::Vector3d(lr0, ap1, atIs), Eigen::Vector3d(lr1, ap1, atIs),
                     Eigen::Vector3d(lr1, ap0, atIs), Eigen::Vector3d(lr0, ap0, atIs)};
    planes.push_back(std::move(axi));
    view->setPlanes(std::move(planes));

    // One quad per transducer element, from the corners the geometry carries.
    std::vector<SceneQuad> quads;
    if (registrationGeometryLoaded_) {
        const Eigen::MatrixXd& rect = arrayData_.arrayTotal.rect;
        for (Eigen::Index c = 0; c < rect.cols(); ++c) {
            SceneQuad quad;
            for (int k = 0; k < 4; ++k)
                quad.cornersMm[std::size_t(k)] =
                    beam::array::rectCorner(rect, int(c), k + 1) * 1000.0;
            quad.color = QColor(0xff, 0xdc, 0x28, 120);
            quads.push_back(std::move(quad));
        }
    }
    view->setQuads(std::move(quads));

    std::vector<ScenePoint> points;
    for (std::size_t i = 0; i < fiducials_.size(); ++i)
        points.push_back({fiducials_[i].position * 1000.0, QColor(0xe6, 0x2d, 0x37), QString(), 4.0});
    const int currentH =
        leftHorizontalPositionSlider_ == nullptr ? 0 : leftHorizontalPositionSlider_->value();
    const int currentV =
        leftVerticalPositionSlider_ == nullptr ? 0 : leftVerticalPositionSlider_->value();
    for (const beam::registration::LockPositionTarget& target : lockOverlayTargets_) {
        const bool current = target.horizontal == currentH && target.vertical == currentV;
        points.push_back({target.centreMm,
                          current ? QColor(0x5c, 0xcd, 0xe5) : QColor(0xff, 0xcc, 0x33),
                          current ? QStringLiteral("H%1V%2")
                                        .arg(target.horizontal)
                                        .arg(target.vertical)
                                  : QString(),
                          current ? 5.0 : 3.0});
    }
    view->setPoints(std::move(points));
    view->setCentreMm(targetMm_);
}


// Its own top-level window, never a fourth pane: a pane would take width from the three MRI
void WorkflowWindow::openSceneWindow() {
    if (!mriLoaded_) {
        showMessage(QStringLiteral("Load the patient MRI before opening the 3D view."), true);
        return;
    }
    if (sceneWindow_ == nullptr) {
        sceneWindow_ = new QWidget(this, Qt::Window);
        sceneWindow_->setWindowTitle(QStringLiteral("BeamAI - array in space"));
        sceneWindow_->setAttribute(Qt::WA_DeleteOnClose, false);
        sceneWindow_->resize(720, 620);
        auto* layout = new QVBoxLayout(sceneWindow_);
        layout->setContentsMargins(0, 0, 0, 0);
        sceneView_ = new Scene3DView(sceneWindow_);
        layout->addWidget(sceneView_);
        auto* close = new QShortcut(QKeySequence(Qt::Key_Escape), sceneWindow_);
        connect(close, &QShortcut::activated, sceneWindow_, &QWidget::close);
    }
    buildScene(sceneView_);
    sceneWindow_->show();
    sceneWindow_->raise();
    sceneWindow_->activateWindow();
}

void WorkflowWindow::openDetachedViewer(WorkflowMriView* source, const QString& title, QSlider* slider,
                                        QToolButton* previous, QToolButton* next, QToolButton* reset,
                                        QLabel* sliceLabel) {
    if (!source || !mriLoaded_) {
        showMessage(QStringLiteral("Load the patient MRI before opening a separate viewer."), true);
        return;
    }
    // One window per plane: re-triggering raises the existing one rather than stacking
    for (const DetachedViewer& open : detachedViewers_) {
        if (open.source == source) {
            open.window->raise();
            open.window->activateWindow();
            return;
        }
    }

    auto* window = new QWidget(this, Qt::Window);
    window->setWindowTitle(QStringLiteral("%1 — Beam MRI").arg(title));
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setAutoFillBackground(true);
    QPalette palette = window->palette();
    palette.setColor(QPalette::Window, QColor(27, 27, 27));
    palette.setColor(QPalette::WindowText, QColor(242, 247, 248));
    window->setPalette(palette);

    auto* layout = new QVBoxLayout(window);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto* view = new WorkflowMriView(window);
    view->setMinimumSize(kMinViewerSide, kMinViewerSide);
    view->mirrorFrom(*source);
    view->setMeasurementPeer(source);
    view->setContextWidgetFactory(source->contextWidgetFactory());
    layout->addWidget(view, 1);

    // The control row drives the pane's own widgets, so this window never holds slice state of
    auto* controls = new QHBoxLayout;
    const auto mirrorButton = [&](QToolButton* origin, const QString& fallbackText) {
        auto* button = new QToolButton(window);
        button->setText(origin ? origin->text() : fallbackText);
        button->setToolTip(origin ? origin->toolTip() : QString());
        button->setStyleSheet(origin ? origin->styleSheet() : QString());
        if (origin) {
            connect(button, &QToolButton::clicked, origin, &QToolButton::click);
            button->setEnabled(origin->isEnabled());
        }
        return button;
    };
    auto* previousButton = mirrorButton(previous, QStringLiteral("◀"));
    auto* nextButton = mirrorButton(next, QStringLiteral("▶"));
    auto* resetButton = mirrorButton(reset, QStringLiteral("↺"));

    auto* windowSlider = new QSlider(Qt::Horizontal, window);
    windowSlider->setStyleSheet(QString::fromLatin1(kSliderStyle));
    if (slider) {
        windowSlider->setRange(slider->minimum(), slider->maximum());
        windowSlider->setValue(slider->value());
        windowSlider->setEnabled(slider->isEnabled());
        // Two-way, guarded by Qt's own no-op-on-equal-value behaviour.
        connect(windowSlider, &QSlider::valueChanged, slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, windowSlider, &QSlider::setValue);
        connect(slider, &QSlider::rangeChanged, windowSlider, &QSlider::setRange);
        windowSlider->setToolTip(slider->toolTip());
        installSliceReadout(windowSlider, slider->property("sliceAxis").toString());
        windowSlider->setStyle(slider->style());
    }

    auto* label = new QLabel(sliceLabel ? sliceLabel->text() : QString(), window);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumWidth(110);
    label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; font-weight: 600; }"));

    controls->addWidget(previousButton);
    controls->addWidget(windowSlider, 1);
    controls->addWidget(nextButton);
    controls->addWidget(resetButton);
    controls->addSpacing(12);
    controls->addWidget(label);
    layout->addLayout(controls);

    auto* hint = new QLabel(QStringLiteral(
        "Right-click for brightness and fiducial controls · scroll to zoom · Esc closes this window"), window);
    hint->setStyleSheet(QStringLiteral("QLabel { color: #8fa3ab; }"));
    layout->addWidget(hint);

    // Escape closes, matching Beam's viewer.
    auto* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), window);
    connect(closeShortcut, &QShortcut::activated, window, [window, view] {
        // Same rule as the panes: abandon a half-drawn shape, otherwise close.
        if (view->measurementInProgress()) {
            view->cancelMeasurementInProgress();
            return;
        }
        window->close();
    });

    detachedViewers_.push_back(DetachedViewer{window, view, source, label, sliceLabel});
    connect(window, &QObject::destroyed, this, [this, window] {
        for (auto it = detachedViewers_.begin(); it != detachedViewers_.end(); ++it) {
            if (it->window == window) {
                detachedViewers_.erase(it);
                return;
            }
        }
    });

    window->resize(std::max(static_cast<int>(source->width() * kViewerScale), kMinViewerSide + 40),
                   std::max(static_cast<int>(source->height() * kViewerScale), kMinViewerSide + 110));
    window->show();
    window->raise();
    window->activateWindow();
}

// Called at the end of showMriPreviews: every open viewer re-copies its source pane, so slice
void WorkflowWindow::refreshDetachedViewers() {
    for (const DetachedViewer& open : detachedViewers_) {
        open.view->mirrorFrom(*open.source);
        if (open.label && open.sourceLabel) open.label->setText(open.sourceLabel->text());
    }
}

// Every pane offers the same context-menu entry.
void WorkflowWindow::wireDetachedViewerMenus() {
    struct Pane {
        WorkflowMriView* view;
        const char* title;
        QSlider* slider;
        QToolButton* previous;
        QToolButton* next;
        QToolButton* reset;
        QLabel* label;
    };
    const std::array<Pane, 9> panes = {{
        {ui_->sagittalPreview, "Sagittal", ui_->sagittalSlider, ui_->sagittalPreviousButton,
         ui_->sagittalNextButton, ui_->resetSagittalButton, ui_->sagittalSliceLabel},
        {ui_->coronalPreview, "Coronal", ui_->coronalSlider, ui_->coronalPreviousButton,
         ui_->coronalNextButton, ui_->resetCoronalButton, ui_->coronalSliceLabel},
        {ui_->axialPreview, "Axial", ui_->axialSlider, ui_->axialPreviousButton, ui_->axialNextButton,
         ui_->resetAxialButton, ui_->axialSliceLabel},
        {ui_->registrationSagittalPreview, "Sagittal", ui_->registrationSagittalSlider,
         ui_->registrationSagittalPreviousButton, ui_->registrationSagittalNextButton,
         ui_->registrationResetSagittalButton, ui_->registrationSagittalLabel},
        {ui_->registrationCoronalPreview, "Coronal", ui_->registrationCoronalSlider,
         ui_->registrationCoronalPreviousButton, ui_->registrationCoronalNextButton,
         ui_->registrationResetCoronalButton, ui_->registrationCoronalLabel},
        {ui_->registrationAxialPreview, "Axial", ui_->registrationAxialSlider,
         ui_->registrationAxialPreviousButton, ui_->registrationAxialNextButton,
         ui_->registrationResetAxialButton, ui_->registrationAxialLabel},
        {treatmentSagittalPreview_, "Sagittal", ui_->treatmentSagittalSlider,
         ui_->treatmentSagittalPreviousButton, ui_->treatmentSagittalNextButton,
         ui_->treatmentSagittalResetButton, ui_->treatmentSagittalSliceLabel},
        {treatmentCoronalPreview_, "Coronal", ui_->treatmentCoronalSlider,
         ui_->treatmentCoronalPreviousButton, ui_->treatmentCoronalNextButton,
         ui_->treatmentCoronalResetButton, ui_->treatmentCoronalSliceLabel},
        {treatmentAxialPreview_, "Axial", ui_->treatmentAxialSlider, ui_->treatmentAxialPreviousButton,
         ui_->treatmentAxialNextButton, ui_->treatmentAxialResetButton, ui_->treatmentAxialSliceLabel},
    }};
    for (const Pane& pane : panes) {
        if (!pane.view) continue;
        const Pane captured = pane;
        pane.view->setOpenViewerHandler([this, captured] {
            openDetachedViewer(captured.view, QString::fromLatin1(captured.title), captured.slider,
                               captured.previous, captured.next, captured.reset, captured.label);
        });
        pane.view->setOpenSceneHandler([this] { openSceneWindow(); });
        // Only the Registration panes carry the overlay, which is what their lock position is
        if (pane.view == ui_->registrationSagittalPreview ||
            pane.view == ui_->registrationCoronalPreview ||
            pane.view == ui_->registrationAxialPreview)
            pane.view->setLockOverlayHandler([this](bool on) { setLockOverlayShown(on); });
    }
}

}  // namespace beam::app





