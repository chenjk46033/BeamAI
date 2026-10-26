#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPointF>
#include <QMarginsF>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gui/case_report_form.hpp"
#include "gui/patient_registry.hpp"
#include "gui/treatment_session.hpp"
#include "gui/treatment_log.hpp"
#include "gui/treatment_target_store.hpp"
#include "treatment_library_dialog.hpp"
#include "gui/treatment_workflow.hpp"
#include "array/array_types.hpp"
#include "mri/fiducial_detect.hpp"
#include "mri/slice.hpp"
#include "mri/ras_transform.hpp"
#include "registration/array_transform.hpp"
#include "stimulation/calibration_curve.hpp"
// Needed complete, not forward-declared: targetSonications_ stores vectors of
// StimParamRow by value.
#include "gui_qt/stim_param_table_model.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class WorkflowShell; }
class QChartView;
class QHBoxLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
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
struct WorkflowMriMarker;
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
class SerialLink;
class SerialPort;
struct SonicationTiming;
}  // namespace beam::serialcom

namespace beam::app {

class DeviceLinkPanel;

// The 3D window this fills; defined in scene_3d_view.hpp.
class Scene3DView;

// The Targeting Examples panes: pan and zoom in place, example_image_view.hpp.
class ExampleImageView;

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
    // Dark text on a light field. Anything built after the constructor has to
    // ask for this, or it is typed into invisibly.
    static void styleInputField(QLineEdit* edit);
    void selectStage(beam::gui::WorkflowStage stage);
    // Navigates to workflow_.nextStage() by a row the list actually shows:
    // four stages are folded into other tabs and their rows are hidden.
    void goToNextStage();
    void completeCurrentStage();
    // Where the three imaging dialogs open: the last place one was used,
    // falling back to the MRI library.
    QString imagingStartDirectory() const;
    void rememberImagingDirectory(const QString& path);

