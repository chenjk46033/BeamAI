// "Open viewer in a new window" -- a larger, resizable window showing one
// MRI plane, with the same controls as the pane it came from.
//
// Modelled on Beam's own construct (libs/gui_qt/src/mri_slice_view.cpp's
// openStandaloneViewer: a separate top-level window holding a second view,
// sized a multiple of the pane's, closed by Escape). Two differences follow
// from how this application is built:
//
//   * The slice controls live in the Designer form next to each pane, not
//     inside the view. Rather than give the window its own slice state, its
//     buttons and slider drive the pane's own widgets -- so the two can never
//     disagree, and closing the window loses nothing.
//   * The view is refreshed by mirroring the pane (WorkflowMriView::mirrorFrom)
//     each time showMriPreviews runs, instead of re-deriving the slice here.

#include "workflow_window.hpp"

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

#include "ui_workflow_shell.h"

namespace beam::app {

namespace {

// Beam's own factor for the same feature, relative to the source pane.
constexpr double kViewerScale = 2.0;
constexpr int kMinViewerSide = 520;

}  // namespace

void WorkflowWindow::openDetachedViewer(WorkflowMriView* source, const QString& title, QSlider* slider,
                                        QToolButton* previous, QToolButton* next, QToolButton* reset,
                                        QLabel* sliceLabel) {
    if (!source || !mriLoaded_) {
        showMessage(QStringLiteral("Load the patient MRI before opening a separate viewer."), true);
        return;
    }
    // One window per plane: re-triggering raises the existing one rather than
    // stacking duplicates that would all mirror the same pane.
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
    layout->addWidget(view, 1);

    // The control row drives the pane's own widgets, so this window never
    // holds slice state of its own.
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
        // Finished measurements go only through "Clear measurements".
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

// Called at the end of showMriPreviews: every open viewer re-copies its
// source pane, so slice changes, overlays, markers and brightness all follow
// without this file knowing how any of them are assembled.
void WorkflowWindow::refreshDetachedViewers() {
    for (const DetachedViewer& open : detachedViewers_) {
        open.view->mirrorFrom(*open.source);
        if (open.label && open.sourceLabel) open.label->setText(open.sourceLabel->text());
    }
}

// Every pane offers the same context-menu entry. The window it opens drives
// that pane's own slider and buttons, so the plane has one source of truth
// whether or not a viewer is open.
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
    }
}

}  // namespace beam::app
