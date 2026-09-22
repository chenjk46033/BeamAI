#include "gui_qt/main_window.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QFileInfo>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>

#include "gui/countdown_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "gui_qt/fiducial_table_model.hpp"
#include "gui_qt/mri_slice_view.hpp"
#include "gui_qt/safety_report_view.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "registration/fiducial_markers.hpp"

namespace beam::gui_qt {

namespace {

QWidget* placeholderTab(const QString& text) {
    auto* w = new QWidget();
    auto* layout = new QVBoxLayout(w);
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setEnabled(false);
    layout->addWidget(label);
    return w;
}

}  // namespace

BeamMainWindow::BeamMainWindow(QWidget* parent)
    : QMainWindow(parent),
      tabs_(new QTabWidget(this)),
      safetyView_(new SafetyReportView()),
      sonicateHost_(new QWidget()),
      stimParamTableView_(new QTableView()),
      sortByOrderButton_(new QPushButton(QStringLiteral("Sort by #"))),
      addSonicationButton_(new QPushButton(QStringLiteral("Add Sonication"))),
      removeMarkedButton_(new QPushButton(QStringLiteral("Remove Marked"))),
      treatmentProtocolTableView_(new QTableView()),
      accFlagCombo_(new QComboBox()),
      computeBestTargetsButton_(new QPushButton(QStringLiteral("Compute Best Targets"))),
      treatmentProtocolCombo_(new QComboBox()),
      visitNumberCombo_(new QComboBox()),
      newVisitButton_(new QPushButton(QStringLiteral("New Visit"))),
      bestTargetsResultList_(new QListWidget()),
      hydrogelSizeSmallRadio_(new QRadioButton(QStringLiteral("Small"))),
      hydrogelSizeMediumRadio_(new QRadioButton(QStringLiteral("Medium"))),
      hydrogelSizeLargeRadio_(new QRadioButton(QStringLiteral("Large"))),
      currentSonicationNumberEdit_(new QLineEdit(QStringLiteral("1"))),
      targetListListBox_(new QListWidget()),
      renameButton_(new QPushButton(QStringLiteral("Rename"))),
      removeSelectedProtocolButton_(new QPushButton(QStringLiteral("Remove Selected Protocol"))),
      createNewProtocolButton_(new QPushButton(QStringLiteral("Create New Protocol"))),
      moveToTargetButton_(new QPushButton(QStringLiteral("Move To Target"))),
      previewLayout_(nullptr),
      pulseChartLayout_(nullptr),
      timelineChartLayout_(nullptr),
      triggerModeCombo_(new QComboBox()),
      sonicateButton_(new QPushButton(QStringLiteral("Sonicate"))),
      shamButton_(new QPushButton(QStringLiteral("Sham"))),
      sonicationStatusLabel_(new QLabel(QStringLiteral("No sonication attempted yet."))),
      countdownLabel_(new QLabel(QStringLiteral("No sonication running."))),
      serialPortCombo_(new QComboBox()),
      serialConnectButton_(new QPushButton(QStringLiteral("Serial Connect"))),
      connectedLamp_(new QLabel()),
      getParamsButton_(new QPushButton(QStringLiteral("Get Params"))),
      abortSonicationButton_(new QPushButton(QStringLiteral("Abort Sonication"))),
      countdownTimer_(new QTimer(this)),
      countdownDurationSeconds_(0),
      countdownTickCount_(0),
      stimParamModel_(nullptr),
      treatmentProtocolModel_(nullptr),
      fiducialModel_(nullptr),
      registerButton_(new QPushButton(QStringLiteral("Register To MRI Fiducials"))),
      registerStatusLabel_(new QLabel(QStringLiteral("Not registered yet."))),
      rightHorizontalPositionSlider_(new QSlider(Qt::Horizontal)),
      rightVerticalPositionSlider_(new QSlider(Qt::Vertical)),
      leftHorizontalPositionSlider_(new QSlider(Qt::Horizontal)),
      leftVerticalPositionSlider_(new QSlider(Qt::Vertical)),
      registerCurrentPositionButton_(new QPushButton(QStringLiteral("Register Arrays to Current Position"))),
      transducerTransparencySlider_(new QSlider(Qt::Horizontal)),
      registrationTypeTabs_(new QTabWidget()),
      registrationTypeMriFiducialsRadio_(new QRadioButton(QStringLiteral("MRI Fiducials"))),
      registrationTypePhotoBasedRadio_(new QRadioButton(QStringLiteral("Photo Based"))),
      registrationTypeMriFreeRadio_(new QRadioButton(QStringLiteral("MRI Free"))),
      moveToLeftY1Z3Radio_(new QRadioButton(QStringLiteral("LeftY1Z3"))),
      moveToLeftY1Z1Radio_(new QRadioButton(QStringLiteral("LeftY1Z1"))),
      moveToLeftY4Z1Radio_(new QRadioButton(QStringLiteral("LeftY4Z1"))),
      moveToRightY4Z1Radio_(new QRadioButton(QStringLiteral("RightY4Z1"))),
      moveToRightY1Z1Radio_(new QRadioButton(QStringLiteral("RightY1Z1"))),
      moveToRightY1Z3Radio_(new QRadioButton(QStringLiteral("RightY1Z3"))),
      moveFiducialToCurrentViewButton_(new QPushButton(QStringLiteral("Move Fiducial to Current View"))),
      loadWithoutFrameImageButton_(new QPushButton(QStringLiteral("Load Without Frame Image"))),
      loadWithFrameImageButton_(new QPushButton(QStringLiteral("Load With Frame Image"))),
      imageWithoutFrameLabel_(new QLabel()),
      imageWithFrameLabel_(new QLabel()),
      photoBasedRegistrationButton_(new QPushButton(QStringLiteral("Photo Based Registration"))),
      correctionChartGrid_(nullptr),
      runCorrectionButton_(new QPushButton(QStringLiteral("Run Correction"))),
      correctionStatusLabel_(new QLabel(QStringLiteral("Showing synthetic coupling data."))),
      correctionHost_(new QWidget()),
      registerHost_(new QWidget()),
      registerContentLayout_(nullptr),
      fiducialTableView_(nullptr),
      sagitalView_(new MriSliceView("sagital")),
      coronalView_(new MriSliceView("coronal")),
      axialView_(new MriSliceView("axial")),
      rightRegistrationLamp_(new QLabel(QStringLiteral("Right"))),
      leftRegistrationLamp_(new QLabel(QStringLiteral("Left"))),
      insideMriRegistrationLamp_(new QLabel(QStringLiteral("Inside MRI"))),
      showTransducersCheckBox_(new QCheckBox(QStringLiteral("Show Transducers"))),
      showTargetCheckBox_(new QCheckBox(QStringLiteral("Show Target"))),
      showFieldCheckBox_(new QCheckBox(QStringLiteral("Show Field"))),
      siteIdEdit_(new QLineEdit()),
      visitNumberEdit_(new QLineEdit()),
      participantIdEdit_(new QLineEdit()) {
    setWindowTitle(QStringLiteral("Beam"));

    // BeamV0.mlapp parents its 3 MRI slice axes (axSag/axCor/axAxial)
    // directly to `app.UIFigure`, not to any tab -- so the real app shows
    // them immediately on startup, in every tab, not just Register.
    // Replicate that: a persistent top row here, with the tab group below
    // it, rather than nesting the MRI views inside the Register tab's own
    // layout (an earlier, structurally-wrong placement that made them
    // invisible unless Register was the selected tab).
    auto* central = new QWidget();
    auto* centralLayout = new QVBoxLayout(central);
    // Explicit margin, not Qt's platform-default (which rendered as ~0 in
    // at least one environment, clipping the first checkbox in
    // overlayCheckboxRow -- its indicator square sat flush against the
    // window's left edge, barely visible in a screenshot the user sent).
    centralLayout->setContentsMargins(12, 8, 12, 8);

    // Site ID / Participant ID / Visit Number + safetyView_
    // (app.SystemStatusTextArea) as their own full-width row above the MRI
    // images was this port's own invention, not BeamV0.mlapp's real
    // layout -- checked against the actual app.UIFigure (Site ID etc. sit
    // to the *right* of the 3 MRI axes, sharing their row, not stacked
    // above them). That invented row was stealing real vertical space from
    // the images (measured: it alone was ~150-257px tall) on top of
    // already being wrong -- the single biggest reason the MRI row looked
    // "too small" after every other fix (user: "the image size is still
    // way too small"). Moved down into persistentRow, below the images
    // alongside the checkboxes/trigger combo, so the images get that
    // height back. QLineEdit's default size policy is Horizontal::
    // Expanding, which would otherwise stretch each field to fill the row
    // -- capped, but wider than an earlier 150px pass ("those 3 boxes ...
    // bigger").
    auto* crfGroup = new QWidget();
    auto* crfRow = new QHBoxLayout(crfGroup);
    QFont crfFont = siteIdEdit_->font();
    crfFont.setPointSize(crfFont.pointSize() + 3);
    const auto addCrfField = [&](const QString& labelText, QLineEdit* edit) {
        auto* label = new QLabel(labelText);
        label->setFont(crfFont);
        edit->setFont(crfFont);
        edit->setMaximumWidth(220);
        edit->setMinimumHeight(QFontMetrics(crfFont).height() + 12);
        crfRow->addWidget(label);
        crfRow->addWidget(edit);
    };
    addCrfField(QStringLiteral("Site ID:"), siteIdEdit_);
    addCrfField(QStringLiteral("Participant ID:"), participantIdEdit_);
    addCrfField(QStringLiteral("Visit Number:"), visitNumberEdit_);

    auto* mriRow = new QHBoxLayout();
    mriRow->addWidget(sagitalView_);
    mriRow->addWidget(coronalView_);
    mriRow->addWidget(axialView_);

    // Persistent controls below the MRI images, above the tab group: every
    // one of these is confirmed `app.UIFigure`-parented in BeamV0.mlapp
    // (not scoped to any tab), the same pattern that made the MRI axes
    // invisible on the default tab until fixed -- caught this time by
    // building a full parent-assignment map up front instead of
    // discovering each one individually. `triggerModeCombo_` moves here
    // from where it was originally built (inside the Sonicate tab, a
    // wrong guess at the time); the visibility checkboxes are new. Two
    // stacked rows (checkboxes/trigger, then the CRF/status fields) rather
    // than one wide row -- keeps every field readable instead of
    // compressing them all into one line. See the "Fifth pass" note in
    // docs/known_gaps_gui.md for the full persistent-vs-tab-scoped audit.
    auto* persistentRow = new QVBoxLayout();
    auto* controlsRow = new QHBoxLayout();
    auto* overlayCheckboxRow = new QHBoxLayout();
    showTransducersCheckBox_->setChecked(true);  // app.ShowTransducersCheckBox's own default Value
    showTargetCheckBox_->setChecked(true);       // app.ShowTargetCheckBox's own default Value
    showFieldCheckBox_->setChecked(true);        // app.ShowFieldCheckBox's own default Value
    overlayCheckboxRow->addWidget(showTransducersCheckBox_);
    overlayCheckboxRow->addWidget(showTargetCheckBox_);
    overlayCheckboxRow->addWidget(showFieldCheckBox_);
    controlsRow->addLayout(overlayCheckboxRow);
    QObject::connect(showTransducersCheckBox_, &QCheckBox::toggled, this, [this](bool) { applyOverlayVisibility(); });
    QObject::connect(showFieldCheckBox_, &QCheckBox::toggled, this, [this](bool) { applyOverlayVisibility(); });
    QObject::connect(showTargetCheckBox_, &QCheckBox::toggled, this, [this](bool) { refreshTargetCrosshairs(); });
    persistentRow->addLayout(controlsRow);
    auto* crfStatusRow = new QHBoxLayout();
    crfStatusRow->addWidget(crfGroup, /*stretch=*/1);
    crfStatusRow->addWidget(safetyView_, /*stretch=*/1);
    persistentRow->addLayout(crfStatusRow);

    // QVBoxLayout stretch factors turned out not to reliably split space
    // between mriRow and tabs_ at all -- measured directly: mriRow stayed
    // pinned at its minimum regardless of stretch weight (tried 1:1, then
    // 3:1 favoring it, zero difference each time), while removing tabs_
    // from the layout entirely let it more than double. QTabWidget (and
    // its table/chart children) apparently claims available space in a
    // way plain stretch factors don't fairly counter. QSplitter is Qt's
    // actual tool for dividing space between two panes and does this
    // properly. mriRow + persistentRow share the top pane (keeping their
    // existing visual order -- images, then the checkbox/trigger row,
    // then tabs_ below); initial sizes follow BeamV0.mlapp's own
    // UIFigure, which gives its 3 MRI axes 370/789 (~47%) of window
    // height vs. TabGroup's 338/789 (~43%), not measured/guessed like
    // the stretch factors were.
    auto* topPane = new QWidget();
    auto* topPaneLayout = new QVBoxLayout(topPane);
    topPaneLayout->setContentsMargins(0, 0, 0, 0);
    topPaneLayout->addLayout(mriRow, /*stretch=*/1);
    topPaneLayout->addLayout(persistentRow, /*stretch=*/0);

    // tabs_'s own minimumSizeHint (aggregated bottom-up from its nested
    // table views/splitters/tab groups) grows well past what any of this
    // actually needs once real models are attached post-construction --
    // measured: 438px tall, pushing the whole window's minimum height to
    // 973px against an ordinary 912px-tall available screen. A minimum
    // that doesn't fit the screen is what was producing the resize
    // instability/squeezed layouts, not the MRI row's own sizing -- Qt and
    // Windows fighting over an unreachable floor. An explicit override
    // here replaces that aggregated hint outright for layout purposes;
    // the tab content still scrolls/clips normally below this floor.
    tabs_->setMinimumHeight(150);
    auto* mainSplitter = new QSplitter(Qt::Vertical);
    mainSplitter->addWidget(topPane);
    mainSplitter->addWidget(tabs_);
    // Weighted toward the MRI row, not an even split -- checked directly
    // against the real BeamV0 app (loaded with real subject data via
    // MATLAB, not just its empty startup state): its 3 MRI axes alone
    // consume ~80% of the space above the tab group, with the checkbox/
    // status row a thin strip underneath, not a near-even share. The user
    // confirmed images are the thing a nurse needs to read clearly, so
    // this pane gets the bulk of any extra room, not a 50/50 split with
    // the tab group.
    mainSplitter->setSizes({650, 250});
    centralLayout->addWidget(mainSplitter, /*stretch=*/1);
    setCentralWidget(central);

    // Sonicate tab: matches BeamV0.mlapp's real structure now, not this
    // port's earlier guess -- app.stimParamTable (the Sonications grid)
    // sits directly on SonicateTab, always visible, NOT nested inside any
    // tab (confirmed: app.stimParamTable's parent is app.SonicateTab
    // itself in the .mlapp source, positioned above TabGroup3). TabGroup3
    // below it has 3 real tabs, in this order: "Treatment Protocol",
    // "Pulse Details", "Targeting Examples" -- this port's earlier 2-tab
    // "Pulse Details"/"Treatment Protocol" (with the Sonications grid
    // nested inside "Pulse Details") didn't match either the grouping or
    // the order (user: "the display is not the same as BeamV0").
    auto* stimParamButtonRow = new QHBoxLayout();
    stimParamButtonRow->addWidget(sortByOrderButton_);
    stimParamButtonRow->addWidget(addSonicationButton_);
    stimParamButtonRow->addWidget(removeMarkedButton_);
    // QTableView's own minimumSizeHint grows once a model with real rows/
    // headers is attached (setStimParamTableModel, well after this
    // constructor returns) -- that growth would otherwise propagate up
    // through sonicateHost_'s layout and tabs_ itself, inflating the whole
    // window's minimum height past what an ordinary screen offers (the
    // same class of bug already root-caused for tabs_ itself -- see its
    // own setMinimumHeight comment below). An explicit minimum overrides
    // that hint; the view still scrolls internally.
    stimParamTableView_->setMinimumHeight(80);
    // QTableView's own default column width (~100+px each) times this
    // table's real 12 columns runs well past 1200px -- nearly this port's
    // entire default window width on its own, before the Target List
    // sidebar, tab group, or the narrow Serial Port column get anything.
    // BeamV0.mlapp's own stimParamTable fits all 12 in 883px (the real
    // .mlapp Position width) -- MATLAB's default uitable columns are
    // simply narrower. A comparable cap here is what actually gets the
    // Serial Port column back on-screen without scrolling, not the
    // scroll area alone (still real, still there for whatever doesn't
    // fit even at this width).
    stimParamTableView_->horizontalHeader()->setDefaultSectionSize(72);

    // Treatment Protocol tab: treatmentProtocolTable + its real selector
    // row (TreatmentProtocolDropDown/VisitNumberListBox/NewVisitButton --
    // already correct) plus 2 real fields this port had never placed
    // anywhere: HydrogelSizeButtonGroup (setCRFHydrogelSize.m -- 3
    // QRadioButtons are naturally mutually exclusive as siblings in one
    // QWidget, no QButtonGroup needed) and CurrentSonicationNumberEditField
    // (warnForSonicate.m/updateSonicateSettingsForCurrentSonication.m's
    // real 1-based row index; exposed via currentSonicationNumber() but
    // not yet wired into that gating logic -- see the header comment).
    // accFlagCombo_/computeBestTargetsButton_/bestTargetsResultList_
    // (an earlier invented UI for beam::gui::computeBestTargets) don't
    // belong on this tab at all in the real app -- BeamV0 computes best
    // targets automatically inside setTreatmentProtocolTableData.m, with
    // no manual button anywhere; moved to the Targeting Examples tab
    // below instead of deleted, since the underlying computation is real
    // and tested.
    auto* protocolSelectorRow = new QHBoxLayout();
    protocolSelectorRow->addWidget(new QLabel(QStringLiteral("Treatment protocol:")));
    protocolSelectorRow->addWidget(treatmentProtocolCombo_);
    protocolSelectorRow->addWidget(new QLabel(QStringLiteral("Visit:")));
    protocolSelectorRow->addWidget(visitNumberCombo_);
    protocolSelectorRow->addWidget(newVisitButton_);

    auto* hydrogelSizeGroup = new QWidget();
    auto* hydrogelSizeLayout = new QHBoxLayout(hydrogelSizeGroup);
    hydrogelSizeLayout->addWidget(new QLabel(QStringLiteral("Hydrogel Size:")));
    hydrogelSizeSmallRadio_->setChecked(true);  // HydrogelSizeButtonGroup's own default selection
    hydrogelSizeLayout->addWidget(hydrogelSizeSmallRadio_);
    hydrogelSizeLayout->addWidget(hydrogelSizeMediumRadio_);
    hydrogelSizeLayout->addWidget(hydrogelSizeLargeRadio_);
    hydrogelSizeLayout->addWidget(new QLabel(QStringLiteral("Current Sonication Number:")));
    currentSonicationNumberEdit_->setMaximumWidth(60);
    hydrogelSizeLayout->addWidget(currentSonicationNumberEdit_);
    hydrogelSizeLayout->addStretch(1);

    auto* treatmentProtocolTab = new QWidget();
    auto* treatmentProtocolLayout = new QVBoxLayout(treatmentProtocolTab);
    treatmentProtocolLayout->addLayout(protocolSelectorRow);
    treatmentProtocolLayout->addWidget(hydrogelSizeGroup);
    treatmentProtocolTableView_->setMinimumHeight(80);  // see stimParamTableView_'s comment above
    treatmentProtocolTableView_->horizontalHeader()->setDefaultSectionSize(72);  // see stimParamTableView_'s comment
    treatmentProtocolLayout->addWidget(treatmentProtocolTableView_);

    // Pulse Details tab: BeamV0's real axPulseWaveformPlot/
    // axBurstWaveformPlot/axTotalSonicationPlot live here, not floating in
    // a free-standing right-hand column the way this port's earlier guess
    // had them. Only 2 of the 3 charts move in -- pulseChartLayout_ (a
    // real port of updateSonicationPlots.m's first plot) and
    // timelineChartLayout_ (an honest stand-in for the still-unported
    // burst/total-sonication plots, see sonication_tab_chart.hpp's header
    // comment); no fabricated third chart.
    auto* pulseDetailsTab = new QWidget();
    auto* pulseDetailsLayout = new QVBoxLayout(pulseDetailsTab);
    auto* pulseChartHost = new QWidget();
    pulseChartLayout_ = new QVBoxLayout(pulseChartHost);
    pulseChartLayout_->addWidget(placeholderTab(QStringLiteral("Pulse waveform — no sonication loaded")));
    pulseDetailsLayout->addWidget(pulseChartHost);
    auto* timelineChartHost = new QWidget();
    timelineChartLayout_ = new QVBoxLayout(timelineChartHost);
    timelineChartLayout_->addWidget(placeholderTab(QStringLiteral("Sonication timeline — no sonication loaded")));
    pulseDetailsLayout->addWidget(timelineChartHost);

    // Targeting Examples tab: the real app's version needs 3 example-
    // target MRI images (SCC1Sag.png etc.) this repo doesn't bundle (see
    // exampleTargetHelpText.m's header comment) -- rehoming the best-
    // targets controls here instead is this port's own substitute, not a
    // faithful widget-for-widget copy of TargetsListBox/TargetingTextArea.
    accFlagCombo_->addItem(QStringLiteral("Other (SCC/aMCC interleaved)"));
    accFlagCombo_->addItem(QStringLiteral("SCC"));
    accFlagCombo_->addItem(QStringLiteral("aMCC"));
    auto* accFlagRow = new QHBoxLayout();
    accFlagRow->addWidget(new QLabel(QStringLiteral("ACC region:")));
    accFlagRow->addWidget(accFlagCombo_);
    accFlagRow->addWidget(computeBestTargetsButton_);
    auto* targetingExamplesTab = new QWidget();
    auto* targetingExamplesLayout = new QVBoxLayout(targetingExamplesTab);
    targetingExamplesLayout->addLayout(accFlagRow);
    targetingExamplesLayout->addWidget(new QLabel(QStringLiteral("Best targets:")));
    bestTargetsResultList_->setMinimumHeight(60);
    targetingExamplesLayout->addWidget(bestTargetsResultList_);

    auto* tableColumn = new QTabWidget();
    tableColumn->addTab(treatmentProtocolTab, QStringLiteral("Treatment Protocol"));
    tableColumn->addTab(pulseDetailsTab, QStringLiteral("Pulse Details"));
    tableColumn->addTab(targetingExamplesTab, QStringLiteral("Targeting Examples"));

    // app.SonicateTab's real right-hand column: a narrow (~10% of the
    // tab's width) stack of Serial Port controls + the Sonicate/Sham/
    // Abort buttons, in the .mlapp's own real top-to-bottom order --
    // Serial Port dropdown, Serial Connect, Connected lamp, Get Params,
    // Sham, Sonicate, Abort Sonication (its Y positions run 314 down to
    // 5 in the .mlapp's bottom-up coordinates). This port's earlier pass
    // put a much wider column here (a 2:1 splitter share) holding only
    // Sonicate/Sham + free-text status -- neither the real width nor the
    // real widget set (no Serial Port controls, Get Params, or Abort
    // Sonication at all) -- read as clearly different from BeamV0 (user:
    // "still has a lot difference from that of BeamV0").
    auto* previewColumn = new QWidget();
    previewColumn->setMaximumWidth(160);
    previewLayout_ = new QVBoxLayout(previewColumn);
    // safetyView_ moved to the persistent row above (see the constructor
    // top) -- app.SystemStatusTextArea is UIFigure-parented, visible on
    // every tab, not just Sonicate.

    previewLayout_->addWidget(new QLabel(QStringLiteral("Serial Port:")));
    previewLayout_->addWidget(serialPortCombo_);
    previewLayout_->addWidget(serialConnectButton_);
    auto* connectedRow = new QHBoxLayout();
    connectedLamp_->setAutoFillBackground(true);
    connectedLamp_->setAlignment(Qt::AlignCenter);
    connectedLamp_->setFixedSize(20, 20);
    setSerialConnectedState(false);
    connectedRow->addWidget(new QLabel(QStringLiteral("Connected")));
    connectedRow->addWidget(connectedLamp_);
    previewLayout_->addLayout(connectedRow);
    previewLayout_->addWidget(getParamsButton_);
    previewLayout_->addWidget(shamButton_);

    // Sonicate tab: the button that runs generalSonicateMaster.m's
    // decision logic (beam::gui::prepareSonication, in main.cpp) and, if
    // it passes, sends the resulting command over serial. triggerModeCombo_
    // itself (app.TriggerSwitch) moved to the persistent row above --
    // UIFigure-parented in the source, not Sonicate-tab-scoped -- but its
    // wiring (setSonicateHandler reads currentIndex() below) is unaffected
    // by where the widget visually sits.
    triggerModeCombo_->addItem(QStringLiteral("Immediate"));
    triggerModeCombo_->addItem(QStringLiteral("External"));
    controlsRow->addWidget(new QLabel(QStringLiteral("Trigger:")));
    controlsRow->addWidget(triggerModeCombo_);
    previewLayout_->addWidget(sonicateButton_);
    previewLayout_->addWidget(abortSonicationButton_);
    previewLayout_->addStretch(1);

    countdownTimer_->setInterval(2000);  // startStandaloneCountdown.m's hardcoded 2s period
    QObject::connect(countdownTimer_, &QTimer::timeout, this, [this]() {
        ++countdownTickCount_;
        const int timeLeft = beam::gui::countdownTimeLeftSeconds(countdownDurationSeconds_, countdownTickCount_);
        if (beam::gui::isCountdownDone(timeLeft)) {
            countdownTimer_->stop();
            countdownLabel_->setText(QStringLiteral("No sonication running."));
            return;
        }
        countdownLabel_->setText(QString::fromStdString(beam::gui::countdownDisplayText(timeLeft)));
    });

    auto* splitter = new QSplitter();
    splitter->addWidget(tableColumn);
    splitter->addWidget(previewColumn);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    // sonicationStatusLabel_/countdownLabel_ are this port's own
    // addition -- BeamV0.mlapp has no dedicated status/countdown text
    // widget on SonicateTab at all (status goes to app.SystemStatusTextArea,
    // already ported as safetyView_ above; the countdown is a separate
    // popup figure, startStandaloneCountdown.m, not part of this tab).
    // Kept (real, useful information with nowhere else to go) but placed
    // full-width below the real row instead of stealing space from the
    // narrow real right column, which only has room for short labels.
    auto* statusRow = new QHBoxLayout();
    sonicationStatusLabel_->setWordWrap(true);
    statusRow->addWidget(sonicationStatusLabel_, /*stretch=*/1);
    statusRow->addWidget(countdownLabel_);

    // app.SonicationsPanel: a real column of its own to the left of
    // everything else built above (matches the .mlapp: SonicationsPanel
    // and app.stimParamTable are separate, side-by-side children of
    // SonicateTab). targetListListBox_ starts pre-populated with
    // getTopTargetsFromTreatmentProtocolTable.m's own hardcoded
    // bestTargetsRanking names (the only real target names this port
    // knows, already used for bestTargetsResultList_'s ranking) --
    // Create/Rename/Remove below edit *this* list directly, real local
    // list editing, not the deeper app.sys.protocolTables CRUD model
    // (per-target stimParamTable switching, "N sonications" counts, etc.)
    // docs/known_gaps_gui.md documents as deliberately unreplicated.
    // Move To Target has no per-target position data to move sliders to
    // without that model, so it's left unconnected rather than faked.
    for (const char* name : {"SCC1", "SCC2", "SCC3", "SCC4", "SCC5", "SCC6", "aMCC1", "aMCC2", "aMCC3", "aMCC4",
                              "aMCC5", "aMCC6"}) {
        targetListListBox_->addItem(QString::fromLatin1(name));
    }
    targetListListBox_->setCurrentRow(0);
    QObject::connect(renameButton_, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* item = targetListListBox_->currentItem();
        if (item == nullptr) return;
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Name:"), QLineEdit::Normal,
                                   item->text(), &ok);
        if (ok && !name.isEmpty()) item->setText(name);
    });
    QObject::connect(removeSelectedProtocolButton_, &QPushButton::clicked, this, [this]() {
        delete targetListListBox_->takeItem(targetListListBox_->currentRow());
    });
    QObject::connect(createNewProtocolButton_, &QPushButton::clicked, this, [this]() {
        std::vector<std::string> existing;
        for (int i = 0; i < targetListListBox_->count(); ++i) {
            existing.push_back(targetListListBox_->item(i)->text().toStdString());
        }
        targetListListBox_->addItem(QString::fromStdString(beam::gui::newProtocolName(existing)));
        targetListListBox_->setCurrentRow(targetListListBox_->count() - 1);
    });

    auto* sonicationsPanel = new QWidget();
    auto* sonicationsPanelLayout = new QVBoxLayout(sonicationsPanel);
    auto* targetListHeaderRow = new QHBoxLayout();
    targetListHeaderRow->addWidget(new QLabel(QStringLiteral("Target List")));
    targetListHeaderRow->addWidget(renameButton_);
    sonicationsPanelLayout->addLayout(targetListHeaderRow);
    targetListListBox_->setMinimumHeight(60);
    sonicationsPanelLayout->addWidget(targetListListBox_);
    sonicationsPanelLayout->addWidget(removeSelectedProtocolButton_);
    sonicationsPanelLayout->addWidget(createNewProtocolButton_);
    sonicationsPanelLayout->addStretch(1);
    sonicationsPanelLayout->addWidget(moveToTargetButton_);
    sonicationsPanel->setMaximumWidth(220);

    auto* sonicateRightColumn = new QVBoxLayout();
    sonicateRightColumn->addLayout(stimParamButtonRow);
    sonicateRightColumn->addWidget(stimParamTableView_);
    sonicateRightColumn->addWidget(splitter);
    sonicateRightColumn->addLayout(statusRow);

    // The always-visible Sonications grid (12 real columns) plus the
    // Target List sidebar plus the now-real narrow Serial Port column
    // add up to more width than an ordinary window offers -- without a
    // scroll area, QSplitter just shrinks tableColumn to its minimum and
    // the narrow right column gets pushed off past the window's own
    // right edge entirely (measured: invisible at this port's actual
    // default launch size). Same QScrollArea fix as the Register tab,
    // just horizontal here instead of vertical.
    auto* sonicateScrollArea = new QScrollArea();
    sonicateScrollArea->setWidgetResizable(true);
    sonicateScrollArea->setFrameShape(QFrame::NoFrame);
    auto* sonicateContent = new QWidget();
    sonicateScrollArea->setWidget(sonicateContent);
    auto* sonicateHostLayout = new QVBoxLayout(sonicateHost_);
    sonicateHostLayout->setContentsMargins(0, 0, 0, 0);
    sonicateHostLayout->addWidget(sonicateScrollArea);

    auto* sonicateLayout = new QHBoxLayout(sonicateContent);
    sonicateLayout->addWidget(sonicationsPanel);
    sonicateLayout->addLayout(sonicateRightColumn, /*stretch=*/1);

    // The chart grid gets its own nested layout (rather than being
    // correctionHost_'s direct layout) so setCorrectionCharts's clear-and-
    // rebuild loop can't sweep away the Run Correction button/status
    // added below it.
    auto* correctionLayout = new QVBoxLayout(correctionHost_);
    auto* correctionChartHost = new QWidget();
    correctionChartGrid_ = new QGridLayout(correctionChartHost);
    correctionLayout->addWidget(correctionChartHost);
    correctionLayout->addWidget(runCorrectionButton_);
    correctionStatusLabel_->setWordWrap(true);
    correctionLayout->addWidget(correctionStatusLabel_);

    // Register tab: matches BeamV0.mlapp's real RegisterTab now, not this
    // port's earlier flat single-column guess -- TransducerTransparencySlider,
    // real Left/Right array-lock-position panels (each its own H/V
    // slider + registration lamp, Limits [1,4] matching the .mlapp, not
    // this port's earlier [1,5] guess), the Register Arrays to Current
    // Position button, then TabGroup4 (MRI Fiducial Based/MRI Free/Photo
    // Based) alongside RegistrationTypeButtonGroup. The fiducial marker
    // grid (a placeholder until setFiducialTableModel() fills it --
    // draggable ROIs (allFiducialEvents.m) aren't here, X/Y/Z are plain
    // editable cells instead) stays at the very top, same as before.
    //
    // All of that is real vertical content -- far more than this tab had
    // before -- and the MRI row above already claims most of the
    // window's height (see mainSplitter's own comment). Measured at this
    // port's actual default launch size (1616x1039, not a manually
    // enlarged test window): both position panels showed nothing but
    // their title bars, and the inner tab group showed no content at all
    // -- not just tight, genuinely unreachable (user: "I dont see any of
    // the guis you said you implemented"). A QScrollArea makes everything
    // reachable by scrolling regardless of window/screen size, the same
    // way the tables elsewhere in this tab already handle overflow,
    // rather than gambling on a fixed allocation being enough.
    auto* registerScrollArea = new QScrollArea();
    registerScrollArea->setWidgetResizable(true);
    registerScrollArea->setFrameShape(QFrame::NoFrame);
    auto* registerContent = new QWidget();
    registerScrollArea->setWidget(registerContent);
    auto* registerHostLayout = new QVBoxLayout(registerHost_);
    registerHostLayout->setContentsMargins(0, 0, 0, 0);
    registerHostLayout->addWidget(registerScrollArea);
    auto* registerLayout = new QVBoxLayout(registerContent);
    registerContentLayout_ = registerLayout;
    registerLayout->addWidget(placeholderTab(QStringLiteral("Registration — no fiducials loaded")));
    registerStatusLabel_->setWordWrap(true);
    registerLayout->addWidget(registerStatusLabel_);

    // Real proportions, not a plain vertical stack: the .mlapp's own
    // RegisterTab is a 2-column layout -- TabGroup4 (MRI Fiducial Based/
    // MRI Free/Photo Based) a tall panel on the LEFT (~31% width, nearly
    // the tab's full height), everything else (transparency slider,
    // position panels, register button, registration-type selector)
    // stacked on the RIGHT. This port's first pass instead stacked
    // everything in one long vertical column top-to-bottom, which read
    // as very different from the real app (user: "The display of those
    // gui are so different from BeamV0") even once every widget existed.
    // Each position panel's own H/V slider pair is genuinely
    // perpendicular here too (Z sliders built Qt::Vertical, matching
    // LeftVerticalPositionZSlider.Orientation = 'vertical' -- this port's
    // first pass had both horizontal, reading as two stacked bars
    // instead of the real cross/L shape).
    const auto buildPositionPanel = [this](const QString& title, QSlider* horizontalSlider, const QString& hLabel,
                                            QSlider* verticalSlider, const QString& vLabel, QLabel* lamp,
                                            const QString& lampLabel) {
        auto* panel = new QGroupBox(title);
        auto* layout = new QGridLayout(panel);
        horizontalSlider->setRange(1, 4);  // the .mlapp's own Limits, not this port's earlier [1,5] guess
        horizontalSlider->setValue(static_cast<int>(beam::gui::defaultArrayFramePosition().horizontal));
        layout->addWidget(new QLabel(hLabel), 0, 0);
        layout->addWidget(horizontalSlider, 1, 0);
        verticalSlider->setRange(1, 4);
        verticalSlider->setValue(static_cast<int>(beam::gui::defaultArrayFramePosition().vertical));
        auto* vLabelWidget = new QLabel(vLabel);
        vLabelWidget->setWordWrap(true);
        layout->addWidget(vLabelWidget, 0, 1);
        layout->addWidget(verticalSlider, 1, 1, 3, 1);  // spans down alongside the lamp row below
        lamp->setAutoFillBackground(true);
        lamp->setAlignment(Qt::AlignCenter);
        layout->addWidget(new QLabel(lampLabel), 2, 0);
        layout->addWidget(lamp, 3, 0);
        return panel;
    };
    auto* leftPanel = buildPositionPanel(
        QStringLiteral("Array Lock Position Subject Left"), leftHorizontalPositionSlider_,
        QStringLiteral("Left Horizontal Position (Y)"), leftVerticalPositionSlider_,
        QStringLiteral("Left Vertical Position (Z)"), leftRegistrationLamp_, QStringLiteral("Left Registration"));
    auto* rightPanel = buildPositionPanel(
        QStringLiteral("Transducer Lock Position Subject Right"), rightHorizontalPositionSlider_,
        QStringLiteral("Right Horizontal Position (Y)"), rightVerticalPositionSlider_,
        QStringLiteral("Right Vertical Position (Z)"), rightRegistrationLamp_, QStringLiteral("Right Registration"));
    setRegistrationCheckLampState(beam::gui::RegistrationCheckLampState{});

    // TabGroup4: MRI Fiducial Based (the "Move to" lock-position radios +
    // Register To MRI Fiducials + Move Fiducial to Current View + its own
    // Inside MRI Registration lamp -- all really inside this tab in the
    // .mlapp, not floating at the top level like this port's earlier
    // guess had them), MRI Free (genuinely empty in the real app too --
    // not a gap, just nothing there), Photo Based (image loading, real;
    // the registration button itself is not -- see its own member
    // comment for why).
    auto* movetoGroupBox = new QGroupBox(QStringLiteral("Move to"));
    auto* movetoLayout = new QGridLayout(movetoGroupBox);
    moveToLeftY1Z3Radio_->setChecked(true);  // LeftY1Z3Button's own default (Value = true)
    movetoLayout->addWidget(moveToLeftY1Z3Radio_, 0, 0);
    movetoLayout->addWidget(moveToLeftY1Z1Radio_, 1, 0);
    movetoLayout->addWidget(moveToLeftY4Z1Radio_, 1, 1);
    movetoLayout->addWidget(moveToRightY1Z3Radio_, 2, 0);
    movetoLayout->addWidget(moveToRightY1Z1Radio_, 3, 0);
    movetoLayout->addWidget(moveToRightY4Z1Radio_, 3, 1);
    movetoLayout->addWidget(registerButton_, 4, 0, 1, 2);
    auto* insideMriRow = new QHBoxLayout();
    insideMriRegistrationLamp_->setAutoFillBackground(true);
    insideMriRegistrationLamp_->setAlignment(Qt::AlignCenter);
    insideMriRow->addWidget(new QLabel(QStringLiteral("Inside MRI Registration")));
    insideMriRow->addWidget(insideMriRegistrationLamp_);
    movetoLayout->addLayout(insideMriRow, 5, 0, 1, 2);
    movetoLayout->addWidget(moveFiducialToCurrentViewButton_, 6, 0, 1, 2);

    auto* mriFiducialBasedTab = new QWidget();
    auto* mriFiducialBasedLayout = new QVBoxLayout(mriFiducialBasedTab);
    mriFiducialBasedLayout->addWidget(movetoGroupBox);

    auto* mriFreeTab = new QWidget();  // genuinely empty in BeamV0.mlapp too

    auto* photoBasedTab = new QWidget();
    auto* photoBasedLayout = new QVBoxLayout(photoBasedTab);
    auto* loadImageRow = new QHBoxLayout();
    loadImageRow->addWidget(loadWithoutFrameImageButton_);
    loadImageRow->addWidget(loadWithFrameImageButton_);
    photoBasedLayout->addLayout(loadImageRow);
    auto* imagePreviewRow = new QHBoxLayout();
    for (QLabel* imageLabel : {imageWithoutFrameLabel_, imageWithFrameLabel_}) {
        imageLabel->setMinimumSize(120, 120);
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setFrameShape(QFrame::Box);
        imagePreviewRow->addWidget(imageLabel);
    }
    photoBasedLayout->addLayout(imagePreviewRow);
    photoBasedLayout->addWidget(photoBasedRegistrationButton_);
    QObject::connect(loadWithoutFrameImageButton_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select Without Frame Image"));
        if (path.isEmpty()) return;
        imageWithoutFrameLabel_->setPixmap(
            QPixmap(path).scaled(imageWithoutFrameLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    QObject::connect(loadWithFrameImageButton_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select With Frame Image"));
        if (path.isEmpty()) return;
        imageWithFrameLabel_->setPixmap(
            QPixmap(path).scaled(imageWithFrameLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    QObject::connect(photoBasedRegistrationButton_, &QPushButton::clicked, this, [this]() {
        registerStatusLabel_->setText(QStringLiteral(
            "Photo-based registration isn't available -- BeamV0's own pipeline needs an external Python "
            "face-de-identification model this build doesn't include."));
    });

    QObject::connect(moveFiducialToCurrentViewButton_, &QPushButton::clicked, this, [this]() {
        if (fiducialModel_ == nullptr) return;
        const int row = fiducialTableView_->currentIndex().row();
        if (row < 0 || row >= static_cast<int>(fiducialModel_->rows().size())) return;
        std::vector<FiducialRow> rows = fiducialModel_->rows();
        rows[static_cast<std::size_t>(row)].x = sagitalView_->currentSliderValueMm();
        rows[static_cast<std::size_t>(row)].y = coronalView_->currentSliderValueMm();
        rows[static_cast<std::size_t>(row)].z = axialView_->currentSliderValueMm();
        fiducialModel_->setRows(std::move(rows));
    });

    // moveToFiducialMarkerButtonFunction.m, via MovetoButtonGroupSelectionChanged:
    // clicking one of the 6 named "Move to" radios looks the fiducial up
    // by name and jumps all 3 MRI slice sliders to its stored position --
    // the operator manual's own documented first step of fiducial
    // registration ("Click the corresponding radio button for each
    // fiducial. This will bring you to the currently selected fiducial
    // marker in the MRI."), not just internal glue. Also selects that
    // fiducial's row in the table, so it's the one
    // moveFiducialToCurrentViewButton_ (above) then writes back to.
    const auto moveToSelectedFiducial = [this](QRadioButton* button) {
        if (fiducialModel_ == nullptr) return;
        const std::vector<FiducialRow>& rows = fiducialModel_->rows();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].name != button->text()) continue;
            fiducialTableView_->selectRow(static_cast<int>(i));
            sagitalView_->setSliderValueMm(rows[i].x);
            coronalView_->setSliderValueMm(rows[i].y);
            axialView_->setSliderValueMm(rows[i].z);
            break;
        }
    };
    for (QRadioButton* button : {moveToLeftY1Z3Radio_, moveToLeftY1Z1Radio_, moveToLeftY4Z1Radio_,
                                  moveToRightY1Z3Radio_, moveToRightY1Z1Radio_, moveToRightY4Z1Radio_}) {
        QObject::connect(button, &QRadioButton::toggled, this, [button, moveToSelectedFiducial](bool checked) {
            if (checked) moveToSelectedFiducial(button);
        });
    }

    registrationTypeTabs_->addTab(mriFiducialBasedTab, QStringLiteral("MRI Fiducial Based"));
    registrationTypeTabs_->addTab(mriFreeTab, QStringLiteral("MRI Free"));
    registrationTypeTabs_->addTab(photoBasedTab, QStringLiteral("Photo Based"));

    auto* registrationTypeGroupBox = new QGroupBox(QStringLiteral("Registration Type"));
    auto* registrationTypeLayout = new QVBoxLayout(registrationTypeGroupBox);
    registrationTypeMriFiducialsRadio_->setChecked(true);  // MRIFiducialsButton's own default (Value = true)
    registrationTypeLayout->addWidget(registrationTypeMriFiducialsRadio_);
    registrationTypeLayout->addWidget(registrationTypePhotoBasedRadio_);
    registrationTypeLayout->addWidget(registrationTypeMriFreeRadio_);
    registrationTypeLayout->addStretch(1);
    // RegistrationTypeButtonGroupSelectionChanged.m: switches which
    // TabGroup4 tab is current. This port stops there -- the source also
    // sets app.sys.registration.type for registerCurrentTransducerPostion.m's
    // MRIFree/PhotoBased branches, but this port only implements the
    // MRIFiducials branch (see docs/known_gaps_registration.md), so
    // there's nothing further those two selections would change here.
    QObject::connect(registrationTypeMriFiducialsRadio_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) registrationTypeTabs_->setCurrentIndex(0);
    });
    QObject::connect(registrationTypePhotoBasedRadio_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) registrationTypeTabs_->setCurrentIndex(2);
    });
    QObject::connect(registrationTypeMriFreeRadio_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) registrationTypeTabs_->setCurrentIndex(1);
    });

    // Right column: transparency slider (top-right in the .mlapp), the
    // two position panels, then Register Arrays to Current Position
    // alongside the Registration Type selector at the very bottom --
    // matching the .mlapp's own bottom-right cluster, not a full-width
    // button floating below the panels.
    auto* transparencyRow = new QHBoxLayout();
    transparencyRow->addWidget(new QLabel(QStringLiteral("Transducer Transparency:")));
    transducerTransparencySlider_->setRange(0, 100);
    transducerTransparencySlider_->setValue(80);  // TransducerTransparencySlider's own default (0.8)
    transparencyRow->addWidget(transducerTransparencySlider_);

    auto* positionPanelsRow = new QHBoxLayout();
    positionPanelsRow->addWidget(leftPanel);
    positionPanelsRow->addWidget(rightPanel);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->addWidget(registerCurrentPositionButton_, /*stretch=*/1);
    bottomRow->addWidget(registrationTypeGroupBox);

    auto* rightColumn = new QVBoxLayout();
    rightColumn->addLayout(transparencyRow);
    rightColumn->addLayout(positionPanelsRow);
    rightColumn->addStretch(1);
    rightColumn->addLayout(bottomRow);

    auto* registerBodyRow = new QHBoxLayout();
    registerBodyRow->addWidget(registrationTypeTabs_, /*stretch=*/1);
    registerBodyRow->addLayout(rightColumn, /*stretch=*/2);
    registerLayout->addLayout(registerBodyRow);

    // Order matches BeamV0.mlapp's TabGroup: Sonicate, Register, Correction.
    tabs_->addTab(sonicateHost_, QStringLiteral("Sonicate"));
    tabs_->addTab(registerHost_, QStringLiteral("Register"));
    tabs_->addTab(correctionHost_, QStringLiteral("Correction"));

    // File menu -- BeamV0.mlapp's real order: Load MRI, Save Subject,
    // Load Subject, Enable All Buttons (LoadMRIMenu/SaveSubjectMenu/
    // LoadSubjectMenu/EnableAllButtonsMenu). "Save/Load Subject" here use
    // this project's own `.beamsession` text format (fiducials/
    // stimParamTable/treatmentProtocol only), not the source's `.mat` of
    // the whole `app.sys` -- see docs/known_gaps_gui.md for why.
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    // LoadMRIMenuSelected.m doesn't statically declare a submenu -- it
    // pops a listdlg() at click time with 2 choices, 'select file' /
    // 'select folder', then calls uigetfile/uigetdir accordingly. A
    // static 2-item submenu is this port's own (more Qt-idiomatic)
    // substitute for that runtime dialog, not a literal structural
    // match, but the destination behavior is the same real dispatcher
    // either way (loadMRIRAS.m -> beam::infra::dicom::loadMriRas, which
    // already handles both a DICOM series folder and a single file --
    // this port previously only ever exposed the file half).
    QMenu* loadMriMenu = fileMenu->addMenu(QStringLiteral("Load MRI"));
    QAction* loadMriFileAction = loadMriMenu->addAction(QStringLiteral("Load File"));
    QAction* loadMriFolderAction = loadMriMenu->addAction(QStringLiteral("Load Folder"));
    QAction* saveAction = fileMenu->addAction(QStringLiteral("Save Subject"));
    QAction* loadAction = fileMenu->addAction(QStringLiteral("Load Subject"));
    QAction* enableAllButtonsAction = fileMenu->addAction(QStringLiteral("Enable All Buttons"));
    QObject::connect(loadMriFileAction, &QAction::triggered, this, &BeamMainWindow::loadMriFile);
    QObject::connect(loadMriFolderAction, &QAction::triggered, this, &BeamMainWindow::loadMriFolder);
    QObject::connect(saveAction, &QAction::triggered, this, &BeamMainWindow::saveSession);
    QObject::connect(loadAction, &QAction::triggered, this, &BeamMainWindow::loadSession);
    // enableAllButtonsMenuSelectedCallback.m's literal 3 set(...,'Enable','on')
    // calls -- a manual "unstick" escape hatch, no computation.
    QObject::connect(enableAllButtonsAction, &QAction::triggered, this, [this]() {
        sonicateButton_->setEnabled(true);
        shamButton_->setEnabled(true);
        runCorrectionButton_->setEnabled(true);
    });

    // Taller than before (was 960x640) -- the MRI row is now a persistent
    // fixture above the tab group instead of living inside just one tab,
    // so the default window needs room for both at once.
    resize(1000, 900);
}

