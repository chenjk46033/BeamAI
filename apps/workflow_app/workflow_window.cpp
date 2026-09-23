#include "workflow_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <QListWidgetItem>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QProgressDialog>
#include <QThread>

#include "infra_dicom/load_mri_ras.hpp"
#include "infra_mat/legacy_beam_session.hpp"
#include "array/array_data.hpp"
#include "array/array_struct.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "mri/mri_loader.hpp"
#include "mri/ras_transform.hpp"
#include "mri_view.hpp"
#include "registration/array_transform.hpp"
#include "ui_workflow_shell.h"

namespace beam::app {
namespace {

constexpr std::size_t stageCount = static_cast<std::size_t>(beam::gui::WorkflowStage::Count);

Eigen::MatrixXd readArrayRectCsv(const QString& path) {
    std::ifstream file(path.toStdString());
    if (!file) throw std::runtime_error("Cannot open default transducer geometry: " + path.toStdString());
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::string cell;
        std::vector<double> row;
        while (std::getline(stream, cell, ',')) row.push_back(std::stod(cell));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 19 || rows.front().empty()) throw std::runtime_error("Invalid transducer geometry CSV");
    const Eigen::Index columns = static_cast<Eigen::Index>(rows.front().size());
    Eigen::MatrixXd result(19, columns);
    for (Eigen::Index r = 0; r < 19; ++r) {
        if (static_cast<Eigen::Index>(rows[static_cast<std::size_t>(r)].size()) != columns)
            throw std::runtime_error("Ragged transducer geometry CSV");
        for (Eigen::Index c = 0; c < columns; ++c)
            result(r, c) = rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
    }
    return result;
}

// defaultSubjectArrayRect.csv contains both physical panels in one matrix.
// BeamV0's saved MAT structure has separate left/right ArrayStruct entries,
// but defineArrayData(rect) alone duplicates the combined matrix into both.
// Reconstruct those two entries from the panel separation before computing
// fiducials; otherwise both panel centres collapse to the midline.
void reconstructPhysicalArrayHalves(beam::array::ArrayData& data) {
    const Eigen::MatrixXd& total = data.arrayTotal.rect;
    const double midline = total.row(16).mean();
    std::array<std::vector<Eigen::Index>, 2> columns;
    for (Eigen::Index c = 0; c < total.cols(); ++c) {
        // MATLAB designation 1 is Right, designation 2 is Left.
        columns[total(16, c) >= midline ? 0 : 1].push_back(c);
    }
    if (columns[0].empty() || columns[1].empty())
        throw std::runtime_error("Default transducer geometry does not contain distinct left/right panels");
    for (std::size_t half = 0; half < 2; ++half) {
        Eigen::MatrixXd rect(total.rows(), static_cast<Eigen::Index>(columns[half].size()));
        for (Eigen::Index c = 0; c < rect.cols(); ++c)
            rect.col(c) = total.col(columns[half][static_cast<std::size_t>(c)]);
        data.array[half] = beam::array::defineArrayStruct(rect, data.arrayTotal.frequency,
                                                          data.arrayTotal.elementDimensions);
        data.array[half].elementMapping = static_cast<int>(half) + 1;
    }
}

double normalizedAxisPosition(const Eigen::VectorXd& axis, double mm) {
    if (axis.size() < 2 || axis(axis.size() - 1) == axis(0)) return 0.5;
    return std::clamp((mm - axis(0)) / (axis(axis.size() - 1) - axis(0)), 0.0, 1.0);
}

QString statusSymbol(beam::gui::WorkflowStatus status) {
    switch (status) {
        case beam::gui::WorkflowStatus::Complete: return QStringLiteral("✓");
        case beam::gui::WorkflowStatus::Blocked: return QStringLiteral("!");
        case beam::gui::WorkflowStatus::Invalidated: return QStringLiteral("↻");
        case beam::gui::WorkflowStatus::InProgress: return QStringLiteral("●");
        case beam::gui::WorkflowStatus::NotStarted: return QStringLiteral("○");
    }
    return QStringLiteral("?");
}

}  // namespace

