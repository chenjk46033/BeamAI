#include "workflow_internal.hpp"

namespace beam::app {

bool WorkflowWindow::registrationFitApplied() const {
    return registrationPhase_ == RegistrationPhase::FitApplied ||
           registrationPhase_ == RegistrationPhase::LockPositionRegistered ||
           registrationPhase_ == RegistrationPhase::Accepted;
}

bool WorkflowWindow::registrationLockPositionRegistered() const {
    return registrationPhase_ == RegistrationPhase::LockPositionRegistered ||
           registrationPhase_ == RegistrationPhase::Accepted;
}

void WorkflowWindow::setRegistrationPhase(RegistrationPhase phase) {
    registrationPhase_ = phase;
    updateRegistrationAvailability();
}

// The MRI library this site keeps its scans in.
QString WorkflowWindow::imagingStartDirectory() const {
    if (!lastImagingDirectory_.isEmpty() && QFileInfo::exists(lastImagingDirectory_))
        return lastImagingDirectory_;
    const QString library =
        QStringLiteral("C:/Users/jkche/dev/spire.us/BeamExampleDataDICOMandMatFiles/BEAM MRIs");
    if (QFileInfo::exists(library)) return library;
    return QString();
}

void WorkflowWindow::rememberImagingDirectory(const QString& path) {
    const QFileInfo chosen(path);
    lastImagingDirectory_ = chosen.isDir() ? chosen.absoluteFilePath() : chosen.absolutePath();
}

void WorkflowWindow::chooseNifti() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load patient MRI"), imagingStartDirectory(),
        QStringLiteral("NIfTI images (*.nii *.nii.gz);;All files (*)"));
    if (path.isEmpty()) return;
    rememberImagingDirectory(path);
    loadMri(path);
}

void WorkflowWindow::chooseDicomDirectory() {
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select DICOM series folder"), imagingStartDirectory());
    if (path.isEmpty()) return;
    rememberImagingDirectory(path);
    loadMri(path);
}

// Imports the MRI and six fiducials from a BeamV0 session.


