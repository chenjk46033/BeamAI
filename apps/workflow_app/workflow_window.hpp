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
class QGroupBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QSlider;
class QComboBox;
class QTableWidget;
class QCheckBox;
class QTabWidget;
class WorkflowMriView;
class QWidget;
class QGroupBox;
QT_END_NAMESPACE

namespace beam::app {

class WorkflowWindow final : public QMainWindow {
public:
    explicit WorkflowWindow(QWidget* parent = nullptr);
    ~WorkflowWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class RegistrationPhase { Locating, FitApplied, LockPositionRegistered, Accepted };
    bool registrationFitApplied() const;
    bool registrationLockPositionRegistered() const;
    void setRegistrationPhase(RegistrationPhase phase);
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
    void performCurrentPositionRegistration();
    void acceptFiducialRegistration();
    void applyRegistrationResult(beam::registration::AffineArrayResult result);
    void beginFiducialPlacement();
    void placeSelectedFiducial(const Eigen::Vector3d& positionMm);
    void confirmSelectedFiducial();
    void setPlacementMode(bool enabled);
    void updateRegistrationAvailability();
    void updateRegistrationStepIndicators();
    void refresh();
    void showMessage(const QString& text, bool error);
    void syncRegistrationPreviewHeights();

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
    std::array<bool, 6> fiducialLocated_{};
    std::array<bool, 6> sourceFiducialLocated_{};
    bool placingFiducial_ = false;
    Eigen::Vector3d targetMm_ = Eigen::Vector3d::Zero();
    bool registrationGeometryLoaded_ = false;
    RegistrationPhase registrationPhase_ = RegistrationPhase::Locating;
    bool deviceCheckPassed_ = false;
    bool mriLoaded_ = false;
    bool focusImageLoaded_ = false;
    bool suppressRegistrationNavigation_ = false;
    bool couplingCheckPassed_ = false;
    bool correctionCheckPassed_ = false;
    int registrationStartFiducialRow_ = -1;
    QString mriPath_;
    QSlider* leftHorizontalPositionSlider_ = nullptr;
    QSlider* leftVerticalPositionSlider_ = nullptr;
    QSlider* rightHorizontalPositionSlider_ = nullptr;
    QSlider* rightVerticalPositionSlider_ = nullptr;
    QPushButton* registerCurrentPositionButton_ = nullptr;
    QLabel* registrationStep1Label_ = nullptr;
    QLabel* registrationStep2Label_ = nullptr;
    QLabel* registrationStep3Label_ = nullptr;
    QLabel* registrationStep4Label_ = nullptr;
    QLabel* couplingStatusLabel_ = nullptr;
    QLabel* couplingTitleLabel_ = nullptr;
    QLabel* couplingDescriptionLabel_ = nullptr;
    QLabel* correctionTitleLabel_ = nullptr;
    QLabel* correctionDescriptionLabel_ = nullptr;
    QLabel* correctionStatusLabel_ = nullptr;
    QProgressBar* couplingProgressBar_ = nullptr;
    QProgressBar* correctionProgressBar_ = nullptr;
    QPushButton* runCouplingCheckButton_ = nullptr;
    QPushButton* acceptCouplingButton_ = nullptr;
    QPushButton* runCorrectionButton_ = nullptr;
    QPushButton* acceptCorrectionButton_ = nullptr;
    QLabel* treatmentPlanTitleLabel_ = nullptr;
    QLabel* treatmentPlanDescriptionLabel_ = nullptr;
    QLabel* treatmentPlanTargetLabel_ = nullptr;
    QLabel* treatmentProtocolLabel_ = nullptr;
    QLabel* treatmentTargetTableLabel_ = nullptr;
    QLabel* treatmentProtocolTableLabel_ = nullptr;
    QComboBox* treatmentProtocolCombo_ = nullptr;
    QTableWidget* treatmentTargetTable_ = nullptr;
    QTableWidget* treatmentProtocolTable_ = nullptr;
    QPushButton* addSonicationButton_ = nullptr;
    QPushButton* removeSonicationButton_ = nullptr;
    QTabWidget* treatmentTabs_ = nullptr;
    QPushButton* acceptTreatmentPlanButton_ = nullptr;
    QWidget* treatmentMriPage_ = nullptr;
    QWidget* treatmentBodySplitter_ = nullptr;
    QWidget* treatmentControlsPanel_ = nullptr;
    QGroupBox* calibrationGroup_ = nullptr;
    WorkflowMriView* treatmentSagittalPreview_ = nullptr;
    WorkflowMriView* treatmentCoronalPreview_ = nullptr;
    WorkflowMriView* treatmentAxialPreview_ = nullptr;
    QSlider* treatmentSagittalSlider_ = nullptr;
    QSlider* treatmentCoronalSlider_ = nullptr;
    QSlider* treatmentAxialSlider_ = nullptr;
    QLabel* treatmentExecutionTitleLabel_ = nullptr;
    QLabel* treatmentExecutionDescriptionLabel_ = nullptr;
    QLabel* treatmentExecutionStatusLabel_ = nullptr;
    QProgressBar* treatmentExecutionProgressBar_ = nullptr;
    QPushButton* startTreatmentButton_ = nullptr;
    QPushButton* abortTreatmentButton_ = nullptr;
    QLabel* safetyReviewTitleLabel_ = nullptr;
    QLabel* safetyReviewDescriptionLabel_ = nullptr;
    QLabel* safetyReviewStatusLabel_ = nullptr;
    QCheckBox* safetyRegistrationCheckBox_ = nullptr;
    QCheckBox* safetyCorrectionCheckBox_ = nullptr;
    QCheckBox* safetyPlanCheckBox_ = nullptr;
    QPushButton* acceptSafetyReviewButton_ = nullptr;
};

}  // namespace beam::app