WorkflowWindow::WorkflowWindow(QWidget* parent) : QMainWindow(parent), ui_(new Ui::WorkflowShell) {
    ui_->setupUi(this);
    for (QLabel* label : findChildren<QLabel*>()) {
        label->setTextInteractionFlags(label->textInteractionFlags() |
                                       Qt::TextSelectableByMouse |
                                       Qt::TextSelectableByKeyboard);
        label->setFocusPolicy(Qt::ClickFocus);
    }
    for (std::size_t i = 0; i < stageCount; ++i) ui_->stageList->addItem(QString());

    connect(ui_->stageList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(stageCount)) selectStage(static_cast<beam::gui::WorkflowStage>(row));
    });
    connect(ui_->completeStageButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
    connect(ui_->simulatePassButton, &QPushButton::clicked, this, [this] {
        std::string reason;
        if (workflow_.complete(beam::gui::WorkflowStage::SystemCheck, &reason)) {
            ui_->deviceResult->setText(QStringLiteral("Device responded correctly (simulated)."));
            ui_->deviceHeader->setText(QStringLiteral("● Device ready (simulated)"));
            showMessage(QStringLiteral("System check passed."), false);
        } else {
            showMessage(QString::fromStdString(reason), true);
        }
        refresh();
    });
    connect(ui_->simulateFailButton, &QPushButton::clicked, this, [this] {
        workflow_.block(beam::gui::WorkflowStage::SystemCheck, "Device did not respond (simulated).");
        ui_->deviceResult->setText(QStringLiteral("Device did not respond (simulated)."));
        ui_->deviceHeader->setText(QStringLiteral("● Device fault (simulated)"));
        showMessage(QStringLiteral("System check blocked: device did not respond."), true);
        refresh();
    });
    connect(ui_->loadNiftiButton, &QPushButton::clicked, this, [this] { chooseNifti(); });
    connect(ui_->loadDicomButton, &QPushButton::clicked, this, [this] { chooseDicomDirectory(); });
    connect(ui_->loadBeamSessionButton, &QPushButton::clicked, this, [this] { chooseBeamSession(); });
    connect(ui_->sagittalSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->coronalSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->axialSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->registrationSagittalSlider, &QSlider::valueChanged, ui_->sagittalSlider, &QSlider::setValue);
    connect(ui_->registrationCoronalSlider, &QSlider::valueChanged, ui_->coronalSlider, &QSlider::setValue);
    connect(ui_->registrationAxialSlider, &QSlider::valueChanged, ui_->axialSlider, &QSlider::setValue);
    connect(ui_->sagittalSlider, &QSlider::valueChanged, ui_->registrationSagittalSlider, &QSlider::setValue);
    connect(ui_->coronalSlider, &QSlider::valueChanged, ui_->registrationCoronalSlider, &QSlider::setValue);
    connect(ui_->axialSlider, &QSlider::valueChanged, ui_->registrationAxialSlider, &QSlider::setValue);
    connect(ui_->resetSagittalButton, &QToolButton::clicked, this, [this] {
        ui_->sagittalSlider->setValue(static_cast<int>(mriVolume_.nx / 2));
    });
    connect(ui_->resetCoronalButton, &QToolButton::clicked, this, [this] {
        ui_->coronalSlider->setValue(static_cast<int>(mriVolume_.ny / 2));
    });
    connect(ui_->resetAxialButton, &QToolButton::clicked, this, [this] {
        ui_->axialSlider->setValue(static_cast<int>(mriVolume_.nz / 2));
    });
    connect(ui_->goToFiducialButton, &QPushButton::clicked, this, [this] {
        const int markerIndex = ui_->fiducialCombo->currentIndex();
        if (markerIndex < 0 || markerIndex >= static_cast<int>(fiducials_.size())) return;
        const Eigen::Vector3d markerMm = fiducials_[static_cast<std::size_t>(markerIndex)].position * 1000.0;
        const auto voxel = beam::gui::imagePositionToVoxelIndex(markerMm, mriAxes_);
        ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
        ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
        ui_->axialSlider->setValue(static_cast<int>(voxel.k));
        showMessage(QStringLiteral("Showing fiducial %1 on all three planes.")
                        .arg(QString::fromStdString(fiducials_[static_cast<std::size_t>(markerIndex)].name)), false);
    });
    connect(ui_->acceptImagingButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_) {
            showMessage(QStringLiteral("Load a patient MRI before accepting imaging."), true);
            return;
        }
        std::string reason;
        if (workflow_.complete(beam::gui::WorkflowStage::Imaging, &reason)) {
            showMessage(QStringLiteral("MRI accepted. Registration can begin."), false);
        } else {
            showMessage(QString::fromStdString(reason), true);
        }
        refresh();
    });
    connect(ui_->resetRegistrationButton, &QPushButton::clicked, this, [this] {
        populateRegistrationTable();
        registrationComplete_ = false;
        workflow_.change(beam::gui::WorkflowStage::Registration,
                         "Registration measurements changed; later approvals require review.");
        ui_->registrationResult->setText(QStringLiteral("Coordinates reset. Review the six measurements, then register."));
        refresh();
    });
    connect(ui_->registerFiducialsButton, &QPushButton::clicked, this, [this] { performFiducialRegistration(); });
    connect(ui_->registrationTable, &QTableWidget::cellChanged, this, [this](int, int column) {
        if (!registrationGeometryLoaded_ || column == 0) return;
        workflow_.change(beam::gui::WorkflowStage::Registration,
                         "Registration measurements changed; later approvals require review.");
        registrationComplete_ = false;
        ui_->registrationResult->setText(QStringLiteral("Measurements changed. Run registration again to accept them."));
        refresh();
    });
    ui_->registrationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(ui_->participantEdit, &QLineEdit::textChanged, this, [this](const QString& value) {
        ui_->participantHeader->setText(QStringLiteral("Participant: %1").arg(value.isEmpty() ? QStringLiteral("—") : value));
    });
    connect(ui_->visitSpin, &QSpinBox::valueChanged, this, [this](int value) {
        ui_->visitHeader->setText(QStringLiteral("Visit: %1").arg(value));
    });
    connect(ui_->abortButton, &QPushButton::clicked, this, [this] {
        showMessage(QStringLiteral("No treatment is active; no device command was sent."), true);
    });

    ui_->stageList->setCurrentRow(0);
    refresh();
}