beam::gui::SessionData BeamMainWindow::collectSessionData() const {
    beam::gui::SessionData data;
    if (fiducialModel_ != nullptr) {
        for (const FiducialRow& r : fiducialModel_->rows()) {
            beam::registration::FiducialMarker m;
            m.name = r.name.toStdString();
            // FiducialRow is mm (setFiducialROIs.m's *1000 scaling); this
            // project's canonical FiducialMarker convention is meters.
            m.position = Eigen::Vector3d(r.x / 1000.0, r.y / 1000.0, r.z / 1000.0);
            data.fiducials.push_back(std::move(m));
        }
    }
    if (stimParamModel_ != nullptr) {
        for (const StimParamRow& r : stimParamModel_->rows()) {
            data.stimParams.push_back({r.order, r.show, r.x, r.y, r.z, r.amplitude, r.startTime, r.endTime, r.bd,
                                       r.bi, r.pd, r.pi});
        }
    }
    if (treatmentProtocolModel_ != nullptr) {
        for (const TreatmentProtocolRow& r : treatmentProtocolModel_->rows()) {
            data.treatmentProtocol.push_back({r.number, r.target.toStdString(), r.duration, r.amplitude,
                                              r.parameters.toStdString(), r.responsePain, r.responseMood,
                                              r.notes.toStdString()});
        }
    }
    return data;
}

