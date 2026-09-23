#pragma once

#include <QMainWindow>
#include <array>

#include "gui/treatment_workflow.hpp"
#include "array/array_types.hpp"
#include "mri/slice.hpp"
#include "mri/ras_transform.hpp"
#include "registration/array_transform.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class WorkflowShell; }
QT_END_NAMESPACE

namespace beam::app {

class WorkflowWindow final : public QMainWindow {
public:
    explicit WorkflowWindow(QWidget* parent = nullptr);
    ~WorkflowWindow() override;

private:
    void selectStage(beam::gui::WorkflowStage stage);
    void completeCurrentStage();
    void chooseNifti();
    void chooseDicomDirectory();
    void chooseBeamSession();
    void loadMri(const QString& path);
    void installMri(beam::mri::Volume3D volume, beam::mri::RasAxisVectors axes, const QString& path);
    void showMriPreviews();
    void resetMriViews();
    void rebuildFocusImage();
    void initializeRegistrationGeometry();
    void populateRegistrationTable();
    void navigateToRegistrationFiducial(int row);
    void performFiducialRegistration();
    void applyRegistrationResult(beam::registration::AffineArrayResult result);
    void beginFiducialPlacement();
    void placeSelectedFiducial(const Eigen::Vector3d& positionMm);
    void setPlacementMode(bool enabled);
    void updateRegistrationAvailability();
    void refresh();
    void showMessage(const QString& text, bool error);

    Ui::WorkflowShell* ui_;
    beam::gui::TreatmentWorkflow workflow_;
    beam::gui::WorkflowStage selectedStage_ = beam::gui::WorkflowStage::CaseSetup;
    beam::mri::Volume3D mriVolume_;
    beam::mri::RasAxisVectors mriAxes_;
    beam::array::ArrayData arrayData_;
    beam::array::ArrayData registrationOriginArrayData_;
    beam::mri::Volume3D arrayMask_;
    beam::mri::Volume3D focusImage_;
    std::vector<beam::registration::FiducialMarker> fiducials_;
    std::vector<beam::registration::FiducialMarker> registrationSourceFiducials_;
    std::array<bool, 6> fiducialConfirmed_{};
    std::array<bool, 6> sourceFiducialConfirmed_{};
    bool placingFiducial_ = false;
    Eigen::Vector3d targetMm_ = Eigen::Vector3d::Zero();
    bool registrationGeometryLoaded_ = false;
    bool registrationComplete_ = false;
    bool mriLoaded_ = false;
    bool focusImageLoaded_ = false;
    QString mriPath_;
};

}  // namespace beam::app