    void chooseNifti();
    void chooseDicomDirectory();
    void loadMri(const QString& path);
    void installMri(beam::mri::Volume3D volume, beam::mri::RasAxisVectors axes, const QString& path);
    void showMriPreviews();
    // The sixteen lock positions, drawn together so the choice is visible.
    void appendLockPositionOverlay(std::vector<WorkflowMriMarker>& sagittal);
    void setLockOverlayShown(bool shown);
    void recomputeLockOverlayTargets();
    // Fills the 3D window: slices where they sit, the array, the targets.
    void buildScene(Scene3DView* view);
    void openSceneWindow();
    void resetMriViews();
    void rebuildFocusImage();
    void initializeRegistrationGeometry();
    void populateRegistrationTable();
    void navigateToRegistrationFiducial(int row);
    // Not focusTreatmentViewsOn: that writes targetMm_, and looking at a
    // marker must not edit the planned target.
    // announce: the operator picked it, so say so. Entering the stage moves
    // here on its own and should not narrate it.
    void navigateToFiducialOnTreatment(int index, bool announce = true);
    // False when fewer than six fiducials exist.
    bool fiducialDiagramData(std::array<QString, 6>* labels,
                             std::array<QPointF, 6>* apIsMm,
                             std::array<double, 6>* lrMm = nullptr) const;
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
    void updateCouplingCharts(double transmissionAmp);
    // Throws the measurement away -- the plots, the numbers and the reading
    // treatment would have used -- so the next Run starts from nothing.
    void clearCouplingMeasurement();
    // The top-bar ABORT follows one thing only: whether something is being
    // delivered right now. It is on every page, so it is the one control that
    // has to mean the same thing everywhere.
    void updateAbortButton();
    // checkSonicationSafety.m: re-run every time a parameter is typed, and
    // the gate on whether anything may be sent.
    void updateSonicationSafety();
    // updateSonicateSettingsForCurrentSonication.m: the schedule's Duration
    // goes onto the grid when a sonication becomes the current one.
    void applyScheduledDurationToGrid();
    // The same rules over every sonication still to run, so one that cannot
    // be delivered is visible before the visit reaches it. Advisory: what
    // stops a delivery is still the check against the grid at fire time.
    void precheckPendingSonications();
    // It rewrites the rows it read, so it must not answer its own change.
    bool prechecking_ = false;
    // Writes a refused attempt into the row's notes.
    void recordRefusedSonication(bool sham);
    // SonicateButtonPushed ends with `if mod(i,4)==0 RunCorrectionButtonPushed`:
    // coupling is re-measured every fourth delivery, because gel dries and
    // the subject moves and the figure taken at the start of the visit goes
    // stale. `delivered` is the sonication number that just ran.
    void remeasureCouplingIfDue(int delivered);
    static constexpr int kCouplingRecheckEvery = 4;
    // Set while the periodic re-measurement is running. A measurement the
    // operator asked for re-opens the Correction stage for acceptance; one
    // the treatment takes on its own must not, or every fourth sonication
    // would tear down the workflow it is part of.
    bool autoCouplingRecheck_ = false;
    // The shipped Target List, as installed. Never written to, so it is what
    // "the original value" means: a parameter edited past recognition has
    // somewhere to come back from.
    beam::gui::TreatmentTargetStore shippedTargets_;
    void loadShippedTargetStore();
    // Puts the selected target's parameters back to the shipped ones.
    void restoreShippedParameters();
    // The shipped value of one grid cell, or nothing when there is no
    // shipped target behind it.
    std::optional<double> shippedValueFor(int column) const;
    // "Shipped value: 0.03 (now 0.5)", or nothing for a cell the install has
    // no value for.
    QString shippedTooltip(const QModelIndex& cell) const;
    bool deliveryRunning() const;
    void setExecutionStatus(const QString& text, bool isError = false);
    void showMessage(const QString& text, bool error);
    void syncMriViewerHeights();
    void capWindowToScreen();

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
    // The install's own files -- the protocol/target library and the list of
    // people treated on it -- live in treatmentDataDirectory(). What belongs
    // to one person lives here, under them.
    QString patientDataDirectory() const;
    QString patientsPath() const;
    // The operator's device-wide copy of a data file, else the shipped seed.
    QString shippedDataFile(const QString& fileName) const;
    QString locateDataFile(const QString& fileName) const;
    // This patient's own Target List. Empty when nobody is named.
    QString patientTargetsPath() const;
    void loadPatientRegistry();
    void savePatientRegistry();
    // The list of people on this device, on the Case page. Typing the exact
    // Participant ID was the only way in, with nothing on screen to say who
    // was already on file.
    void buildPatientChooser();
    void refreshPatientChooser();
    // The registry id on one chooser row. Rows are filtered and sorted, so
    // their position no longer says which patient they are.
    int patientIdAtRow(int row) const;
    // Builds the Case page demographics fields; fills them from a patient.
    // The Report stage: the case report for one visit, and the file that
    // carries it off the device.
    void buildReportBody();
    void refreshReport();
    void exportCaseReport();
    void exportCaseReportPdf();
    // The portable session: one subject plus the imaging and registration
    // needed to resume. See gui/treatment_session.hpp.
    void saveTreatmentSessionFile();
    void chooseTreatmentSession();
    void loadTreatmentSessionFile(const beam::gui::TreatmentSessionFile& session,
                                  const QString& path);
    QString patientFileText(const QString& name) const;
    QString caseReportHtml(int visit) const;
    // What an exported report is called: BeamV0's computeCrfAutoSaveFilename,
    // sanitised for the filesystem. One place, so the format is one edit.
    QString caseReportFileStem(const beam::gui::Patient& patient,
                               const beam::gui::CaseReportForm& crf, int visit) const;
    // Where this patient's reports are kept, and what is already in there.
    QString patientReportsDirectory() const;
    void refreshPastReports();
    beam::gui::CaseReportForm reportFormForVisit(int visit) const;
    void buildCaseDemographics();
    void showCaseDemographics(const beam::gui::Patient& patient);
    // What the Case page currently says about who is being treated.
    beam::gui::Patient caseDemographics() const;
    // Blank, or YYYY-MM-DD. False for anything else, so a typo is reported
    // rather than quietly dropped.
    static bool readDateOfBirth(const QString& text, std::string* out);
    // Edit, or delete, the patient on one row of the chooser.
    void showPatientMenu(int row, const QPoint& globalPosition);
    void editPatient(int patientId);
    void deletePatient(int patientId, int plannedRows);
    // How many sonications this patient has had. Zero is the gate on deleting
    // them. plannedRows, when given, counts the rows that never fired.
    int patientDeliveryCount(int patientId, int* plannedRows) const;
    // Opens the record of whoever the Case page names, writing out whoever was
    // open before. Adds them if this device has not treated them.
    // advancing: the caller is leaving the Case page, so the message says to
    // load an MRI next. Otherwise it points at the list, which has just
    // changed and is the reason to have stayed.
    void selectPatient(const beam::gui::Patient& details, bool advancing);
    // Both Case buttons agree about what is acceptable by asking this.
    bool caseFieldsUsable();
    // Reads this patient's log and visit state in place of whoever was there,
    // and puts the treatment plan on their visits.
    void loadPatientData();
    void savePatientData();
    // Moves a pre-patient install's record under the first patient identified
    // on it. Idempotent: it moves what is there and nothing else.
    void adoptLegacyPatientData(int patientId);
    // Throws away everything measured on a person -- their MRI, the fiducials
    // found in it, the registration fitted to them and the array position it
    // placed -- putting those pages back to the state the application starts
    // in. A different patient's head must not be on screen.
    void clearPatientImagingAndRegistration();
    QString currentProtocolName() const;
    void loadTreatmentTargetStore();
    void refreshProtocolCombo(const QString& select);
    void writeProtocolGridToStore();
    // announce: the "log updated at ..." confirmation. The log saves itself on
    // every change with no button to press, so normally the operator has to be
    // told it happened. A save the application takes on its own -- closing one
    // patient to open another -- is not something they asked for, and saying so
    // buries what they did ask about.
    void saveTreatmentTargetStore(bool announce = true);
    int currentVisitNumber() const;
    void recordSonicationStart(const beam::gui_qt::StimParamRow& row, bool sham,
                               std::optional<double> attenuation,
                               std::optional<double> dutyCycle);
    void closeOpenLogRow(bool aborted = false);
    // Sonicate always runs the treatment table row for the current sonication
    // number; this puts the Target List back on it before firing.
    bool selectScheduledTarget();
    static QByteArray wavFromSamples(const std::vector<double>& samples, int sampleRate);
    void playShamAudio(const std::vector<double>& samples, int sampleRate);
    void stopShamAudio();
    void advanceToNextScheduledSonication();
    void showScheduleRowMenu(const QPoint& where);
    void placeSonicateButton(int oneBased);
    bool currentSonicationIsPending() const;
    void setSonicateEnabled(bool enabled);
    void paintRunningButton();
    void selectScheduledSonication(int oneBased);
    void refreshVisitCombo();
    // Selects a visit by number, whatever the item happens to read.
    void selectVisit(int visit);
    // Whether that visit has been created: it has rows, or it was created
    // empty and is remembered as such.
    bool visitExists(const QString& protocol, int visit) const;
    // Creates the next empty visit of the current protocol and goes to it.
    void startNewVisit();
    // Drops a visit that has delivered nothing, and goes to whatever is left.
    void deleteVisit(int visit);
    // Points the treatment sonications still to come at whichever screened
    // targets scored best. BeamV0 does this after every delivery.
    void applyTargetRanking(bool quiet = false);
    // The same, skipped when the operator has turned ranking off.
    void applyTargetRankingIfAutomatic();
    bool automaticRankingOn() const;
    // Says which rows the plan screens with and which it treats with, read
    // off the durations by the same scan the ranking uses.
    void updatePlanBlocksLabel();
    // Opens the note on one sonication for editing. A note is a running
    // record, so the box opens on a fresh stamped line.
    void editRowNotes(int sequence);
    // Which visit a protocol should open on: visits belong to a protocol, so
    // the number showing for the last one means nothing here.
    int visitForProtocol(const std::string& protocol) const;
    // visitSonication_ outlives the session: the operator sets where a visit is
    // and expects to find it there after a restart.
    void loadVisitState();
    void saveVisitState() const;
    void markVisitEdited(const QString& protocol, int visit);
    // One scheduled sonication, as the row a visit holds: the target's own
    // parameters, with the schedule contributing only the duration.
    beam::gui::TreatmentLogRow rowForTarget(const std::string& target,
                                            std::optional<double> durationSeconds) const;
    // Appends every sonication of the chosen protocol to the current visit.
    // Puts every sonication of a chosen protocol into this visit, after the
    // given row. Zero, or a row nothing may follow, puts them at the end.
    void addProtocolToVisit(int afterSequence);
    // Appends one sonication, the target chosen by the operator.
    void addRowToVisit();
    QString visitStatePath() const;
    static QString visitKey(const QString& protocol, int visit);
    void writeTargetPointsToStore();
    void openTreatmentLibrary(TreatmentLibraryDialog::Page page);
    void applyTreatmentLibrary(const beam::gui::TreatmentTargetStore& edited);
    void selectSonicationRow(int row);
    int selectedSonicationRow() const;
    void focusTreatmentViewsOn(const Eigen::Vector3d& positionMm);
    void centreViewsOnTarget();
    void updateSonicationPlots();
    void updateBestTargets();
    void updateExampleTargetImage();