void BeamMainWindow::applySessionData(const beam::gui::SessionData& data) {
    if (fiducialModel_ != nullptr) {
        std::vector<FiducialRow> rows;
        rows.reserve(data.fiducials.size());
        for (const beam::registration::FiducialMarker& m : data.fiducials) {
            FiducialRow r;
            r.name = QString::fromStdString(m.name);
            r.x = m.position.x() * 1000.0;
            r.y = m.position.y() * 1000.0;
            r.z = m.position.z() * 1000.0;
            rows.push_back(std::move(r));
        }
        fiducialModel_->setRows(std::move(rows));
    }
    if (stimParamModel_ != nullptr) {
        std::vector<StimParamRow> rows;
        rows.reserve(data.stimParams.size());
        for (const beam::gui::StimParamRecord& s : data.stimParams) {
            StimParamRow r;
            r.order = s.order;
            r.show = s.show;
            r.x = s.x;
            r.y = s.y;
            r.z = s.z;
            r.amplitude = s.amplitude;
            r.startTime = s.startTime;
            r.endTime = s.endTime;
            r.bd = s.bd;
            r.bi = s.bi;
            r.pd = s.pd;
            r.pi = s.pi;
            rows.push_back(r);
        }
        stimParamModel_->setRows(std::move(rows));
    }
    if (treatmentProtocolModel_ != nullptr) {
        std::vector<TreatmentProtocolRow> rows;
        rows.reserve(data.treatmentProtocol.size());
        for (const beam::gui::TreatmentProtocolRecord& t : data.treatmentProtocol) {
            TreatmentProtocolRow r;
            r.number = t.number;
            r.target = QString::fromStdString(t.target);
            r.duration = t.duration;
            r.amplitude = t.amplitude;
            r.parameters = QString::fromStdString(t.parameters);
            r.responsePain = t.pain;
            r.responseMood = t.mood;
            r.notes = QString::fromStdString(t.notes);
            rows.push_back(r);
        }
        treatmentProtocolModel_->setRows(std::move(rows));
    }
}