WorkflowWindow::~WorkflowWindow() { delete ui_; }

void WorkflowWindow::chooseNifti() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load patient MRI"), QString(),
        QStringLiteral("NIfTI images (*.nii *.nii.gz);;All files (*)"));
    if (!path.isEmpty()) loadMri(path);
}

void WorkflowWindow::chooseDicomDirectory() {
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Select DICOM series folder"));
    if (!path.isEmpty()) loadMri(path);
}

void WorkflowWindow::chooseBeamSession() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load legacy Beam MRI session"), QString(),
                                                       QStringLiteral("MATLAB Beam sessions (*.mat)"));
    if (path.isEmpty()) return;
    auto* progress = new QProgressDialog(QStringLiteral("Loading MRI from Beam session…"), QString(), 0, 0, this);
    progress->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumWidth(520);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();
    ui_->loadBeamSessionButton->setEnabled(false);

    using Import = beam::infra::mat::LegacyBeamMri;
    struct ImportState { std::optional<Import> value; std::exception_ptr error; };
    const auto state = std::make_shared<ImportState>();
    QThread* worker = QThread::create([state, path] {
        try { state->value = beam::infra::mat::loadLegacyBeamMri(path.toStdString()); }
        catch (...) { state->error = std::current_exception(); }
    });
    connect(worker, &QThread::finished, this, [this, state, progress, path] {
        progress->close();
        progress->deleteLater();
        ui_->loadBeamSessionButton->setEnabled(true);
        try {
            if (state->error) std::rethrow_exception(state->error);
            auto imported = std::move(state->value.value());
            installMri(std::move(imported.volume), std::move(imported.axes), path);
            showMessage(QStringLiteral("Beam MRI session imported read-only. Review and accept the embedded MRI."), false);
        } catch (const std::exception& error) {
            QMessageBox::critical(this, QStringLiteral("Beam session import failed"), QString::fromUtf8(error.what()));
            showMessage(QStringLiteral("The session was not imported; the previous MRI was preserved."), true);
        }
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void WorkflowWindow::installMri(beam::mri::Volume3D volume, beam::mri::RasAxisVectors axes,
                                const QString& path) {
    if (volume.nx < 1 || volume.ny < 1 || volume.nz < 1) throw std::runtime_error("MRI contains no image voxels");
    if (axes.dimLR.size() != volume.nx || axes.dimAP.size() != volume.ny || axes.dimIS.size() != volume.nz)
        throw std::runtime_error("MRI physical axes do not match the voxel dimensions");
    const bool replacing = mriLoaded_;
    mriVolume_ = std::move(volume);
    mriAxes_ = std::move(axes);
    if (mriAxes_.dimLR.size() > 1 && mriAxes_.dimLR(0) > mriAxes_.dimLR(mriAxes_.dimLR.size() - 1)) mriAxes_.dimLR.reverseInPlace();
    if (mriAxes_.dimAP.size() > 1 && mriAxes_.dimAP(0) > mriAxes_.dimAP(mriAxes_.dimAP.size() - 1)) mriAxes_.dimAP.reverseInPlace();
    if (mriAxes_.dimIS.size() > 1 && mriAxes_.dimIS(0) > mriAxes_.dimIS(mriAxes_.dimIS.size() - 1)) mriAxes_.dimIS.reverseInPlace();
    mriLoaded_ = true;
    mriPath_ = path;
    workflow_.change(beam::gui::WorkflowStage::Imaging, "MRI changed; registration and later approvals require review.");
    ui_->mriPathLabel->setText(path);
    const auto resolution = beam::mri::computeVoxelResolution(mriAxes_);
    ui_->mriMetadataLabel->setText(
        QStringLiteral("Volume: %1 × %2 × %3 voxels · spacing: %4 × %5 × %6 mm · legacy Beam session\n"
                       "RAS bounds — LR: %7 to %8 mm · AP: %9 to %10 mm · IS: %11 to %12 mm")
            .arg(mriVolume_.nx).arg(mriVolume_.ny).arg(mriVolume_.nz)
            .arg(resolution.lr,0,'f',2).arg(resolution.ap,0,'f',2).arg(resolution.is,0,'f',2)
            .arg(mriAxes_.dimLR(0),0,'f',3).arg(mriAxes_.dimLR(mriAxes_.dimLR.size()-1),0,'f',3)
            .arg(mriAxes_.dimAP(0),0,'f',3).arg(mriAxes_.dimAP(mriAxes_.dimAP.size()-1),0,'f',3)
            .arg(mriAxes_.dimIS(0),0,'f',3).arg(mriAxes_.dimIS(mriAxes_.dimIS.size()-1),0,'f',3));
    ui_->sagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    ui_->coronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    ui_->axialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    ui_->registrationSagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
    ui_->registrationCoronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
    ui_->registrationAxialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
    ui_->sagittalSlider->setValue(static_cast<int>(mriVolume_.nx / 2));
    ui_->coronalSlider->setValue(static_cast<int>(mriVolume_.ny / 2));
    ui_->axialSlider->setValue(static_cast<int>(mriVolume_.nz / 2));
    for (QSlider* slider : {ui_->sagittalSlider, ui_->coronalSlider, ui_->axialSlider,
                            ui_->registrationSagittalSlider, ui_->registrationCoronalSlider, ui_->registrationAxialSlider}) slider->setEnabled(true);
    ui_->resetSagittalButton->setEnabled(true); ui_->resetCoronalButton->setEnabled(true); ui_->resetAxialButton->setEnabled(true);
    ui_->acceptImagingButton->setEnabled(true);
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
        mriVolume_ = std::move(volume);
        mriAxes_ = ras.axes;
        // applyVoxelRasXform3D makes voxel indices increase in R/A/S. Keep
        // the displayed physical axes in that same order after a source flip.
        if (mriAxes_.dimLR.size() > 1 && mriAxes_.dimLR(0) > mriAxes_.dimLR(mriAxes_.dimLR.size() - 1))
            mriAxes_.dimLR.reverseInPlace();
        if (mriAxes_.dimAP.size() > 1 && mriAxes_.dimAP(0) > mriAxes_.dimAP(mriAxes_.dimAP.size() - 1))
            mriAxes_.dimAP.reverseInPlace();
        if (mriAxes_.dimIS.size() > 1 && mriAxes_.dimIS(0) > mriAxes_.dimIS(mriAxes_.dimIS.size() - 1))
            mriAxes_.dimIS.reverseInPlace();
        mriLoaded_ = true;
        mriPath_ = path;
        workflow_.change(beam::gui::WorkflowStage::Imaging,
                         "MRI changed; registration and later approvals require review.");
        ui_->mriPathLabel->setText(path);
        const beam::mri::VoxelResolution resolution = beam::mri::computeVoxelResolution(mriAxes_);
        ui_->mriMetadataLabel->setText(
            QStringLiteral("Volume: %1 × %2 × %3 voxels · spacing: %4 × %5 × %6 mm · RAS oriented\n"
                           "RAS bounds — LR: %7 to %8 mm · AP: %9 to %10 mm · IS: %11 to %12 mm")
                .arg(mriVolume_.nx).arg(mriVolume_.ny).arg(mriVolume_.nz)
                .arg(resolution.lr, 0, 'f', 2).arg(resolution.ap, 0, 'f', 2).arg(resolution.is, 0, 'f', 2)
                .arg(mriAxes_.dimLR(0),0,'f',3).arg(mriAxes_.dimLR(mriAxes_.dimLR.size()-1),0,'f',3)
                .arg(mriAxes_.dimAP(0),0,'f',3).arg(mriAxes_.dimAP(mriAxes_.dimAP.size()-1),0,'f',3)
                .arg(mriAxes_.dimIS(0),0,'f',3).arg(mriAxes_.dimIS(mriAxes_.dimIS.size()-1),0,'f',3));
        ui_->sagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        ui_->coronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        ui_->axialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        ui_->registrationSagittalSlider->setRange(0, static_cast<int>(mriVolume_.nx - 1));
        ui_->registrationCoronalSlider->setRange(0, static_cast<int>(mriVolume_.ny - 1));
        ui_->registrationAxialSlider->setRange(0, static_cast<int>(mriVolume_.nz - 1));
        ui_->sagittalSlider->setValue(static_cast<int>(mriVolume_.nx / 2));
        ui_->coronalSlider->setValue(static_cast<int>(mriVolume_.ny / 2));
        ui_->axialSlider->setValue(static_cast<int>(mriVolume_.nz / 2));
        ui_->sagittalSlider->setEnabled(true);
        ui_->coronalSlider->setEnabled(true);
        ui_->axialSlider->setEnabled(true);
        ui_->registrationSagittalSlider->setEnabled(true);
        ui_->registrationCoronalSlider->setEnabled(true);
        ui_->registrationAxialSlider->setEnabled(true);
        ui_->resetSagittalButton->setEnabled(true);
        ui_->resetCoronalButton->setEnabled(true);
        ui_->resetAxialButton->setEnabled(true);
        ui_->acceptImagingButton->setEnabled(true);
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
    reconstructPhysicalArrayHalves(nominal);
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
    registrationOriginArrayData_ = arrayData_;
    registrationComplete_ = false;
    ui_->fiducialCombo->clear();
    for (const auto& marker : fiducials_) ui_->fiducialCombo->addItem(QString::fromStdString(marker.name));
    ui_->fiducialCombo->setEnabled(!fiducials_.empty());
    ui_->goToFiducialButton->setEnabled(!fiducials_.empty());
    targetMm_ = arrayData_.arrayTotal.rect.block(16, 0, 3, arrayData_.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
    arrayMask_ = beam::gui::rasterizeArrayOntoMriGrid(arrayData_.arrayTotal, mriAxes_, mriVolume_.nx,
                                                       mriVolume_.ny, mriVolume_.nz);
    registrationGeometryLoaded_ = true;
    populateRegistrationTable();
    ui_->registerFiducialsButton->setEnabled(true);
    ui_->resetRegistrationButton->setEnabled(true);
    ui_->registrationResult->setText(QStringLiteral("Ready: review all six MRI fiducial coordinates."));
}

void WorkflowWindow::populateRegistrationTable() {
    if (!registrationGeometryLoaded_ && fiducials_.empty()) return;
    ui_->registrationTable->blockSignals(true);
    for (int row = 0; row < static_cast<int>(fiducials_.size()) && row < 6; ++row) {
        const auto& marker = fiducials_[static_cast<std::size_t>(row)];
        auto* name = new QTableWidgetItem(QString::fromStdString(marker.name));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        ui_->registrationTable->setItem(row, 0, name);
        for (int axis = 0; axis < 3; ++axis) {
            auto* coordinate = new QTableWidgetItem(QString::number(marker.position(axis) * 1000.0, 'f', 2));
            coordinate->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            ui_->registrationTable->setItem(row, axis + 1, coordinate);
        }
    }
    ui_->registrationTable->blockSignals(false);
}

void WorkflowWindow::applyRegistrationResult(beam::registration::AffineArrayResult result) {
    arrayData_ = std::move(result.arrayData);
    fiducials_ = std::move(result.fiducialMarkers);
    targetMm_ = arrayData_.arrayTotal.rect.block(16, 0, 3, arrayData_.arrayTotal.rect.cols()).rowwise().mean() * 1000.0;
    arrayMask_ = beam::gui::rasterizeArrayOntoMriGrid(arrayData_.arrayTotal, mriAxes_, mriVolume_.nx,
                                                       mriVolume_.ny, mriVolume_.nz);
    showMriPreviews();
}

void WorkflowWindow::performFiducialRegistration() {
    if (!mriLoaded_ || workflow_.state(beam::gui::WorkflowStage::Imaging).status != beam::gui::WorkflowStatus::Complete) {
        showMessage(QStringLiteral("Accept the patient MRI before registration."), true);
        return;
    }
    std::vector<Eigen::Vector3d> measured;
    measured.reserve(6);
    for (int row = 0; row < 6; ++row) {
        Eigen::Vector3d point;
        for (int axis = 0; axis < 3; ++axis) {
            const QTableWidgetItem* item = ui_->registrationTable->item(row, axis + 1);
            bool valid = false;
            const double value = item ? item->text().toDouble(&valid) : 0.0;
            if (!valid || !std::isfinite(value)) {
                showMessage(QStringLiteral("Fiducial row %1 contains an invalid coordinate.").arg(row + 1), true);
                return;
            }
            point(axis) = value;
        }
        measured.push_back(point);
    }
    try {
        auto result = beam::registration::registerArrayToFiducials(registrationOriginArrayData_, measured);
        double squaredError = 0.0;
        for (std::size_t i = 0; i < measured.size(); ++i)
            squaredError += (result.fiducialMarkers[i].position * 1000.0 - measured[i]).squaredNorm();
        const double rmsMm = std::sqrt(squaredError / static_cast<double>(measured.size()));
        applyRegistrationResult(std::move(result));
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::Registration, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        registrationComplete_ = true;
        ui_->registrationResult->setText(QStringLiteral("Registration complete · RMS residual %1 mm · overlays updated")
                                             .arg(rmsMm, 0, 'f', 2));
        showMessage(QStringLiteral("Registration accepted. Review the updated red fiducials and yellow array overlay."), false);
        refresh();
    } catch (const std::exception& error) {
        ui_->registrationResult->setText(QStringLiteral("Registration failed: %1").arg(QString::fromUtf8(error.what())));
        showMessage(QStringLiteral("Registration failed. Verify that all six points are distinct and correctly paired."), true);
    }
}

void WorkflowWindow::showMriPreviews() {
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
    const double iNorm = mriVolume_.nx > 1 ? static_cast<double>(sagittal - 1) / (mriVolume_.nx - 1) : 0.5;
    const double jNorm = mriVolume_.ny > 1 ? static_cast<double>(coronal - 1) / (mriVolume_.ny - 1) : 0.5;
    const double kNorm = mriVolume_.nz > 1 ? static_cast<double>(axial - 1) / (mriVolume_.nz - 1) : 0.5;
    ui_->sagittalPreview->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    ui_->coronalPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    ui_->axialPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));
    ui_->registrationSagittalPreview->setNavigationCrosshair(QPointF(1.0 - jNorm, 1.0 - kNorm));
    ui_->registrationCoronalPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - kNorm));
    ui_->registrationAxialPreview->setNavigationCrosshair(QPointF(iNorm, 1.0 - jNorm));

    if (registrationGeometryLoaded_) {
        ui_->sagittalPreview->setMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"),
                                             QColor(255, 220, 40), 0.55, true);
        ui_->coronalPreview->setMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"),
                                            QColor(255, 220, 40), 0.55);
        ui_->axialPreview->setMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"),
                                          QColor(255, 220, 40), 0.55);
        ui_->registrationSagittalPreview->setMaskOverlay(beam::mri::getSliceImage(arrayMask_, sagittal, "sagital"), QColor(255, 220, 40), 0.55, true);
        ui_->registrationCoronalPreview->setMaskOverlay(beam::mri::getSliceImage(arrayMask_, coronal, "coronal"), QColor(255, 220, 40), 0.55);
        ui_->registrationAxialPreview->setMaskOverlay(beam::mri::getSliceImage(arrayMask_, axial, "axial"), QColor(255, 220, 40), 0.55);

        std::vector<WorkflowMriMarker> sagMarkers, corMarkers, axialMarkers;
        for (const auto& marker : fiducials_) {
            const Eigen::Vector3d mm = marker.position * 1000.0;
            const auto voxel = beam::gui::imagePositionToVoxelIndex(mm, mriAxes_);
            if (voxel.i == sagittal - 1)
                sagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y()),
                                               1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                      QString::fromStdString(marker.name), QColor(230, 45, 55), false});
            if (voxel.j == coronal - 1)
                corMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                               1.0 - normalizedAxisPosition(mriAxes_.dimIS, mm.z())),
                                      QString::fromStdString(marker.name), QColor(230, 45, 55), false});
            if (voxel.k == axial - 1)
                axialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, mm.x()),
                                                 1.0 - normalizedAxisPosition(mriAxes_.dimAP, mm.y())),
                                        QString::fromStdString(marker.name), QColor(230, 45, 55), false});
        }
        const auto targetVoxel = beam::gui::imagePositionToVoxelIndex(targetMm_, mriAxes_);
        if (targetVoxel.i == sagittal - 1)
            sagMarkers.push_back({QPointF(1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y()),
                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                  QString(), QColor(65, 235, 100), true});
        if (targetVoxel.j == coronal - 1)
            corMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                           1.0 - normalizedAxisPosition(mriAxes_.dimIS, targetMm_.z())),
                                  QString(), QColor(65, 235, 100), true});
        if (targetVoxel.k == axial - 1)
            axialMarkers.push_back({QPointF(normalizedAxisPosition(mriAxes_.dimLR, targetMm_.x()),
                                             1.0 - normalizedAxisPosition(mriAxes_.dimAP, targetMm_.y())),
                                    QString(), QColor(65, 235, 100), true});
        ui_->sagittalPreview->setMarkers(sagMarkers);
        ui_->coronalPreview->setMarkers(corMarkers);
        ui_->axialPreview->setMarkers(axialMarkers);
        ui_->registrationSagittalPreview->setMarkers(std::move(sagMarkers));
        ui_->registrationCoronalPreview->setMarkers(std::move(corMarkers));
        ui_->registrationAxialPreview->setMarkers(std::move(axialMarkers));
    }

    const auto coordinate = [](const Eigen::VectorXd& axis, int oneBasedIndex) {
        const Eigen::Index i = std::clamp<Eigen::Index>(oneBasedIndex - 1, 0, axis.size() - 1);
        return axis(i);
    };
    const double lr = coordinate(mriAxes_.dimLR, sagittal);
    const double ap = coordinate(mriAxes_.dimAP, coronal);
    const double is = coordinate(mriAxes_.dimIS, axial);
    ui_->sagittalPreview->setRasMapping(QStringLiteral("sagittal"), lr,
                                        mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1),
                                        mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), true, true);
    ui_->coronalPreview->setRasMapping(QStringLiteral("coronal"), ap,
                                       mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1),
                                       mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), false, true);
    ui_->axialPreview->setRasMapping(QStringLiteral("axial"), is,
                                     mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1),
                                     mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), false, true);
    ui_->registrationSagittalPreview->setRasMapping(QStringLiteral("sagittal"), lr, mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), true, true);
    ui_->registrationCoronalPreview->setRasMapping(QStringLiteral("coronal"), ap, mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1), mriAxes_.dimIS(0), mriAxes_.dimIS(mriAxes_.dimIS.size() - 1), false, true);
    ui_->registrationAxialPreview->setRasMapping(QStringLiteral("axial"), is, mriAxes_.dimLR(0), mriAxes_.dimLR(mriAxes_.dimLR.size() - 1), mriAxes_.dimAP(0), mriAxes_.dimAP(mriAxes_.dimAP.size() - 1), false, true);
    const double lrCenter = coordinate(mriAxes_.dimLR, static_cast<int>(mriVolume_.nx / 2) + 1);
    const double apCenter = coordinate(mriAxes_.dimAP, static_cast<int>(mriVolume_.ny / 2) + 1);
    const double isCenter = coordinate(mriAxes_.dimIS, static_cast<int>(mriVolume_.nz / 2) + 1);
    ui_->sagittalSliceLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->coronalSliceLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->axialSliceLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    ui_->registrationSagittalLabel->setText(QStringLiteral("LR %1 mm").arg(lr, 0, 'f', 1));
    ui_->registrationCoronalLabel->setText(QStringLiteral("AP %1 mm").arg(ap, 0, 'f', 1));
    ui_->registrationAxialLabel->setText(QStringLiteral("IS %1 mm").arg(is, 0, 'f', 1));
    ui_->sagittalSlider->setToolTip(QStringLiteral("LR %1 mm · %2 mm from initial center")
                                        .arg(lr, 0, 'f', 1).arg(lr - lrCenter, 0, 'f', 1));
    ui_->coronalSlider->setToolTip(QStringLiteral("AP %1 mm · %2 mm from initial center")
                                      .arg(ap, 0, 'f', 1).arg(ap - apCenter, 0, 'f', 1));
    ui_->axialSlider->setToolTip(QStringLiteral("IS %1 mm · %2 mm from initial center")
                                    .arg(is, 0, 'f', 1).arg(is - isCenter, 0, 'f', 1));
}

