#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPointF>
#include <QSize>
#include <array>
#include <memory>
#include <vector>

#include "gui/treatment_target_store.hpp"
#include "gui/treatment_workflow.hpp"
#include "array/array_types.hpp"
#include "mri/fiducial_detect.hpp"
#include "mri/slice.hpp"
#include "mri/ras_transform.hpp"
#include "registration/array_transform.hpp"
// Needed complete, not forward-declared: targetSonications_ stores vectors of
// StimParamRow by value.
#include "gui_qt/stim_param_table_model.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class WorkflowShell; }
class QGroupBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QProgressBar;
class QRadioButton;
class QSlider;
class QSpacerItem;
class QSpinBox;
class QComboBox;
class QTableView;
class QTableWidget;
class QCheckBox;
class QTabWidget;
class QTimer;
class QToolButton;
class WorkflowMriView;
class QWidget;
class QGroupBox;
QT_END_NAMESPACE

// The Treatment plan and Treatment stages reuse BeamV0's own Sonicate-tab
// models and plots; see treatment_plan_body.cpp.
namespace beam::gui_qt {
class PulseWaveformView;
class StimParamTableModel;
class TotalSonicationView;
class TreatmentProtocolTableModel;
}  // namespace beam::gui_qt

namespace beam::serialcom {
class SerialPort;
}  // namespace beam::serialcom

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
    // Not focusTreatmentViewsOn: that writes targetMm_, and looking at a
    // marker must not edit the planned target.
    void navigateToFiducialOnTreatment(int index);
    // False when fewer than six fiducials exist.
    bool fiducialDiagramData(std::array<QString, 6>* labels,
                             std::array<QPointF, 6>* apIsMm) const;
    QWidget* buildFiducialPickerWidget(QMenu* menu);
    void performFiducialRegistration();
    void performCurrentPositionRegistration();
    void acceptFiducialRegistration();
    void applyRegistrationResult(beam::registration::AffineArrayResult result);
    // heldAxis is the axis the clicked plane cannot measure (0=LR, 1=AP,
    // 2=IS); it keeps its current value. -1 writes all three.
    void placeSelectedFiducial(const Eigen::Vector3d& positionMm, int heldAxis = -1);
    void confirmSelectedFiducial();
    QString currentMriGeometry() const;
    static bool sameMriGeometry(const QString& recorded, const QString& current);
    std::vector<std::pair<QSlider*, QString>> sliceSliders() const;
    void installSliceReadout(QSlider* slider, const QString& axis);
    QString sliceReadoutText(const QString& axis, int slice) const;
    int sliceAtSliderPosition(const QSlider* slider, QPoint position) const;
    double sliceCoordinateMm(const QString& axis, int index) const;
    QString fiducialDirectory() const;
    QString writeFiducialsCsv(const QString& path) const;
    void saveFiducialsCsv();
    void importFiducialsCsv();
    void detectFiducials();
    void detectSingleFiducial(int row);
    void applyFiducialConfidenceToTable();
    void highlightRegistrationRow(int currentRow);
    void updateArrayPositionReadout();
    void updateRegistrationAvailability();
    void updateRegistrationStepIndicators();
    void refresh();
    void setMriPathDisplay(const QString& path);
    void showMessage(const QString& text, bool error);
    void syncMriViewerHeights();

    // "Open viewer in a new window" -- detached_viewer.cpp. One window per
    // pane, driving that pane's own slice controls rather than holding state.
    struct DetachedViewer {
        QWidget* window = nullptr;
        WorkflowMriView* view = nullptr;
        WorkflowMriView* source = nullptr;
        QLabel* label = nullptr;        // this window's slice readout
        QLabel* sourceLabel = nullptr;  // the pane's, which it copies
    };
    void openDetachedViewer(WorkflowMriView* source, const QString& title, QSlider* slider,
                            QToolButton* previous, QToolButton* next, QToolButton* reset,
                            QLabel* sliceLabel);
    void refreshDetachedViewers();
    void wireDetachedViewerMenus();

    // Treatment plan stage (BeamV0's Sonicate tab) -- treatment_plan_body.cpp.
    void buildTreatmentPlanBody(const QString& acceptButtonStyle);
    void wireTreatmentPlanBody();
    void addTargetListRow(const QString& name, int atRow = -1);
    void switchToSelectedTarget();
    void saveCurrentTargetSonications();
    void applyArrayCentreToAllTargets(const Eigen::Vector3d& centreMm);
    void updateStimGridHeight();
    void loadTreatmentProtocol(const QString& protocolName);
    QString treatmentDataDirectory() const;
    void loadTreatmentTargetStore();
    void refreshProtocolCombo(const QString& select);
    void writeProtocolGridToStore();
    void saveTreatmentTargetStore();
    void setTargetStoreDirty(bool dirty);
    void selectSonicationRow(int row);
    int selectedSonicationRow() const;
    void setSonicationTargetFromMri(const Eigen::Vector3d& positionMm);
    void focusTreatmentViewsOn(const Eigen::Vector3d& positionMm);
    void updateTreatmentPlanSummary();
    void updateSonicationPlots();
    void updateBestTargets();
    void updateExampleTargetImage();

    // Treatment stage: the firing half of that tab, gated behind Safety
    // Review by the workflow model rather than by a button's enabled state.
    void buildTreatmentExecutionBody(const QString& acceptButtonStyle);
    void setSerialConnectedLamp(bool connected);
    void runSonication(bool sham);
    void startSonicationCountdown(int durationSeconds);
    void abortSonication();
    void finishTreatmentStage();

    // app.sys.RTT(1).couplingThreshold. beam_app's port uses the same value.
    static constexpr double kCouplingThreshold = 0.10;
    // Height the Treatment plan body is guaranteed under the MRI viewers; it
    // scrolls below that rather than squeezing them. syncMriViewerHeights
    // reserves it so the viewers stay the same size as on the other stages.
    static constexpr int kTreatmentBodyMinHeight = 420;
    // Fixed viewer height for the Treatment plan stage, which shares its page
    // with the whole Sonicate body. Small enough that the body fits under it
    // on an ordinary screen, large enough to place a target in.
    static constexpr int kTreatmentViewerHeight = 360;

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
    // 0 when placed by hand or imported rather than detected.
    std::array<double, 6> fiducialConfidence_{};
    std::array<double, 6> fiducialStability_{};
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
    QHash<QString, QString> sliceSliderTips_;
    QSlider* leftHorizontalPositionSlider_ = nullptr;
    QSlider* leftVerticalPositionSlider_ = nullptr;
    QSlider* rightHorizontalPositionSlider_ = nullptr;
    QSlider* rightVerticalPositionSlider_ = nullptr;
    QPushButton* registerCurrentPositionButton_ = nullptr;
    QPushButton* importFiducialsButton_ = nullptr;
    QPushButton* detectFiducialsButton_ = nullptr;
    QToolButton* fiducialActionsButton_ = nullptr;
    QLabel* registrationMriPathLabel_ = nullptr;
    // The picker is rebuilt per right-click, so the pick is remembered here.
    int treatmentFiducialPick_ = -1;
    QPushButton* saveFiducialsButton_ = nullptr;
    QLabel* arrayPositionLabel_ = nullptr;
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
    QComboBox* treatmentProtocolCombo_ = nullptr;
    QTabWidget* treatmentTabs_ = nullptr;
    QPushButton* acceptTreatmentPlanButton_ = nullptr;
    QWidget* treatmentMriPage_ = nullptr;
    int mriHeightSyncPasses_ = 0;
    // app.sys.protocolTables: each target's own stimParamTable data, keyed by
    // the target row's stable id so a rename or insert cannot orphan it.
    QHash<int, std::vector<beam::gui_qt::StimParamRow>> targetSonications_;
    int currentTargetId_ = -1;
    int nextTargetId_ = 0;
    QSize lastMriSyncWindowSize_;
    QSpacerItem* placeholderTailSpacer_ = nullptr;
    std::vector<DetachedViewer> detachedViewers_;
    QGroupBox* calibrationGroup_ = nullptr;
    // Treatment plan body: BeamV0's Sonicate-tab planning half.
    QWidget* treatmentPlanBody_ = nullptr;
    beam::gui_qt::StimParamTableModel* stimParamModel_ = nullptr;
    beam::gui_qt::TreatmentProtocolTableModel* protocolModel_ = nullptr;
    // BeamAI's own protocol/target library. Seeded from data/*.csv in the
    // install, saved to the user's own copy -- see treatmentDataDirectory().
    beam::gui::TreatmentTargetStore targetStore_;
    bool targetStoreDirty_ = false;
    QPushButton* newProtocolButton_ = nullptr;
    QPushButton* renameProtocolButton_ = nullptr;
    QPushButton* deleteProtocolButton_ = nullptr;
    QPushButton* addProtocolEntryButton_ = nullptr;
    QPushButton* removeProtocolEntryButton_ = nullptr;
    QPushButton* saveProtocolsButton_ = nullptr;
    QTableView* stimParamView_ = nullptr;
    QTableView* protocolView_ = nullptr;
    QListWidget* targetListWidget_ = nullptr;
    QPushButton* moveToTargetButton_ = nullptr;
    QPushButton* sortByOrderButton_ = nullptr;
    QPushButton* addSonicationButton_ = nullptr;
    QPushButton* removeSonicationButton_ = nullptr;
    QComboBox* visitNumberCombo_ = nullptr;
    QPushButton* newVisitButton_ = nullptr;
    QRadioButton* hydrogelSmallRadio_ = nullptr;
    QRadioButton* hydrogelMediumRadio_ = nullptr;
    QRadioButton* hydrogelLargeRadio_ = nullptr;
    QSpinBox* currentSonicationNumberEdit_ = nullptr;
    QComboBox* accFlagCombo_ = nullptr;
    QPushButton* computeBestTargetsButton_ = nullptr;
    QListWidget* bestTargetsList_ = nullptr;
    QLabel* exampleTargetImage_ = nullptr;
    QLabel* exampleTargetImage2_ = nullptr;
    QLabel* exampleTargetImage3_ = nullptr;
    QLabel* exampleTargetText_ = nullptr;
    beam::gui_qt::PulseWaveformView* pulsePlotView_ = nullptr;
    beam::gui_qt::PulseWaveformView* burstPlotView_ = nullptr;
    beam::gui_qt::TotalSonicationView* timelineView_ = nullptr;
    WorkflowMriView* treatmentSagittalPreview_ = nullptr;
    WorkflowMriView* treatmentCoronalPreview_ = nullptr;
    WorkflowMriView* treatmentAxialPreview_ = nullptr;
    QSlider* treatmentSagittalSlider_ = nullptr;
    QSlider* treatmentCoronalSlider_ = nullptr;
    QSlider* treatmentAxialSlider_ = nullptr;
    // Treatment execution body: that tab's firing half.
    QWidget* treatmentExecutionBody_ = nullptr;
    QLabel* treatmentExecutionTitleLabel_ = nullptr;
    QLabel* treatmentExecutionDescriptionLabel_ = nullptr;
    QLabel* treatmentExecutionStatusLabel_ = nullptr;
    QProgressBar* treatmentExecutionProgressBar_ = nullptr;
    QPushButton* startTreatmentButton_ = nullptr;
    QPushButton* abortTreatmentButton_ = nullptr;
    QPushButton* shamButton_ = nullptr;
    QPushButton* finishTreatmentButton_ = nullptr;
    QComboBox* serialPortCombo_ = nullptr;
    QPushButton* serialConnectButton_ = nullptr;
    QLabel* serialConnectedLamp_ = nullptr;
    QComboBox* triggerModeCombo_ = nullptr;
    QLabel* sonicationCountdownLabel_ = nullptr;
    QTimer* sonicationCountdownTimer_ = nullptr;
    int sonicationCountdownSeconds_ = 0;
    int sonicationCountdownTicks_ = 0;
    std::unique_ptr<beam::serialcom::SerialPort> serialLink_;
    // The through-transmit amplitude the Correction stage measured.
    // prepareSonication compares it against kCouplingThreshold, so it has to
    // outlive that measurement rather than only its pass/fail verdict.
    double transmissionAmplitude_ = 0.0;
    QLabel* safetyReviewTitleLabel_ = nullptr;
    QLabel* safetyReviewDescriptionLabel_ = nullptr;
    QLabel* safetyReviewStatusLabel_ = nullptr;
    QCheckBox* safetyRegistrationCheckBox_ = nullptr;
    QCheckBox* safetyCorrectionCheckBox_ = nullptr;
    QCheckBox* safetyPlanCheckBox_ = nullptr;
    QPushButton* acceptSafetyReviewButton_ = nullptr;
};

}  // namespace beam::app