void BeamMainWindow::saveSession() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Session"), QString(),
                                                        QStringLiteral("Beam session (*.beamsession)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        sonicationStatusLabel_->setText(QStringLiteral("Save failed: could not open %1").arg(path));
        return;
    }
    const std::string text = beam::gui::serializeSession(collectSessionData());
    file.write(text.data(), static_cast<qint64>(text.size()));
}

void BeamMainWindow::loadSession() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load Session"), QString(),
                                                        QStringLiteral("Beam session (*.beamsession)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        sonicationStatusLabel_->setText(QStringLiteral("Load failed: could not open %1").arg(path));
        return;
    }
    const QByteArray bytes = file.readAll();
    try {
        applySessionData(
            beam::gui::deserializeSession(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
        if (onSessionLoaded_) onSessionLoaded_();
    } catch (const std::exception& e) {
        sonicationStatusLabel_->setText(QStringLiteral("Load failed: %1").arg(QString::fromStdString(e.what())));
    }
}

void BeamMainWindow::setSessionLoadedHandler(std::function<void()> onSessionLoaded) {
    onSessionLoaded_ = std::move(onSessionLoaded);
}

void BeamMainWindow::setLoadMriHandler(std::function<void(QString)> onLoadMri) {
    onLoadMri_ = std::move(onLoadMri);
}

namespace {
const QString kLastMriDirSettingsKey = QStringLiteral("lastMriDir");

// A plain .ini file next to the executable -- not the Windows registry
// (QSettings's own default store on this platform). Beam's own
// self-contained config, matching how its Qt/DCMTK DLLs already sit
// alongside beam_app.exe rather than being installed system-wide.
QSettings appSettings() {
    return QSettings(QCoreApplication::applicationDirPath() + QStringLiteral("/beam.ini"), QSettings::IniFormat);
}

}  // namespace

void BeamMainWindow::loadMriFile() {
    // uigetfile's own real filter (loadMRIRAS.m's file branch reads a
    // single .nii*); onLoadMri_ itself dispatches through
    // beam::infra::dicom::loadMriRas, so a single DICOM file picked here
    // would also work, but the filter stays NIfTI-only to keep this
    // action's own label ("Load File") matching what it's actually for.
    QSettings settings = appSettings();
    const QString startDir = settings.value(kLastMriDirSettingsKey).toString();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load MRI File"), startDir,
                                                        QStringLiteral("NIfTI (*.nii *.nii.gz)"));
    if (path.isEmpty()) return;
    settings.setValue(kLastMriDirSettingsKey, QFileInfo(path).absolutePath());
    if (onLoadMri_) onLoadMri_(path);
}