void WorkflowWindow::installMri(beam::mri::Volume3D volume, beam::mri::RasAxisVectors axes,
                                const QString& path) {
    if (volume.nx < 1 || volume.ny < 1 || volume.nz < 1) throw std::runtime_error("MRI contains no image voxels");
    if (axes.dimLR.size() != volume.nx || axes.dimAP.size() != volume.ny || axes.dimIS.size() != volume.nz)
        throw std::runtime_error("MRI physical axes do not match the voxel dimensions");
    const bool replacing = mriLoaded_;
    registrationStartFiducialRow_ = -1;
    ui_->registrationReturnStartButton->setEnabled(false);
    mriVolume_ = std::move(volume);
    focusImageLoaded_ = false;
    mriAxes_ = std::move(axes);
    if (mriAxes_.dimLR.size() > 1 && mriAxes_.dimLR(0) > mriAxes_.dimLR(mriAxes_.dimLR.size() - 1)) mriAxes_.dimLR.reverseInPlace();
    if (mriAxes_.dimAP.size() > 1 && mriAxes_.dimAP(0) > mriAxes_.dimAP(mriAxes_.dimAP.size() - 1)) mriAxes_.dimAP.reverseInPlace();
    if (mriAxes_.dimIS.size() > 1 && mriAxes_.dimIS(0) > mriAxes_.dimIS(mriAxes_.dimIS.size() - 1)) mriAxes_.dimIS.reverseInPlace();
    mriLoaded_ = true;
    mriPath_ = path;
    mriFingerprint_ = mriGeometryFingerprint();
    workflow_.change(beam::gui::WorkflowStage::Imaging, "MRI changed; registration and later approvals require review.");
    setMriPathDisplay(path);
    const auto resolution = beam::mri::computeVoxelResolution(mriAxes_);
    ui_->mriMetadataLabel->setText(
        QStringLiteral("Volume: %1 x %2 x %3 voxels; spacing: %4 x %5 x %6 mm; legacy Beam session\n"
                       "RAS bounds - LR: %7 to %8 mm; AP: %9 to %10 mm; IS: %11 to %12 mm")
            .arg(mriVolume_.nx).arg(mriVolume_.ny).arg(mriVolume_.nz)
            .arg(resolution.lr,0,'f',2).arg(resolution.ap,0,'f',2).arg(resolution.is,0,'f',2)
            .arg(mriAxes_.dimLR(0),0,'f',3).arg(mriAxes_.dimLR(mriAxes_.dimLR.size()-1),0,'f',3)
            .arg(mriAxes_.dimAP(0),0,'f',3).arg(mriAxes_.dimAP(mriAxes_.dimAP.size()-1),0,'f',3)
            .arg(mriAxes_.dimIS(0),0,'f',3).arg(mriAxes_.dimIS(mriAxes_.dimIS.size()-1),0,'f',3));
    ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text() +
        QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
            .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
    ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text().left(
        ui_->mriMetadataLabel->text().indexOf(QStringLiteral("Initial slices"))) +
        QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
            .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
            .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
    ui_->sagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    ui_->coronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    ui_->axialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    ui_->registrationSagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    ui_->registrationCoronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    ui_->registrationAxialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    treatmentSagittalSlider_->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    treatmentCoronalSlider_->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    treatmentAxialSlider_->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
    ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
    ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
    for (QSlider* slider : {ui_->sagittalSlider, ui_->coronalSlider, ui_->axialSlider,
                            ui_->registrationSagittalSlider, ui_->registrationCoronalSlider, ui_->registrationAxialSlider,
                            treatmentSagittalSlider_, treatmentCoronalSlider_, treatmentAxialSlider_}) slider->setEnabled(true);
    for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->sagittalNextButton,
                                ui_->resetSagittalButton,
                                ui_->coronalPreviousButton, ui_->coronalNextButton,
                                ui_->resetCoronalButton,
                                ui_->axialPreviousButton, ui_->axialNextButton,
                                ui_->resetAxialButton,
                                ui_->registrationSagittalPreviousButton, ui_->registrationSagittalNextButton,
                                ui_->registrationResetSagittalButton,
                                ui_->registrationCoronalPreviousButton, ui_->registrationCoronalNextButton,
                                ui_->registrationResetCoronalButton,
                                ui_->registrationAxialPreviousButton, ui_->registrationAxialNextButton,
                                ui_->registrationResetAxialButton,
                                ui_->treatmentSagittalPreviousButton, ui_->treatmentSagittalNextButton,
                                ui_->treatmentSagittalResetButton,
                                ui_->treatmentCoronalPreviousButton, ui_->treatmentCoronalNextButton,
                                ui_->treatmentCoronalResetButton,
                                ui_->treatmentAxialPreviousButton, ui_->treatmentAxialNextButton,
                                ui_->treatmentAxialResetButton}) button->setEnabled(true);
    ui_->acceptImagingButton->setEnabled(true);
    ui_->resetInitialSlicesButton->setEnabled(true);
    ui_->registrationResetInitialSlicesButton->setEnabled(true);
    resetMriViews();
    initializeRegistrationGeometry();
    showMriPreviews();
    showMessage(replacing ? QStringLiteral("Replacement MRI loaded; review and accept it.")
                          : QStringLiteral("MRI loaded. Verify all three views, then accept it."), replacing);
    refresh();
}

