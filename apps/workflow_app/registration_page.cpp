#include "workflow_internal.hpp"

namespace beam::app {

void WorkflowWindow::buildRegistrationPage() {
    auto* markerSplitter = new QWidget(this);
    auto* markerSplitterLayout = new QHBoxLayout(markerSplitter);
    markerSplitterLayout->setContentsMargins(0, 0, 0, 0);
    markerSplitterLayout->setSpacing(8);
    markerSplitter->setMinimumHeight(196);
    // Both of step 1's buttons, built here so they can share the row under the diagram.
    const QString stepButtonStyle = QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; "
        "border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } "
        "QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }");
    detectFiducialsButton_ = new QPushButton(QStringLiteral("Detect fiducials"), this);
    detectFiducialsButton_->setObjectName(QStringLiteral("detectFiducialsButton"));
    detectFiducialsButton_->setToolTip(QStringLiteral(
        "Search the MRI for the six marker donuts. Each result carries a confidence; "
        "anything below 70% is flagged and must be checked by hand."));
    detectFiducialsButton_->setMinimumHeight(30);
    detectFiducialsButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    detectFiducialsButton_->setStyleSheet(stepButtonStyle);
    connect(detectFiducialsButton_, &QPushButton::clicked, this, [this] { detectFiducials(); });

