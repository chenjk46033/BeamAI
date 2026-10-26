#include "workflow_internal.hpp"

namespace beam::app {

void WorkflowWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // Let Qt finish the new layout first.
    QTimer::singleShot(0, this, [this] { mriHeightSyncPasses_ = 0; syncMriViewerHeights(); });
}

// Tagging the slider with its axis rather than keeping a list lets any slider carry the
void WorkflowWindow::installSliceReadout(QSlider* slider, const QString& axis) {
    if (!slider || axis.isEmpty()) return;
    slider->setProperty("sliceAxis", axis);
    slider->setMouseTracking(true);
    slider->installEventFilter(this);
    connect(slider, &QSlider::sliderMoved, this, [this, slider, axis](int value) {
        QToolTip::showText(slider->mapToGlobal(QPoint(slider->width() / 2, -2)),
                           sliceReadoutText(axis, value), slider);
    });
    connect(slider, &QSlider::sliderReleased, this, [] { QToolTip::hideText(); });
}

QString WorkflowWindow::sliceReadoutText(const QString& axis, int slice) const {
    return QStringLiteral("%1 %2 mm").arg(axis).arg(sliceCoordinateMm(axis, slice), 0, 'f', 1);
}

// The slice under a point on the groove, without moving the slider.
int WorkflowWindow::sliceAtSliderPosition(const QSlider* slider, QPoint position) const {
    QStyleOptionSlider option;
    option.initFrom(slider);
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    option.sliderValue = slider->value();
    option.singleStep = slider->singleStep();
    option.pageStep = slider->pageStep();
    option.orientation = slider->orientation();
    option.upsideDown = slider->orientation() == Qt::Horizontal
                            ? slider->invertedAppearance()
                            : !slider->invertedAppearance();
    option.subControls = QStyle::SC_All;

    const QRect groove =
        slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, slider);
    const QRect handle =
        slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
    const bool horizontal = slider->orientation() == Qt::Horizontal;
    const int span = horizontal ? groove.width() - handle.width() : groove.height() - handle.height();
    const int offset = horizontal ? position.x() - groove.x() - handle.width() / 2
                                  : position.y() - groove.y() - handle.height() / 2;
    if (span <= 0) return slider->value();
    return QStyle::sliderValueFromPosition(slider->minimum(), slider->maximum(), offset, span,
                                           option.upsideDown);
}

double WorkflowWindow::sliceCoordinateMm(const QString& axis, int index) const {
    const Eigen::VectorXd& vector = axis == QStringLiteral("LR")   ? mriAxes_.dimLR
                                    : axis == QStringLiteral("AP") ? mriAxes_.dimAP
                                                                   : mriAxes_.dimIS;
    if (vector.size() == 0) return 0.0;
    return vector(std::clamp<Eigen::Index>(index, 0, vector.size() - 1));
}