void BeamMainWindow::loadMriFolder() {
    // uigetdir's real counterpart -- a DICOM series directory.
    QSettings settings = appSettings();
    const QString startDir = settings.value(kLastMriDirSettingsKey).toString();
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Load MRI Folder"), startDir);
    if (path.isEmpty()) return;
    settings.setValue(kLastMriDirSettingsKey, path);
    if (onLoadMri_) onLoadMri_(path);
}

void BeamMainWindow::setSafetyReport(const beam::gui::SonicationSafetyReport& report) {
    safetyView_->setReport(report);
}

void BeamMainWindow::setPulseWaveformChart(QChart* chart) {
    if (QLayoutItem* old = pulseChartLayout_->takeAt(0)) {
        delete old->widget();
        delete old;
    }
    if (chart != nullptr) {
        pulseChartLayout_->addWidget(new QChartView(chart));
    } else {
        pulseChartLayout_->addWidget(placeholderTab(QStringLiteral("Pulse waveform — no sonication loaded")));
    }
}

void BeamMainWindow::setSonicationTimelineChart(QChart* chart) {
    if (QLayoutItem* old = timelineChartLayout_->takeAt(0)) {
        delete old->widget();
        delete old;
    }
    if (chart != nullptr) {
        timelineChartLayout_->addWidget(new QChartView(chart));
    } else {
        timelineChartLayout_->addWidget(
            placeholderTab(QStringLiteral("Sonication timeline — no sonication loaded")));
    }
}