    ui_->registrationActions->removeWidget(ui_->resetRegistrationButton);
    ui_->resetRegistrationButton->setText(QStringLiteral("Restore Default"));
    ui_->resetRegistrationButton->setMinimumHeight(30);
    ui_->resetRegistrationButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui_->resetRegistrationButton->setStyleSheet(stepButtonStyle);
    auto* markerDiagramPanel = new QWidget(markerSplitter);
    markerDiagramPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* markerDiagramLayout = new QVBoxLayout(markerDiagramPanel);
    markerDiagramLayout->setContentsMargins(0, 0, 0, 0);
    markerDiagramLayout->setSpacing(0);
    auto* triangleColumn = new QVBoxLayout;
    triangleColumn->setContentsMargins(0, 0, 4, 0);
    triangleColumn->setSpacing(0);
    triangleColumn->addWidget(ui_->registrationFiducialLayout, 0, Qt::AlignTop);
    // One 30px row, matching the Confirm/Register row under the table opposite, so step 3 and
    auto* step1Buttons = new QHBoxLayout;
    step1Buttons->setContentsMargins(0, 0, 0, 0);
    step1Buttons->setSpacing(4);
    // The three buttons stay as hidden drivers so their connections hold.
    fiducialActionsButton_ = new QToolButton(markerDiagramPanel);
    fiducialActionsButton_->setPopupMode(QToolButton::MenuButtonPopup);
    fiducialActionsButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    fiducialActionsButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fiducialActionsButton_->setMinimumHeight(detectFiducialsButton_->minimumHeight());
    fiducialActionsButton_->setStyleSheet(QStringLiteral(
        "QToolButton { background: #176b87; color: white; border: 1px solid #0f5269; "
        "border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QToolButton:hover { background: #2083a3; } "
        "QToolButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; } "
        "QToolButton::menu-button { width: 18px; border-left: 1px solid #0f5269; "
        "border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"));
    const auto fiducialAction = [this](QPushButton* driver) {
        auto* action = new QAction(driver->text(), this);
        action->setToolTip(driver->toolTip());
        connect(action, &QAction::triggered, driver, &QPushButton::click);
        return action;
    };
    auto* fiducialMenu = new QMenu(fiducialActionsButton_);
    QAction* detectAction = fiducialAction(detectFiducialsButton_);
    fiducialMenu->addAction(detectAction);
    fiducialMenu->addSeparator();
    fiducialMenu->addAction(fiducialAction(ui_->resetRegistrationButton));
    fiducialActionsButton_->setMenu(fiducialMenu);
    fiducialActionsButton_->setDefaultAction(detectAction);
    for (QPushButton* driver : {detectFiducialsButton_, ui_->resetRegistrationButton})
        driver->hide();
    step1Buttons->addWidget(fiducialActionsButton_, 1);
    triangleColumn->addLayout(step1Buttons);
    markerDiagramLayout->addLayout(triangleColumn);
    markerDiagramLayout->addStretch(1);
    markerSplitterLayout->addWidget(markerDiagramPanel, 2);
    ui_->registrationFiducialLayout->setMinimumWidth(300);
    ui_->registrationFiducialLayout->setMinimumHeight(0);
    ui_->registrationFiducialLayout->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // Matched to the marker table opposite it, so step 3 below this and step 4 below the table
    ui_->registrationFiducialLayout->setMaximumHeight(kRegistrationColumnHeight);
    ui_->registrationFiducialLayout->setFixedHeight(kRegistrationColumnHeight);
    markerDiagramPanel->setMinimumHeight(264 + kRegistrationColumnHeight - 120);
    // The Imaging tab shows the same diagram; give it the same box so the two tabs do not draw
    ui_->imagingFiducialLayout->setMinimumWidth(300);
    ui_->imagingFiducialLayout->setMinimumHeight(0);
    ui_->imagingFiducialLayout->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui_->imagingFiducialLayout->setMaximumHeight(kRegistrationColumnHeight);
    ui_->imagingFiducialLayout->setFixedHeight(kRegistrationColumnHeight);
    markerDiagramPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    // Every viewer column on every page is built the same way: tight margins around the viewer,
    struct SliceColumn {
        QVBoxLayout* layout;
        QLabel* label;
    };
    for (const SliceColumn column : {
             SliceColumn{ui_->sagittalLayout, ui_->sagittalSliceLabel},
             SliceColumn{ui_->coronalLayout, ui_->coronalSliceLabel},
             SliceColumn{ui_->axialLayout, ui_->axialSliceLabel},
             SliceColumn{ui_->registrationSagittalLayout, ui_->registrationSagittalLabel},
             SliceColumn{ui_->registrationCoronalLayout, ui_->registrationCoronalLabel},
             SliceColumn{ui_->registrationAxialLayout, ui_->registrationAxialLabel},
             SliceColumn{ui_->treatmentSagittalLayout, ui_->treatmentSagittalSliceLabel},
             SliceColumn{ui_->treatmentCoronalLayout, ui_->treatmentCoronalSliceLabel},
             SliceColumn{ui_->treatmentAxialLayout, ui_->treatmentAxialSliceLabel}}) {
        column.layout->setContentsMargins(4, 3, 4, 2);
        column.layout->setSpacing(0);
        column.label->setFixedSize(78, 14);
        column.label->setContentsMargins(0, 0, 0, 0);
        column.label->setMargin(0);
        column.label->setIndent(0);
        column.label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        column.label->setStyleSheet(QStringLiteral("QLabel { font-size: 10px; padding: 0; }"));
        column.layout->setAlignment(column.label, Qt::AlignHCenter | Qt::AlignTop);
    }
    auto* markerTablePanel = new QWidget(markerSplitter);
    markerTablePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* markerTableLayout = new QVBoxLayout(markerTablePanel);
    markerTableLayout->setContentsMargins(0, 0, 0, 0);
    markerTableLayout->setSpacing(0);
    ui_->registrationActions->removeWidget(ui_->registerFiducialsButton);
    ui_->confirmFiducialButton->setText(QStringLiteral("Confirm all located fiducials"));
    ui_->confirmFiducialButton->setToolTip(QStringLiteral(
        "Confirm every fiducial currently marked Located after reviewing the MRI views"));
    ui_->confirmFiducialButton->setMinimumHeight(30);
    ui_->confirmFiducialButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui_->confirmFiducialButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    ui_->registerFiducialsButton->setMinimumHeight(30);
    ui_->registerFiducialsButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui_->registerFiducialsButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    // detectFiducialsButton_ is built earlier, with step 1's button row.