bool WorkflowWindow::eventFilter(QObject* watched, QEvent* event) {
    // What a stim parameter shipped as.
    if (stimParamView_ != nullptr && watched == stimParamView_->viewport() &&
        event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QModelIndex cell = stimParamView_->indexAt(help->pos());
        const QString text = shippedTooltip(cell);
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, stimParamView_);
        return true;
    }
    // The registration message is one line now, so a long one is elided.
    if (watched == ui_->registrationResult && event->type() == QEvent::ToolTip) {
        QLabel* const line = ui_->registrationResult;
        const int needed = line->fontMetrics().horizontalAdvance(line->text());
        if (needed > line->contentsRect().width())
            QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), line->text(), line);
        else
            QToolTip::hideText();
        return true;
    }
    if (mriLoaded_ && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave)) {
        if (auto* slider = qobject_cast<QSlider*>(watched)) {
            const QString axis = slider->property("sliceAxis").toString();
            if (!axis.isEmpty()) {
                if (event->type() == QEvent::Leave) {
                    QToolTip::hideText();
                } else {
                    const QPoint local = static_cast<QMouseEvent*>(event)->position().toPoint();
                    QToolTip::showText(slider->mapToGlobal(local),
                                       sliceReadoutText(axis, sliceAtSliderPosition(slider, local)),
                                       slider);
                }
            }
        }
    }
    // A Target List row is a widget laid over its item, so the list never sees clicks on it.
    if (event->type() == QEvent::MouseButtonPress && targetListWidget_) {
        if (auto* pressed = qobject_cast<QWidget*>(watched)) {
            for (int i = 0; i < targetListWidget_->count(); ++i) {
                QWidget* rowWidget = targetListWidget_->itemWidget(targetListWidget_->item(i));
                if (rowWidget && (rowWidget == pressed || rowWidget->isAncestorOf(pressed))) {
                    targetListWidget_->setCurrentRow(i);
                    break;
                }
            }
        }
    }
    if ((watched == ui_->simulatePassButton || watched == ui_->simulateFailButton ||
         watched == ui_->acceptDeviceReadinessButton) &&
        event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            static_cast<QPushButton*>(watched)->click();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// A stage change can ask for more height than the screen has for one layout pass -- entering
void WorkflowWindow::capWindowToScreen() {
    const QScreen* host = screen();
    if (host == nullptr) return;
    const int available = host->availableGeometry().height();
    if (available > 0 && maximumHeight() != available) setMaximumHeight(available);
}

void WorkflowWindow::syncMriViewerHeights() {
    capWindowToScreen();
    // The nine viewer columns are vertically Fixed, so they never take a share of their page's
    if (!ui_) return;
    // Recompute only when the window itself changes size, never on a stage change.
    const QSize windowSize = size();
    if (windowSize == lastMriSyncWindowSize_ && mriHeightSyncPasses_ == 0) return;
    // Only an operator resize relearns the affordance.
    if (std::abs(windowSize.height() - lastMriSyncWindowSize_.height()) > 80)
        maxStackHeight_ = 0;
    lastMriSyncWindowSize_ = windowSize;
    // Normalise the banner out of the basis, so the height is the one that fits the stages that
    int stackHeight = ui_->pageStack->height();
    if (ui_->workflowMessage && !ui_->workflowMessage->isVisible())
        stackHeight -= ui_->workflowMessage->sizeHint().height();
    if (stackHeight <= 0) return;
    // A page whose own minimum exceeds the stack pushes the stack past the window rather than
    if (maxStackHeight_ == 0 && stackHeight * 2 > QWidget::height())
        maxStackHeight_ = stackHeight;
    if (maxStackHeight_ > 0) stackHeight = std::min(stackHeight, maxStackHeight_);
    // What each page needs for everything except its viewer row.
    const auto reservedFor = [](const QWidget* page, const QLayout* viewerRow) {
        return page->layout()->sizeHint().height() - viewerRow->sizeHint().height();
    };
    const int reserved = std::max(reservedFor(ui_->imagingPage, ui_->mriPreviewLayout),
                                  reservedFor(ui_->registrationPage, ui_->registrationImages));
    // A column is taller than its viewer by the slider row, slice label, margins and group-box
    const int columnChrome = ui_->sagittalGroup->sizeHint().height() - ui_->sagittalPreview->minimumHeight();
    // Past its own width a viewer only adds letterboxing, so that is the cap.
    const int widthCap = ui_->sagittalPreview->width() > kMinMriViewerHeight ? ui_->sagittalPreview->width()
                                                                            : kMaxMriViewerHeight;
    const int height = std::clamp(stackHeight - reserved - columnChrome, kMinMriViewerHeight,
                                  std::min(widthCap, kMaxMriViewerHeight));
    // Treatment plan is the one stage that cannot have this height: the whole Sonicate body
    const int treatmentFits =
        stackHeight - kTreatmentBodyMinHeight - kTreatmentControlsHeight - columnChrome;
    // No fraction here: treatmentFits is the measured leftover, so the viewers can take all of
    const int treatmentHeight =
        std::clamp(std::min({height, kTreatmentViewerHeight, treatmentFits}),
                   kMinTreatmentViewerHeight, kTreatmentViewerHeight);
    if (height == ui_->sagittalPreview->minimumHeight() &&
        height == ui_->sagittalPreview->maximumHeight() &&
        treatmentHeight == treatmentSagittalPreview_->minimumHeight() &&
        treatmentHeight == treatmentSagittalPreview_->maximumHeight())
        return;
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                  ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview}) {
        view->setMinimumHeight(height);
        view->setMaximumHeight(height);
    }
    for (WorkflowMriView* view : {treatmentSagittalPreview_, treatmentCoronalPreview_, treatmentAxialPreview_}) {
        if (!view) continue;
        view->setMinimumHeight(treatmentHeight);
        view->setMaximumHeight(treatmentHeight);
    }
    // The hints above are read before the new geometry has settled, so the first pass after a
    if (++mriHeightSyncPasses_ < 4)
        QTimer::singleShot(0, this, [this] { syncMriViewerHeights(); });
}

