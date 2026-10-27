#include "workflow_internal.hpp"

namespace beam::app {

void WorkflowWindow::initializeRegistrationGeometry() {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        executableDir + QStringLiteral("/../../../../DefaultSubjectV0/defaultSubjectArrayRect.csv"),
        QDir::current().filePath(QStringLiteral("../DefaultSubjectV0/defaultSubjectArrayRect.csv"))};
    QString geometryPath;
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) { geometryPath = candidate; break; }
    }
    if (geometryPath.isEmpty()) throw std::runtime_error("Default transducer geometry was not found");

    beam::array::ArrayData nominal = beam::array::defineArrayData(readArrayRectCsv(geometryPath));
    beam::array::reconstructPhysicalArrayHalves(nominal);
    const Eigen::Vector3d rectCenter = nominal.arrayTotal.rect.block(16, 0, 3, nominal.arrayTotal.rect.cols()).rowwise().mean();
    // BeamV0/initTransducers.m: centerArray = mean(rect center) - [0,50,-25] mm.
    const Eigen::Vector3d placementReference = rectCenter - Eigen::Vector3d(0.0, 0.050, -0.025);
    const Eigen::Vector3d mriCenterMeters(mriAxes_.dimLR.mean() / 1000.0,
                                          mriAxes_.dimAP.mean() / 1000.0,
                                          mriAxes_.dimIS.mean() / 1000.0);
    Eigen::Matrix4d translation = Eigen::Matrix4d::Identity();
    translation.block<3, 1>(0, 3) = mriCenterMeters - placementReference;
    beam::registration::AffineArrayResult placed = beam::registration::applyAffineToArrayData(translation, nominal);
    arrayData_ = std::move(placed.arrayData);
    fiducials_ = std::move(placed.fiducialMarkers);
    registrationSourceFiducials_ = fiducials_;
    fiducialConfirmed_.fill(false);
    sourceFiducialConfirmed_.fill(false);
    fiducialLocated_.fill(false);
    sourceFiducialLocated_.fill(false);
    fiducialDetected_.fill(false);
    fiducialConfidence_.fill(0.0);
    fiducialStability_.fill(0.0);
    registrationOriginArrayData_ = arrayData_;
    setRegistrationPhase(RegistrationPhase::Locating);
    ui_->imagingFiducialLayout->setEnabled(!fiducials_.empty());
    ui_->imagingFiducialLayout->setSelectedIndex(-1);
    targetMm_ = arrayData_.arrayTotal.rect.block(16, 0, 3, arrayData_.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
    // MATLAB drawMrImages always calls drawFocusOnMRI after its initial transducer placement.
    rebuildFocusImage();
    arrayMask_ = beam::gui::rasterizeArrayOntoMriGrid(arrayData_.arrayTotal, mriAxes_, mriVolume_.nx,
                                                       mriVolume_.ny, mriVolume_.nz);
    registrationGeometryLoaded_ = true;
    populateRegistrationTable();
    ui_->resetRegistrationButton->setEnabled(true);
    ui_->registrationResult->setText(QStringLiteral("Ready: review all six MRI fiducial coordinates."));
}

