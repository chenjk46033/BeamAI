#include "treatment_internal.hpp"

namespace beam::app {

void WorkflowWindow::addTargetListRow(const QString& name, int atRow) {
    auto* item = new QListWidgetItem;
    // The row widget draws the name.
    item->setData(kTargetNameRole, name);
    item->setData(kTargetIdRole, nextTargetId_++);
    if (atRow < 0)
        targetListWidget_->addItem(item);
    else
        targetListWidget_->insertItem(atRow, item);

    auto* row = new QWidget(targetListWidget_);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(6, 1, 2, 1);
    rowLayout->setSpacing(2);
    auto* nameLabel = new QLabel(name, row);
    // Named so the current-sonication marking can recolour it: the row widget draws the text,
    nameLabel->setObjectName(QStringLiteral("targetNameLabel"));
    nameLabel->setStyleSheet(QStringLiteral("QLabel { color: #17313f; background: transparent; }"));
    rowLayout->addWidget(nameLabel, 1);
    // The row widget covers the item, so a click on it never reaches the list and the current
    row->installEventFilter(this);
    nameLabel->installEventFilter(this);

    item->setSizeHint(row->sizeHint());
    targetListWidget_->setItemWidget(item, row);

}

void WorkflowWindow::buildTreatmentPlanBody(const QString& acceptButtonStyle) {
    // Before the store: which patient's log is read depends on this.
    loadPatientRegistry();
    buildCaseDemographics();
    buildPatientChooser();
    refreshPatientChooser();
    // Before anything that reads it: the Target List is seeded from the store, and the protocol
    loadTreatmentTargetStore();

    // Layout mirrors app.SonicateTab as beam_app's port builds it: the Sonications panel on the
    auto* scrollArea = new QScrollArea(ui_->placeholderPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumHeight(kTreatmentBodyMinHeight);
    // A QScrollArea reports its scrolled widget's minimum as its own, so the grid and the three
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    treatmentPlanBody_ = scrollArea;
    auto* bodyContent = new QWidget(scrollArea);
    scrollArea->setWidget(bodyContent);
    auto* bodyLayout = new QHBoxLayout(bodyContent);
    bodyLayout->setContentsMargins(0, 6, 0, 0);
    bodyLayout->setSpacing(8);

    // ---- app.SonicationsPanel ------------------------------------------
    auto* targetPanel = new QWidget(bodyContent);
    // Narrow: it holds short target names and small row buttons, and every pixel it gives up
    targetPanel->setMaximumWidth(140);
    auto* targetPanelLayout = new QVBoxLayout(targetPanel);
    targetPanelLayout->setContentsMargins(0, 0, 0, 0);
    auto* targetListLabel = new QLabel(QStringLiteral("Target List"), targetPanel);
    targetListLabel->setStyleSheet(QString::fromLatin1(kPaneHeadingStyle));
    targetListLabel->setFixedHeight(kHeadingHeight);
    targetPanelLayout->addWidget(targetListLabel);
    moveToTargetButton_ = new QPushButton(QStringLiteral("Move to Target"), targetPanel);
    moveToTargetButton_->setToolTip(
        QStringLiteral("Centre all three MRI planes on the shown sonication's position"));
    moveToTargetButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    targetListWidget_ = new QListWidget(targetPanel);
    applyItemViewPalette(targetListWidget_);
    {
        QPalette palette = targetListWidget_->palette();
        palette.setColor(QPalette::Highlight, kRowSelectedFill);
        palette.setColor(QPalette::HighlightedText, QColor(0x17, 0x31, 0x3f));
        targetListWidget_->setPalette(palette);
    }
    targetListWidget_->setItemDelegate(new CurrentTargetDelegate(targetListWidget_));
    // Each row carries its own rename / create / delete icons, so beam_app's separate Rename,
    if (targetStore_.targets().empty()) {
        for (const char* name : kTargetNames) addTargetListRow(QString::fromLatin1(name));
    } else {
        for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
            addTargetListRow(QString::fromStdString(target.name));
    }
    targetListWidget_->setCurrentRow(0);
    // Expanding with a small minimum rather than a fixed height: it takes whatever the panel
    targetListWidget_->setMinimumHeight(96);
    targetListWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    targetListWidget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    targetListWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    targetPanelLayout->addWidget(targetListWidget_, 1);
    // Under the list, not above it: it acts on the row that is selected there, so it reads as
    targetPanelLayout->addWidget(moveToTargetButton_);
    // Under the list it edits.
    manageTargetsButton_ = new QPushButton(QStringLiteral("Manage Target List..."), targetPanel);
    manageTargetsButton_->setVisible(false);  // re-homed beside Manage Protocols
    manageTargetsButton_->setToolTip(
        QStringLiteral("Add, rename, delete targets and edit the points they sonicate at"));
    manageTargetsButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    bodyLayout->addWidget(targetPanel);
    treatmentBodyRow_ = bodyLayout;

    // ---- app.stimParamTable and its button row --------------------------
    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(6);
    // No Sort / Add / Remove buttons: BeamV0 has one sonication row per target and no UI to
    stimParamModel_ = new beam::gui_qt::StimParamTableModel(this);
    stimParamView_ = new QTableView(bodyContent);
    stimParamView_->setObjectName(QStringLiteral("stimParamView"));
    // configureModelTable fits the log table's own editor; these columns are seconds and
    stimParamView_->setItemDelegate(new StimParamDelegate(stimParamView_));
    stimParamView_->setModel(stimParamModel_);
    configureModelTable(stimParamView_);
    stimParamView_->setItemDelegate(new StimParamDelegate(stimParamView_));
    // Stretch gave a coordinate the same width as "Burst Duration", which put the grid's own
    {
        using Stim = beam::gui_qt::StimParamTableModel;
        QHeaderView* header = stimParamView_->horizontalHeader();
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setMinimumSectionSize(40);
        header->setStretchLastSection(false);
        static constexpr struct { int column; int width; } kStimWidths[] = {
            {Stim::kTarget, 78},         {Stim::kX, 68},
            {Stim::kY, 68},              {Stim::kZ, 68},
            {Stim::kAmplitude, 76},      {Stim::kStartTime, 78},
            {Stim::kEndTime, 72},        {Stim::kBurstDuration, 104},
            {Stim::kBurstInterval, 100}, {Stim::kPulseDuration, 104},
            {Stim::kPulseInterval, 100},
        };
        int total = 0;
        for (const auto& entry : kStimWidths) {
            stimParamView_->setColumnWidth(entry.column, entry.width);
            total += entry.width;
        }
        // Exactly its columns wide: nothing absorbs the slack as a stretched column did, and
        stimParamView_->setMaximumWidth(total + 2 * stimParamView_->frameWidth() + 2);
        stimParamView_->setSizePolicy(QSizePolicy::Ignored,
                                      stimParamView_->sizePolicy().verticalPolicy());
        // Hidden last: setSectionResizeMode above hands a hidden section its default size back,
        stimParamView_->setColumnHidden(Stim::kShow, true);
        // Order numbers a column of one: every BeamV0 target carries exactly one stimParamTable
        stimParamView_->setColumnHidden(Stim::kOrder, true);
    }
    // The sonication being fired is read off this one row, so it reads on paper rather than in
    stimParamView_->setAlternatingRowColors(true);
    stimParamView_->setStyleSheet(QStringLiteral(
        "QTableView { background: #ffffff; alternate-background-color: #f2f4f7; "
        "color: #10232b; gridline-color: #b9c9d0; } "
        "QTableView::item:selected { background: #ffffff; color: #10232b; }"));
    // The selected row is painted from the palette's Highlight, which the stylesheet rule above
    {
        QPalette plain = stimParamView_->palette();
        plain.setColor(QPalette::Highlight, QColor(0xff, 0xff, 0xff));
        plain.setColor(QPalette::HighlightedText, QColor(0x10, 0x23, 0x2b));
        stimParamView_->setPalette(plain);
    }
    // Right-click to put the parameters back to what the install shipped.
    stimParamView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(stimParamView_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& where) {
                QMenu menu(this);
                QAction* const restore =
                    menu.addAction(QStringLiteral("Restore Default Parameters"));
                if (menu.exec(stimParamView_->viewport()->mapToGlobal(where)) == restore)
                    restoreShippedParameters();
            });
    // Hovering a parameter says what it shipped as, so an edited one can be recognised without
    stimParamView_->viewport()->installEventFilter(this);
    // The grid is exactly as tall as the rows it holds -- normally one -- so the sub-tabs below
    connect(stimParamModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateStimGridHeight();
        // Switching target refills the grid wholesale: these are different numbers, and they
        updateSonicationSafety();
    });
    connect(stimParamModel_, &QAbstractItemModel::rowsInserted, this, [this] { updateStimGridHeight(); });
    connect(stimParamModel_, &QAbstractItemModel::rowsRemoved, this, [this] { updateStimGridHeight(); });
    rightColumn->addWidget(stimParamView_);
    // Under the grid, because that is where the numbers that fail it are typed.
    sonicationSafetyLabel_ = new QLabel(bodyContent);
    sonicationSafetyLabel_->setWordWrap(true);
    sonicationSafetyLabel_->setTextFormat(Qt::RichText);
    // Worth copying: the numbers in it are what an operator quotes when they ask someone about
    sonicationSafetyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                   Qt::TextSelectableByKeyboard);
    sonicationSafetyLabel_->setCursor(Qt::IBeamCursor);
    rightColumn->addWidget(sonicationSafetyLabel_);
    // Separate from the banner above it: that one is about the sonication being fired now, this
    sonicationPrecheckLabel_ = new QLabel(bodyContent);
    sonicationPrecheckLabel_->setWordWrap(true);
    sonicationPrecheckLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sonicationPrecheckLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffd9a8; background: #3a2a12; border: 1px solid #8a5a10; "
        "border-radius: 4px; padding: 4px 10px; }"));
    sonicationPrecheckLabel_->hide();
    rightColumn->addWidget(sonicationPrecheckLabel_);

    // ---- TabGroup3 ------------------------------------------------------
    treatmentTabs_ = new QTabWidget(bodyContent);
    treatmentTabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #356574; background: #173944; } "
        "QTabBar::tab { color: #dcebef; background: #244b58; padding: 7px 13px; } "
        "QTabBar::tab:selected { background: #176b87; }"));
    // A QStackedWidget takes the largest minimum of ALL its pages, so Pulse Details and
    treatmentTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    treatmentTabs_->setMinimumHeight(228);  // five schedule rows, header and tab bar

    // Treatment Protocol tab.
    auto* protocolTab = new QWidget(treatmentTabs_);
    auto* protocolLayout = new QVBoxLayout(protocolTab);
    auto* protocolSelectorRow = new QHBoxLayout;
    treatmentProtocolLabel_ = new QLabel(QStringLiteral("Protocol:"), protocolTab);
    treatmentProtocolCombo_ = new QComboBox(protocolTab);
    treatmentProtocolCombo_->addItems({QStringLiteral("Default"), QStringLiteral("Addiction"),
                                       QStringLiteral("PTSD"), QStringLiteral("PainACC"),
                                       QStringLiteral("PainSCCandAMCC"), QStringLiteral("PainAMCCandSCC")});
    // Off the page: a plan is a visit, filled by Add Row and Add Protocol to Plan, so there is
    treatmentProtocolLabel_->setVisible(false);
    treatmentProtocolCombo_->setVisible(false);
    visitNumberCombo_ = new QComboBox(protocolTab);
    visitNumberCombo_->setObjectName(QStringLiteral("visitNumberCombo"));
    // Filled by refreshVisitCombo from the log: only visits that exist are offered.
    visitNumberCombo_->setView(new QListView(visitNumberCombo_));
    visitNumberCombo_->setMaxVisibleItems(kMaxTreatmentVisits);
    // Wide enough for "Visit 12 -- 3 of 24 delivered" and fixed there, so the buttons beside it
    visitNumberCombo_->setMinimumWidth(150);
    visitNumberCombo_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Nothing but the create item until a visit exists: an item reading "Visit 1" says a visit
    visitNumberCombo_->addItem(QStringLiteral("Create New Visit"), kNewVisitItem);
    // No "Visit:" caption: every item in the list below begins with the word.
    auto* visitLabel = new QLabel(QString(), protocolTab);
    visitLabel->setVisible(false);
    for (QComboBox* combo : {treatmentProtocolCombo_, visitNumberCombo_}) applyItemViewPalette(combo);
    const auto makeProtocolButton = [&](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text, protocolTab);
        button->setToolTip(tip);
        button->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    };
    manageLibraryButton_ = makeProtocolButton(
        QStringLiteral("Protocols..."),
        QStringLiteral("Add, rename, delete protocols and the targets each one schedules"));
    manageTargetsButton_->setParent(protocolTab);
    manageTargetsButton_->setVisible(true);
    manageTargetsButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    manageTargetsButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // HydrogelSizeButtonGroup, as a combo rather than BeamV0's three radios: this row now also
    auto* hydrogelLabel = new QLabel(QStringLiteral("Hydrogel:"), protocolTab);
    hydrogelLabel->setToolTip(QStringLiteral("Hydrogel size"));
    hydrogelSizeCombo_ = new QComboBox(protocolTab);
    hydrogelSizeCombo_->addItems({QStringLiteral("Small"), QStringLiteral("Medium"),
                                  QStringLiteral("Large")});
    hydrogelSizeCombo_->setCurrentIndex(0);  // HydrogelSizeButtonGroup's own default
    applyItemViewPalette(hydrogelSizeCombo_);
    // BeamV0 ranks after every delivery and offers no way out.
    autoRankCheckBox_ = new QCheckBox(QStringLiteral("Auto-rank targets"), protocolTab);
    autoRankCheckBox_->setObjectName(QStringLiteral("autoRankCheckBox"));
    autoRankCheckBox_->setChecked(true);
    autoRankCheckBox_->setStyleSheet(QStringLiteral("QCheckBox { color: #dcebef; }"));
    autoRankCheckBox_->setToolTip(
        QStringLiteral("After each sonication, point the treatment rows still to come at "
                       "whichever targets scored best in the screening rows above them. "
                       "Delivered rows are never changed. Untick to edit the plan yourself."));
    auto* sonicationNumberLabel = new QLabel(QStringLiteral("Sonication:"), protocolTab);
    sonicationNumberLabel->setToolTip(QStringLiteral("Current sonication number"));
    currentSonicationNumberLabel_ = new QLabel(protocolTab);
    currentSonicationNumberLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; background: #1f4e79; border: 1px solid #163c5e; "
        "border-radius: 3px; padding: 2px 10px; font-weight: 700; }"));
    for (QLabel* label : {treatmentProtocolLabel_, visitLabel, hydrogelLabel, sonicationNumberLabel})
        label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    // One row, not two: the row these fields used to occupy goes to the schedule below.
    protocolSelectorRow->addWidget(visitNumberCombo_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(sonicationNumberLabel);
    protocolSelectorRow->addWidget(currentSonicationNumberLabel_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(hydrogelLabel);
    protocolSelectorRow->addWidget(hydrogelSizeCombo_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(autoRankCheckBox_);
    protocolSelectorRow->addStretch(1);
    protocolSelectorRow->addWidget(manageLibraryButton_);
    protocolSelectorRow->addWidget(manageTargetsButton_);
    // Also here: a visit is saved from where the operator is working, and once treatment has
    saveSessionOnTreatmentButton_ = makeProtocolButton(
        QStringLiteral("Save session..."),
        QStringLiteral("Writes the MRI, the registration and this patient's record to one "
                       "file, beside the MRIs"));
    connect(saveSessionOnTreatmentButton_, &QPushButton::clicked, this,
            [this] { saveTreatmentSessionFile(); });
    protocolSelectorRow->addWidget(saveSessionOnTreatmentButton_);
    protocolLayout->addLayout(protocolSelectorRow);

    protocolModel_ = new beam::gui_qt::TreatmentProtocolTableModel(this);
    protocolView_ = new QTableView(protocolTab);
    protocolView_->setObjectName(QStringLiteral("treatmentPlanView"));
    // Drag or shift-click across rows to take several at once; the row menu then acts on what
    protocolView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    protocolView_->setModel(protocolModel_);
    configureModelTable(protocolView_);
    // Explicit widths, not ResizeToContents: that sizes the view to the sum of its content, and
    protocolView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Notes takes the slack.
    protocolView_->horizontalHeader()->setStretchLastSection(true);
    protocolView_->horizontalHeader()->setMinimumSectionSize(48);
    protocolView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (int column : {beam::gui_qt::TreatmentProtocolTableModel::kResponsePain,
                       beam::gui_qt::TreatmentProtocolTableModel::kResponseMood})
        protocolView_->setItemDelegateForColumn(column, new ResponseDelegate(protocolView_));
    // A value that still arrives out of range -- pasted, or from a file -- is clamped by the
    connect(protocolModel_, &beam::gui_qt::TreatmentProtocolTableModel::responseClamped, this,
            [this] {
                showMessage(QStringLiteral("Response Pain and Response Mood run -2 to 2; the "
                                           "value was brought into range."),
                            true);
            });
    protocolView_->setToolTip(QStringLiteral(
        "This visit's log: what was actually delivered. The protocol it started from is "
        "edited in Manage Protocols."));
    protocolView_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    {
        // Each the width of what it holds: Parameters fits its whole string (X, Y, Z and the
        const int widths[] = {112, 28, 62, 68, 72, 300, 50, 54, 86, 86, 56, 200};
        for (int column = 0; column < static_cast<int>(std::size(widths)); ++column)
            protocolView_->setColumnWidth(column, widths[column]);
    }
    // Both tables carry one line of text per cell, so the default padding is most of their
    protocolView_->horizontalHeader()->setFixedHeight(22);
    protocolView_->verticalHeader()->setDefaultSectionSize(19);
    stimParamView_->horizontalHeader()->setFixedHeight(22);
    stimParamView_->verticalHeader()->setDefaultSectionSize(21);
    protocolView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(protocolView_, &QTableView::customContextMenuRequested, this,
            [this](const QPoint& where) { showScheduleRowMenu(where); });
    // A protocol is 24-odd entries; at the default row height only a handful fit and the rest
    protocolView_->verticalHeader()->setDefaultSectionSize(22);
    protocolView_->verticalHeader()->setMinimumSectionSize(18);
    protocolView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    protocolView_->setMinimumHeight(96);
    protocolView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // The table takes the tab's leftover height, so there is no band of empty space under it
    protocolLayout->addWidget(protocolView_, 1);
    // Nothing in the plan says which rows screen and which treat -- it is read off the
    planBlocksLabel_ = new QLabel(protocolTab);
    // Not wrapped: a wrapped label's height depends on its width, which this layout settles
    planBlocksLabel_->setWordWrap(false);
    planBlocksLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    planBlocksLabel_->setStyleSheet(QStringLiteral("QLabel { color: #9fb0ba; padding: 2px 2px; }"));
    protocolLayout->addWidget(planBlocksLabel_, 0);
    // The table takes the slack in this column, and nothing else does.
    protocolEmptySpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
    protocolLayout->addItem(protocolEmptySpacer_);

    treatmentTabs_->addTab(protocolTab, QStringLiteral("Treatment Log"));

    // Pulse Details tab: BeamV0's axPulseWaveformPlot / axBurstWaveformPlot /
    auto* pulseTab = new QWidget(treatmentTabs_);
    auto* pulseLayout = new QVBoxLayout(pulseTab);
    pulsePlotView_ = new beam::gui_qt::PulseWaveformView(pulseTab);
    burstPlotView_ = new beam::gui_qt::PulseWaveformView(pulseTab);
    timelineView_ = new beam::gui_qt::TotalSonicationView(pulseTab);
    auto* waveformRow = new QHBoxLayout;
    waveformRow->addWidget(pulsePlotView_, 1);
    waveformRow->addWidget(burstPlotView_, 1);
    pulseLayout->addLayout(waveformRow, 1);
    pulseLayout->addWidget(timelineView_, 1);
    treatmentTabs_->addTab(pulseTab, QStringLiteral("Pulse Details"));

    // Targeting Examples tab: BeamV0's three reference views for the chosen region, side by
    auto* targetingTab = new QWidget(treatmentTabs_);
    auto* targetingLayout = new QVBoxLayout(targetingTab);
    auto* accFlagRow = new QHBoxLayout;
    auto* accFlagLabel = new QLabel(QStringLiteral("ACC region:"), targetingTab);
    accFlagLabel->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    accFlagCombo_ = new QComboBox(targetingTab);
    accFlagCombo_->addItem(QStringLiteral("Other (SCC/aMCC interleaved)"));
    accFlagCombo_->addItem(QStringLiteral("SCC"));
    accFlagCombo_->addItem(QStringLiteral("aMCC"));
    applyItemViewPalette(accFlagCombo_);
    computeBestTargetsButton_ = new QPushButton(QStringLiteral("Compute Best Targets"), targetingTab);
    computeBestTargetsButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    auto* bestTargetsCaption =
        new QLabel(QStringLiteral("Best targets to carry forward:"), targetingTab);
    bestTargetsCaption->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    bestTargetsLabel_ = new QLabel(QStringLiteral("--"), targetingTab);
    bestTargetsLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; background: #1f4e79; border: 1px solid #163c5e; "
        "border-radius: 3px; padding: 2px 10px; font-weight: 700; }"));
    bestTargetsLabel_->setToolTip(QStringLiteral(
        "What the ranking would carry forward from this visit's screening responses, best "
        "first. Rank Targets, on the Treatment Log tab, is what puts them into the plan."));
    accFlagRow->addWidget(accFlagLabel);
    accFlagRow->addWidget(accFlagCombo_);
    accFlagRow->addWidget(computeBestTargetsButton_);
    accFlagRow->addSpacing(18);
    accFlagRow->addWidget(bestTargetsCaption);
    accFlagRow->addWidget(bestTargetsLabel_);
    accFlagRow->addStretch(1);
    targetingLayout->addLayout(accFlagRow);
    auto* imageRow = new QHBoxLayout;
    for (ExampleImageView** imageSlot :
         {&exampleTargetImage_, &exampleTargetImage2_, &exampleTargetImage3_}) {
        *imageSlot = new ExampleImageView(targetingTab);
        imageRow->addWidget(*imageSlot, 1);
    }
    targetingLayout->addLayout(imageRow, 1);
    exampleTargetText_ = new QLabel(targetingTab);
    exampleTargetText_->setWordWrap(true);
    exampleTargetText_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px; }"));
    targetingLayout->addWidget(exampleTargetText_);
    const int targetingIndex =
        treatmentTabs_->addTab(targetingTab, QStringLiteral("Targeting Examples"));
    // Reference anatomy is useless at thumbnail size, and this tab is read rather than
    connect(treatmentTabs_, &QTabWidget::currentChanged, this, [this, targetingIndex](int index) {
        treatmentTabs_->setMinimumHeight(index == targetingIndex ? 326 : 228);
    });

    rightColumn->addWidget(treatmentTabs_, 1);

    bodyLayout->addLayout(rightColumn, 1);
    treatmentPlanBody_->setVisible(false);

    wireTreatmentPlanBody();
}

