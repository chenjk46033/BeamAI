#include "workflow_internal.hpp"

namespace beam::app {


WorkflowWindow::WorkflowWindow(QWidget* parent) : QMainWindow(parent), ui_(new Ui::WorkflowShell) {
    ui_->setupUi(this);
    // Align each workflow body with the top of the process-navigation panel; the large
    ui_->pageTitle->hide();
    ui_->pageSubtitle->hide();
    ui_->contentLayout->setContentsMargins(28, 0, 28, 12);
    // Imaging, Registration and Treatment plan all show the same three MRI viewers, so one rule
    for (QHBoxLayout* mriRow : {ui_->mriPreviewLayout, ui_->registrationImages,
                                ui_->treatmentMriViews})
        mriRow->setAlignment(Qt::AlignTop);
    for (QGroupBox* group : {ui_->sagittalGroup, ui_->coronalGroup, ui_->axialGroup,
                             ui_->registrationSagittalGroup, ui_->registrationCoronalGroup,
                             ui_->registrationAxialGroup, ui_->treatmentSagittalGroup,
                             ui_->treatmentCoronalGroup, ui_->treatmentAxialGroup})
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Spare page height belongs below the viewers.
    ui_->imagingGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui_->imagingLayout->addStretch(1);
    ui_->placeholderPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui_->placeholderLayout->setContentsMargins(0, 0, 0, 0);
    // Keep the loaded-file line compact.
    ui_->mriPathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ui_->mriPathLabel->setMinimumHeight(0);
    ui_->mriPathLabel->setMaximumHeight(36);
    ui_->mriPathLabel->setContentsMargins(0, 0, 0, 0);
    ui_->registrationInstructions->setStyleSheet(
        QStringLiteral("QLabel { color: #17313f; background: #f1f8fa; "
                       "border: 1px solid #b7d1d9; border-radius: 4px; "
                       "padding: 8px; }"));
    ui_->registrationInstructions->setText(QStringLiteral(
        "Select a fiducial in the triangle diagram to navigate the three planes to it. Drag its red ring onto "
        "the centre of the grayscale donut, verify it in all three views, and repeat for all six. Each drag "
        "sets only the two coordinates lying in that plane, so refining one view never moves the fiducial in "
        "the third. To place a fiducial somewhere far from where it sits, right-click the point and use "
        "\"Move fiducial … to mouse point\"."));
    ui_->registrationInstructions->hide();
    // Use the original deep slate-blue work surface.
    const QColor bodySurface(27, 27, 27);
    for (QWidget* page : {ui_->casePage, ui_->systemPage, ui_->imagingPage,
                          ui_->registrationPage, ui_->placeholderPage}) {
        page->setAutoFillBackground(true);
        QPalette palette = page->palette();
        palette.setColor(QPalette::Window, bodySurface);
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::WindowText, QColor(242, 247, 248));
        palette.setColor(QPalette::Text, QColor(242, 247, 248));
        page->setPalette(palette);
    }
    // The page palette is intentionally light-text for the dark work surface.
    for (QLineEdit* edit : findChildren<QLineEdit*>()) styleInputField(edit);
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setStyleSheet(QStringLiteral(
            "QComboBox { background: #ffffff; color: #17313f; } "
            "QComboBox QAbstractItemView { background: #ffffff; color: #17313f; selection-background-color: #58c7e5; }"));
    }
    ui_->stageList->setAutoFillBackground(true);
    QPalette stagePalette = ui_->stageList->palette();
    stagePalette.setColor(QPalette::Base, QColor(36, 36, 36));
    stagePalette.setColor(QPalette::Window, QColor(36, 36, 36));
    stagePalette.setColor(QPalette::Text, QColor(242, 247, 248));
    stagePalette.setColor(QPalette::WindowText, QColor(242, 247, 248));
    ui_->stageList->setPalette(stagePalette);
    ui_->registrationOverlayControls->setSpacing(4);
    for (QCheckBox* checkBox : {ui_->registrationShowFieldCheckBox,
                                ui_->registrationShowTargetCheckBox,
                                ui_->registrationShowTransducersCheckBox,
                                ui_->registrationShowFiducialsCheckBox}) {
        checkBox->setStyleSheet(QStringLiteral("QCheckBox { color: #f2f7f8; spacing: 5px; }"));
        QPalette palette = checkBox->palette();
        palette.setColor(QPalette::WindowText, QColor(242, 247, 248));
        palette.setColor(QPalette::Text, QColor(242, 247, 248));
        checkBox->setPalette(palette);
        checkBox->setMinimumWidth(checkBox->sizeHint().width());
    }
    for (QLabel* label : {ui_->registrationBrightnessControlsLabel,
                          ui_->registrationTransparencyLabel})
        label->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; }"));
    for (QToolButton* button : {ui_->registrationBrightnessUpButton,
                                ui_->registrationBrightnessDownButton,
                                ui_->registrationResetAllMriViewsButton})
        button->setStyleSheet(QStringLiteral("QToolButton { color: #f2f7f8; }"));
    // Their stylesheet turns the text bold after Qt sized them, so the label is
    // wider than the hint the layout was given and gets clipped.
    for (QPushButton* button :
         {ui_->resetInitialSlicesButton, ui_->registrationResetInitialSlicesButton,
          ui_->registrationReturnStartButton}) {
        button->ensurePolished();
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 34);
    }
    for (QSlider* slider : findChildren<QSlider*>())
        slider->setStyleSheet(QString::fromLatin1(kSliderStyle));
    // The generic case subtitle consumes vertical space on every workflow page; Registration
    ui_->pageSubtitle->hide();

    // Registration is kept as a fixed, non-scrolling page.

    // Registration contains additional controls below the MRI row, so its layout can otherwise
    QTimer::singleShot(0, this, [this] { mriHeightSyncPasses_ = 0; syncMriViewerHeights(); });

    // Keep the registration actions in their Designer order: immediately below the marker
    ui_->registrationDetailsLayout->removeWidget(ui_->registrationFiducialLayout);
    ui_->registrationDetailsLayout->removeWidget(ui_->registrationTable);
    buildRegistrationPage();

    // Fiducials are part of the operator's spatial reference and are visible by default on
    for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->coronalPreviousButton,
                                ui_->axialPreviousButton, ui_->registrationSagittalPreviousButton,
                                ui_->registrationCoronalPreviousButton, ui_->registrationAxialPreviousButton,
                                ui_->treatmentSagittalPreviousButton, ui_->treatmentCoronalPreviousButton,
                                ui_->treatmentAxialPreviousButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x25C0)));
    }
    for (QToolButton* button : {ui_->sagittalNextButton, ui_->coronalNextButton,
                                ui_->axialNextButton, ui_->registrationSagittalNextButton,
                                ui_->registrationCoronalNextButton, ui_->registrationAxialNextButton,
                                ui_->treatmentSagittalNextButton, ui_->treatmentCoronalNextButton,
                                ui_->treatmentAxialNextButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x25B6)));
    }
    for (QToolButton* button : {ui_->resetSagittalButton, ui_->resetCoronalButton,
                                ui_->resetAxialButton, ui_->registrationResetSagittalButton,
                                ui_->registrationResetCoronalButton, ui_->registrationResetAxialButton,
                                ui_->treatmentSagittalResetButton, ui_->treatmentCoronalResetButton,
                                ui_->treatmentAxialResetButton,
                                ui_->resetAllMriViewsButton, ui_->registrationResetAllMriViewsButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x21BA)));
    }
    for (QToolButton* button : {ui_->brightnessDownButton, ui_->registrationBrightnessDownButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x2193)));
    }
    for (QToolButton* button : {ui_->brightnessUpButton, ui_->registrationBrightnessUpButton}) {
        button->setIcon(QIcon());
        button->setText(QString(QChar(0x2191)));
    }
    const QString sliceButtonStyle = QStringLiteral(
        "QToolButton { background: #f7fafb; color: #17313f; border: 1px solid #9eacb4; "
        "border-radius: 3px; min-width: 22px; max-width: 22px; min-height: 20px; max-height: 20px; padding: 0; "
        "font-family: 'Segoe UI Symbol'; font-size: 12px; font-weight: 700; } "
        "QToolButton:hover { background: #e3f2f6; border-color: #2888a2; } "
        "QToolButton:pressed { background: #cce7ee; } "
        "QToolButton:disabled { background: #e8ecee; border-color: #c9d0d4; }");
    for (QToolButton* button : {ui_->sagittalPreviousButton, ui_->sagittalNextButton,
                                ui_->coronalPreviousButton, ui_->coronalNextButton,
                                ui_->axialPreviousButton, ui_->axialNextButton,
                                ui_->resetSagittalButton, ui_->resetCoronalButton,
                                ui_->resetAxialButton, ui_->registrationSagittalPreviousButton,
                                ui_->registrationSagittalNextButton, ui_->registrationCoronalPreviousButton,
                                ui_->registrationCoronalNextButton, ui_->registrationAxialPreviousButton,
                                ui_->registrationAxialNextButton, ui_->registrationResetSagittalButton,
                                ui_->registrationResetCoronalButton, ui_->registrationResetAxialButton,
                                ui_->treatmentSagittalPreviousButton, ui_->treatmentSagittalNextButton,
                                ui_->treatmentSagittalResetButton, ui_->treatmentCoronalPreviousButton,
                                ui_->treatmentCoronalNextButton, ui_->treatmentCoronalResetButton,
                                ui_->treatmentAxialPreviousButton, ui_->treatmentAxialNextButton,
                                ui_->treatmentAxialResetButton,
                                ui_->brightnessDownButton, ui_->brightnessUpButton,
                                ui_->registrationBrightnessDownButton, ui_->registrationBrightnessUpButton,
                                ui_->resetAllMriViewsButton, ui_->registrationResetAllMriViewsButton})
        button->setStyleSheet(sliceButtonStyle);
    const QString acceptButtonStyle = QStringLiteral(
        "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; "
        "border-radius: 5px; min-height: 34px; padding: 4px 18px; font-weight: 700; } "
        "QPushButton:hover { background: #2083a3; border-color: #0c4357; } "
        "QPushButton:pressed { background: #10566e; } "
        "QPushButton:disabled { background: #d9e0e4; color: #75838b; border-color: #c4cdd2; }");
    for (QPushButton* button : {ui_->acceptDeviceReadinessButton, ui_->acceptImagingButton,
                                ui_->acceptRegistrationButton}) {
        button->setStyleSheet(acceptButtonStyle);
        button->setMinimumWidth(220);
    }
    // acceptButtonStyle carries min-height: 34px, and a stylesheet minimum feeds
    ui_->acceptRegistrationButton->setStyleSheet(
        acceptButtonStyle +
        QStringLiteral(" QPushButton { min-height: %1px; max-height: %1px; padding: 4px 14px; }")
            .arg(kRegistrationButtonHeight - 10));
    ui_->acceptRegistrationButton->setFixedHeight(kRegistrationButtonHeight);
    for (QLabel* label : findChildren<QLabel*>()) {
        label->setTextInteractionFlags(label->textInteractionFlags() |
                                       Qt::TextSelectableByMouse |
                                       Qt::TextSelectableByKeyboard);
        label->setFocusPolicy(Qt::ClickFocus);
    }
    // Fixed-size resource indicators keep the label origin stable while providing an explicit X
    for (QCheckBox* checkBox : findChildren<QCheckBox*>()) {
        checkBox->setStyleSheet(QStringLiteral(
            "QCheckBox { spacing: 6px; padding: 2px 3px; background: transparent; } "
            "QCheckBox::indicator { width: 13px; height: 13px; } "
            "QCheckBox::indicator:unchecked { image: url(:/checkbox/checkbox_unchecked.xpm); } "
            "QCheckBox::indicator:checked { image: url(:/checkbox/checkbox_checked.xpm); }"));
    }
    for (std::size_t i = 0; i < stageCount; ++i) ui_->stageList->addItem(QString());
    // Hardware readiness is not a prerequisite for offline MRI loading and registration.
    ui_->stageList->item(static_cast<int>(beam::gui::WorkflowStage::SystemCheck))->setHidden(true);
    ui_->deviceGroup->hide();
    // Correction is executed as part of the combined Correction & coupling gate; keep its
    ui_->stageList->item(static_cast<int>(beam::gui::WorkflowStage::Correction))->setHidden(true);
    // Safety review and treatment delivery are folded into the Treatment tab: its Accept
    ui_->stageList->item(static_cast<int>(beam::gui::WorkflowStage::SafetyReview))->setHidden(true);
    ui_->stageList->item(static_cast<int>(beam::gui::WorkflowStage::Treatment))->setHidden(true);

    // Coupling is kept as a real Qt page (rather than the former generic placeholder) so the
    ui_->placeholderText->hide();
    couplingTitleLabel_ = new QLabel(QStringLiteral("Correction & coupling"), ui_->placeholderPage);
    couplingTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    couplingTitleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    couplingDescriptionLabel_ = new QLabel(
        QStringLiteral("Run the BeamV0 through-transmit measurement. It evaluates correction data and verifies that acoustic coupling is adequate before treatment."),
        ui_->placeholderPage);
    couplingDescriptionLabel_->setWordWrap(true);
    couplingDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    couplingStatusLabel_ = new QLabel(QStringLiteral("No coupling measurement has been run."), ui_->placeholderPage);
    couplingStatusLabel_->setWordWrap(true);
    couplingStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    couplingProgressBar_ = new QProgressBar(ui_->placeholderPage);
    couplingProgressBar_->setRange(0, 100);
    couplingProgressBar_->setValue(0);
    couplingProgressBar_->setFormat(QStringLiteral("Transmission: %p%"));
    couplingProgressBar_->setMinimumHeight(24);
    couplingProgressBar_->setStyleSheet(QStringLiteral(
        "QProgressBar { color: #eafaff; background: #263a42; border: 1px solid #416b78; border-radius: 4px; text-align: center; } "
        "QProgressBar::chunk { background: #2fa7c7; border-radius: 3px; }"));
    runCouplingCheckButton_ = new QPushButton(QStringLiteral("Run correction / coupling measurement"), ui_->placeholderPage);
    runCouplingCheckButton_->setMinimumHeight(38);
    runCouplingCheckButton_->setStyleSheet(acceptButtonStyle);
    clearCouplingButton_ = new QPushButton(QStringLiteral("Zero correction"), ui_->placeholderPage);
    clearCouplingButton_->setMinimumHeight(38);
    clearCouplingButton_->setMaximumWidth(160);
    clearCouplingButton_->setToolTip(
        QStringLiteral("Zero the device's correction and discard this measurement"));
    clearCouplingButton_->setStyleSheet(QStringLiteral(
        "QPushButton { padding: 9px 16px; background: #2b4c57; color: #dcebef; "
        "border: 1px solid #416b78; border-radius: 5px; font-weight: 600; } "
        "QPushButton:hover { background: #356070; } "
        "QPushButton:disabled { background: #233038; color: #6d7f87; border-color: #2b3b43; }"));
    clearCouplingButton_->setEnabled(false);
    acceptCouplingButton_ = new QPushButton(QStringLiteral("Accept correction && coupling and continue →"), ui_->placeholderPage);
    acceptCouplingButton_->setMinimumHeight(40);
    acceptCouplingButton_->setEnabled(false);
    acceptCouplingButton_->setStyleSheet(acceptButtonStyle);
    // Every stage hosted on the placeholder page packs its widgets at the top; the trailing
    ui_->placeholderLayout->addWidget(couplingTitleLabel_);
    ui_->placeholderLayout->addWidget(couplingDescriptionLabel_);
    couplingStatusLabel_->setContentsMargins(0, 12, 0, 0);
    ui_->placeholderLayout->addWidget(couplingStatusLabel_);
    ui_->placeholderLayout->addWidget(couplingProgressBar_);
    // The three numbers checkCouplingSafety.m decides on, and its verdict: the bars show the
    couplingNumbersLabel_ = new QLabel(ui_->placeholderPage);
    couplingNumbersLabel_->setTextFormat(Qt::RichText);
    couplingNumbersLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #16323b; border: 1px solid #2b6172; "
        "border-radius: 5px; padding: 8px 12px; }"));
    couplingNumbersLabel_->setText(QStringLiteral("Nothing measured yet."));
    ui_->placeholderLayout->addWidget(couplingNumbersLabel_);
    // updateAttenuationPlots.m draws the raw and filtered receive waveform per array;
    couplingChartsRow_ = new QWidget(ui_->placeholderPage);
    auto* couplingChartsLayout = new QHBoxLayout(couplingChartsRow_);
    couplingChartsLayout->setContentsMargins(0, 0, 0, 0);
    couplingChartsLayout->setSpacing(8);
    for (QChartView** view : {&couplingCh0ChartView_, &couplingCh1ChartView_, &couplingBarChartView_}) {
        *view = new QChartView(couplingChartsRow_);
        (*view)->setRenderHint(QPainter::Antialiasing, true);
        (*view)->setMinimumHeight(260);
        (*view)->hide();
    }
    couplingChartsLayout->addWidget(couplingCh0ChartView_, 2);
    couplingChartsLayout->addWidget(couplingCh1ChartView_, 2);
    couplingChartsLayout->addWidget(couplingBarChartView_, 1);
    // Without a device there are no waveforms to draw.
    couplingChartsPlaceholder_ = new QLabel(
        QStringLiteral("Receive waveforms are drawn here once a measurement is read from the "
                       "device. No Beam device is connected, so this run has no waveform data."),
        couplingChartsRow_);
    couplingChartsPlaceholder_->setWordWrap(true);
    couplingChartsPlaceholder_->setAlignment(Qt::AlignCenter);
    couplingChartsPlaceholder_->setMinimumHeight(210);
    couplingChartsPlaceholder_->setStyleSheet(QStringLiteral(
        "QLabel { color: #9fb3ba; background: #16323b; border: 1px dashed #2b6172; "
        "border-radius: 5px; padding: 12px; }"));
    couplingChartsLayout->addWidget(couplingChartsPlaceholder_, 5);
    couplingChartsRow_->hide();
    ui_->placeholderLayout->addWidget(couplingChartsRow_);
    // Side by side: Run is the action, Clear is its undo, and Clear earns no more width than
    auto* couplingActionRow = new QHBoxLayout;
    couplingActionRow->setContentsMargins(0, 0, 0, 0);
    couplingActionRow->setSpacing(8);
    couplingActionRow->addWidget(runCouplingCheckButton_, 1);
    couplingActionRow->addWidget(clearCouplingButton_, 0);
    ui_->placeholderLayout->addLayout(couplingActionRow);
    ui_->placeholderLayout->addWidget(acceptCouplingButton_);

    // The device link panel.
    buildDeviceLinkPanel();

    treatmentPlanTitleLabel_ = new QLabel(QStringLiteral("Treatment plan"), ui_->placeholderPage);
    treatmentPlanTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    treatmentPlanDescriptionLabel_ = new QLabel(
        QStringLiteral("Review the registered target and select a treatment protocol. Device output remains disabled until the plan is accepted and Safety Review is complete."),
        ui_->placeholderPage);
    treatmentPlanDescriptionLabel_->setWordWrap(true);
    treatmentPlanDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    treatmentPlanTitleLabel_->hide();
    treatmentPlanDescriptionLabel_->hide();
    // The Treatment MRI page and its three viewers are defined in the Qt Designer form, just
    treatmentMriPage_ = ui_->treatmentMriPage;
    treatmentMriPage_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* treatmentMriLayout = ui_->treatmentMriLayout;
    auto* treatmentMriInstruction = new QLabel(
        QStringLiteral("Select a target row, then click a location in any MRI plane. The selected target is updated in RAS coordinates and shown in all three planes."),
        treatmentMriPage_);
    treatmentMriInstruction->setWordWrap(true);
    treatmentMriInstruction->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    treatmentMriInstruction->hide();
    auto* treatmentViewerControls = new QHBoxLayout;
    auto* treatmentShowField = new QCheckBox(QStringLiteral("Show field"), treatmentMriPage_);
    auto* treatmentShowTarget = new QCheckBox(QStringLiteral("Show target"), treatmentMriPage_);
    auto* treatmentShowTransducers = new QCheckBox(QStringLiteral("Show transducers"), treatmentMriPage_);
    auto* treatmentShowFiducials = new QCheckBox(QStringLiteral("Show fiducials"), treatmentMriPage_);
    auto* treatmentShowSteeringRange =
        new QCheckBox(QStringLiteral("Show steering range"), treatmentMriPage_);
    treatmentShowSteeringRange->setToolTip(ui_->showSteeringRangeCheckBox->toolTip());
    for (QCheckBox* check : {treatmentShowField, treatmentShowTarget, treatmentShowTransducers,
                             treatmentShowFiducials, treatmentShowSteeringRange}) {
        check->setStyleSheet(QStringLiteral("QCheckBox { color: #dcebef; padding: 2px; }"));
        treatmentViewerControls->addWidget(check);
    }
    auto* treatmentBrightnessDown = new QToolButton(treatmentMriPage_);
    treatmentBrightnessDown->setText(QStringLiteral("▼"));
    treatmentBrightnessDown->setToolTip(QStringLiteral("Decrease brightness"));
    auto* treatmentBrightnessUp = new QToolButton(treatmentMriPage_);
    treatmentBrightnessUp->setText(QStringLiteral("▲"));
    treatmentBrightnessUp->setToolTip(QStringLiteral("Increase brightness"));
    auto* treatmentBrightnessReset = new QToolButton(treatmentMriPage_);
    treatmentBrightnessReset->setText(QStringLiteral("Reset"));
    treatmentBrightnessReset->setToolTip(QStringLiteral("Reset MRI view brightness"));
    treatmentViewerControls->addWidget(treatmentBrightnessUp);
    treatmentViewerControls->addWidget(treatmentBrightnessDown);
    treatmentViewerControls->addWidget(treatmentBrightnessReset);
    auto* treatmentTransparencyLabel = new QLabel(QStringLiteral("Transducer transparency"), treatmentMriPage_);
    auto* treatmentTransparency = new QSlider(Qt::Horizontal, treatmentMriPage_);
    treatmentTransparency->setRange(0, 100);
    treatmentTransparency->setValue(ui_->transducerTransparencySlider->value());
    treatmentTransparency->setFixedWidth(110);
    // Created after the loop that styles the shell's sliders.
    treatmentTransparency->setStyleSheet(QString::fromLatin1(kSliderStyle));
    treatmentViewerControls->addWidget(treatmentTransparencyLabel);
    treatmentViewerControls->addWidget(treatmentTransparency);
    treatmentViewerControls->addStretch(1);
    treatmentMriLayout->insertLayout(0, treatmentViewerControls);
    connect(treatmentShowField, &QCheckBox::toggled, ui_->showFieldCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowTarget, &QCheckBox::toggled, ui_->showTargetCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowTransducers, &QCheckBox::toggled, ui_->showTransducersCheckBox, &QCheckBox::setChecked);
    connect(treatmentShowFiducials, &QCheckBox::toggled, ui_->showFiducialsCheckBox, &QCheckBox::setChecked);
    connect(ui_->showFieldCheckBox, &QCheckBox::toggled, treatmentShowField, &QCheckBox::setChecked);
    connect(ui_->showTargetCheckBox, &QCheckBox::toggled, treatmentShowTarget, &QCheckBox::setChecked);
    connect(ui_->showTransducersCheckBox, &QCheckBox::toggled, treatmentShowTransducers, &QCheckBox::setChecked);
    connect(ui_->showFiducialsCheckBox, &QCheckBox::toggled, treatmentShowFiducials, &QCheckBox::setChecked);
    connect(treatmentShowSteeringRange, &QCheckBox::toggled, ui_->showSteeringRangeCheckBox,
            &QCheckBox::setChecked);
    connect(ui_->showSteeringRangeCheckBox, &QCheckBox::toggled, treatmentShowSteeringRange,
            &QCheckBox::setChecked);
    connect(treatmentTransparency, &QSlider::valueChanged, ui_->transducerTransparencySlider, &QSlider::setValue);
    connect(ui_->transducerTransparencySlider, &QSlider::valueChanged, treatmentTransparency, &QSlider::setValue);
    connect(treatmentBrightnessDown, &QToolButton::clicked, ui_->brightnessDownButton, &QToolButton::click);
    connect(treatmentBrightnessUp, &QToolButton::clicked, ui_->brightnessUpButton, &QToolButton::click);
    connect(treatmentBrightnessReset, &QToolButton::clicked, ui_->resetAllMriViewsButton, &QToolButton::click);
    treatmentShowField->setChecked(ui_->showFieldCheckBox->isChecked());
    treatmentShowTarget->setChecked(ui_->showTargetCheckBox->isChecked());
    treatmentShowTransducers->setChecked(ui_->showTransducersCheckBox->isChecked());
    treatmentShowFiducials->setChecked(ui_->showFiducialsCheckBox->isChecked());
    treatmentShowSteeringRange->setChecked(ui_->showSteeringRangeCheckBox->isChecked());
    treatmentSagittalPreview_ = ui_->treatmentSagittalPreview;
    treatmentCoronalPreview_ = ui_->treatmentCoronalPreview;
    treatmentAxialPreview_ = ui_->treatmentAxialPreview;
    treatmentSagittalSlider_ = ui_->treatmentSagittalSlider;
    treatmentCoronalSlider_ = ui_->treatmentCoronalSlider;
    treatmentAxialSlider_ = ui_->treatmentAxialSlider;
    // The slice buttons themselves are glyphed and styled with the Imaging and Registration
    connect(ui_->treatmentSagittalPreviousButton, &QToolButton::clicked,
            ui_->sagittalPreviousButton, &QToolButton::click);
    connect(ui_->treatmentSagittalNextButton, &QToolButton::clicked,
            ui_->sagittalNextButton, &QToolButton::click);
    connect(ui_->treatmentSagittalResetButton, &QToolButton::clicked,
            ui_->resetSagittalButton, &QToolButton::click);
    connect(ui_->treatmentCoronalPreviousButton, &QToolButton::clicked,
            ui_->coronalPreviousButton, &QToolButton::click);
    connect(ui_->treatmentCoronalNextButton, &QToolButton::clicked,
            ui_->coronalNextButton, &QToolButton::click);
    connect(ui_->treatmentCoronalResetButton, &QToolButton::clicked,
            ui_->resetCoronalButton, &QToolButton::click);
    connect(ui_->treatmentAxialPreviousButton, &QToolButton::clicked,
            ui_->axialPreviousButton, &QToolButton::click);
    connect(ui_->treatmentAxialNextButton, &QToolButton::clicked,
            ui_->axialNextButton, &QToolButton::click);
    connect(ui_->treatmentAxialResetButton, &QToolButton::clicked,
            ui_->resetAxialButton, &QToolButton::click);

    wireDetachedViewerMenus();

    // The Treatment plan and Treatment stage bodies are BeamV0's Sonicate tab, split across the
    buildTreatmentPlanBody(acceptButtonStyle);
    buildTreatmentExecutionBody(acceptButtonStyle);
    // After the patient chooser: it borrows that table's styling.
    buildReportBody();
    // The plan takes the lion's share: it holds the target list, the grid and the schedule, all
    ui_->placeholderLayout->addWidget(treatmentPlanBody_, 4);
    ui_->placeholderLayout->addWidget(treatmentExecutionBody_, 1);
    ui_->placeholderLayout->addWidget(reportBody_, 1);

    safetyReviewTitleLabel_ = new QLabel(QStringLiteral("Safety Review"), ui_->placeholderPage);
    safetyReviewTitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    safetyReviewDescriptionLabel_ = new QLabel(
        QStringLiteral("Confirm the required safety gates before treatment. This review does not send device output."),
        ui_->placeholderPage);
    safetyReviewDescriptionLabel_->setWordWrap(true);
    safetyReviewDescriptionLabel_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    safetyRegistrationCheckBox_ = new QCheckBox(QStringLiteral("MRI registration accepted"), ui_->placeholderPage);
    safetyCorrectionCheckBox_ = new QCheckBox(QStringLiteral("Correction and coupling accepted"), ui_->placeholderPage);
    safetyPlanCheckBox_ = new QCheckBox(QStringLiteral("Treatment plan accepted"), ui_->placeholderPage);
    for (QCheckBox* check : {safetyRegistrationCheckBox_, safetyCorrectionCheckBox_, safetyPlanCheckBox_}) {
        check->setEnabled(false);
        check->setStyleSheet(QStringLiteral("QCheckBox { color: #f2f7f8; padding: 6px; }"));
    }
    safetyReviewStatusLabel_ = new QLabel(QStringLiteral("Review not yet completed."), ui_->placeholderPage);
    safetyReviewStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    acceptSafetyReviewButton_ = new QPushButton(QStringLiteral("Accept safety review and continue →"), ui_->placeholderPage);
    acceptSafetyReviewButton_->setMinimumHeight(40);
    acceptSafetyReviewButton_->setStyleSheet(acceptButtonStyle);
    ui_->placeholderLayout->addWidget(safetyReviewTitleLabel_);
    ui_->placeholderLayout->addWidget(safetyReviewDescriptionLabel_);
    ui_->placeholderLayout->addWidget(safetyRegistrationCheckBox_);
    ui_->placeholderLayout->addWidget(safetyCorrectionCheckBox_);
    ui_->placeholderLayout->addWidget(safetyPlanCheckBox_);
    ui_->placeholderLayout->addWidget(safetyReviewStatusLabel_);
    ui_->placeholderLayout->addWidget(acceptSafetyReviewButton_);
    // Packs the shorter placeholder stages (coupling, correction, safety) at the top.
    placeholderTailSpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    ui_->placeholderLayout->addItem(placeholderTailSpacer_);

    connect(ui_->stageList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(stageCount)) selectStage(static_cast<beam::gui::WorkflowStage>(row));
    });
    connect(ui_->completeStageButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
    connect(ui_->acceptCaseButton, &QPushButton::clicked, this, [this] { completeCurrentStage(); });
    connect(runCouplingCheckButton_, &QPushButton::clicked, this, [this] {
        // In the continuation: the measurement is asynchronous, and mirroring it before the
        // device answers copies the previous run's verdict.
        startCorrectionMeasurement([this] {
            couplingCheckPassed_ = correctionCheckPassed_;
            workflow_.change(beam::gui::WorkflowStage::Coupling,
                             "Correction/through-transmit measured; awaiting operator acceptance.");
            acceptCouplingButton_->setEnabled(couplingCheckPassed_);
            clearCouplingButton_->setEnabled(true);
            refresh();
        });
    });
    connect(clearCouplingButton_, &QPushButton::clicked, this, [this] {
        // ZeroCorrectionButtonPushed.m: the device is zeroed first, then the host
        // state goes back to initializeCorrectionValues.m's defaults.
        if (deviceLink_ != nullptr && deviceLink_->isHealthy()) {
            clearCouplingButton_->setEnabled(false);
            const bool started = deviceLink_->runCommand(
                "Zero Correction", 1, [this](std::vector<std::string>, bool) {
                    clearCouplingButton_->setEnabled(true);
                    clearCouplingMeasurement();
                });
            if (started) return;
            clearCouplingButton_->setEnabled(true);
        }
        clearCouplingMeasurement();
    });
    connect(acceptCouplingButton_, &QPushButton::clicked, this, [this] {
        if (!couplingCheckPassed_) {
            showMessage(QStringLiteral("Run and pass the coupling check before accepting it."), true);
            return;
        }
        std::string reason;
        if (workflow_.state(beam::gui::WorkflowStage::Coupling).status != beam::gui::WorkflowStatus::Complete &&
            !workflow_.complete(beam::gui::WorkflowStage::Coupling, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        if (workflow_.state(beam::gui::WorkflowStage::Correction).status != beam::gui::WorkflowStatus::Complete &&
            !workflow_.complete(beam::gui::WorkflowStage::Correction, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        // Both stay live: the stage is accepted, not closed.
        refresh();
        goToNextStage();
    });
    connect(acceptSafetyReviewButton_, &QPushButton::clicked, this, [this] {
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::SafetyReview, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        acceptSafetyReviewButton_->setEnabled(false);
        safetyReviewStatusLabel_->setText(QStringLiteral("Safety review accepted. Treatment may now begin."));
        showMessage(QStringLiteral("Safety review accepted. Continuing to Treatment."), false);
        refresh();
        goToNextStage();
    });
    connect(ui_->simulatePassButton, &QPushButton::clicked, this, [this] {
        deviceCheckPassed_ = true;
        workflow_.change(beam::gui::WorkflowStage::SystemCheck,
                         "Device readiness check passed; awaiting operator acceptance.");
        ui_->deviceResult->setText(QStringLiteral("Device responded correctly (simulated). Review and accept readiness."));
        ui_->deviceHeader->setText(QStringLiteral("● Device ready (simulated)"));
        ui_->acceptDeviceReadinessButton->setEnabled(true);
        showMessage(QStringLiteral("System check passed. Accept readiness to continue."), false);
        refresh();
        updateRegistrationAvailability();
    });
    connect(ui_->simulateFailButton, &QPushButton::clicked, this, [this] {
        deviceCheckPassed_ = false;
        ui_->acceptDeviceReadinessButton->setEnabled(false);
        workflow_.block(beam::gui::WorkflowStage::SystemCheck, "Device did not respond (simulated).");
        ui_->deviceResult->setText(QStringLiteral("Device did not respond (simulated)."));
        ui_->deviceHeader->setText(QStringLiteral("● Device fault (simulated)"));
        showMessage(QStringLiteral("System check blocked: device did not respond."), true);
        refresh();
    });
    connect(ui_->acceptDeviceReadinessButton, &QPushButton::clicked, this, [this] {
        if (!deviceCheckPassed_) {
            showMessage(QStringLiteral("Run and pass the device readiness check before accepting it."), true);
            return;
        }
        std::string reason;
        if (!workflow_.complete(beam::gui::WorkflowStage::SystemCheck, &reason)) {
            showMessage(QString::fromStdString(reason), true);
            return;
        }
        ui_->acceptDeviceReadinessButton->setEnabled(false);
        showMessage(QStringLiteral("Device readiness accepted. Continuing to Imaging."), false);
        refresh();
        goToNextStage();
    });
    // Two explicit buttons rather than BeamV0's one Load MRI that first asks file-or-folder:
    // The same blue every other action button on the page wears.
    for (QPushButton* button : {ui_->loadDicomButton, ui_->loadNiftiButton}) {
        button->setMinimumHeight(30);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: #176b87; color: white; border: 1px solid #0f5269; "
            "border-radius: 4px; padding: 5px 12px; font-weight: 600; } "
            "QPushButton:hover { background: #2083a3; } "
            "QPushButton:disabled { background: #414141; color: #8b8b8b; "
            "border-color: #555555; }"));
    }
    connect(ui_->loadDicomButton, &QPushButton::clicked, this, [this] { chooseDicomDirectory(); });
    connect(ui_->loadNiftiButton, &QPushButton::clicked, this, [this] { chooseNifti(); });

    // The loaded path reads better beside the button that loaded it than on a line of its own.
    for (int i = ui_->loadMriButtons->count() - 1; i >= 0; --i)
        if (ui_->loadMriButtons->itemAt(i)->spacerItem())
            delete ui_->loadMriButtons->takeAt(i);
    ui_->imagingBox->removeWidget(ui_->mriPathLabel);
    ui_->mriPathLabel->setWordWrap(false);
    ui_->mriPathLabel->setStyleSheet(QStringLiteral("color: #bdc7cb; padding-left: 10px;"));
    ui_->loadMriButtons->addWidget(ui_->mriPathLabel, 1);
    connect(ui_->showFiducialsCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->registrationShowFiducialsCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    const auto adjustAllViewBrightness = [this](double amount) {
        for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                     ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                     ui_->registrationAxialPreview, treatmentSagittalPreview_,
                                     treatmentCoronalPreview_, treatmentAxialPreview_}) {
            if (view)
                view->adjustBrightness(amount);
        }
    };
    for (QToolButton* button : {ui_->brightnessDownButton, ui_->registrationBrightnessDownButton})
        connect(button, &QToolButton::clicked, this, [adjustAllViewBrightness] { adjustAllViewBrightness(-0.1); });
    for (QToolButton* button : {ui_->brightnessUpButton, ui_->registrationBrightnessUpButton})
        connect(button, &QToolButton::clicked, this, [adjustAllViewBrightness] { adjustAllViewBrightness(0.1); });
    const auto resetAllViewBrightness = [this] {
        for (WorkflowMriView* view : {ui_->sagittalPreview, ui_->coronalPreview, ui_->axialPreview,
                                     ui_->registrationSagittalPreview, ui_->registrationCoronalPreview,
                                     ui_->registrationAxialPreview, treatmentSagittalPreview_,
                                     treatmentCoronalPreview_, treatmentAxialPreview_}) {
            if (view)
                view->resetBrightness();
        }
    };
    connect(ui_->resetAllMriViewsButton, &QToolButton::clicked, this, resetAllViewBrightness);
    connect(ui_->registrationResetAllMriViewsButton, &QToolButton::clicked, this, resetAllViewBrightness);
    connect(ui_->showFieldCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showTargetCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showTransducersCheckBox, &QCheckBox::toggled, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->transducerTransparencySlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showFieldCheckBox, &QCheckBox::toggled, ui_->registrationShowFieldCheckBox, &QCheckBox::setChecked);
    // Show Target is NOT mirrored between the two panes the way Show Field and Show Transducers
    connect(ui_->registrationShowTargetCheckBox, &QCheckBox::toggled, this,
            [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showSteeringRangeCheckBox, &QCheckBox::toggled,
            ui_->registrationShowSteeringRangeCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationShowSteeringRangeCheckBox, &QCheckBox::toggled,
            ui_->showSteeringRangeCheckBox, &QCheckBox::setChecked);
    connect(ui_->showSteeringRangeCheckBox, &QCheckBox::toggled, this,
            [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->showTransducersCheckBox, &QCheckBox::toggled, ui_->registrationShowTransducersCheckBox, &QCheckBox::setChecked);
    connect(ui_->transducerTransparencySlider, &QSlider::valueChanged, ui_->registrationTransparencySlider, &QSlider::setValue);
    connect(ui_->registrationShowFieldCheckBox, &QCheckBox::toggled, ui_->showFieldCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationShowTransducersCheckBox, &QCheckBox::toggled, ui_->showTransducersCheckBox, &QCheckBox::setChecked);
    connect(ui_->registrationTransparencySlider, &QSlider::valueChanged, ui_->transducerTransparencySlider, &QSlider::setValue);
    connect(ui_->sagittalSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->coronalSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    connect(ui_->axialSlider, &QSlider::valueChanged, this, [this] { if (mriLoaded_) showMriPreviews(); });
    const auto connectSliceStep = [this](QToolButton* button, QSlider* slider, int direction) {
        connect(button, &QToolButton::clicked, this, [slider, direction] {
            slider->setValue(slider->value() + direction);
        });
    };
    connectSliceStep(ui_->sagittalPreviousButton, ui_->sagittalSlider, -1);
    connectSliceStep(ui_->sagittalNextButton, ui_->sagittalSlider, 1);
    connectSliceStep(ui_->coronalPreviousButton, ui_->coronalSlider, -1);
    connectSliceStep(ui_->coronalNextButton, ui_->coronalSlider, 1);
    connectSliceStep(ui_->axialPreviousButton, ui_->axialSlider, -1);
    connectSliceStep(ui_->axialNextButton, ui_->axialSlider, 1);
    connectSliceStep(ui_->registrationSagittalPreviousButton, ui_->registrationSagittalSlider, -1);
    connectSliceStep(ui_->registrationSagittalNextButton, ui_->registrationSagittalSlider, 1);
    connectSliceStep(ui_->registrationCoronalPreviousButton, ui_->registrationCoronalSlider, -1);
    connectSliceStep(ui_->registrationCoronalNextButton, ui_->registrationCoronalSlider, 1);
    connectSliceStep(ui_->registrationAxialPreviousButton, ui_->registrationAxialSlider, -1);
    connectSliceStep(ui_->registrationAxialNextButton, ui_->registrationAxialSlider, 1);
    connect(ui_->registrationSagittalSlider, &QSlider::valueChanged, ui_->sagittalSlider, &QSlider::setValue);
    connect(ui_->registrationCoronalSlider, &QSlider::valueChanged, ui_->coronalSlider, &QSlider::setValue);
    connect(ui_->registrationAxialSlider, &QSlider::valueChanged, ui_->axialSlider, &QSlider::setValue);
    connect(ui_->sagittalSlider, &QSlider::valueChanged, ui_->registrationSagittalSlider, &QSlider::setValue);
    connect(ui_->coronalSlider, &QSlider::valueChanged, ui_->registrationCoronalSlider, &QSlider::setValue);
    connect(ui_->axialSlider, &QSlider::valueChanged, ui_->registrationAxialSlider, &QSlider::setValue);
    // A tooltip alone only appears on hover, after a delay, and disappears the moment the
    for (const auto& [slider, axis] : sliceSliders()) {
        installSliceReadout(slider, axis);
        if (slider) slider->setStyle(new ClickToPositionStyle(slider->style()));
    }

    // Treatment uses its own three-plane viewer, while sharing the loaded volume and slice
    for (auto pair : {std::pair<QSlider*, QSlider*>{ui_->sagittalSlider, treatmentSagittalSlider_},
                      std::pair<QSlider*, QSlider*>{ui_->coronalSlider, treatmentCoronalSlider_},
                      std::pair<QSlider*, QSlider*>{ui_->axialSlider, treatmentAxialSlider_}}) {
        connect(pair.first, &QSlider::rangeChanged, pair.second, &QSlider::setRange);
        connect(pair.first, &QSlider::valueChanged, pair.second, &QSlider::setValue);
        connect(pair.second, &QSlider::valueChanged, pair.first, &QSlider::setValue);
    }
    // Clicking a treatment plane does not move the target. The Registration
    // panes get the same popup: that is the page the lock is actually set on.
    for (WorkflowMriView* view : {treatmentSagittalPreview_, treatmentCoronalPreview_,
                                  treatmentAxialPreview_, ui_->registrationSagittalPreview,
                                  ui_->registrationCoronalPreview, ui_->registrationAxialPreview})
        view->setContextWidgetFactory([this](QMenu* menu) { return buildFiducialPickerWidget(menu); });
    connect(ui_->resetSagittalButton, &QToolButton::clicked, this, [this] {
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
    });
    connect(ui_->resetCoronalButton, &QToolButton::clicked, this, [this] {
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
    });
    connect(ui_->resetAxialButton, &QToolButton::clicked, this, [this] {
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
    });
    connect(ui_->registrationResetSagittalButton, &QToolButton::clicked, ui_->resetSagittalButton, &QToolButton::click);
    connect(ui_->registrationResetCoronalButton, &QToolButton::clicked, ui_->resetCoronalButton, &QToolButton::click);
    connect(ui_->registrationResetAxialButton, &QToolButton::clicked, ui_->resetAxialButton, &QToolButton::click);
    ui_->imagingFiducialLayout->setSelectionHandler([this](int markerIndex) {
        if (!mriLoaded_ || markerIndex < 0 || markerIndex >= static_cast<int>(fiducials_.size())) return;
        ui_->imagingFiducialLayout->setSelectedIndex(markerIndex);
        const Eigen::Vector3d markerMm = fiducials_[static_cast<std::size_t>(markerIndex)].position * 1000.0;
        const auto voxel = beam::gui::imagePositionToVoxelIndex(markerMm, mriAxes_);
        ui_->sagittalSlider->setValue(static_cast<int>(voxel.i));
        ui_->coronalSlider->setValue(static_cast<int>(voxel.j));
        ui_->axialSlider->setValue(static_cast<int>(voxel.k));
        showMessage(QStringLiteral("Showing fiducial %1 on all three planes.")
                        .arg(QString::fromStdString(fiducials_[static_cast<std::size_t>(markerIndex)].name)), false);
    });
    connect(ui_->resetInitialSlicesButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_) return;
        ui_->sagittalSlider->setValue(static_cast<int>((mriVolume_.nx - 1) / 2));
        ui_->coronalSlider->setValue(static_cast<int>((mriVolume_.ny - 1) / 2));
        ui_->axialSlider->setValue(static_cast<int>((mriVolume_.nz - 1) / 2));
        ui_->imagingFiducialLayout->setSelectedIndex(-1);
        showMessage(QStringLiteral("MRI returned to the initially loaded LR, AP, and IS slices."), false);
    });
    connect(ui_->registrationResetInitialSlicesButton, &QPushButton::clicked,
            ui_->resetInitialSlicesButton, &QPushButton::click);
    connect(ui_->registrationReturnStartButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_ || registrationStartFiducialRow_ < 0) return;
        ui_->registrationTable->setCurrentCell(registrationStartFiducialRow_, 0);
        navigateToRegistrationFiducial(registrationStartFiducialRow_);
        showMessage(QStringLiteral("Returned to the view shown when Registration was first opened."), false);
    });
    connect(ui_->acceptImagingButton, &QPushButton::clicked, this, [this] {
        if (!mriLoaded_) {
            showMessage(QStringLiteral("Load a patient MRI before accepting imaging."), true);
            return;
        }
        std::string reason;
        if (workflow_.complete(beam::gui::WorkflowStage::Imaging, &reason)) {
            showMessage(QStringLiteral("MRI accepted. Continuing to Registration."), false);
            refresh();
            updateRegistrationAvailability();
            goToNextStage();
        } else {
            showMessage(QString::fromStdString(reason), true);
            refresh();
            updateRegistrationAvailability();
        }
    });
    connect(ui_->resetRegistrationButton, &QPushButton::clicked, this, [this] {
        fiducialConfirmed_ = sourceFiducialConfirmed_;
        fiducialLocated_ = sourceFiducialLocated_;
        fiducials_ = registrationSourceFiducials_;
        populateRegistrationTable();
        showMriPreviews();
        setRegistrationPhase(RegistrationPhase::Locating);
        workflow_.change(beam::gui::WorkflowStage::Registration,
                         "Registration measurements changed; later approvals require review.");
        ui_->registrationResult->setText(QStringLiteral("Original fiducial measurements restored. Review them, then fit the transducers."));
        refresh();
        updateRegistrationAvailability();
    });
    connect(ui_->confirmFiducialButton, &QPushButton::clicked, this, [this] { confirmSelectedFiducial(); });
    connect(ui_->registerFiducialsButton, &QPushButton::clicked, this, [this] { performFiducialRegistration(); });
    connect(ui_->acceptRegistrationButton, &QPushButton::clicked, this, [this] { acceptFiducialRegistration(); });
    connect(ui_->registrationTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                ui_->registrationFiducialLayout->setSelectedIndex(currentRow);
                if (currentRow >= 0) ui_->registrationTable->selectRow(currentRow);
                highlightRegistrationRow(currentRow);
                if (!suppressRegistrationNavigation_)
                    navigateToRegistrationFiducial(currentRow);
                // Selecting a fiducial only looks at it.
                updateRegistrationAvailability();
            });
    ui_->registrationTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui_->registrationTable, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                const int row = ui_->registrationTable->rowAt(position.y());
                if (row < 0 || row >= ui_->registrationTable->rowCount()) return;
                ui_->registrationTable->setCurrentCell(row, 0);
                QMenu menu(this);
                QAction* detect = nullptr;
                if (row < 6) {
                    const QTableWidgetItem* nameCell = ui_->registrationTable->item(row, 0);
                    detect = menu.addAction(
                        QStringLiteral("Detect this fiducial (%1)")
                            .arg(nameCell != nullptr ? nameCell->text()
                                                     : QString::number(row + 1)));
                }
                QAction* confirm = menu.addAction(QStringLiteral("Confirm this fiducial"));
                confirm->setEnabled(row >= 0 && row < 6 &&
                                    fiducialLocated_[static_cast<std::size_t>(row)] &&
                                    !fiducialConfirmed_[static_cast<std::size_t>(row)]);
                QAction* chosen =
                    menu.exec(ui_->registrationTable->viewport()->mapToGlobal(position));
                if (chosen == nullptr) return;
                if (chosen == confirm) {
                    ui_->registrationTable->setCurrentCell(row, 0);
                    confirmSelectedFiducial();
                } else if (chosen == detect) {
                    detectSingleFiducial(row);
                }
            });
    connect(ui_->registrationTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (row < 0 || row >= 6) return;
                ui_->registrationTable->setCurrentCell(row, 0);
                confirmSelectedFiducial();
            });
    ui_->registrationFiducialLayout->setSelectionHandler([this](int markerIndex) {
        if (markerIndex < 0 || markerIndex >= 6) return;
        ui_->registrationTable->setCurrentCell(markerIndex, 0);
        // setCurrentCell() emits no signal when the same row is clicked again.
        navigateToRegistrationFiducial(markerIndex);
    });
    connect(ui_->registrationTable, &QTableWidget::cellChanged, this, [this](int changedRow, int column) {
        if (!registrationGeometryLoaded_ || column == 0 || column >= 4) return;
        // Use the row reported by Qt.
        const int row = changedRow;
        if (row >= 0 && row < 6) {
            fiducialLocated_[static_cast<std::size_t>(row)] = true;
            fiducialConfirmed_[static_cast<std::size_t>(row)] = false;
        }
        if (row >= 0 && row < 6) {
            Eigen::Vector3d point;
            bool valid = true;
            for (int axis = 0; axis < 3; ++axis) {
                bool coordinateValid = false;
                point(axis) = ui_->registrationTable->item(row, axis + 1)->text().toDouble(&coordinateValid);
                valid = valid && coordinateValid;
            }
            if (valid) {
                fiducials_[static_cast<std::size_t>(row)].position = point / 1000.0;
                showMriPreviews();
            }
        }
        workflow_.change(beam::gui::WorkflowStage::Registration,
                         "Registration measurements changed; later approvals require review.");
        setRegistrationPhase(RegistrationPhase::Locating);
        ui_->registrationResult->setText(QStringLiteral("Measurements changed. Run registration again to accept them."));
        refresh();
        ui_->registrationTable->blockSignals(true);
        ui_->registrationTable->item(row, 4)->setText(QStringLiteral("Located"));
        ui_->registrationTable->blockSignals(false);
        updateRegistrationAvailability();
    });
    // Each plane reports which axis it cannot measure: a click in the sagittal view says
    const auto pickedIn = [this](int heldAxis) {
        return [this, heldAxis](const Eigen::Vector3d& point) { placeSelectedFiducial(point, heldAxis); };
    };
    const auto pickedMarker = [this](int markerIndex) {
        if (markerIndex < 0 || markerIndex >= 6) return;
        suppressRegistrationNavigation_ = true;
        ui_->registrationTable->setCurrentCell(markerIndex, 0);
        ui_->registrationTable->selectRow(markerIndex);
        ui_->registrationTable->setFocus(Qt::OtherFocusReason);
        suppressRegistrationNavigation_ = false;
        highlightRegistrationRow(markerIndex);
        ui_->registrationFiducialLayout->setSelectedIndex(markerIndex);
    };
    ui_->registrationSagittalPreview->setPointPickedHandler(pickedIn(0));  // holds LR
    ui_->registrationCoronalPreview->setPointPickedHandler(pickedIn(1));   // holds AP
    ui_->registrationAxialPreview->setPointPickedHandler(pickedIn(2));     // holds IS
    // "Move fiducial ... to mouse point" writes all three: the operator is relocating the
    const auto moved = [this](const Eigen::Vector3d& point) { placeSelectedFiducial(point, -1); };
    ui_->registrationSagittalPreview->setPointMovedHandler(moved);
    ui_->registrationCoronalPreview->setPointMovedHandler(moved);
    ui_->registrationAxialPreview->setPointMovedHandler(moved);
    ui_->registrationSagittalPreview->setMarkerPickedHandler(pickedMarker);
    ui_->registrationCoronalPreview->setMarkerPickedHandler(pickedMarker);
    ui_->registrationAxialPreview->setMarkerPickedHandler(pickedMarker);
    ui_->registrationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui_->registrationTable->horizontalHeader()->setFixedHeight(30);
    // Do not present a residual/error score to the operator.
    ui_->registrationTable->setColumnHidden(5, true);
    ui_->registrationTable->verticalHeader()->setDefaultSectionSize(25);
    ui_->registrationTable->verticalHeader()->setMinimumSectionSize(22);
    // Six complete rows plus the header, without an oversized viewport.
    ui_->registrationTable->setFixedHeight(kRegistrationColumnHeight);
    if (auto* fiducialHeader = ui_->registrationTable->horizontalHeaderItem(0))
        fiducialHeader->setText(QStringLiteral("Fiducial"));
    ui_->acceptRegistrationButton->setText(QStringLiteral("4  Accept registration and continue →"));
    ui_->acceptRegistrationButton->setText(ui_->acceptRegistrationButton->text().mid(3));
    ui_->registrationTable->setStyleSheet(
        QStringLiteral("QTableWidget { color: #17313f; background: #ffffff; } "
                       "QTableWidget::item { color: #17313f; background: #ffffff; } "
                       "QTableWidget::item:alternate { color: #17313f; background: #f1f6f8; } "
                       "QTableWidget::item:selected, QTableWidget::item:selected:!active "
                       "{ background: #58c7e5; color: #083d4d; }"));
    ui_->registrationTable->horizontalHeader()->setStyleSheet(
        QStringLiteral("QHeaderView::section { padding: 0px; margin: 0px; }"));
    connect(ui_->participantEdit, &QLineEdit::textChanged, this, [this](const QString& value) {
        ui_->participantHeader->setText(QStringLiteral("Participant: %1").arg(value.isEmpty() ? QStringLiteral("—") : value));
    });
    // The visit in the header is the one the treatment plan is on, set by
    ui_->visitHeader->setText(QStringLiteral("Visit: —"));
    connect(ui_->abortButton, &QPushButton::clicked, this, [this] {
        // Nothing running means nothing to stop, and sending the device a command it did not
        if (!deliveryRunning()) {
            showMessage(QStringLiteral("Nothing is being delivered; no device command was sent."),
                        true);
            return;
        }
        abortSonication();
    });
    ui_->abortButton->setToolTip(
        QStringLiteral("Stops the countdown, the log row and any sham audio. It does not "
                       "stop ultrasound: the device has no stop command. Dark when "
                       "nothing is being delivered."));

    ui_->stageList->setCurrentRow(0);
    ui_->acceptCaseButton->setEnabled(true);
    ui_->acceptCaseButton->setFocusPolicy(Qt::StrongFocus);
    ui_->acceptCaseButton->setAutoDefault(true);
    ui_->acceptCaseButton->setDefault(true);
    ui_->acceptImagingButton->setFocusPolicy(Qt::StrongFocus);
    ui_->acceptImagingButton->setAutoDefault(true);
    ui_->acceptImagingButton->setDefault(true);
    for (QPushButton* button : {ui_->simulatePassButton, ui_->simulateFailButton,
                                ui_->acceptDeviceReadinessButton}) {
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAutoDefault(true);
        button->setDefault(true);
        button->installEventFilter(this);
    }
    refresh();
    // Start the operator in the first required identification field so the Participant ID can
    QTimer::singleShot(0, this, [this] {
        ui_->participantEdit->setFocus(Qt::OtherFocusReason);
    });
}

}  // namespace beam::app