void WorkflowWindow::populateRegistrationTable() {
    if (!registrationGeometryLoaded_ && fiducials_.empty()) return;
    ui_->registrationTable->blockSignals(true);
    for (int row = 0; row < static_cast<int>(registrationSourceFiducials_.size()) && row < 6; ++row) {
        const auto& marker = registrationSourceFiducials_[static_cast<std::size_t>(row)];
        auto* name = new QTableWidgetItem(QString::fromStdString(marker.name));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        ui_->registrationTable->setItem(row, 0, name);
        for (int axis = 0; axis < 3; ++axis) {
            auto* coordinate = new QTableWidgetItem(QString::number(marker.position(axis) * 1000.0, 'f', 1));
            coordinate->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            ui_->registrationTable->setItem(row, axis + 1, coordinate);
        }
        const std::size_t statusIndex = static_cast<std::size_t>(row);
        QString stateText;
        if (!fiducialLocated_[statusIndex]) stateText = QStringLiteral("Unidentified");
        else if (!fiducialConfirmed_[statusIndex])
            stateText = fiducialDetected_[statusIndex] ? QStringLiteral("Detected")
                                                       : QStringLiteral("Located");
        else if (sourceFiducialConfirmed_[statusIndex]) stateText = QStringLiteral("Imported");
        else stateText = QStringLiteral("Confirmed");
        auto* source = new QTableWidgetItem(stateText);
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);
        ui_->registrationTable->setItem(row, 4, source);
        auto* residual = new QTableWidgetItem(QStringLiteral("—"));
        residual->setFlags(residual->flags() & ~Qt::ItemIsEditable);
        residual->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui_->registrationTable->setItem(row, 5, residual);
        auto* confidence = new QTableWidgetItem(QStringLiteral("—"));
        confidence->setFlags(confidence->flags() & ~Qt::ItemIsEditable);
        confidence->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui_->registrationTable->setItem(row, 6, confidence);
    }
    ui_->registrationTable->blockSignals(false);
    applyFiducialConfidenceToTable();
    if (ui_->registrationTable->currentRow() < 0 && !registrationSourceFiducials_.empty())
        ui_->registrationTable->selectRow(0);
    const int selectedRow = ui_->registrationTable->currentRow();
    bool hasPendingLocated = false;
    for (std::size_t index = 0; index < fiducialLocated_.size(); ++index)
        hasPendingLocated = hasPendingLocated || (fiducialLocated_[index] && !fiducialConfirmed_[index]);
    updateRegistrationAvailability();
}

// Move To Target: centre all three treatment planes on a stored position.
void WorkflowWindow::focusTreatmentViewsOn(const Eigen::Vector3d& positionMm) {
    if (!mriLoaded_) return;
    const auto voxel = beam::gui::imagePositionToVoxelIndex(positionMm, mriAxes_);
    ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
    ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
    ui_->axialSlider->setValue(static_cast<int>(voxel.k));
    targetMm_ = positionMm;
    showMriPreviews();
    // And frame it, in every plane of every page: moving the sliders puts the target on the
    centreViewsOnTarget();
}

// Puts the target in the middle of all nine planes.
void WorkflowWindow::centreViewsOnTarget() {
    if (!mriLoaded_ || targetMm_.isZero()) return;
    const QPointF sagittal(1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()),
                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z()));
    const QPointF coronal(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                          1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z()));
    const QPointF axial(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                        1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()));
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->registrationSagittalPreview,
                                  treatmentSagittalPreview_})
        view->centreOn(sagittal, kTargetViewZoom);
    for (WorkflowMriView* view : {ui_->coronalPreview, ui_->registrationCoronalPreview,
                                  treatmentCoronalPreview_})
        view->centreOn(coronal, kTargetViewZoom);
    for (WorkflowMriView* view : {ui_->axialPreview, ui_->registrationAxialPreview,
                                  treatmentAxialPreview_})
        view->centreOn(axial, kTargetViewZoom);
}

bool WorkflowWindow::fiducialDiagramData(std::array<QString, 6>* labels,
                                         std::array<QPointF, 6>* apIsMm,
                                         std::array<double, 6>* lrMm) const {
    if (fiducials_.size() < 6) return false;
    for (std::size_t index = 0; index < 6; ++index) {
        const Eigen::Vector3d rasMm = fiducials_[index].position * 1000.0;
        if (labels != nullptr)
            (*labels)[index] = QStringLiteral("(%1, %2, %3)")
                                   .arg(rasMm.x(), 0, 'f', 1)
                                   .arg(rasMm.y(), 0, 'f', 1)
                                   .arg(rasMm.z(), 0, 'f', 1);
        // The diagram is a view down the LR axis, so it plots AP against IS.
        if (apIsMm != nullptr) (*apIsMm)[index] = QPointF(rasMm.y(), rasMm.z());
        if (lrMm != nullptr) (*lrMm)[index] = rasMm.x();
    }
    return true;
}

