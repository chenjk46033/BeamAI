#pragma once

#include <functional>
#include <optional>

#include <QMainWindow>
#include <QString>
#include <Eigen/Core>

#include "gui/initial_placement_presenter.hpp"
#include "gui/registration_check_presenter.hpp"
#include "gui/safety_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/top_targets.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// The app shell: a QMainWindow with the top-level tab structure of
// BeamV0.mlapp's `TabGroup` -- Sonicate, Register, Correction (that order).
// Tabs are populated by the caller from libs/gui presenters + libs/gui_qt
// chart builders; tabs with nothing ported yet show a placeholder.

class QTabWidget;
class QTableView;
class QPushButton;
class QComboBox;
class QListWidget;
class QLabel;
class QSlider;
class QTimer;
class QVBoxLayout;
class QGridLayout;
class QChart;
class QWidget;
class QCheckBox;
class QLineEdit;
class QRadioButton;
class QResizeEvent;

namespace beam::gui_qt {

class SafetyReportView;
class StimParamTableModel;
class TreatmentProtocolTableModel;
class FiducialTableModel;
class MriSliceView;

class BeamMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit BeamMainWindow(QWidget* parent = nullptr);

    // Sonicate tab: the pre-sonication safety report
    // (checkSonicationSafety.m's SystemStatusTextArea).
    void setSafetyReport(const beam::gui::SonicationSafetyReport& report);

    // Sonicate tab: the pulse-waveform preview
    // (updateSonicationPlots.m's axPulseWaveformPlot). Takes ownership of
    // the QChart; replaces any previous one.
    void setPulseWaveformChart(QChart* chart);

    // Sonicate tab: the burst/pause timeline for the whole sonication
    // duration (GeneralSonication/setBurstEventsFromStimParams.m's
    // `eventVector` -- updateSonicationPlots.m's "burst/total-sonication
    // plots", not otherwise ported; see docs/known_gaps_gui.md). Takes
    // ownership of the QChart; replaces any previous one.
    void setSonicationTimelineChart(QChart* chart);

    // Sonicate tab: the "Sonicate" button + Immediate/External trigger
    // selector (Stimulation/GeneralSonication/generalSonicateMaster.m's
    // `app.TriggerSwitch`). `onSonicate` is called with
    // waitForExternalTrigger = (combo == "External") on click -- this
    // widget owns no serial/stimulation-math dependency itself, so the
    // real decision (beam::gui::prepareSonication) and the serial send
    // happen in the caller. Does not take ownership of anything.
    void setSonicateHandler(std::function<void(bool waitForExternalTrigger)> onSonicate);

    // Sonicate tab: free-text result of the last Sonicate attempt (pass/
    // fail messages, duty cycle, whether it's waiting on an external
    // trigger, or a serial-transport error) -- separate from the
    // continuously-updated safety report above it.
    void setSonicationStatus(const QString& text);

    // Sonicate tab: the "Sham" button, next to "Sonicate"
    // (Stimulation/GeneralSonication/unfocusedSonicate.m -- masking audio
    // + a countdown, no real ultrasound; see beam::gui::prepareShamSonication).
    // `onSham` is called with no arguments; the real computation
    // (duration + beam::sham::setShamAudio) is the caller's job, same
    // division of labor as setSonicateHandler. Does not send anything over
    // serial -- the source's own unfocusedSonicate.m doesn't either.
    void setShamHandler(std::function<void()> onSham);

    // Sonicate tab: app.SerialPortDropDown/SerialConnectButton/
    // ConnectedLamp -- a real, persistent serial connection the operator
    // opens explicitly (connectSerial.m), separate from the fire-and-
    // forget per-command connect this port's Sonicate/Sham/Correction
    // handlers already do on their own. `ports` replaces the dropdown's
    // full item list (beam::serialcom::listAvailableComPorts(), the
    // caller's job to call). `onSerialConnect` fires with the currently-
    // selected port name; the caller opens/closes the real
    // beam::serialcom::SerialPort and reports success via
    // setSerialConnectedState -- this widget owns no serialcom dependency
    // itself, same division of labor as setSonicateHandler.
    void setSerialPorts(const std::vector<QString>& ports);
    void setSerialConnectHandler(std::function<void(QString)> onSerialConnect);
    void setSerialConnectedState(bool connected);