void BeamMainWindow::setSonicateHandler(std::function<void(bool)> onSonicate) {
    QObject::connect(sonicateButton_, &QPushButton::clicked, this, [this, onSonicate]() {
        onSonicate(triggerModeCombo_->currentIndex() == 1);  // 1 == "External"
    });
}

void BeamMainWindow::setSonicationStatus(const QString& text) { sonicationStatusLabel_->setText(text); }

void BeamMainWindow::setShamHandler(std::function<void()> onSham) {
    QObject::connect(shamButton_, &QPushButton::clicked, this, [onSham]() { onSham(); });
}

void BeamMainWindow::setSerialPorts(const std::vector<QString>& ports) {
    serialPortCombo_->clear();
    for (const QString& port : ports) serialPortCombo_->addItem(port);
}

void BeamMainWindow::setSerialConnectHandler(std::function<void(QString)> onSerialConnect) {
    QObject::connect(serialConnectButton_, &QPushButton::clicked, this, [this, onSerialConnect]() {
        onSerialConnect(serialPortCombo_->currentText());
    });
}

void BeamMainWindow::setSerialConnectedState(bool connected) {
    // connectSerial.m: red (Color = [1,0,0]) before attempting, green
    // ([0,1,0]) on success -- same failure color as setLampOn's default
    // "off" reuse here rather than duplicating a third color.
    connectedLamp_->setStyleSheet(connected ? QStringLiteral("background-color: rgb(0,255,0);")
                                             : QStringLiteral("background-color: rgb(255,0,0);"));
}