void WorkflowWindow::navigateToFiducialOnTreatment(int index, bool announce) {
    if (!mriLoaded_ || index < 0 || index >= static_cast<int>(fiducials_.size())) return;
    const Eigen::Vector3d markerMm = fiducials_[static_cast<std::size_t>(index)].position * 1000.0;
    const auto voxel = beam::gui::imagePositionToVoxelIndex(markerMm, mriAxes_);
    ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
    ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
    ui_->axialSlider->setValue(static_cast<int>(voxel.k));
    showMriPreviews();
    treatmentSagittalPreview_->revealAt(
        QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, markerMm.y()),
                1.0 - normalizedAxisPosition(mriAxes_.dimIS, markerMm.z())));
    treatmentCoronalPreview_->revealAt(
        QPointF(normalizedAxisPosition(mriAxes_.dimLR, markerMm.x()),
                1.0 - normalizedAxisPosition(mriAxes_.dimIS, markerMm.z())));
    treatmentAxialPreview_->revealAt(
        QPointF(normalizedAxisPosition(mriAxes_.dimLR, markerMm.x()),
                1.0 - normalizedAxisPosition(mriAxes_.dimAP, markerMm.y())));
    if (announce)
        showMessage(QStringLiteral("Showing fiducial %1. The planned target is unchanged.")
                        .arg(QString::fromStdString(
                            fiducials_[static_cast<std::size_t>(index)].name)),
                    false);
}

// The treatment viewer's popup.
QWidget* WorkflowWindow::buildFiducialPickerWidget(QMenu* menu) {
    auto* holder = new QWidget(menu);
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(4);

    auto* caption = new QLabel(QStringLiteral("Move the array lock, then Accept."), holder);
    caption->setStyleSheet(QStringLiteral(
        "QLabel { background: #d8f1f6; color: #123d4b; border: 1px solid #63b7c9; "
        "border-radius: 3px; padding: 3px 4px; font-weight: 600; }"));
    layout->addWidget(caption);

    // Same control as the Registration page's, borrowing its stylesheet rather than repeating
    const auto makeLockSlider = [this, holder](Qt::Orientation orientation, int value) {
        auto* slider = new NumberedSlider(orientation, holder);
        slider->setStyleSheet(QString::fromLatin1(kSliderStyle));
        slider->setRange(1, 4);
        slider->setSingleStep(1);
        slider->setPageStep(1);
        slider->setValue(value);
        slider->setStyleSheet(leftHorizontalPositionSlider_->styleSheet());
        slider->setStyle(new ClickToPositionStyle(slider->style()));
        return slider;
    };
    auto* vertical = makeLockSlider(Qt::Vertical, leftVerticalPositionSlider_->value());
    auto* horizontal = makeLockSlider(Qt::Horizontal, leftHorizontalPositionSlider_->value());
    vertical->setFixedSize(50, 88);
    horizontal->setMinimumSize(120, 48);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(7);
    grid->setVerticalSpacing(2);
    grid->addWidget(new QLabel(QStringLiteral("Vertical"), holder), 0, 0, Qt::AlignCenter);
    grid->addWidget(vertical, 1, 0, Qt::AlignCenter);
    grid->addWidget(new QLabel(QStringLiteral("Horizontal"), holder), 0, 1, Qt::AlignCenter);
    grid->addWidget(horizontal, 1, 1, Qt::AlignCenter);
    layout->addLayout(grid);

    auto* accept = new QPushButton(QStringLiteral("Accept new array position"), holder);
    accept->setMinimumHeight(30);
    accept->setStyleSheet(registerCurrentPositionButton_->styleSheet());
    accept->setEnabled(registrationFitApplied());
    if (!accept->isEnabled())
        accept->setToolTip(QStringLiteral("Fit the array to the fiducials on Registration first."));
    connect(accept, &QPushButton::clicked, this, [this, horizontal, vertical, menu] {
        // Both locks together: performCurrentPositionRegistration refuses a left/right
        for (QSlider* slider : {leftHorizontalPositionSlider_, rightHorizontalPositionSlider_})
            slider->setValue(horizontal->value());
        for (QSlider* slider : {leftVerticalPositionSlider_, rightVerticalPositionSlider_})
            slider->setValue(vertical->value());
        performCurrentPositionRegistration();
        menu->close();
    });
    layout->addWidget(accept);
    return holder;
}