    // Sonicate tab: app.GetParamsButton -- the source's own action
    // (setStimParamsFromApp + assignin('base','app',app)) is MATLAB-
    // workspace-debugging glue with no equivalent in a compiled app, so
    // `onGetParams` is this port's own substitute for whatever the
    // caller wants that button to do instead (see main.cpp for what, if
    // anything, it's wired to).
    void setGetParamsHandler(std::function<void()> onGetParams);

    // Sonicate tab: app.AbortSonicationButton -- real
    // (AbortSonicationButtonPushed.m sends the literal command
    // "Correction" over serial, same as the Correction tab's own ping;
    // preserved exactly, not corrected to a differently-named command).
    // `onAbortSonication` is called with no arguments; the real serial
    // send is the caller's job, same division of labor as
    // setSonicateHandler.
    void setAbortSonicationHandler(std::function<void()> onAbortSonication);

    // Sonicate tab: starts the countdown display (GUI/UIFeatures/
    // startStandaloneCountdown.m / updateFigureTimer.m -- see
    // beam::gui::countdown_presenter for the ported tick/format logic;
    // this owns only the QTimer mechanics, matching every other Qt/libs
    // split here). Ticks every 2 seconds (the source's hardcoded period)
    // until beam::gui::isCountdownDone. Starting a new countdown while
    // one is already running restarts it from `durationSeconds`.
    void startSonicationCountdown(int durationSeconds);

    // Exposed so `apps/beam_app --check` can confirm the countdown label
    // actually reflects beam::gui::countdownDisplayText, not just that
    // the presenter function itself is correct (already gtest-covered).
    QString sonicationCountdownText() const;

    // Sonicate tab: the stimParamTable grid (createStimParamTable.m /
    // setStimParamTableNames.m) + a "Sort by #" button
    // (sortSonicationTable.m). Does not take ownership -- the caller keeps
    // the model alive (typically parented to this window or itself).
    void setStimParamTableModel(StimParamTableModel* model);

    // Sonicate tab, "Treatment Protocol" sub-tab: the treatmentProtocolTable
    // grid (createTreatmentProtocolParamTable.m /
    // setTreatmentProtocolTableNames.m), an ACC-region selector, a "Compute
    // Best Targets" button, and the resulting best-targets list --
    // getTopTargetsFromTreatmentProtocolTable.m's real caller. Does not
    // take ownership of `model`.
    void setTreatmentProtocolTableModel(TreatmentProtocolTableModel* model);

    // Sonicate tab, "Treatment Protocol" sub-tab: the protocol/visit
    // selector (BeamV0.mlapp's TreatmentProtocolDropDown -- the 3 real,
    // fixed regimen names -- and VisitNumberListBox, simplified to a
    // second QComboBox here) and "New Visit" button
    // (NewVisitButtonPushed's real action, `addTreatmentProtocolSession.m`
    // -- its `uiconfirm` prompt isn't reproduced, see
    // docs/known_gaps_gui.md). `onSelectionChanged` fires with the newly-
    // selected protocol name + visit number whenever either combo
    // changes; loading the right beam::gui::TreatmentSession's rows into
    // setTreatmentProtocolTableModel is the caller's job, same division
    // of labor as setSonicateHandler. `onNewVisit` is called with no
    // arguments; the real beam::gui::addTreatmentProtocolSession call and
    // refreshing the visit list (setVisitNumbers) are the caller's job.
    void setTreatmentProtocolNames(const std::vector<QString>& names);
    void setVisitNumbers(const std::vector<int>& visitNumbers, int currentVisitNumber);
    void setTreatmentProtocolSelectorHandler(std::function<void(const QString&, int)> onSelectionChanged);
    void setNewVisitHandler(std::function<void()> onNewVisit);
    QString currentTreatmentProtocolName() const;
    int currentVisitNumber() const;