void BeamMainWindow::setGetParamsHandler(std::function<void()> onGetParams) {
    QObject::connect(getParamsButton_, &QPushButton::clicked, this, [onGetParams]() { onGetParams(); });
}

void BeamMainWindow::setAbortSonicationHandler(std::function<void()> onAbortSonication) {
    QObject::connect(abortSonicationButton_, &QPushButton::clicked, this,
                     [onAbortSonication]() { onAbortSonication(); });
}

void BeamMainWindow::startSonicationCountdown(int durationSeconds) {
    countdownDurationSeconds_ = durationSeconds;
    countdownTickCount_ = 0;
    countdownLabel_->setText(QString::fromStdString(beam::gui::countdownDisplayText(durationSeconds)));
    countdownTimer_->start();
}

QString BeamMainWindow::sonicationCountdownText() const { return countdownLabel_->text(); }

void BeamMainWindow::setStimParamTableModel(StimParamTableModel* model) {
    stimParamModel_ = model;
    stimParamTableView_->setModel(model);
    QObject::connect(sortByOrderButton_, &QPushButton::clicked, model,
                     &StimParamTableModel::sortByOrderColumn);
    QObject::connect(addSonicationButton_, &QPushButton::clicked, model,
                     &StimParamTableModel::addSonicationRow);
    QObject::connect(removeMarkedButton_, &QPushButton::clicked, model,
                     &StimParamTableModel::removeMarkedRows);

    refreshTargetCrosshairs();
    QObject::connect(model, &QAbstractItemModel::dataChanged, this,
                     [this](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                         refreshTargetCrosshairs();
                     });
    QObject::connect(model, &QAbstractItemModel::modelReset, this, [this]() { refreshTargetCrosshairs(); });
}

void BeamMainWindow::setTreatmentProtocolTableModel(TreatmentProtocolTableModel* model) {
    treatmentProtocolModel_ = model;
    treatmentProtocolTableView_->setModel(model);

    QObject::connect(computeBestTargetsButton_, &QPushButton::clicked, this, [this, model]() {
        beam::gui::AccFlag flag = beam::gui::AccFlag::kOther;
        switch (accFlagCombo_->currentIndex()) {
            case 1: flag = beam::gui::AccFlag::kScc; break;
            case 2: flag = beam::gui::AccFlag::kAmcc; break;
            default: flag = beam::gui::AccFlag::kOther; break;
        }
        const beam::gui::BestTargets best = model->computeBestTargets(flag);

        bestTargetsResultList_->clear();
        for (std::size_t i = 0; i < best.name.size(); ++i) {
            QString line = QString::fromStdString(best.name[i]);
            if (i < best.numericResponse.size()) {
                line += QStringLiteral("  (response %1)").arg(best.numericResponse[i]);
            }
            bestTargetsResultList_->addItem(line);
        }
    });
}

void BeamMainWindow::setTreatmentProtocolNames(const std::vector<QString>& names) {
    treatmentProtocolCombo_->clear();
    for (const QString& name : names) {
        treatmentProtocolCombo_->addItem(name);
    }
}

void BeamMainWindow::setVisitNumbers(const std::vector<int>& visitNumbers, int currentVisitNumber) {
    visitNumberCombo_->clear();
    int currentIndex = 0;
    for (std::size_t i = 0; i < visitNumbers.size(); ++i) {
        visitNumberCombo_->addItem(QStringLiteral("Visit %1").arg(visitNumbers[i]), visitNumbers[i]);
        if (visitNumbers[i] == currentVisitNumber) {
            currentIndex = static_cast<int>(i);
        }
    }
    visitNumberCombo_->setCurrentIndex(currentIndex);
}

void BeamMainWindow::setTreatmentProtocolSelectorHandler(std::function<void(const QString&, int)> onSelectionChanged) {
    const auto fire = [this, onSelectionChanged]() {
        if (treatmentProtocolCombo_->count() == 0 || visitNumberCombo_->count() == 0) return;
        onSelectionChanged(currentTreatmentProtocolName(), currentVisitNumber());
    };
    QObject::connect(treatmentProtocolCombo_, &QComboBox::currentIndexChanged, this, fire);
    QObject::connect(visitNumberCombo_, &QComboBox::currentIndexChanged, this, fire);
}

void BeamMainWindow::setNewVisitHandler(std::function<void()> onNewVisit) {
    QObject::connect(newVisitButton_, &QPushButton::clicked, this, [onNewVisit]() { onNewVisit(); });
}

QString BeamMainWindow::currentTreatmentProtocolName() const { return treatmentProtocolCombo_->currentText(); }

int BeamMainWindow::currentVisitNumber() const { return visitNumberCombo_->currentData().toInt(); }

void BeamMainWindow::setTreatmentProtocolDataChangedHandler(std::function<void()> onDataChanged) {
    QObject::connect(treatmentProtocolTableView_->model(), &QAbstractItemModel::dataChanged, this,
                      [onDataChanged](const QModelIndex&, const QModelIndex&) { onDataChanged(); });
}

