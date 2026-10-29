// "Open viewer in a new window" -- a larger, resizable window showing one MRI plane, with the

#include "workflow_window.hpp"
#include "workflow_internal.hpp"

#include <array>
#include <functional>

#include <QCloseEvent>
#include <QMenu>
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

    // The window and tone curve the 2D panes draw with, taken from the pane
    // showing that plane so the two cannot disagree. No flip: the plane's
    // corners already place the image in the patient.
    const auto toImage = [](const Eigen::MatrixXd& slice, const WorkflowMriView* pane) {
        return workflowToneMapSlice(slice, workflowGreyWindowFor(slice),
                                    pane == nullptr ? 1.0 : pane->brightness(),
                                    pane == nullptr ? 1.0 : pane->contrast(), false);
    };

    std::vector<ScenePlane> planes;
    ScenePlane sag;
    sag.image = toImage(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"),
                        ui_->sagittalPreview);
    sag.cornersMm = {Eigen::Vector3d(atLr, ap0, is1), Eigen::Vector3d(atLr, ap1, is1),
                     Eigen::Vector3d(atLr, ap1, is0), Eigen::Vector3d(atLr, ap0, is0)};
    planes.push_back(std::move(sag));
    ScenePlane cor;
    cor.image = toImage(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"),
                        ui_->coronalPreview);
    cor.cornersMm = {Eigen::Vector3d(lr0, atAp, is1), Eigen::Vector3d(lr1, atAp, is1),
                     Eigen::Vector3d(lr1, atAp, is0), Eigen::Vector3d(lr0, atAp, is0)};
    planes.push_back(std::move(cor));
    ScenePlane axi;
    axi.image = toImage(beam::mri::getSliceImage(mriVolume_, axial, "axial"),
                        ui_->axialPreview);
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

    // The acoustic focus rebuildFocusImage() fills: 30 mm along the beam, 5 mm
    // across. A 2D pane only ever cuts one slice through it.
    std::vector<SceneEllipsoid> field;
    if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked())
        field.push_back({targetMm_, Eigen::Vector3d(30.0, 5.0, 5.0),
                         QColor(255, 128, 128), 0.38});
    view->setEllipsoids(std::move(field));

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
        sceneWindow_->setWindowTitle(QStringLiteral("Beam - array in space"));
        sceneWindow_->setAttribute(Qt::WA_DeleteOnClose, false);
        // Sized off a slice pane the way a detached 2D viewer is, plus a tenth:
        // this one holds three planes and the array, not a single slice.
        constexpr double kSceneExtra = 1.1;
        sceneWindow_->resize(
            static_cast<int>(
                std::max(static_cast<int>(ui_->sagittalPreview->width() * kViewerScale),
                         kMinViewerSide + 40) * kSceneExtra),
            static_cast<int>(
                std::max(static_cast<int>(ui_->sagittalPreview->height() * kViewerScale),
                         kMinViewerSide + 110) * kSceneExtra));
        auto* layout = new QVBoxLayout(sceneWindow_);
        layout->setContentsMargins(0, 0, 0, 0);
        sceneView_ = new Scene3DView(sceneWindow_);
        sceneView_->setContextWidgetFactory(
            [this](QMenu* menu) { return buildSceneControlsWidget(menu); });
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
    // The 3D window too: it was built once when opened, so a re-registration or
    // a slice change left it showing the array where it used to be.
    if (sceneWindow_ != nullptr && sceneWindow_->isVisible()) buildScene(sceneView_);
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
        // The panes the overlay can appear on: the Registration three, and the
        // Treatment sagittal, which now draws it too.
        if (pane.view == ui_->registrationSagittalPreview ||
            pane.view == ui_->registrationCoronalPreview ||
            pane.view == ui_->registrationAxialPreview ||
            pane.view == treatmentSagittalPreview_)
            pane.view->setLockOverlayHandler([this](bool on) { setLockOverlayShown(on); });
    }
}

// The 3D window's popup: the brightness its planes are drawn with, over the
// same lock picker the slice panes carry.
QWidget* WorkflowWindow::buildSceneControlsWidget(QMenu* menu) {
    auto* container = new QWidget(menu);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 8, 10, 4);
    layout->setSpacing(6);

    const QString buttonStyle = QStringLiteral(
        "QToolButton { background: #f7fafb; color: #17313f; border: 1px solid #9eacb4; "
        "border-radius: 3px; min-width: 24px; max-width: 24px; min-height: 22px; max-height: 22px; "
        "font-family: 'Segoe UI Symbol'; font-weight: 700; } "
        "QToolButton:hover { background: #e3f2f6; border-color: #2888a2; }");

    // One brightness, held by the slice panes: the planes here are drawn with
    // it, so the 3D view cannot carry a second setting that disagrees.
    const auto eachPane = [this](const std::function<void(WorkflowMriView*)>& change) {
        for (WorkflowMriView* pane :
             {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview})
            if (pane != nullptr) change(pane);
        refreshDetachedViewers();
    };

    const auto addRow = [&](const QString& name,
                            const std::function<void(WorkflowMriView*, double)>& adjust,
                            const std::function<void(WorkflowMriView*)>& reset) {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(name, container);
        label->setMinimumWidth(72);
        row->addWidget(label);
        row->addSpacing(6);
        const auto makeButton = [container, &buttonStyle](QChar symbol, const QString& tip) {
            auto* button = new QToolButton(container);
            button->setText(QString(symbol));
            button->setToolTip(tip);
            button->setStyleSheet(buttonStyle);
            return button;
        };
        auto* up = makeButton(QChar(0x2191), QStringLiteral("Increase %1").arg(name.toLower()));
        auto* down = makeButton(QChar(0x2193), QStringLiteral("Decrease %1").arg(name.toLower()));
        auto* back = makeButton(QChar(0x21BA), QStringLiteral("Reset %1").arg(name.toLower()));
        row->addWidget(up);
        row->addWidget(down);
        row->addWidget(back);
        layout->addLayout(row);
        connect(up, &QToolButton::clicked, this, [eachPane, adjust] {
            eachPane([adjust](WorkflowMriView* pane) { adjust(pane, 0.1); });
        });
        connect(down, &QToolButton::clicked, this, [eachPane, adjust] {
            eachPane([adjust](WorkflowMriView* pane) { adjust(pane, -0.1); });
        });
        connect(back, &QToolButton::clicked, this, [eachPane, reset] { eachPane(reset); });
    };

    addRow(QStringLiteral("Brightness"),
           [](WorkflowMriView* pane, double amount) { pane->adjustBrightness(amount); },
           [](WorkflowMriView* pane) { pane->resetBrightness(); });
    addRow(QStringLiteral("Contrast"),
           [](WorkflowMriView* pane, double amount) { pane->adjustContrast(amount); },
           [](WorkflowMriView* pane) { pane->resetContrast(); });

    if (QWidget* picker = buildFiducialPickerWidget(menu)) layout->addWidget(picker);
    return container;
}

}  // namespace beam::app