    // Sonicate tab, "Treatment Protocol" sub-tab: fires whenever the
    // treatmentProtocolTable grid's data changes (a cell edit) --
    // setTreatmentProtocolTableData.m's write-back half
    // (beam::gui::setCurrentSessionRows, keyed by
    // currentTreatmentProtocolName()/currentVisitNumber() at edit time)
    // is the caller's job; its row-coloring half is already wired via
    // TreatmentProtocolTableModel::computeBestTargets. Must be called
    // after setTreatmentProtocolTableModel (connects to that model).
    void setTreatmentProtocolDataChangedHandler(std::function<void()> onDataChanged);

    // Correction tab: the three CorrectionTab charts. Takes ownership of the
    // QChart pointers (wraps each in a QChartView). Replaces any previous
    // set. Pass nullptr for a chart to leave that cell empty.
    void setCorrectionCharts(QChart* transmissionBars, QChart* rfFirstArray, QChart* rfSecondArray);

    // Correction tab: the "Run Correction" button
    // (Correction/getRecieveWaveformFromSerial.m -- see the operator
    // manual's Correction Tab section; called from `RunCorrectionButtonPushed`).
    // `onRunCorrection` is called with no arguments; the real serial read +
    // butterBandpass/filterIir pipeline and the resulting chart rebuild are
    // the caller's job, same division of labor as setSonicateHandler /
    // setRegisterHandler.
    void setRunCorrectionHandler(std::function<void()> onRunCorrection);
    void setCorrectionStatus(const QString& text);

    // Register tab: the fiducial marker grid (setFiducialROIs.m's live
    // path). Replaces the tab's placeholder the first time this is called.
    // Does not take ownership of `model`.
    void setFiducialTableModel(FiducialTableModel* model);

    // Register tab: the sagittal/coronal/axial MRI slice views
    // (GUI/MRI/getSliceImage.m via MriSliceView). Copies `volume` into
    // each of the three views.
    void setMriVolume(const beam::mri::Volume3D& volume);

    // Register tab: the array-footprint / fiducial-marker / focus-ellipsoid
    // overlay on the MRI slice views (GUI/drawTransducersOnMRI.m's
    // rasterization + GUI/RegistrationTab/AutoReg/setFiducialTemplate.m's
    // ellipsoid, plus new fiducial-marker plumbing -- see
    // beam::gui::mri_overlay_presenter). Pass default-constructed
    // Volume3Ds to clear an overlay.
    void setMriOverlays(const beam::mri::Volume3D& arrayMask, const beam::mri::Volume3D& fiducialMask,
                        const beam::mri::Volume3D& focusMask);

    // drawFocusOnMRI.m's centerArrayMM -- also, per drawROIs.m, the one
    // position every currently-shown target-row crosshair is drawn at
    // (see refreshTargetCrosshairs()'s comment). Call whenever it changes
    // (startup, a fresh MRI load, or either Register button's success).
    void setArrayCenterMm(const Eigen::Vector3d& centerArrayMm);

    // Jumps the Sagittal slider to a real mm position -- for showing the
    // array/target crosshair right away at startup, without the operator
    // having to search for the one exact slice that contains it (user:
    // "I want it to show right away without user clicking to search for
    // it"; then, explicitly, "I just want that applied to sagital view" --
    // Coronal/Axial keep their own default). Same underlying
    // setSliderValueMm as the "Move to" fiducial radios, just driven
    // directly rather than from a click.
    void centerSagittalSliceOnMm(double xMm);

    // The MRI slice views' physical-mm axis tick labels (GUI/drawMrImages.m's
    // xdata/ydata) -- std::nullopt (the default) draws no ticks, matching
    // the synthetic volume's lack of physical calibration.
    void setMriAxes(std::optional<beam::mri::RasAxisVectors> axes);