void WorkflowWindow::wireTreatmentPlanBody() {
    // ---- the sonication grid -------------------------------------------
    connect(stimParamModel_, &beam::gui_qt::StimParamTableModel::showFlagsChanged, this,
            [this] { updateSonicationPlots(); });
    connect(stimParamModel_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                // Colour only: the safety check marks the cells it blamed, and that must not
                const bool presentationOnly =
                    !roles.isEmpty() &&
                    std::all_of(roles.begin(), roles.end(), [](int role) {
                        return role == Qt::BackgroundRole || role == Qt::ForegroundRole;
                    });
                if (presentationOnly) return;
                saveCurrentTargetSonications();
                saveTreatmentTargetStore();
                updateSonicationPlots();
                // The value just typed is the one most likely to be out of range.
                updateSonicationSafety();
            });
    connect(stimParamView_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) updateSonicationPlots();
            });

    // ---- the target list ------------------------------------------------ Rename / create /
    connect(moveToTargetButton_, &QPushButton::clicked, this, [this] {
        const QListWidgetItem* item = targetListWidget_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) {
            showMessage(QStringLiteral("That target has no position yet. Select a sonication and click a point "
                                       "in any MRI plane to give it one."),
                        true);
            return;
        }
        // BeamV0 moves to the sonication getCurrentShownSonication.m picks -- the last row with
        const std::vector<beam::gui_qt::StimParamRow>& rows = stimParamModel_->rows();
        if (rows.empty()) return;
        const std::size_t shown = static_cast<std::size_t>(
            std::clamp(stimParamModel_->currentShownRow(), 1, static_cast<int>(rows.size()))) - 1;
        focusTreatmentViewsOn(Eigen::Vector3d(rows[shown].x, rows[shown].y, rows[shown].z));
    });
    // The grid row belongs to whichever target is selected, so switching target loads that
    connect(targetListWidget_, &QListWidget::currentRowChanged, this, [this](int) {
        switchToSelectedTarget();
        const QListWidgetItem* item = targetListWidget_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) return;
        const QList<QVariant> ras = item->data(Qt::UserRole).toList();
        if (ras.size() == 3)
            focusTreatmentViewsOn(
                Eigen::Vector3d(ras.at(0).toDouble(), ras.at(1).toDouble(), ras.at(2).toDouble()));
    });

    // ---- the protocol sub-tab -------------------------------------------
    connect(treatmentProtocolCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString& name) { loadTreatmentProtocol(name); });

    connect(manageLibraryButton_, &QPushButton::clicked, this,
            [this] { openTreatmentLibrary(TreatmentLibraryDialog::Page::Protocols); });
    connect(manageTargetsButton_, &QPushButton::clicked, this,
            [this] { openTreatmentLibrary(TreatmentLibraryDialog::Page::Targets); });
    connect(protocolModel_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                saveTreatmentTargetStore();
                // The plan row hands its Duration to the grid when the sonication becomes
                using Plan = beam::gui_qt::TreatmentProtocolTableModel;
                const bool touchedDuration = topLeft.column() <= Plan::kDuration &&
                                             bottomRight.column() >= Plan::kDuration;
                const int row = currentSonication_ - 1;
                if (touchedDuration && row >= topLeft.row() && row <= bottomRight.row())
                    applyScheduledDurationToGrid();
                // Queued: this rewrites the rows it was handed, and resetting a model from
                QMetaObject::invokeMethod(
                    this, [this] { precheckPendingSonications(); }, Qt::QueuedConnection);
            });
    // The last item of the visit list makes a visit; every other item is one.
    connect(visitNumberCombo_, &QComboBox::activated, this, [this](int index) {
        const int chosen = visitNumberCombo_->itemData(index).toInt();
        // Queued, and by the visit number rather than the item position: both handlers below
        QMetaObject::invokeMethod(this, [this, chosen] {
            if (chosen == kNewVisitItem) {
                startNewVisit();
                return;
            }
            selectVisit(chosen);
            loadTreatmentProtocol(currentProtocolName());
        }, Qt::QueuedConnection);
    });
    connect(protocolView_, &QTableView::doubleClicked, this, [this](const QModelIndex& where) {
        if (where.column() == beam::gui_qt::TreatmentProtocolTableModel::kNotes)
            editRowNotes(where.row() + 1);
    });
    connect(autoRankCheckBox_, &QCheckBox::toggled, this, [this](bool on) {
        const QString key = visitKey(currentProtocolName(), currentVisitNumber());
        if (on) {
            rankingOffVisits_.remove(key);
            applyTargetRanking();
        } else {
            rankingOffVisits_.insert(key);
            // Said plainly: unticking stops the ranking, it does not undo it.
            showMessage(QStringLiteral("Automatic ranking is off for visit %1. The targets "
                                       "already in the plan stay as they are.")
                            .arg(currentVisitNumber()),
                        false);
        }
        saveVisitState();
    });
    connect(computeBestTargetsButton_, &QPushButton::clicked, this, [this] { updateBestTargets(); });
    connect(accFlagCombo_, &QComboBox::currentTextChanged, this, [this] {
        updateBestTargets();
        updateExampleTargetImage();
    });
    connect(protocolModel_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                updateBestTargets();
                updatePlanBlocksLabel();
                // A score is the ranking's input, so the plan follows it at once.
                using Model = beam::gui_qt::TreatmentProtocolTableModel;
                if (topLeft.column() > Model::kResponseMood ||
                    bottomRight.column() < Model::kResponsePain)
                    return;
                applyTargetRankingIfAutomatic();
            });

    // Before the first load: it decides which sonication each visit opens on.
    loadVisitState();
    // Already loaded in buildTreatmentPlanBody -- the Target List needs it.
    refreshProtocolCombo(QStringLiteral("Default"));

    loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    updateExampleTargetImage();
}

// Port of BeamV0's per-protocol CSVs, unchanged from this application's earlier hand-built
}  // namespace beam::app