    auto* markerActions = new QHBoxLayout;
    markerActions->setContentsMargins(0, 0, 0, 0);
    for (QPushButton* button : {ui_->confirmFiducialButton, ui_->registerFiducialsButton})
        button->setFixedHeight(kRegistrationButtonHeight);
    markerActions->setSpacing(4);
    markerActions->addWidget(ui_->confirmFiducialButton, 1);
    markerActions->addWidget(ui_->registerFiducialsButton, 1);
    markerTableLayout->addWidget(ui_->registrationTable, 0);
    markerTableLayout->addLayout(markerActions);
    markerTableLayout->setAlignment(ui_->registrationTable, Qt::AlignTop);
    markerTableLayout->setAlignment(markerActions, Qt::AlignTop);
    ui_->registrationLayout->removeWidget(ui_->acceptRegistrationButton);
    ui_->acceptRegistrationButton->setFixedHeight(kRegistrationButtonHeight);
    // Import is where the stage starts, Save where it ends: nothing is worth saving until the
    markerSplitterLayout->addWidget(markerTablePanel, 2);
    auto* step1Group = new QGroupBox(QStringLiteral("1    Locate and review the six fiducials"), this);
    auto* step1GroupLayout = new QVBoxLayout(step1Group);
    step1GroupLayout->setContentsMargins(6, 4, 6, 6);
    auto* step1ActionRow = new QHBoxLayout;
    step1ActionRow->setContentsMargins(0, 0, 0, 0);
    step1ActionRow->setSpacing(4);
    step1ActionRow->addWidget(fiducialActionsButton_, 1);
    step1ActionRow->addWidget(ui_->confirmFiducialButton, 1);
    step1ActionRow->addWidget(ui_->registerFiducialsButton, 1);
    step1GroupLayout->addLayout(step1ActionRow);
    step1GroupLayout->addWidget(markerSplitter);
    ui_->registrationDetailsLayout->addWidget(step1Group, 2);
    // The Designer action row is now empty because its buttons were moved into the two splitter
    ui_->registrationLayout->removeItem(ui_->registrationActions);
    ui_->registrationLayout->removeWidget(ui_->registrationResult);
    ui_->registrationLayout->setSpacing(0);
    // Import now sits in step 1's button row beside Detect and Restore Default: all three act
    ui_->registrationResult->setMaximumHeight(28);
    ui_->registrationResult->hide();
    ui_->registrationDetailsLayout->removeWidget(ui_->registrationResult);
    ui_->registrationDetailsLayout->setContentsMargins(0, 0, 0, 0);
    ui_->registrationDetailsLayout->setSpacing(0);
    ui_->registrationActions->removeWidget(ui_->confirmFiducialButton);