    // The persistent Show Transducers/Target/Field checkboxes
    // (`app.ShowTransducersCheckBox`/`ShowTargetCheckBox`/`ShowFieldCheckBox`
    // -- confirmed parented to `app.UIFigure` directly in BeamV0.mlapp,
    // same as the MRI axes themselves, not to any tab) are built and
    // wired entirely internally (see the .cpp): ShowTransducers gates the
    // array-footprint overlay (`showMrImage`'s
    // `app.ShowTransducersCheckBox.Value*app.TransducerTransparencySlider.Value*
    // arraySliceImage` alpha); ShowField gates the focus/"field" overlay,
    // preserving a real source quirk -- its alpha is *also* multiplied by
    // ShowTransducersCheckBox.Value (apparently copy-pasted from the
    // array block, not written fresh for this one), so the focus glow
    // only actually renders when *both* are checked, not ShowField alone.
    // ShowTarget gates `drawROIs.m`'s target-ROI drawing in the source,
    // which isn't ported here (deferred, see docs/known_gaps_gui.md) --
    // present as a real widget for structural completeness, but it has
    // nothing of this port's to gate yet. The fiducial-marker overlay is
    // this port's own new plumbing (the source's own fiducial-drawing
    // code in `showMrImage` is dead, commented out) -- tied to
    // ShowTransducers too, a disclosed design choice, not a literal port.
    // No public API needed: setMriOverlays (above) still takes the real,
    // ungated masks; this class stores them and reapplies gating itself
    // whenever a checkbox toggles.

    // Register tab: the "Register To MRI Fiducials" button
    // (Registration/registerArrayToFiducials.m -- see the operator
    // manual's Device to Subject Registration section). `onRegister` is
    // called on click with no arguments; the real math
    // (beam::registration::registerArrayToFiducials) and the resulting
    // array/overlay updates are the caller's job, same division of labor
    // as setSonicateHandler.
    void setRegisterHandler(std::function<void()> onRegister);
    void setRegisterStatus(const QString& text);

    // Register tab: the "Register Arrays to Current Position" button --
    // the operator manual's "Outside the MRI" registration step
    // (Registration/MRINeuroNav/TranslateArrayPosition/registerCurrentTransducerPostion.m).
    // `onRegisterCurrentPosition` is called on click with the horizontal/
    // vertical physical lock-position slider values; the real math
    // (beam::registration::registerCurrentTransducerPosition) is the
    // caller's job, same division of labor as setRegisterHandler.
    void setRegisterCurrentPositionHandler(std::function<void(double horizontalValue, double verticalValue)>
                                                onRegisterCurrentPosition);

    // Registration/setRegistrationCheck.m: the Right/Left/InsideMRI
    // Registration lamps plus the Sonicate button's enable state, driven
    // by beam::gui::computeRegistrationCheckLampState. The caller decides
    // when to recompute this (after each registration button's success).
    void setRegistrationCheckLampState(const beam::gui::RegistrationCheckLampState& state);

    // HorizontalPositionSliderValueChanged.m / VerticalPositionSliderValueChanged.m:
    // moving ANY of the 4 array-lock-position sliders sets
    // app.sys.frame.CurrentRegistrationComplete = 0 and immediately
    // recomputes the registration-check lamps -- the "current position"
    // registration goes stale the instant the operator nudges a slider,
    // not just when they next click a button. `onPositionSlidersChanged`
    // is called with no arguments; the caller owns the completion-flag
    // state and lamp recompute, same division of labor as the other
    // Register tab handlers.
    void setPositionSlidersChangedHandler(std::function<void()> onPositionSlidersChanged);

    // The File -> Save/Load Session actions' underlying conversion,
    // exposed directly so it's callable (and testable via
    // `apps/beam_app --check`) without going through a real QFileDialog.
    // Gathers/applies the fiducial, stimParamTable, and treatmentProtocol
    // grids only -- see docs/known_gaps_gui.md for what's deliberately not
    // included (correction measurements, the per-sonication log).
    beam::gui::SessionData collectSessionData() const;
    void applySessionData(const beam::gui::SessionData& data);