void WorkflowWindow::navigateToRegistrationFiducial(int row) {
    // Table population selects its first row.
    if (selectedStage_ != beam::gui::WorkflowStage::Registration || !mriLoaded_ ||
        row < 0 || row >= static_cast<int>(fiducials_.size())) return;
    const Eigen::Vector3d markerMm = fiducials_[static_cast<std::size_t>(row)].position * 1000.0;
    const auto voxel = beam::gui::imagePositionToVoxelIndex(markerMm, mriAxes_);
    ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
    ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
    ui_->axialSlider->setValue(static_cast<int>(voxel.k));
    showMriPreviews();
    ui_->registrationSagittalPreview->revealAt(
        QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, markerMm.y()),
                1.0 - normalizedAxisPosition(mriAxes_.dimIS, markerMm.z())));
    ui_->registrationCoronalPreview->revealAt(
        QPointF(normalizedAxisPosition(mriAxes_.dimLR, markerMm.x()),
                1.0 - normalizedAxisPosition(mriAxes_.dimIS, markerMm.z())));
    ui_->registrationAxialPreview->revealAt(
        QPointF(normalizedAxisPosition(mriAxes_.dimLR, markerMm.x()),
                1.0 - normalizedAxisPosition(mriAxes_.dimAP, markerMm.y())));
    showMessage(QStringLiteral("Showing %1: center its red marker on the corresponding MRI donut.")
                    .arg(QString::fromStdString(fiducials_[static_cast<std::size_t>(row)].name)), false);
}

void WorkflowWindow::updateRegistrationAvailability() {
    const bool allLocated = std::all_of(fiducialLocated_.begin(), fiducialLocated_.end(), [](bool v) { return v; });
    const bool hasPendingLocated = std::any_of(fiducialLocated_.begin(), fiducialLocated_.end(),
                                               [this, index = std::size_t{0}](bool located) mutable {
                                                   const bool pending = located && !fiducialConfirmed_[index];
                                                   ++index;
                                                   return pending;
                                               });
    const bool allMeasured = std::all_of(fiducialConfirmed_.begin(), fiducialConfirmed_.end(), [](bool v) { return v; });
    // A loaded Beam session can be marked InProgress after import even though its MRI and
    const bool geometryReady = registrationGeometryLoaded_ && mriLoaded_;
    const bool fitApplied = registrationFitApplied();
    const bool calibrationReady = geometryReady && fitApplied;

    // This is the single source of truth for Registration interaction state.
    ui_->confirmFiducialButton->setEnabled(allLocated && hasPendingLocated);
    ui_->registerFiducialsButton->setEnabled(geometryReady && allMeasured && !fitApplied);
    // Deliberately not gated: detectFiducials() reports what is missing, which is more use to
    if (registerCurrentPositionButton_)
        // Step 3 is intentionally repeatable.
        registerCurrentPositionButton_->setEnabled(calibrationReady);
    ui_->acceptRegistrationButton->setEnabled(registrationPhase_ == RegistrationPhase::LockPositionRegistered);
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_}) {
        // Do not write the enabled state back on every slider event.
        if (slider && slider->isEnabled() != calibrationReady)
            slider->setEnabled(calibrationReady);
    }
    updateRegistrationStepIndicators();
}

void WorkflowWindow::updateRegistrationStepIndicators() {}

// Sixteen registrations is most of a second, so this runs when the fit changes or the overlay
void WorkflowWindow::recomputeLockOverlayTargets() {
    lockOverlayTargets_.clear();
    if (!registrationGeometryLoaded_ || fiducials_.size() != 6) return;
    std::vector<Eigen::Vector3d> measured;
    measured.reserve(fiducials_.size());
    for (const auto& marker : fiducials_) measured.push_back(marker.position * 1000.0);
    try {
        lockOverlayTargets_ = beam::registration::targetsForEveryLockPosition(
            registrationOriginArrayData_, measured);
    } catch (const std::exception& error) {
        lockOverlayShown_ = false;
        showMessage(QStringLiteral("The lock positions could not be worked out: %1")
                        .arg(QString::fromUtf8(error.what())),
                    true);
    }
}