// PlaySound is reading shamAudioBuffer_ in another thread while a mask plays, so the buffer
WorkflowWindow::~WorkflowWindow() {
    stopShamAudio();
    delete ui_;
}

std::vector<std::pair<QSlider*, QString>> WorkflowWindow::sliceSliders() const {
    const QString lr = QStringLiteral("LR");
    const QString ap = QStringLiteral("AP");
    const QString is = QStringLiteral("IS");
    return {{ui_->sagittalSlider, lr},
            {ui_->coronalSlider, ap},
            {ui_->axialSlider, is},
            {ui_->registrationSagittalSlider, lr},
            {ui_->registrationCoronalSlider, ap},
            {ui_->registrationAxialSlider, is},
            {treatmentSagittalSlider_, lr},
            {treatmentCoronalSlider_, ap},
            {treatmentAxialSlider_, is}};
}

void WorkflowWindow::showMriPreviews() {
    QStringList markerNames;
    for (std::size_t index = 0; index < 6; ++index) {
        if (index < fiducials_.size() && !fiducials_[index].name.empty())
            markerNames.push_back(QStringLiteral("%1. %2").arg(index + 1)
                                  .arg(QString::fromStdString(fiducials_[index].name)));
        else
            markerNames.push_back(QStringLiteral("Marker %1").arg(index + 1));
    }
    for (WorkflowMriView* view : {ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview})
        view->setCoordinatePasteOptions(markerNames);
    std::array<QString, 6> coordinateLabels;
    std::array<QPointF, 6> markerApIsMm;
    std::array<double, 6> markerLrMm;
    if (fiducialDiagramData(&coordinateLabels, &markerApIsMm, &markerLrMm)) {
        ui_->imagingFiducialLayout->setMarkerCoordinateLabels(coordinateLabels);
        ui_->registrationFiducialLayout->setMarkerCoordinateLabels(coordinateLabels);
        ui_->imagingFiducialLayout->setMarkerPositions(markerApIsMm);
        ui_->registrationFiducialLayout->setMarkerPositions(markerApIsMm);
        ui_->imagingFiducialLayout->setMarkerDepthsMm(markerLrMm);
        ui_->registrationFiducialLayout->setMarkerDepthsMm(markerLrMm);
    }
    const int sagittal = ui_->sagittalSlider->value() + 1;
    const int coronal = ui_->coronalSlider->value() + 1;
    const int axial = ui_->axialSlider->value() + 1;
    // BeamV0/drawMrImages.m applies XDir="reverse" to the sagittal axes.
    ui_->sagittalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"), true);
    ui_->coronalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    ui_->axialPreview->setSlice(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    ui_->registrationSagittalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"), true);
    ui_->registrationCoronalPreview->setSlice(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    ui_->registrationAxialPreview->setSlice(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    treatmentSagittalPreview_->setSlice(beam::mri::getSliceImage(mriVolume_, sagittal, "sagital"), true);
    treatmentCoronalPreview_->setSlice(beam::mri::getSliceImage(mriVolume_, coronal, "coronal"));
    treatmentAxialPreview_->setSlice(beam::mri::getSliceImage(mriVolume_, axial, "axial"));
    if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked()) {
        treatmentSagittalPreview_->setMaskOverlay(beam::mri::getSliceImage(focusImage_, sagittal, "sagital"), QColor(255, 128, 128), 0.75, true);
        treatmentCoronalPreview_->setMaskOverlay(beam::mri::getSliceImage(focusImage_, coronal, "coronal"), QColor(255, 128, 128), 0.75);
        treatmentAxialPreview_->setMaskOverlay(beam::mri::getSliceImage(focusImage_, axial, "axial"), QColor(255, 128, 128), 0.75);
    } else {
        treatmentSagittalPreview_->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentCoronalPreview_->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentAxialPreview_->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }
    const double treatmentOpacity = static_cast<double>(ui_->transducerTransparencySlider->value()) / 100.0;
    if (registrationGeometryLoaded_ && ui_->showTransducersCheckBox->isChecked()) {
        treatmentSagittalPreview_->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), treatmentOpacity, true);
        treatmentCoronalPreview_->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), treatmentOpacity);
        treatmentAxialPreview_->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), treatmentOpacity);
    } else {
        treatmentSagittalPreview_->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentCoronalPreview_->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        treatmentAxialPreview_->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }
    // "Show target" draws a crosshair through the target in every plane.
    const bool showTarget = ui_->showTargetCheckBox->isChecked();
    const bool showTargetOnRegistration = ui_->registrationShowTargetCheckBox->isChecked();
    const QPointF targetSagittal(1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()),
                                 1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z()));
    const QPointF targetCoronal(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z()));
    const QPointF targetAxial(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                              1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()));
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->registrationSagittalPreview,
                                  treatmentSagittalPreview_})
        view->setNavigationCrosshair(targetSagittal);
    for (WorkflowMriView* view : {ui_->coronalPreview, ui_->registrationCoronalPreview,
                                  treatmentCoronalPreview_})
        view->setNavigationCrosshair(targetCoronal);
    for (WorkflowMriView* view : {ui_->axialPreview, ui_->registrationAxialPreview,
                                  treatmentAxialPreview_})
        view->setNavigationCrosshair(targetAxial);
    // Imaging only once a registration has been computed.
    const bool showTargetOnImaging = showTarget && registrationFitApplied();
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview})
        view->setNavigationCrosshairVisible(showTargetOnImaging);
    // Treatment always: by then the target is the registered array centre and marking it is the
    for (WorkflowMriView* view : {treatmentSagittalPreview_, treatmentCoronalPreview_,
                                  treatmentAxialPreview_})
        view->setNavigationCrosshairVisible(showTarget);
    // Registration has its own Show Target, off by default: the operator is reading fiducial
    for (WorkflowMriView* view : {ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview})
        view->setNavigationCrosshairVisible(showTargetOnRegistration);

    // Imaging is the unregistered source image review.
    if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked()) {
        ui_->sagittalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, sagittal, "sagital"), QColor(255, 128, 128), 0.75, true);
        ui_->coronalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, coronal, "coronal"), QColor(255, 128, 128), 0.75);
        ui_->axialPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, axial, "axial"), QColor(255, 128, 128), 0.75);
    } else {
        ui_->sagittalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->coronalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->axialPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }
    ui_->sagittalPreview->setMarkers({});
    ui_->coronalPreview->setMarkers({});
    ui_->axialPreview->setMarkers({});

    const double transducerOpacity = static_cast<double>(ui_->transducerTransparencySlider->value()) / 100.0;
    if (registrationGeometryLoaded_ && ui_->showTransducersCheckBox->isChecked()) {
        ui_->sagittalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), transducerOpacity, true);
        ui_->coronalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), transducerOpacity);
        ui_->axialPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), transducerOpacity);
    } else {
        ui_->sagittalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->coronalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->axialPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
    }

    if (registrationGeometryLoaded_) {
        if (focusImageLoaded_ && ui_->showFieldCheckBox->isChecked()) {
            ui_->registrationSagittalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, sagittal, "sagital"), QColor(255, 128, 128), 0.75, true);
            ui_->registrationCoronalPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, coronal, "coronal"), QColor(255, 128, 128), 0.75);
            ui_->registrationAxialPreview->setMaskOverlay(beam::mri::getSliceImage(focusImage_, axial, "axial"), QColor(255, 128, 128), 0.75);
        } else {
            ui_->registrationSagittalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationCoronalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationAxialPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        }
        if (ui_->showTransducersCheckBox->isChecked()) {
            ui_->registrationSagittalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), transducerOpacity, true);
            ui_->registrationCoronalPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), transducerOpacity);
            ui_->registrationAxialPreview->setSecondaryMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), transducerOpacity);
        } else {
            ui_->registrationSagittalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationCoronalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
            ui_->registrationAxialPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        }

        // The marker diagram's selected blue, so one marker reads as current in both the
        const auto fiducialColor = [](bool selected) {
            return selected ? QColor(92, 205, 229) : QColor(230, 45, 55);
        };
        const int selectedFiducialRow = ui_->registrationTable->currentRow();
        std::vector<WorkflowMriMarker> sagMarkers, corMarkers, axialMarkers;
        std::vector<WorkflowMriMarker> imagingSagMarkers, imagingCorMarkers, imagingAxialMarkers;
        for (std::size_t markerIndex = 0; markerIndex < fiducials_.size(); ++markerIndex) {
            const auto& marker = fiducials_[markerIndex];
            const QString markerLabel = QStringLiteral("%1. %2").arg(markerIndex + 1)
                .arg(QString::fromStdString(marker.name));
            const bool draggable = selectedStage_ == beam::gui::WorkflowStage::Registration;
            const Eigen::Vector3d mm = marker.position * 1000.0;
            const auto voxel = beam::gui::imagePositionToVoxelIndex(mm, mriAxes_);
            const double markerConfidence = fiducialConfidence_[markerIndex];
            const QString confidenceText =
                fiducialTrustOf(markerConfidence) == FiducialTrust::Unscored
                    ? QStringLiteral("not scored")
                    : QStringLiteral("%1%").arg(markerConfidence * 100.0, 0, 'f', 0);
            const QString markerTooltip =
                QStringLiteral("%1\nLR %2  AP %3  IS %4 mm\nconfidence %5")
                    .arg(QString::fromStdString(marker.name))
                    .arg(mm.x(), 0, 'f', 1)
                    .arg(mm.y(), 0, 'f', 1)
                    .arg(mm.z(), 0, 'f', 1)
                    .arg(confidenceText);
            if (voxel.i == sagittal - 1) {
                const WorkflowMriMarker viewMarker{QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y()),
                                                            1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   markerLabel,
                                                   fiducialColor(static_cast<int>(markerIndex) ==
                                                                 selectedFiducialRow),
                                                   false, draggable,
                                                   static_cast<int>(markerIndex),
                                                   markerTooltip};
                if (ui_->registrationShowFiducialsCheckBox->isChecked()) sagMarkers.push_back(viewMarker);
                if (ui_->showFiducialsCheckBox->isChecked()) imagingSagMarkers.push_back(viewMarker);
            }
            if (voxel.j == coronal - 1) {
                const WorkflowMriMarker viewMarker{QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                            1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   markerLabel,
                                                   fiducialColor(static_cast<int>(markerIndex) ==
                                                                 selectedFiducialRow),
                                                   false, draggable,
                                                   static_cast<int>(markerIndex),
                                                   markerTooltip};
                if (ui_->registrationShowFiducialsCheckBox->isChecked()) corMarkers.push_back(viewMarker);
                if (ui_->showFiducialsCheckBox->isChecked()) imagingCorMarkers.push_back(viewMarker);
            }
            if (voxel.k == axial - 1) {
                const WorkflowMriMarker viewMarker{QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                            1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y())),
                                                   markerLabel,
                                                   fiducialColor(static_cast<int>(markerIndex) ==
                                                                 selectedFiducialRow),
                                                   false, draggable,
                                                   static_cast<int>(markerIndex),
                                                   markerTooltip};
                if (ui_->registrationShowFiducialsCheckBox->isChecked()) axialMarkers.push_back(viewMarker);
                if (ui_->showFiducialsCheckBox->isChecked()) imagingAxialMarkers.push_back(viewMarker);
            }
        }
        std::vector<WorkflowMriMarker> treatmentSagMarkers, treatmentCorMarkers, treatmentAxialMarkers;
        // No green ring on the target: the crosshair marks it in every plane and is readable
        if (ui_->showFiducialsCheckBox->isChecked()) {
            for (std::size_t markerIndex = 0; markerIndex < fiducials_.size(); ++markerIndex) {
                const Eigen::Vector3d mm = fiducials_[markerIndex].position * 1000.0;
                const auto markerVoxel = beam::gui::imagePositionToVoxelIndex(mm, mriAxes_);
                const QString label = QStringLiteral("%1. %2").arg(markerIndex + 1)
                    .arg(QString::fromStdString(fiducials_[markerIndex].name));
                // The same hover text the Registration views carry: a marker that names itself
                const double markerConfidence = fiducialConfidence_[markerIndex];
                const QString markerTooltip =
                    QStringLiteral("%1\nLR %2  AP %3  IS %4 mm\nconfidence %5")
                        .arg(QString::fromStdString(fiducials_[markerIndex].name))
                        .arg(mm.x(), 0, 'f', 1)
                        .arg(mm.y(), 0, 'f', 1)
                        .arg(mm.z(), 0, 'f', 1)
                        .arg(fiducialTrustOf(markerConfidence) == FiducialTrust::Unscored
                                 ? QStringLiteral("not scored")
                                 : QStringLiteral("%1%").arg(markerConfidence * 100.0, 0, 'f', 0));
                if (markerVoxel.i == sagittal - 1)
                    treatmentSagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y()),
                                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   label,
                                                   fiducialColor(static_cast<int>(markerIndex) ==
                                                                 treatmentFiducialPick_),
                                                   false, false, static_cast<int>(markerIndex),
                                                   markerTooltip});
                if (markerVoxel.j == coronal - 1)
                    treatmentCorMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                                   label,
                                                   fiducialColor(static_cast<int>(markerIndex) ==
                                                                 treatmentFiducialPick_),
                                                   false, false, static_cast<int>(markerIndex),
                                                   markerTooltip});
                if (markerVoxel.k == axial - 1)
                    treatmentAxialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                             1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y())),
                                                     label,
                                                     fiducialColor(static_cast<int>(markerIndex) ==
                                                                   treatmentFiducialPick_),
                                                     false, false, static_cast<int>(markerIndex),
                                                     markerTooltip});
            }
        }
        ui_->sagittalPreview->setMarkers(std::move(imagingSagMarkers));
        ui_->coronalPreview->setMarkers(std::move(imagingCorMarkers));
        ui_->axialPreview->setMarkers(std::move(imagingAxialMarkers));
        appendLockPositionOverlay(sagMarkers);
        ui_->registrationSagittalPreview->setMarkers(std::move(sagMarkers));
        ui_->registrationCoronalPreview->setMarkers(std::move(corMarkers));
        ui_->registrationAxialPreview->setMarkers(std::move(axialMarkers));
        treatmentSagittalPreview_->setMarkers(std::move(treatmentSagMarkers));
        treatmentCoronalPreview_->setMarkers(std::move(treatmentCorMarkers));
        treatmentAxialPreview_->setMarkers(std::move(treatmentAxialMarkers));
    } else {
        ui_->registrationSagittalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationCoronalPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationAxialPreview->setMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationSagittalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationCoronalPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationAxialPreview->setSecondaryMaskOverlay(Eigen::MatrixXd{}, QColor(), 0.0);
        ui_->registrationSagittalPreview->setMarkers({});
        ui_->registrationCoronalPreview->setMarkers({});
        ui_->registrationAxialPreview->setMarkers({});
        treatmentSagittalPreview_->setMarkers({});
        treatmentCoronalPreview_->setMarkers({});
        treatmentAxialPreview_->setMarkers({});
        // Cleared above and left cleared: the crosshair is the target mark.
    }

    const auto coordinate = [](const Eigen::VectorXd& axis, int oneBasedIndex) {
        const Eigen::Index i = std::clamp<Eigen::Index>(oneBasedIndex - 1, 0, axis.size() - 1);
        return axis(i);
    };
    const double lr = coordinate(mriAxes_.dimLR, sagittal);
    const double ap = coordinate(mriAxes_.dimAP, coronal);
    const double is = coordinate(mriAxes_.dimIS, axial);
    ui_->sagittalPreview->setRasMapping(QStringLiteral("sagittal"), lr,
                                        axisEdgesMm(mriAxes_.dimAP).first, axisEdgesMm(mriAxes_.dimAP).second,
                                        axisEdgesMm(mriAxes_.dimIS).first, axisEdgesMm(mriAxes_.dimIS).second, true, true);
    ui_->coronalPreview->setRasMapping(QStringLiteral("coronal"), ap,
                                       axisEdgesMm(mriAxes_.dimLR).first, axisEdgesMm(mriAxes_.dimLR).second,
                                       axisEdgesMm(mriAxes_.dimIS).first, axisEdgesMm(mriAxes_.dimIS).second, false, true);
    ui_->axialPreview->setRasMapping(QStringLiteral("axial"), is,
                                     axisEdgesMm(mriAxes_.dimLR).first, axisEdgesMm(mriAxes_.dimLR).second,
                                     axisEdgesMm(mriAxes_.dimAP).first, axisEdgesMm(mriAxes_.dimAP).second, false, true);
    ui_->registrationSagittalPreview->setRasMapping(QStringLiteral("sagittal"), lr, axisEdgesMm(mriAxes_.dimAP).first, axisEdgesMm(mriAxes_.dimAP).second, axisEdgesMm(mriAxes_.dimIS).first, axisEdgesMm(mriAxes_.dimIS).second, true, true);
    ui_->registrationCoronalPreview->setRasMapping(QStringLiteral("coronal"), ap, axisEdgesMm(mriAxes_.dimLR).first, axisEdgesMm(mriAxes_.dimLR).second, axisEdgesMm(mriAxes_.dimIS).first, axisEdgesMm(mriAxes_.dimIS).second, false, true);
    ui_->registrationAxialPreview->setRasMapping(QStringLiteral("axial"), is, axisEdgesMm(mriAxes_.dimLR).first, axisEdgesMm(mriAxes_.dimLR).second, axisEdgesMm(mriAxes_.dimAP).first, axisEdgesMm(mriAxes_.dimAP).second, false, true);
    treatmentSagittalPreview_->setRasMapping(QStringLiteral("sagittal"), lr, axisEdgesMm(mriAxes_.dimAP).first, axisEdgesMm(mriAxes_.dimAP).second, axisEdgesMm(mriAxes_.dimIS).first, axisEdgesMm(mriAxes_.dimIS).second, true, true);
    treatmentCoronalPreview_->setRasMapping(QStringLiteral("coronal"), ap, axisEdgesMm(mriAxes_.dimLR).first, axisEdgesMm(mriAxes_.dimLR).second, axisEdgesMm(mriAxes_.dimIS).first, axisEdgesMm(mriAxes_.dimIS).second, false, true);
    treatmentAxialPreview_->setRasMapping(QStringLiteral("axial"), is, axisEdgesMm(mriAxes_.dimLR).first, axisEdgesMm(mriAxes_.dimLR).second, axisEdgesMm(mriAxes_.dimAP).first, axisEdgesMm(mriAxes_.dimAP).second, false, true);
    const double lrCenter = coordinate(mriAxes_.dimLR, static_cast<int>((mriVolume_.nx - 1) / 2) + 1);
    const double apCenter = coordinate(mriAxes_.dimAP, static_cast<int>((mriVolume_.ny - 1) / 2) + 1);
    const double isCenter = coordinate(mriAxes_.dimIS, static_cast<int>((mriVolume_.nz - 1) / 2) + 1);
    ui_->sagittalSliceLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->coronalSliceLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->axialSliceLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    ui_->registrationSagittalLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->registrationCoronalLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->registrationAxialLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    ui_->treatmentSagittalSliceLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->treatmentCoronalSliceLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->treatmentAxialSliceLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    const auto sliceTip = [](const QString& axis, double mm, double centre) {
        return QStringLiteral("%1 %2 mm · %3 mm from initial center")
            .arg(axis)
            .arg(mm, 0, 'f', 1)
            .arg(mm - centre, 0, 'f', 1);
    };
    sliceSliderTips_[QStringLiteral("LR")] = sliceTip(QStringLiteral("LR"), lr, lrCenter);
    sliceSliderTips_[QStringLiteral("AP")] = sliceTip(QStringLiteral("AP"), ap, apCenter);
    sliceSliderTips_[QStringLiteral("IS")] = sliceTip(QStringLiteral("IS"), is, isCenter);
    for (const auto& [slider, axis] : sliceSliders())
        if (slider) slider->setToolTip(sliceSliderTips_.value(axis));
    refreshDetachedViewers();
}

// Go where the workflow says to go next, by a row the operator can see.
}  // namespace beam::app