void WorkflowWindow::selectStage(beam::gui::WorkflowStage stage) {
    selectedStage_ = stage;
    ui_->pageTitle->setText(QString::fromUtf8(beam::gui::workflowStageName(stage).data()));
    const int stageIndex = static_cast<int>(stage);
    ui_->pageStack->setCurrentIndex(stageIndex <= 3 ? stageIndex : 4);
    ui_->completeStageButton->setVisible(stage != beam::gui::WorkflowStage::SystemCheck &&
                                         stage != beam::gui::WorkflowStage::Imaging &&
                                         stage != beam::gui::WorkflowStage::Registration);
    refresh();
}

void WorkflowWindow::completeCurrentStage() {
    if (selectedStage_ == beam::gui::WorkflowStage::CaseSetup &&
        (ui_->participantEdit->text().trimmed().isEmpty() || ui_->siteEdit->text().trimmed().isEmpty())) {
        showMessage(QStringLiteral("Participant ID and Site ID are required."), true);
        return;
    }
    std::string reason;
    if (!workflow_.complete(selectedStage_, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    showMessage(QStringLiteral("%1 complete.").arg(QString::fromUtf8(beam::gui::workflowStageName(selectedStage_).data())), false);
    const auto next = workflow_.nextStage();
    ui_->stageList->setCurrentRow(static_cast<int>(next));
    refresh();
}

void WorkflowWindow::refresh() {
    for (std::size_t i = 0; i < stageCount; ++i) {
        const auto stage = static_cast<beam::gui::WorkflowStage>(i);
        const auto& state = workflow_.state(stage);
        ui_->stageList->item(static_cast<int>(i))->setText(
            QStringLiteral("%1  %2\n    %3")
                .arg(statusSymbol(state.status), QString::fromUtf8(beam::gui::workflowStageName(stage).data()),
                     QString::fromUtf8(beam::gui::workflowStatusName(state.status).data())));
    }
    std::string reason;
    const bool ready = workflow_.canStartTreatment(&reason);
    ui_->abortButton->setEnabled(workflow_.state(beam::gui::WorkflowStage::Treatment).status ==
                                  beam::gui::WorkflowStatus::InProgress);
    if (selectedStage_ == beam::gui::WorkflowStage::Treatment && !ready) showMessage(QString::fromStdString(reason), true);
}

void WorkflowWindow::showMessage(const QString& text, bool error) {
    ui_->workflowMessage->setText(text);
    ui_->workflowMessage->setStyleSheet(error
        ? QStringLiteral("padding: 10px; color: #845006; background: #fff4dc; border-radius: 5px;")
        : QStringLiteral("padding: 10px; color: #12663f; background: #e8f5ed; border-radius: 5px;"));
}

}  // namespace beam::app