void WorkflowWindow::setLockOverlayShown(bool shown) {
    lockOverlayShown_ = shown;
    if (shown && lockOverlayTargets_.empty()) recomputeLockOverlayTargets();
    if (overlayAllTargetsButton_ != nullptr) {
        const QSignalBlocker blocker(overlayAllTargetsButton_);
        overlayAllTargetsButton_->setChecked(lockOverlayShown_);
        overlayAllTargetsButton_->setText(lockOverlayShown_
                                              ? QStringLiteral("Hide all targets")
                                              : QStringLiteral("Overlay all targets"));
    }
    // So the menu entry reads "Hide" once it is on, in the pane and in any detached viewer
    for (WorkflowMriView* view : {ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview})
        if (view != nullptr) view->setLockOverlayShown(lockOverlayShown_);
    if (lockOverlayShown_ && lockOverlayTargets_.empty())
        showMessage(QStringLiteral("Register to the MRI fiducials first: the lock positions are "
                                   "worked out from that fit."),
                    true);
    showMriPreviews();
}

// Every notch the hardware offers, drawn at once so the choice can be seen rather than tried
void WorkflowWindow::appendLockPositionOverlay(std::vector<WorkflowMriMarker>& sagittal) {
    if (!lockOverlayShown_ || !mriLoaded_ || lockOverlayTargets_.empty()) return;
    const int currentHorizontal =
        leftHorizontalPositionSlider_ == nullptr ? 0 : leftHorizontalPositionSlider_->value();
    const int currentVertical =
        leftVerticalPositionSlider_ == nullptr ? 0 : leftVerticalPositionSlider_->value();
    for (const beam::registration::LockPositionTarget& target : lockOverlayTargets_) {
        const bool current =
            target.horizontal == currentHorizontal && target.vertical == currentVertical;
        const Eigen::Vector3d& mm = target.centreMm;
        const QString tooltip =
            QStringLiteral("H%1 V%2\nLR %3  AP %4  IS %5 mm%6")
                .arg(target.horizontal)
                .arg(target.vertical)
                .arg(mm.x(), 0, 'f', 1)
                .arg(mm.y(), 0, 'f', 1)
                .arg(mm.z(), 0, 'f', 1)
                .arg(current ? QStringLiteral("\nThe lock position now set") : QString()) +
            QStringLiteral("\nSagittal only: the notches move in AP and IS.");
        // The registered-target yellow, with the one in use picked out.
        const QColor ink = current ? QColor(0x5c, 0xcd, 0xe5) : QColor(0xff, 0xcc, 0x33);
        sagittal.push_back(WorkflowMriMarker{
            QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y()),
                    1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
            // No drawn label: sixteen boxes overlap into one band.
            QString(), ink, false, false, -1, tooltip, 3.0});
    }
}