void WorkflowWindow::loadMri(const QString& path) {
    try {
        const beam::mri::MriVolumeRas ras = beam::infra::dicom::loadMriRas(path.toStdString());
        beam::mri::Volume3D volume = beam::mri::reorientedVolumeToVolume3D(ras.volume);
        if (volume.nx < 1 || volume.ny < 1 || volume.nz < 1) throw std::runtime_error("MRI contains no image voxels");
        if (ras.axes.dimLR.size() != volume.nx || ras.axes.dimAP.size() != volume.ny ||
            ras.axes.dimIS.size() != volume.nz) {
            throw std::runtime_error("MRI physical axes do not match the RAS-oriented voxel dimensions");
        }

        const bool replacing = mriLoaded_;
        registrationStartFiducialRow_ = -1;
        ui_->registrationReturnStartButton->setEnabled(false);
        mriVolume_ = std::move(volume);
        focusImageLoaded_ = false;
        mriAxes_ = ras.axes;
        // applyVoxelRasXform3D makes voxel indices increase in R/A/S.
        if (mriAxes_.dimLR.size() > 1 && mriAxes_.dimLR(0) > mriAxes_.dimLR(mriAxes_.dimLR.size() - 1))
            mriAxes_.dimLR.reverseInPlace();
        if (mriAxes_.dimAP.size() > 1 && mriAxes_.dimAP(0) > mriAxes_.dimAP(mriAxes_.dimAP.size() - 1))
            mriAxes_.dimAP.reverseInPlace();
        if (mriAxes_.dimIS.size() > 1 && mriAxes_.dimIS(0) > mriAxes_.dimIS(mriAxes_.dimIS.size() - 1))
            mriAxes_.dimIS.reverseInPlace();
        mriLoaded_ = true;
        mriPath_ = path;
        mriFingerprint_ = mriGeometryFingerprint();
        workflow_.change(beam::gui::WorkflowStage::Imaging,
                         "MRI changed; registration and later approvals require review.");
        setMriPathDisplay(path);
        const beam::mri::VoxelResolution resolution = beam::mri::computeVoxelResolution(mriAxes_);
        ui_->mriMetadataLabel->setText(
        QStringLiteral("Volume: %1 x %2 x %3 voxels; spacing: %4 x %5 x %6 mm; RAS oriented\n"
                       "RAS bounds - LR: %7 to %8 mm; AP: %9 to %10 mm; IS: %11 to %12 mm")
                .arg(mriVolume_.nx).arg(mriVolume_.ny).arg(mriVolume_.nz)
                .arg(resolution.lr, 0, 'f', 2).arg(resolution.ap, 0, 'f', 2).arg(resolution.is, 0, 'f', 2)
                .arg(mriAxes_.dimLR(0),0,'f',3).arg(mriAxes_.dimLR(mriAxes_.dimLR.size()-1),0,'f',3)
                .arg(mriAxes_.dimAP(0),0,'f',3).arg(mriAxes_.dimAP(mriAxes_.dimAP.size()-1),0,'f',3)
                .arg(mriAxes_.dimIS(0),0,'f',3).arg(mriAxes_.dimIS(mriAxes_.dimIS.size()-1),0,'f',3));
        ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text() +
            QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
                .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
        ui_->mriMetadataLabel->setText(ui_->mriMetadataLabel->text().left(
            ui_->mriMetadataLabel->text().indexOf(QStringLiteral("Initial slices"))) +
            QStringLiteral("\nInitial slices - LR: %1 mm, AP: %2 mm, IS: %3 mm")
                .arg(mriAxes_.dimLR(static_cast<Eigen::Index>((mriVolume_.nx - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimAP(static_cast<Eigen::Index>((mriVolume_.ny - 1) / 2)), 0, 'f', 3)
                .arg(mriAxes_.dimIS(static_cast<Eigen::Index>((mriVolume_.nz - 1) / 2)), 0, 'f', 3));
        ui_->sagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        ui_->coronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        ui_->axialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        ui_->registrationSagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        ui_->registrationCoronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        ui_->registrationAxialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        treatmentSagittalSlider_->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        treatmentCoronalSlider_->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        treatmentAxialSlider_->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
        ui_->sagittalSlider->setEnabled(true);
        ui_->coronalSlider->setEnabled(true);
        ui_->axialSlider->setEnabled(true);
        ui_->registrationSagittalSlider->setEnabled(true);
        ui_->registrationCoronalSlider->setEnabled(true);
        ui_->registrationAxialSlider->setEnabled(true);
        treatmentSagittalSlider_->setEnabled(true);
        treatmentCoronalSlider_->setEnabled(true);
        treatmentAxialSlider_->setEnabled(true);
        for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->sagittalNextButton,
                                    ui_->resetSagittalButton,
                                    ui_->coronalPreviousButton, ui_->coronalNextButton,
                                    ui_->resetCoronalButton,
                                    ui_->axialPreviousButton, ui_->axialNextButton,
                                    ui_->resetAxialButton,
                                    ui_->registrationSagittalPreviousButton, ui_->registrationSagittalNextButton,
                                    ui_->registrationResetSagittalButton,
                                    ui_->registrationCoronalPreviousButton, ui_->registrationCoronalNextButton,
                                    ui_->registrationResetCoronalButton,
                                    ui_->registrationAxialPreviousButton, ui_->registrationAxialNextButton,
                                    ui_->registrationResetAxialButton,
                                    ui_->treatmentSagittalPreviousButton, ui_->treatmentSagittalNextButton,
                                    ui_->treatmentSagittalResetButton,
                                    ui_->treatmentCoronalPreviousButton, ui_->treatmentCoronalNextButton,
                                    ui_->treatmentCoronalResetButton,
                                    ui_->treatmentAxialPreviousButton, ui_->treatmentAxialNextButton,
                                    ui_->treatmentAxialResetButton}) button->setEnabled(true);
        ui_->acceptImagingButton->setEnabled(true);
        ui_->resetInitialSlicesButton->setEnabled(true);
        ui_->registrationResetInitialSlicesButton->setEnabled(true);
        resetMriViews();
        initializeRegistrationGeometry();
        showMriPreviews();
        showMessage(replacing ? QStringLiteral("Replacement MRI loaded. Downstream approvals were invalidated; review and accept it.")
                              : QStringLiteral("MRI loaded. Verify all three views, then accept the patient MRI."),
                    replacing);
        refresh();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("MRI load failed"), QString::fromUtf8(error.what()));
        showMessage(QStringLiteral("MRI could not be loaded. The previous imaging state was not changed."), true);
    }
}