    // Fires after a real "File -> Load Session" successfully applies its
    // data (not after a caller-driven applySessionData(), e.g. in
    // --check's round-trip exercise, which restores the exact same data
    // it just saved). `applySessionData`'s treatmentProtocolModel_->setRows()
    // goes through beginResetModel(), which does NOT emit dataChanged --
    // so a loaded session's treatment-protocol rows would otherwise never
    // reach a beam::gui::TreatmentSession store the caller keeps outside
    // this class (see setTreatmentProtocolDataChangedHandler), and get
    // silently overwritten the next time the protocol/visit selector
    // changes. `onSessionLoaded` is the caller's chance to persist the
    // freshly-loaded treatmentProtocolTableModel()'s rows into that store
    // at the currently-selected protocol/visit slot, the same way a real
    // cell edit would.
    void setSessionLoadedHandler(std::function<void()> onSessionLoaded);

    // File -> Load MRI (GUI/LoadMRIMenuSelected). This class only owns the
    // QFileDialog; the actual load (beam::mri::loadNiftiMriRas) and every
    // downstream reset (fiducials, overlays) is the caller's job, same
    // division of labor as setRegisterHandler -- `apps/beam_app/main.cpp`
    // owns the MRI/array state this needs.
    void setLoadMriHandler(std::function<void(QString path)> onLoadMri);

    // The 3 persistent CRF text fields (`app.SiteIDEditField`/
    // `VisitNumberEditField`/`ParticipantIDEditField`, `app.UIFigure`-parented).
    // Not yet wired to `beam::gui::CaseReportForm`/`computeCrfAutoSaveFilename`
    // (no save-file-name feature exists in this port yet to consume them) --
    // this just makes the real widgets present and their text readable,
    // closing the "field exists in the struct, no UI to fill it in" gap
    // noted in docs/gui_widget_inventory.md.
    QString siteId() const;
    QString visitNumber() const;
    QString participantId() const;
    // app.HydrogelSizeButtonGroup / app.CurrentSonicationNumberEditField --
    // real, read fields (setCRFHydrogelSize.m; CurrentSonicationNumberEditField
    // gates warnForSonicate.m/updateSonicateSettingsForCurrentSonication.m),
    // not yet wired into a save/session or sonicate-gating path here -- see
    // the constructor's HydrogelSizeButtonGroup/CurrentSonicationNumberEditField
    // comment.
    QString hydrogelSize() const;  // "Small" / "Medium" / "Large"
    int currentSonicationNumber() const;
    double transducerTransparency() const;  // 0.0-1.0, see the member's own comment

    // Default-subject CRF values (part of `loadDefaultSys.m`'s ~130 MB
    // `sys` struct this port can't read -- no MAT-file reader -- reported
    // directly by the user from a live BeamV0 session, the same provenance
    // as the default MRI export; see docs/known_gaps_mri.md's
    // `loadDefaultSys.m` entry and docs/gui_widget_inventory.md's "default
    // state" gap). The caller (apps/beam_app/main.cpp) owns calling these
    // at startup, same division of labor as setMriVolume/setMriAxes for
    // the MRI piece of that same default state.
    void setSiteId(const QString& value);
    void setParticipantId(const QString& value);
    void setVisitNumberField(const QString& value);

private:
    void saveSession();
    void loadSession();
    void loadMriFile();
    void loadMriFolder();

    std::function<void()> onSessionLoaded_;
    std::function<void(QString)> onLoadMri_;