    // BeamV0's second registration stage: calibrate the array's lock position after the
    calibrationGroup_ = new QGroupBox(QStringLiteral("Transducer lock position"), this);
    auto* calibrationGroup = calibrationGroup_;
    auto* calibrationLayout = new QVBoxLayout(calibrationGroup);
    calibrationLayout->setContentsMargins(8, 4, 8, 4);
    calibrationLayout->setSpacing(8);
    auto* calibrationControls = new QHBoxLayout;
    calibrationControls->setSpacing(8);
    calibrationControls->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    calibrationLayout->addLayout(calibrationControls);
    auto makePositionSlider = [](QWidget* parent, Qt::Orientation orientation) {
        auto* slider = new NumberedSlider(orientation, parent);
        slider->setRange(1, 4);
        slider->setValue(1);
        slider->setSingleStep(1);
        slider->setPageStep(1);
        slider->setTickPosition(QSlider::NoTicks);
        slider->setToolTip(QStringLiteral("Array lock position; each step is 7.5 mm horizontal or 10 mm vertical"));
        slider->setStyleSheet(QString::fromLatin1(kSliderStyle));
        // After setStyleSheet: a stylesheet installs QStyleSheetStyle and would otherwise
        slider->setStyle(new ClickToPositionStyle(slider->style()));
        return slider;
    };
    auto makeLockPanel = [&](const QString& title, QSlider*& horizontal, QSlider*& vertical) {
        auto* panel = new QGroupBox(title, calibrationGroup);
        panel->setMinimumWidth(195);
        // 145 plus the height the status message used to take above the buttons: the taller
        panel->setMinimumHeight(178);
        panel->setMaximumHeight(178);
        panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* panelLayout = new QGridLayout(panel);
        panelLayout->setContentsMargins(5, 2, 5, 2);
        panelLayout->setHorizontalSpacing(7);
        panelLayout->setVerticalSpacing(2);
        panelLayout->setColumnMinimumWidth(1, 22);
        vertical = makePositionSlider(panel, Qt::Vertical);
        vertical->setObjectName(title.startsWith(QStringLiteral("Subject Left"))
                                    ? QStringLiteral("leftVerticalPositionSlider")
                                    : QStringLiteral("rightVerticalPositionSlider"));
        horizontal = makePositionSlider(panel, Qt::Horizontal);
        horizontal->setObjectName(title.startsWith(QStringLiteral("Subject Left"))
                                      ? QStringLiteral("leftHorizontalPositionSlider")
                                      : QStringLiteral("rightHorizontalPositionSlider"));
        vertical->setFixedHeight(121);
        vertical->setFixedWidth(50);
        vertical->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        horizontal->setMinimumWidth(120);
        horizontal->setMinimumHeight(48);
        horizontal->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // Horizontal first, matching the H3V4 readout and the array's own
        // LeftY1Z3 naming, where Y is the horizontal lock.
        panelLayout->addWidget(new QLabel(QStringLiteral("Horizontal (Y)"), panel), 0, 0, Qt::AlignCenter);
        panelLayout->addWidget(horizontal, 1, 0, 2, 1, Qt::AlignCenter);
        panelLayout->addWidget(new QLabel(QStringLiteral("Vertical (Z)"), panel), 0, 1, 1, 2, Qt::AlignCenter);
        panelLayout->addWidget(vertical, 1, 1, 2, 2, Qt::AlignCenter);
        return panel;
    };
    leftLockPanel_ = makeLockPanel(QStringLiteral("Subject Left"),
                                   leftHorizontalPositionSlider_, leftVerticalPositionSlider_);
    rightLockPanel_ = makeLockPanel(QStringLiteral("Subject Right"),
                                    rightHorizontalPositionSlider_, rightVerticalPositionSlider_);
    calibrationControls->addWidget(leftLockPanel_, 0);
    calibrationControls->addWidget(rightLockPanel_, 0);
    registerCurrentPositionButton_ = new QPushButton(QStringLiteral("Register Arrays to Current Position"), calibrationGroup);
    registerCurrentPositionButton_->setFixedHeight(kRegistrationButtonHeight);
    registerCurrentPositionButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    registerCurrentPositionButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 5px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } "
        "QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    registerCurrentPositionButton_->setToolTip(QStringLiteral(
        "Apply the BeamV0 lock-position offset after registering the array to MRI fiducials"));
    // Without this the spare height lands between the panels and the button, pushing it off the
    auto* step2Buttons = new QHBoxLayout;
    step2Buttons->setContentsMargins(0, 0, 0, 0);
    step2Buttons->setSpacing(4);
    // One height for every button on this page.
    for (QPushButton* button : {registerCurrentPositionButton_, ui_->acceptRegistrationButton}) {
        button->setFixedHeight(kRegistrationButtonHeight);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    step2Buttons->addWidget(registerCurrentPositionButton_, 1);
    step2Buttons->addWidget(ui_->acceptRegistrationButton, 1);
    // Above the buttons, not below: most of these messages are preconditions on the buttons
    calibrationLayout->addLayout(step2Buttons);
    calibrationLayout->addWidget(ui_->registrationResult);
    // Slack below everything: anywhere above the buttons it walks them off the line they share
    calibrationLayout->addStretch(1);
    calibrationGroup->setMinimumHeight(0);
    calibrationGroup->setMaximumHeight(QWIDGETSIZE_MAX);
    calibrationGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    calibrationGroup->setTitle(QStringLiteral("2    Set the transducer lock position and accept"));
    ui_->registrationDetailsLayout->addWidget(calibrationGroup, 1);

    // The presses this stage needs, at the top of the pane each one belongs to rather than
    auto* step2ButtonRow = new QHBoxLayout;
    step2ButtonRow->setContentsMargins(0, 0, 0, 0);
    step2ButtonRow->setSpacing(4);
    step2ButtonRow->addWidget(registerCurrentPositionButton_, 1);
    step2ButtonRow->addWidget(ui_->acceptRegistrationButton, 1);
    // Saving belongs where the expensive thing was just done: the fiducials and the lock
    saveSessionOnRegistrationButton_ =
        new QPushButton(QStringLiteral("Save session..."), calibrationGroup_);
    saveSessionOnRegistrationButton_->setFixedHeight(kRegistrationButtonHeight);
    saveSessionOnRegistrationButton_->setStyleSheet(
        registerCurrentPositionButton_->styleSheet());
    saveSessionOnRegistrationButton_->setToolTip(
        QStringLiteral("Writes the MRI, the fiducials, the lock position and this patient's "
                       "record to one file, beside the MRIs."));
    connect(saveSessionOnRegistrationButton_, &QPushButton::clicked, this,
            [this] { saveTreatmentSessionFile(); });
    calibrationLayout->insertLayout(0, step2ButtonRow);
    // Its own line: sharing the row above clipped "Accept registration and continue" to "ccept
    saveSessionOnRegistrationButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Beside Save session, because both belong to the lock position: one shows what the sixteen
    overlayAllTargetsButton_ = new QPushButton(QStringLiteral("Overlay lock positions"),
                                               calibrationGroup_);
    overlayAllTargetsButton_->setCheckable(true);
    overlayAllTargetsButton_->setFixedHeight(kRegistrationButtonHeight);
    overlayAllTargetsButton_->setStyleSheet(registerCurrentPositionButton_->styleSheet());
    // Takes the width Save session leaves: the stretch used to swallow it.
    overlayAllTargetsButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    overlayAllTargetsButton_->setToolTip(
        QStringLiteral("Draws the target each of the sixteen lock positions would reach on the "
                       "sagittal view, here and on the Treatment page. The notches move in "
                       "AP and IS, so they all project onto a sagittal slice."));
    connect(overlayAllTargetsButton_, &QPushButton::toggled, this,
            [this](bool on) { setLockOverlayShown(on); });
    auto* saveSessionRow = new QHBoxLayout;
    saveSessionRow->setContentsMargins(0, 0, 0, 0);
    saveSessionRow->addWidget(overlayAllTargetsButton_, 1);
    saveSessionRow->addWidget(saveSessionOnRegistrationButton_, 0);
    calibrationLayout->insertLayout(1, saveSessionRow);
    // Calibration sliders use their own sliderPressed/valueChanged handlers below.
    ui_->registrationResult->show();
    // Two wrapped lines at a fixed height.
    ui_->registrationResult->setWordWrap(true);
    ui_->registrationResult->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui_->registrationResult->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui_->registrationResult->setFixedHeight(62);
    // A message too long for one line is elided, so hovering has to give it back.
    ui_->registrationResult->installEventFilter(this);
    // BeamV0's drawROIs.m keeps a running readout of where the array actually is:
    arrayPositionLabel_ = new QLabel(this);
    arrayPositionLabel_->setObjectName(QStringLiteral("arrayPositionLabel"));
    arrayPositionLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    arrayPositionLabel_->setStyleSheet(QStringLiteral(
        "QLabel { font-family: 'Consolas','Courier New',monospace; font-size: 12px; color: #d7e3e7; "
        "background: #24323a; border: 1px solid #3c5059; border-radius: 4px; padding: 5px 8px; }"));
    markerTableLayout->addWidget(arrayPositionLabel_);
    // The readout says where the array is; this goes there.
    moveToArrayPositionButton_ = new QPushButton(QStringLiteral("Move to Target"), this);
    moveToArrayPositionButton_->setToolTip(
        QStringLiteral("Centre every MRI view on the array position above."));
    moveToArrayPositionButton_->setMinimumHeight(26);
    moveToArrayPositionButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; border-radius: 4px; padding: 4px 8px; font-weight: 600; } "
        "QPushButton:hover { background: #2083a3; } "
        "QPushButton:disabled { background: #414141; color: #8b8b8b; border-color: #555555; }"));
    connect(moveToArrayPositionButton_, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_ || targetMm_.isZero()) {
            showMessage(QStringLiteral("The array has no position yet. Register it to the "
                                       "current lock position first."),
                        true);
            return;
        }
        focusTreatmentViewsOn(targetMm_);
    });
    markerTableLayout->addWidget(moveToArrayPositionButton_);
    markerTableLayout->addStretch(1);
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_})
        connect(slider, &QSlider::valueChanged, this, [this] {
            // Slider changes are already delivered after Qt has accepted the new value.
            lastLockChangeMs_ = QDateTime::currentMSecsSinceEpoch();
            if (registrationLockPositionRegistered())
                setRegistrationPhase(RegistrationPhase::FitApplied);
            // Through the one function that knows the rules, so a mismatched
            // pair of panels disables the button here too.
            updateRegistrationAvailability();
        });
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_})
        connect(slider, &QSlider::sliderPressed, this, [this] {
            // Pressing a calibration track is an explicit request to review or redo Step 3,
            if (registrationLockPositionRegistered())
                setRegistrationPhase(RegistrationPhase::FitApplied);
            // Through the one function that knows the rules, so a mismatched
            // pair of panels disables the button here too.
            updateRegistrationAvailability();
        });
    connect(registerCurrentPositionButton_, &QPushButton::clicked, this,
            [this] { performCurrentPositionRegistration(); });
    registerCurrentPositionButton_->setEnabled(false);
}

}  // namespace beam::app