void WorkflowWindow::resetMriViews() {
    for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                  ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                  ui_->registrationAxialPreview, treatmentSagittalPreview_,
                                  treatmentCoronalPreview_, treatmentAxialPreview_})
        view->resetView();
}

void WorkflowWindow::rebuildFocusImage() {
    // BeamV0/drawFocusOnMRI.m calls setFiducialTemplate at the current array centre on every
    focusImage_.nx = mriVolume_.nx;
    focusImage_.ny = mriVolume_.ny;
    focusImage_.nz = mriVolume_.nz;
    focusImage_.kSlices.assign(static_cast<std::size_t>(mriVolume_.nz),
                               Eigen::MatrixXd::Zero(mriVolume_.nx, mriVolume_.ny));
    for (Eigen::Index k = 0; k < mriVolume_.nz; ++k) {
        const double dz = (mriAxes_.dimIS(k) - targetMm_.z()) / 5.0;
        if (std::abs(dz) >= 1.0) continue;
        for (Eigen::Index j = 0; j < mriVolume_.ny; ++j) {
            const double dy = (mriAxes_.dimAP(j) - targetMm_.y()) / 5.0;
            if (dy * dy + dz * dz >= 1.0) continue;
            for (Eigen::Index i = 0; i < mriVolume_.nx; ++i) {
                const double dx = (mriAxes_.dimLR(i) - targetMm_.x()) / 30.0;
                if (dx * dx + dy * dy + dz * dz < 1.0)
                    focusImage_.kSlices[static_cast<std::size_t>(k)](i, j) = 1.0;
            }
        }
    }
    focusImageLoaded_ = true;
}

}  // namespace beam::app