    // Treatment stage: the firing half of that tab, gated behind Safety
    // Review by the workflow model rather than by a button's enabled state.
    void buildTreatmentExecutionBody(const QString& acceptButtonStyle);
    void buildDeviceLinkPanel();
    void loadCalibrationCurve();
    QString mriGeometryFingerprint() const;
    QString fiducialsAsText() const;
    std::string calibrationDutyText() const;
    std::string calibrationPressureText() const;
    void startCorrectionMeasurement(std::function<void()> then);
    void applyCorrectionResult(const std::vector<std::string>& reply, bool fromDevice);
    void reportStimEcho(const std::string& reply, const beam::serialcom::SonicationTiming& timing,
                        double dutyCycle);
    void runSonication(bool sham);
    void startSonicationCountdown(int durationSeconds, bool sham);
    void abortSonication();
    void finishTreatmentStage();

    // app.sys.RTT(1).couplingThreshold. beam_app's port uses the same value.
    static constexpr double kCouplingThreshold = 0.10;
    // Height the Treatment plan body is guaranteed under the MRI viewers; it
    // scrolls below that rather than squeezing them. syncMriViewerHeights
    // reserves it so the viewers stay the same size as on the other stages.
    // Raised in step with the Treatment viewers shrinking: the page is at its
    // minimum height, so the plan body can only grow by taking from them.
    static constexpr int kTreatmentBodyMinHeight = 360;
    // Fixed viewer height for the Treatment plan stage, which shares its page
    // with the whole Sonicate body. Small enough that the body fits under it
    // on an ordinary screen, large enough to place a target in.
    // A fixed ceiling, not one derived from the page: these viewers are
    // Fixed, so a page that asks for more than the window holds grows the
    // window, which raises the budget, which asks for more again.
    // 80% of the shared Imaging/Registration height: the Treatment page also
    // carries the plan body and the execution controls below these viewers, and
    // at the full height the Sonicate row fell off the bottom of a 1080p screen.
    static constexpr int kTreatmentViewerHeight = 420;
    // The Treatment page's viewers may not squeeze the plan below this.
    static constexpr int kMinTreatmentViewerHeight = 240;
    // What sits under them: the one-line status row with Finish on its end.
    static constexpr int kTreatmentControlsHeight = 54;
    // treatmentProtocolTables(i).sessions is 15 long in BeamV0.
    static constexpr int kMaxTreatmentVisits = 15;
    // Every action button on the Registration page stands this tall, so the two
    // step rows line up across the page.
    static constexpr int kRegistrationButtonHeight = 30;
    // The visit list's last item, which makes a visit rather than being one.
    static constexpr int kNewVisitItem = -1;

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
    // Found for the operator, not placed by hand.
    std::array<bool, 6> fiducialDetected_{};
    // 0 when placed by hand or imported rather than detected.
    std::array<double, 6> fiducialConfidence_{};
    std::array<double, 6> fiducialStability_{};
    Eigen::Vector3d targetMm_ = Eigen::Vector3d::Zero();
    bool registrationGeometryLoaded_ = false;
    RegistrationPhase registrationPhase_ = RegistrationPhase::Locating;
    QString lastImagingDirectory_;
    bool deviceCheckPassed_ = false;
    bool mriLoaded_ = false;
    bool lockOverlayShown_ = false;
    // Sixteen registrations is most of a second, so they are computed when the
    // fit changes rather than on every toggle.
    std::vector<beam::registration::LockPositionTarget> lockOverlayTargets_;
    QWidget* sceneWindow_ = nullptr;
    Scene3DView* sceneView_ = nullptr;
    bool focusImageLoaded_ = false;
    bool suppressRegistrationNavigation_ = false;
    bool couplingCheckPassed_ = false;
    bool correctionCheckPassed_ = false;
    int registrationStartFiducialRow_ = -1;
    QString mriPath_;
    // Dimensions and affine of the loaded scan, so a delivery can name the
    // image its RAS coordinates came from.
    QString mriFingerprint_;
    QHash<QString, QString> sliceSliderTips_;
    QSlider* leftHorizontalPositionSlider_ = nullptr;
    QSlider* leftVerticalPositionSlider_ = nullptr;
    QSlider* rightHorizontalPositionSlider_ = nullptr;
    QSlider* rightVerticalPositionSlider_ = nullptr;
    QPushButton* registerCurrentPositionButton_ = nullptr;
    QPushButton* detectFiducialsButton_ = nullptr;
    QToolButton* fiducialActionsButton_ = nullptr;
    // The picker is rebuilt per right-click, so the pick is remembered here.
    int treatmentFiducialPick_ = -1;
    QLabel* arrayPositionLabel_ = nullptr;
    QPushButton* moveToArrayPositionButton_ = nullptr;
    QLabel* couplingStatusLabel_ = nullptr;
    QLabel* couplingTitleLabel_ = nullptr;
    QLabel* couplingDescriptionLabel_ = nullptr;
    QLabel* correctionTitleLabel_ = nullptr;
    QLabel* correctionDescriptionLabel_ = nullptr;
    QLabel* correctionStatusLabel_ = nullptr;
    QProgressBar* couplingProgressBar_ = nullptr;
    QProgressBar* correctionProgressBar_ = nullptr;
    QPushButton* runCouplingCheckButton_ = nullptr;
    QPushButton* clearCouplingButton_ = nullptr;
    QPushButton* acceptCouplingButton_ = nullptr;
    QPushButton* runCorrectionButton_ = nullptr;
    QPushButton* acceptCorrectionButton_ = nullptr;
    QLabel* treatmentPlanTitleLabel_ = nullptr;
    QLabel* treatmentPlanDescriptionLabel_ = nullptr;
    QLabel* treatmentProtocolLabel_ = nullptr;
    QComboBox* treatmentProtocolCombo_ = nullptr;
    QTabWidget* treatmentTabs_ = nullptr;
    QWidget* treatmentMriPage_ = nullptr;
    int mriHeightSyncPasses_ = 0;
    // app.sys.protocolTables: each target's own stimParamTable data, keyed by
    // the target row's stable id so a rename or insert cannot orphan it.
    QHash<int, std::vector<beam::gui_qt::StimParamRow>> targetSonications_;
    int currentTargetId_ = -1;
    // Which target the sonication grid is actually showing. Only equal to
    // currentTargetId_ once the grid has been refilled for it; caching while
    // they differ writes one target's parameters onto another.
    int gridTargetId_ = -1;
    int nextTargetId_ = 0;
    QSize lastMriSyncWindowSize_;
    // The stack height this window can afford, learned before any page has
    // forced the window past the screen. Relearned when the operator resizes.
    int maxStackHeight_ = 0;
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
    // What was actually delivered, per visit. The store above is the plan.
    // Holds ONE patient's rows: the switch writes them out and reads the next
    // patient's in, so there is never a second patient's row in here to leak.
    beam::gui::TreatmentLog treatmentLog_;
    // Everybody this device has treated.
    beam::gui::PatientRegistry patients_;
    QGroupBox* patientChooserGroup_ = nullptr;
    QTableWidget* patientTable_ = nullptr;
    QLabel* patientChooserHint_ = nullptr;
    QLineEdit* patientFilterEdit_ = nullptr;
    QLabel* patientCountLabel_ = nullptr;
    // Demographics, on the Case page beside the Participant and Site IDs.
    // All three may be left blank: somebody can be enrolled and treated
    // before their details are to hand.
    QLineEdit* lastNameEdit_ = nullptr;
    QLineEdit* firstNameEdit_ = nullptr;
    QLineEdit* dateOfBirthEdit_ = nullptr;
    QPushButton* createPatientButton_ = nullptr;
    QWidget* reportBody_ = nullptr;
    QComboBox* reportVisitCombo_ = nullptr;
    QLineEdit* reportOperatorEdit_ = nullptr;
    QLabel* reportIdentityLabel_ = nullptr;
    QTableWidget* reportTable_ = nullptr;
    QPushButton* exportReportButton_ = nullptr;
    QPushButton* exportReportPdfButton_ = nullptr;
    QPushButton* saveSessionButton_ = nullptr;
    QPushButton* saveSessionOnRegistrationButton_ = nullptr;
    QPushButton* overlayAllTargetsButton_ = nullptr;
    QPushButton* saveSessionOnTreatmentButton_ = nullptr;
    QListWidget* pastReportsList_ = nullptr;
    // Whose record is open. Zero until the Case page names somebody -- and on
    // a device that has never named anybody, that is where a pre-patient
    // install's single record still is.
    int currentPatientId_ = 0;
    // The row runSonication opened, so the countdown can stamp its end.
    int openLogSequence_ = 0;
    // Held while PlaySound is reading from it.
    QByteArray shamAudioBuffer_;
    // Which protocol the schedule table currently holds, so a reload of the
    // same one does not throw away the visit's position.
    QString lastLoadedProtocol_;
    int lastLoadedVisit_ = 0;
    // Where each visit of each protocol had got to, keyed by visitKey().
    // Written to treatment_visit_state.csv on every change.
    QHash<QString, int> visitSonication_;
    // Visits whose row list the operator has changed by hand. Kept because a
    // visit emptied of every row is indistinguishable from one never opened --
    // both have no rows -- and seeding the second must not refill the first.
    QSet<QString> editedVisits_;
    QCheckBox* autoRankCheckBox_ = nullptr;
    QLabel* planBlocksLabel_ = nullptr;
    // Visits the operator has turned ranking off for. Absent means on, so a
    // visit written before this existed keeps BeamV0's always-on behaviour.
    QSet<QString> rankingOffVisits_;
    // Set while the ranking is rewriting the plan, so the reload it ends with
    // does not send it round again.
    bool applyingRanking_ = false;
    QPushButton* manageLibraryButton_ = nullptr;
    QTableView* stimParamView_ = nullptr;
    QTableView* protocolView_ = nullptr;
    // Takes the height the table leaves behind while it is hidden.
    QSpacerItem* protocolEmptySpacer_ = nullptr;
    QListWidget* targetListWidget_ = nullptr;
    QPushButton* moveToTargetButton_ = nullptr;
    QPushButton* manageTargetsButton_ = nullptr;
    QComboBox* visitNumberCombo_ = nullptr;
    QHBoxLayout* treatmentBodyRow_ = nullptr;
    Eigen::VectorXd couplingCh0Raw_;
    Eigen::VectorXd couplingCh1Raw_;
    Eigen::VectorXd couplingCh0Filtered_;
    Eigen::VectorXd couplingCh1Filtered_;
    double couplingPeakToPeak_ = 0.0;
    QWidget* couplingChartsRow_ = nullptr;
    QLabel* couplingChartsPlaceholder_ = nullptr;
    // What the measurement came out as, in numbers: the bars say pass or
    // fail, the numbers say by how much.
    QLabel* couplingNumbersLabel_ = nullptr;
    // Whether what is on the plots came off a device or was stood in for.
    bool couplingSimulated_ = false;
    // What checkSonicationSafety.m concluded about the parameters now in the
    // grid, and why. Sonicate and Sham are off while this is false.
    bool sonicationSafetyPass_ = true;
    // Marking the unsafe cells changes the grid, which is what calls this in
    // the first place.
    bool checkingSonicationSafety_ = false;
    QStringList sonicationSafetyMessages_;
    QLabel* sonicationSafetyLabel_ = nullptr;
    QLabel* sonicationPrecheckLabel_ = nullptr;
    // How many measurements this session has taken, so a repeat says so.
    int correctionRunCount_ = 0;
    // A stand-in through-transmit pair, so the page can be read and the
    // workflow walked without a Beam device on the bench.
    void fillSimulatedCouplingWaveforms();
    QChartView* couplingCh0ChartView_ = nullptr;
    QChartView* couplingCh1ChartView_ = nullptr;
    QChartView* couplingBarChartView_ = nullptr;
    QComboBox* hydrogelSizeCombo_ = nullptr;
    // Shown, not typed: sonicating out of order is not something the
    // operator chooses, so the number is a readout and the schedule moves it.
    QLabel* currentSonicationNumberLabel_ = nullptr;
    int currentSonication_ = 1;
    QComboBox* accFlagCombo_ = nullptr;
    QPushButton* computeBestTargetsButton_ = nullptr;
    QLabel* bestTargetsLabel_ = nullptr;
    ExampleImageView* exampleTargetImage_ = nullptr;
    ExampleImageView* exampleTargetImage2_ = nullptr;
    ExampleImageView* exampleTargetImage3_ = nullptr;
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
    QProgressBar* treatmentExecutionProgressBar_ = nullptr;
    // Sonicate lives on the current row of the treatment log, not in a pane.
    // placeSonicateButton owns it; sonicateEnabled_ is the gate it is built
    // with, since the row it sits on comes and goes.
    // QPointer, not a raw pointer: setIndexWidget destroys whatever widget it
    // replaces, so this can be left naming a button Qt has already deleted. It
    // nulls itself instead.
    QPointer<QPushButton> inRowSonicateButton_;
    QPointer<QPushButton> inRowShamButton_;
    QPointer<QPushButton> inRowStopButton_;
    // Not a control: the panel lamp that says the row is live.
    QPointer<QLabel> inRowOnLamp_;
    // While a sonication runs the row button pulses, so a glance at the table
    // says something is going out without reading the countdown. Which colour
    // it pulses in says whether ultrasound or only the mask is going out.
    QTimer* runningFlashTimer_ = nullptr;
    bool runningFlashOn_ = false;
    bool runningSham_ = false;
    bool sonicateEnabled_ = false;
    QPushButton* finishTreatmentButton_ = nullptr;
    // The panel owns the port, its polling thread and its own widgets.
    DeviceLinkPanel* deviceLink_ = nullptr;
    // Read from data/runtime once at startup; BeamV0 carried these inline.
    beam::stimulation::CalibrationCurve calibration_;
    bool calibrationFromFile_ = false;
    // The shell's header slot, so a link that drops during treatment is
    // visible on whatever page the operator is on.
    QLabel* serialHeaderLabel_ = nullptr;
    QComboBox* triggerModeCombo_ = nullptr;
    QLabel* sonicationCountdownLabel_ = nullptr;
    QTimer* sonicationCountdownTimer_ = nullptr;
    int sonicationCountdownSeconds_ = 0;
    // When the running sonication is due to end, as a wall clock. Counting
    // ticks instead let the run overshoot what the row asked for.
    qint64 sonicationDeadlineMs_ = 0;
    // Once a pre-delivery check fails the button stays dead until the
    // device answers again.
    bool mcuConnectionInvalid_ = false;
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