void BeamMainWindow::setCorrectionCharts(QChart* transmissionBars, QChart* rfFirstArray,
                                         QChart* rfSecondArray) {
    // Clear any previous chart views.
    while (QLayoutItem* item = correctionChartGrid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (transmissionBars != nullptr) {
        correctionChartGrid_->addWidget(new QChartView(transmissionBars), 0, 0, 1, 2);
    }
    if (rfFirstArray != nullptr) {
        correctionChartGrid_->addWidget(new QChartView(rfFirstArray), 1, 0);
    }
    if (rfSecondArray != nullptr) {
        correctionChartGrid_->addWidget(new QChartView(rfSecondArray), 1, 1);
    }
}

void BeamMainWindow::setRunCorrectionHandler(std::function<void()> onRunCorrection) {
    QObject::connect(runCorrectionButton_, &QPushButton::clicked, this,
                     [onRunCorrection]() { onRunCorrection(); });
}

void BeamMainWindow::setCorrectionStatus(const QString& text) { correctionStatusLabel_->setText(text); }

void BeamMainWindow::setFiducialTableModel(FiducialTableModel* model) {
    fiducialModel_ = model;
    QVBoxLayout* layout = registerContentLayout_;
    if (QLayoutItem* old = layout->takeAt(0)) {
        delete old->widget();
        delete old;
    }
    fiducialTableView_ = new QTableView();
    fiducialTableView_->setModel(model);
    fiducialTableView_->setMinimumHeight(80);  // see stimParamTableView_'s comment in the constructor
    layout->insertWidget(0, fiducialTableView_);  // keep it above the MRI row
}

void BeamMainWindow::setMriVolume(const beam::mri::Volume3D& volume) {
    sagitalView_->setVolume(volume);
    coronalView_->setVolume(volume);
    axialView_->setVolume(volume);
}

void BeamMainWindow::setMriOverlays(const beam::mri::Volume3D& arrayMask, const beam::mri::Volume3D& fiducialMask,
                                    const beam::mri::Volume3D& focusMask) {
    lastArrayMask_ = arrayMask;
    lastFiducialMask_ = fiducialMask;
    lastFocusMask_ = focusMask;
    applyOverlayVisibility();
}

// showMrImage's real gating (see the header comment on the checkboxes'
// declaration): ShowTransducers controls the array footprint (and, as
// this port's own new plumbing, the fiducial markers alongside it);
// ShowField controls the focus glow, but only when ShowTransducers is
// *also* checked -- a real source quirk (the focus alpha line reuses
// ShowTransducersCheckBox.Value, apparently copy-pasted), preserved here
// rather than fixed.
void BeamMainWindow::applyOverlayVisibility() {
    const beam::mri::Volume3D empty;
    const beam::mri::Volume3D& arrayMask = showTransducersCheckBox_->isChecked() ? lastArrayMask_ : empty;
    const beam::mri::Volume3D& fiducialMask = showTransducersCheckBox_->isChecked() ? lastFiducialMask_ : empty;
    const bool focusVisible = showFieldCheckBox_->isChecked() && showTransducersCheckBox_->isChecked();
    const beam::mri::Volume3D& focusMask = focusVisible ? lastFocusMask_ : empty;
    sagitalView_->setOverlayVolumes(arrayMask, fiducialMask, focusMask);
    coronalView_->setOverlayVolumes(arrayMask, fiducialMask, focusMask);
    axialView_->setOverlayVolumes(arrayMask, fiducialMask, focusMask);
}

void BeamMainWindow::setArrayCenterMm(const Eigen::Vector3d& centerArrayMm) {
    arrayCenterMm_ = centerArrayMm;
    refreshTargetCrosshairs();
}

void BeamMainWindow::centerSagittalSliceOnMm(double xMm) { sagitalView_->setInitialSliderValueMm(xMm); }

void BeamMainWindow::refreshTargetCrosshairs() {
    // drawROIs.m overwrites every visible row's crosshair position to the
    // same centerArrayMM before drawing -- so this only needs to know
    // whether *any* row is currently shown, not each row's own X/Y/Z.
    bool anyTargetShown = false;
    if (stimParamModel_ != nullptr && showTargetCheckBox_->isChecked()) {
        for (const StimParamRow& row : stimParamModel_->rows()) {
            if (row.show) {
                anyTargetShown = true;
                break;
            }
        }
    }
    const std::vector<Eigen::Vector3d> targets = anyTargetShown ? std::vector<Eigen::Vector3d>{arrayCenterMm_}
                                                                  : std::vector<Eigen::Vector3d>{};
    sagitalView_->setTargetCrosshairs(targets);
    coronalView_->setTargetCrosshairs(targets);
    axialView_->setTargetCrosshairs(targets);
}

void BeamMainWindow::setMriAxes(std::optional<beam::mri::RasAxisVectors> axes) {
    sagitalView_->setAxes(axes);
    coronalView_->setAxes(axes);
    axialView_->setAxes(axes);
}

QString BeamMainWindow::siteId() const { return siteIdEdit_->text(); }
QString BeamMainWindow::visitNumber() const { return visitNumberEdit_->text(); }
QString BeamMainWindow::participantId() const { return participantIdEdit_->text(); }

QString BeamMainWindow::hydrogelSize() const {
    if (hydrogelSizeMediumRadio_->isChecked()) return QStringLiteral("Medium");
    if (hydrogelSizeLargeRadio_->isChecked()) return QStringLiteral("Large");
    return QStringLiteral("Small");
}

int BeamMainWindow::currentSonicationNumber() const { return currentSonicationNumberEdit_->text().toInt(); }

double BeamMainWindow::transducerTransparency() const {
    return static_cast<double>(transducerTransparencySlider_->value()) / 100.0;
}

void BeamMainWindow::setSiteId(const QString& value) { siteIdEdit_->setText(value); }
void BeamMainWindow::setParticipantId(const QString& value) { participantIdEdit_->setText(value); }
void BeamMainWindow::setVisitNumberField(const QString& value) { visitNumberEdit_->setText(value); }

void BeamMainWindow::setRegisterHandler(std::function<void()> onRegister) {
    QObject::connect(registerButton_, &QPushButton::clicked, this, [this, onRegister]() {
        onRegister();
        // RegisterToMRIFiducialsButtonPushed.m: resets all 4 position
        // sliders back to 1 (no shift) on every successful re-registration.
        rightHorizontalPositionSlider_->setValue(1);
        rightVerticalPositionSlider_->setValue(1);
        leftHorizontalPositionSlider_->setValue(1);
        leftVerticalPositionSlider_->setValue(1);
    });
}

void BeamMainWindow::setRegisterStatus(const QString& text) { registerStatusLabel_->setText(text); }

namespace {
// The source's [0,1,0] green (registration complete) vs. getOffColor.m's
// [0.85,0.33,0.10] (an orange-red, not gray -- MATLAB's default axes
// "off"/uilamp color), as CSS.
void setLampOn(QLabel* lamp, bool on) {
    lamp->setStyleSheet(on ? QStringLiteral("background-color: rgb(0,255,0);")
                           : QStringLiteral("background-color: rgb(217,84,26);"));
}
}  // namespace

void BeamMainWindow::setRegistrationCheckLampState(const beam::gui::RegistrationCheckLampState& state) {
    setLampOn(rightRegistrationLamp_, state.rightLampOn);
    setLampOn(leftRegistrationLamp_, state.leftLampOn);
    setLampOn(insideMriRegistrationLamp_, state.insideMriLampOn);
    sonicateButton_->setEnabled(state.sonicateButtonEnabled);
}

void BeamMainWindow::setRegisterCurrentPositionHandler(
    std::function<void(double, double)> onRegisterCurrentPosition) {
    QObject::connect(registerCurrentPositionButton_, &QPushButton::clicked, this, [this, onRegisterCurrentPosition]() {
        // registerCurrentTransducerPostion.m: warns (but still proceeds,
        // using Right's value either way) if Left/Right disagree -- same
        // literal message both times in the source, a real copy-paste
        // quirk ("Y values..." for the Z/vertical mismatch too),
        // preserved rather than corrected.
        if (rightHorizontalPositionSlider_->value() != leftHorizontalPositionSlider_->value() ||
            rightVerticalPositionSlider_->value() != leftVerticalPositionSlider_->value()) {
            registerStatusLabel_->setText(
                QStringLiteral("Y values of both transducers must be the same, please check alignment"));
        }
        onRegisterCurrentPosition(rightHorizontalPositionSlider_->value(), rightVerticalPositionSlider_->value());
    });
}

void BeamMainWindow::setPositionSlidersChangedHandler(std::function<void()> onPositionSlidersChanged) {
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_, rightHorizontalPositionSlider_,
                             rightVerticalPositionSlider_}) {
        QObject::connect(slider, &QSlider::valueChanged, this, [onPositionSlidersChanged](int) {
            onPositionSlidersChanged();
        });
    }
}

}  // namespace beam::gui_qt