    QTabWidget* tabs_;
    SafetyReportView* safetyView_;
    QWidget* sonicateHost_;
    QTableView* stimParamTableView_;
    QPushButton* sortByOrderButton_;
    QPushButton* addSonicationButton_;
    QPushButton* removeMarkedButton_;
    QTableView* treatmentProtocolTableView_;
    QComboBox* accFlagCombo_;
    QPushButton* computeBestTargetsButton_;
    QComboBox* treatmentProtocolCombo_;
    QComboBox* visitNumberCombo_;
    QPushButton* newVisitButton_;
    QListWidget* bestTargetsResultList_;
    QRadioButton* hydrogelSizeSmallRadio_;
    QRadioButton* hydrogelSizeMediumRadio_;
    QRadioButton* hydrogelSizeLargeRadio_;
    QLineEdit* currentSonicationNumberEdit_;
    // app.SonicationsPanel's TargetListListBox + Rename/Remove Selected
    // Protocol/Create New Protocol/Move To Target -- a local list-editing
    // UI only, not the full app.sys.protocolTables CRUD data model
    // docs/known_gaps_gui.md documents this port as deliberately not
    // replicating (setProtocolListBox.m/setProtocolTableWithStimParamTable.m);
    // see the constructor comment for exactly what is and isn't wired.
    QListWidget* targetListListBox_;
    QPushButton* renameButton_;
    QPushButton* removeSelectedProtocolButton_;
    QPushButton* createNewProtocolButton_;
    QPushButton* moveToTargetButton_;
    QVBoxLayout* previewLayout_;
    QVBoxLayout* pulseChartLayout_;
    QVBoxLayout* timelineChartLayout_;
    QComboBox* triggerModeCombo_;
    QPushButton* sonicateButton_;
    QPushButton* shamButton_;
    QLabel* sonicationStatusLabel_;
    QLabel* countdownLabel_;
    QComboBox* serialPortCombo_;
    QPushButton* serialConnectButton_;
    QLabel* connectedLamp_;
    QPushButton* getParamsButton_;
    QPushButton* abortSonicationButton_;
    QTimer* countdownTimer_;
    int countdownDurationSeconds_;
    int countdownTickCount_;
    StimParamTableModel* stimParamModel_;
    Eigen::Vector3d arrayCenterMm_ = Eigen::Vector3d::Zero();
    TreatmentProtocolTableModel* treatmentProtocolModel_;
    FiducialTableModel* fiducialModel_;
    QPushButton* registerButton_;
    QLabel* registerStatusLabel_;
    // registerCurrentTransducerPostion.m reads ONLY the Right slider pair
    // for the actual translation math -- the Left pair exists solely so
    // the GUI can warn when Left/Right disagree (a real, faithfully-
    // preserved source quirk: the source's own alignment check compares
    // them but still proceeds using Right's value either way). Named
    // right*/left* to make that asymmetry visible at the call site,
    // rather than a single ambiguous horizontal*/vertical* pair.
    QSlider* rightHorizontalPositionSlider_;
    QSlider* rightVerticalPositionSlider_;
    QSlider* leftHorizontalPositionSlider_;
    QSlider* leftVerticalPositionSlider_;
    QPushButton* registerCurrentPositionButton_;
    // app.TransducerTransparencySlider -- a real control (default 0.8,
    // matching the source), value exposed via transducerTransparency()
    // but not yet wired into the overlay rendering itself (MriSliceView's
    // array/fiducial overlays are still hard-threshold, not alpha-
    // blended) -- a rendering change out of scope for this layout pass.
    QSlider* transducerTransparencySlider_;
    // app.TabGroup4's 3 tabs (MRI Fiducial Based/MRI Free/Photo Based) and
    // app.RegistrationTypeButtonGroup, which selects among them.
    QTabWidget* registrationTypeTabs_;
    QRadioButton* registrationTypeMriFiducialsRadio_;
    QRadioButton* registrationTypePhotoBasedRadio_;
    QRadioButton* registrationTypeMriFreeRadio_;
    // app.MovetoButtonGroup's 6 named lock-position radios -- real widgets,
    // matching BeamV0's own naming. Wired (moveToFiducialMarkerButtonFunction.m):
    // selecting one jumps all 3 MRI slice sliders to that fiducial's
    // stored position and selects its row in the fiducial table -- the
    // operator manual's own documented first step of fiducial
    // registration, not just internal glue (see the connection site in
    // the .cpp for the full citation).
    QRadioButton* moveToLeftY1Z3Radio_;
    QRadioButton* moveToLeftY1Z1Radio_;
    QRadioButton* moveToLeftY4Z1Radio_;
    QRadioButton* moveToRightY4Z1Radio_;
    QRadioButton* moveToRightY1Z1Radio_;
    QRadioButton* moveToRightY1Z3Radio_;
    // app.MoveFiducialtoCurrentViewButton -- real: writes the 3 MRI
    // views' current slider mm positions into the fiducial table's
    // currently-selected row (there's no draggable-ROI equivalent here,
    // so this is this port's own substitute for the source's on-image
    // drag gesture, not a literal port of any one .m file).
    QPushButton* moveFiducialToCurrentViewButton_;
    // app.LoadWithoutFrameImageButton/LoadWithFrameImageButton +
    // ImageWithoutFrame/ImageWithFrame -- real file loading + display.
    // PhotoBasedRegistrationButton has no real algorithm behind it to
    // call: docs/known_gaps_registration.md confirms the source's own
    // photo-based registration pipeline shells out to an external Python
    // face-de-identification/reconstruction ML pipeline this repo
    // doesn't bundle and never will (Excluded, not Deferred) -- the
    // button stays real/clickable but reports that honestly rather than
    // silently doing nothing or faking a result.
    QPushButton* loadWithoutFrameImageButton_;
    QPushButton* loadWithFrameImageButton_;
    QLabel* imageWithoutFrameLabel_;
    QLabel* imageWithFrameLabel_;
    QPushButton* photoBasedRegistrationButton_;
    QGridLayout* correctionChartGrid_;
    QPushButton* runCorrectionButton_;
    QLabel* correctionStatusLabel_;
    QWidget* correctionHost_;
    QWidget* registerHost_;
    // The Register tab's real content widget, inside registerHost_'s
    // QScrollArea -- setFiducialTableModel() inserts fiducialTableView_
    // into *this* layout (registerHost_'s own layout just holds the
    // scroll area itself, one level up).
    QVBoxLayout* registerContentLayout_;
    QTableView* fiducialTableView_;
    MriSliceView* sagitalView_;
    MriSliceView* coronalView_;
    MriSliceView* axialView_;
    QLabel* rightRegistrationLamp_;
    QLabel* leftRegistrationLamp_;
    QLabel* insideMriRegistrationLamp_;