void WorkflowWindow::applyRegistrationResult(beam::registration::AffineArrayResult result) {
    // A new fit moves every notch.
    if (lockOverlayShown_) {
        recomputeLockOverlayTargets();
    } else {
        lockOverlayTargets_.clear();
    }
    arrayData_ = std::move(result.arrayData);
    // Keep the red MRI fiducials at the operator-measured coordinates.
    targetMm_ = arrayData_.arrayTotal.rect.block(16, 0, 3, arrayData_.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
    arrayMask_ = beam::gui::rasterizeArrayOntoMriGrid(arrayData_.arrayTotal, mriAxes_, mriVolume_.nx,
                                                       mriVolume_.ny, mriVolume_.nz);
    if (focusImageLoaded_) rebuildFocusImage();
    showMriPreviews();
}

void WorkflowWindow::performFiducialRegistration() {
    if (!mriLoaded_ || !registrationGeometryLoaded_) {
        showMessage(QStringLiteral("Load the patient MRI and initialize the Beam geometry before registration."), true);
        return;
    }
    // Both registration steps read the same stored fiducials, as BeamV0 does
    std::vector<Eigen::Vector3d> measured;
    measured.reserve(6);
    for (const auto& marker : fiducials_) {
        const Eigen::Vector3d point = marker.position * 1000.0;
        if (!point.allFinite()) {
            showMessage(QStringLiteral("A fiducial contains an invalid coordinate."), true);
            return;
        }
        measured.push_back(point);
    }
    try {
        auto result = beam::registration::registerArrayToFiducials(registrationOriginArrayData_, measured);
        applyRegistrationResult(std::move(result));
        setRegistrationPhase(RegistrationPhase::FitApplied);
        updateRegistrationStepIndicators();
        // Step 4 remains locked until the Step 3 lock-position registration has been completed.
        ui_->registrationResult->setText(QStringLiteral(
            "Registration applied. Verify all overlays against the MRI before accepting."));
        // Registration can be initiated immediately after returning to the initial slices.
        int reviewRow = ui_->registrationTable->currentRow();
        if (reviewRow < 0 || reviewRow >= static_cast<int>(fiducials_.size())) reviewRow = 0;
        ui_->registrationTable->setCurrentCell(reviewRow, 0);
        navigateToRegistrationFiducial(reviewRow);
        showMessage(QStringLiteral(
            "Registration applied. Verify all six fiducials and overlays against the MRI before accepting."), false);
        refresh();
    } catch (const std::exception& error) {
        ui_->registrationResult->setText(QStringLiteral("Registration failed: %1").arg(QString::fromUtf8(error.what())));
        showMessage(QStringLiteral("Registration failed. Verify that all six points are distinct and correctly paired."), true);
    }
}

void WorkflowWindow::acceptFiducialRegistration() {
    if (registrationPhase_ != RegistrationPhase::LockPositionRegistered) {
        const QString message = QStringLiteral("Complete the Step 2 transducer lock-position registration before accepting.");
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    std::string reason;
    if (!workflow_.complete(beam::gui::WorkflowStage::Registration, &reason)) {
        const QString message = QString::fromStdString(reason);
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    setRegistrationPhase(RegistrationPhase::Accepted);
    updateRegistrationStepIndicators();
    ui_->registrationResult->setText(ui_->registrationResult->text() + QStringLiteral("; accepted"));
    showMessage(QStringLiteral("Registration accepted. Continuing to the next workflow stage."), false);
    refresh();
    // Correction is an internal half of the combined Coupling/Correction gate and is
    goToNextStage();
}

void WorkflowWindow::performCurrentPositionRegistration() {
    if (!registrationFitApplied() || fiducials_.size() != 6) {
        const QString message = QStringLiteral("Run Register to MRI fiducials before calibrating the current array position.");
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    if (leftHorizontalPositionSlider_->value() != rightHorizontalPositionSlider_->value() ||
        leftVerticalPositionSlider_->value() != rightVerticalPositionSlider_->value()) {
        // Naming the four notches matters: the panels sit side by side and a one-notch
        const QString message =
            QStringLiteral("Step 2 needs both panels on the same notches. Subject Left is at "
                           "horizontal %1, vertical %2; Subject Right is at horizontal %3, "
                           "vertical %4.")
                .arg(leftHorizontalPositionSlider_->value())
                .arg(leftVerticalPositionSlider_->value())
                .arg(rightHorizontalPositionSlider_->value())
                .arg(rightVerticalPositionSlider_->value());
        ui_->registrationResult->setText(message);
        showMessage(message, true);
        return;
    }
    std::vector<Eigen::Vector3d> measured;
    measured.reserve(6);
    for (const auto& marker : fiducials_) measured.push_back(marker.position * 1000.0);
    try {
        auto result = beam::registration::registerCurrentTransducerPosition(
            registrationOriginArrayData_, measured,
            leftHorizontalPositionSlider_->value(), leftVerticalPositionSlider_->value());
        applyRegistrationResult(std::move(result));
        // registerCurrentTransducerPostion.m ends by stamping the registered array centre into
        applyArrayCentreToAllTargets(targetMm_);
        // And show it: the planes move onto the registered centre, so the crosshair and the
        focusTreatmentViewsOn(targetMm_);
        // Registering to the lock position is the point at which the target becomes worth
        ui_->registrationShowTargetCheckBox->setChecked(true);
        setRegistrationPhase(RegistrationPhase::LockPositionRegistered);
        updateRegistrationStepIndicators();
        ui_->acceptRegistrationButton->setFocus(Qt::OtherFocusReason);
        ui_->registrationResult->setText(QStringLiteral("Array registered to the current lock position. Step 2 complete; review and accept Registration."));
        showMessage(QStringLiteral("Array registered to current lock position."), false);
        refresh();
    } catch (const std::exception& error) {
        const QString message = QStringLiteral("Current-position registration failed: %1")
                                    .arg(QString::fromUtf8(error.what()));
        ui_->registrationResult->setText(message);
        showMessage(message, true);
    }
}

// Every slice slider, paired with the axis it moves along.
}  // namespace beam::app