    // Persistent controls (`app.UIFigure`-parented in BeamV0.mlapp --
    // see the "Fifth pass" note in docs/known_gaps_gui.md): visible on
    // every tab, not scoped to one, added 2026-09-14 after the earlier
    // MRI-axes-persistence bug turned out to be one instance of a
    // systemic pattern rather than a one-off.
    QCheckBox* showTransducersCheckBox_;
    QCheckBox* showTargetCheckBox_;
    QCheckBox* showFieldCheckBox_;
    QLineEdit* siteIdEdit_;
    QLineEdit* visitNumberEdit_;
    QLineEdit* participantIdEdit_;
    void applyOverlayVisibility();  // re-gates the 3 views from the stored masks below
    // drawROIs.m's `if app.ShowTargetCheckBox.Value` gate + rebuild --
    // NOTE the source overwrites EVERY visible target row's crosshair
    // position to `centerArrayMM` (drawROIs.m: `app.targetROIs(i).position
    // = centerArrayMM;`, right before the show-gated loop), so every
    // currently-shown row's crosshair lands at the same one place -- the
    // array's own center, not that row's individual X/Y/Z columns. Called
    // on showTargetCheckBox_::toggled, setArrayCenterMm(), and the
    // stimParamModel_'s dataChanged/modelReset (an edit, sort, add, or
    // remove). No-op if setStimParamTableModel() hasn't run yet.
    void refreshTargetCrosshairs();
    beam::mri::Volume3D lastArrayMask_;
    beam::mri::Volume3D lastFiducialMask_;
    beam::mri::Volume3D lastFocusMask_;
};

}  // namespace beam::gui_qt
