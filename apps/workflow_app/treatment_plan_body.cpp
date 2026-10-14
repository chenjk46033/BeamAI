// The Treatment plan stage's body: BeamV0.mlapp's SonicateTab, re-homed
// into this application's workflow model.
//
// What carries over from the Sonicate tab (via libs/gui_qt_models and
// libs/gui_qt_views, the same models and plots beam_app's port uses):
// app.stimParamTable's real 12-column sonication grid with its
// sort/add/remove-marked actions, TabGroup3's three sub-tabs (Treatment
// Protocol, Pulse Details, Targeting Examples), app.SonicationsPanel's
// target list, and the hydrogel-size / current-sonication-number fields.
//
// What deliberately differs, because this workflow already does it better
// or elsewhere:
//
//   * X/Y/Z are not typed into the grid.  Clicking any of the three MRI
//     planes above writes RAS millimetres into the selected sonication --
//     BeamV0 has no MRI view on this tab at all, so its grid is the only
//     place coordinates could go.  The Target List is the same list, named.
//   * Sonicate / Sham / Abort and the serial-port controls are NOT here.
//     In BeamV0 one tab both plans and fires; this workflow gates firing
//     behind Safety Review, so those live on the Treatment stage
//     (buildTreatmentExecutionBody) and cannot be reached until the plan is
//     accepted and reviewed.
//   * No Get Params button: its source action just exposes MATLAB's
//     workspace for debugging, which has no equivalent here.

#include "workflow_window.hpp"

#include "treatment_library_dialog.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "gui/countdown_presenter.hpp"
#include "gui/sham_orchestrator.hpp"
#include "gui/sonicate_orchestrator.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "serialcom/command.hpp"
#include "serialcom/serial_port.hpp"
#include "gui_qt/pulse_waveform_view.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/total_sonication_view.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "mri_view.hpp"
#include "stimulation/events.hpp"

#include "ui_workflow_shell.h"

namespace beam::app {
namespace {

// BeamV0's own target names, the ranking getTopTargetsFromTreatmentProtocolTable.m
// works from, and the order sys.protocolTables is keyed by. The store normally
// supplies these from data/treatment_targets.csv; this is the fallback for when
// that file is missing, so the list is never empty.
const char* const kTargetNames[] = {"SCC1",  "SCC2",  "SCC3",  "SCC4",  "SCC5",  "SCC6",
                                    "aMCC1", "aMCC2", "aMCC3", "aMCC4", "aMCC5", "aMCC6"};
// The workflow page paints a dark work surface and sets a light Text colour
// for it, which item views inherit -- light text on their own white Base,
// plus a near-black AlternateBase. Restoring the ordinary light palette on
// the view itself is what makes them read like the Sonicate tab's tables
// rather than tinting them with a stylesheet (which would also suppress the
// Show column's native checkbox indicator).
void applyItemViewPalette(QWidget* view) {
    QPalette palette = view->palette();
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::AlternateBase, QColor(0xf0, 0xf4, 0xf6));
    palette.setColor(QPalette::Text, QColor(0x17, 0x31, 0x3f));
    palette.setColor(QPalette::WindowText, QColor(0x17, 0x31, 0x3f));
    palette.setColor(QPalette::Highlight, QColor(0x28, 0x88, 0xa2));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    // A closed QComboBox paints its current text with the Button roles, not
    // the Base/Text ones its popup list uses -- without these it stays the
    // page's dark-on-dark and reads as empty.
    palette.setColor(QPalette::Button, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::ButtonText, QColor(0x17, 0x31, 0x3f));
    view->setPalette(palette);
}

const char* const kHeaderStyle =
    "QHeaderView::section { color: #17313f; background: #dcebef; padding: 4px; font-weight: 600; "
    "border: 0; border-right: 1px solid #b7cbd1; border-bottom: 1px solid #b7cbd1; }";

const char* const kPanelButtonStyle =
    "QPushButton { background: #244b58; color: #dcebef; border: 1px solid #356574; border-radius: 4px; "
    "padding: 5px 10px; } "
    "QPushButton:hover { background: #2f6274; } "
    "QPushButton:disabled { background: #1d3a45; color: #7b8c93; border-color: #2a4652; }";

const char* const kSectionLabelStyle = "QLabel { color: #f2f7f8; font-weight: 600; }";

// Where a Target List row keeps its name: see addTargetListRow for why it
// cannot be the item's own text.
constexpr int kTargetNameRole = Qt::UserRole + 1;
// A stable key for the target's own sonications, so renaming a target or
// inserting one above it does not lose them.
constexpr int kTargetIdRole = Qt::UserRole + 2;

QString targetListName(const QListWidgetItem* item) {
    return item ? item->data(kTargetNameRole).toString() : QString();
}

int targetListId(const QListWidgetItem* item) {
    return item ? item->data(kTargetIdRole).toInt() : -1;
}

// The per-row Target List icons. Sized and drawn like the MRI slice buttons
// so the two rows of small controls on this page match.
const char* const kRowIconStyle =
    "QToolButton { background: #f7fafb; color: #17313f; border: 1px solid #9eacb4; border-radius: 3px; "
    "min-width: 20px; max-width: 20px; min-height: 18px; max-height: 18px; padding: 0; "
    "font-family: 'Segoe UI Symbol'; font-size: 11px; font-weight: 700; } "
    "QToolButton:hover { background: #e3f2f6; border-color: #2888a2; } "
    "QToolButton:pressed { background: #cce7ee; }";

void configureModelTable(QTableView* view) {
    applyItemViewPalette(view);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->verticalHeader()->setVisible(false);
    view->horizontalHeader()->setStyleSheet(QString::fromLatin1(kHeaderStyle));
    // Stretch rather than beam_app's fixed 72px: the same 12 columns have a
    // whole window's width here instead of sharing it with a Serial Port
    // column, so stretching fills the row and stops "Burst Duration" from
    // being elided.
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // A QTableView's own minimumSizeHint grows once a populated model is
    // attached, and that growth propagates into the window's minimum size.
    // An explicit minimum overrides it; the view still scrolls internally.
    // (The sonication grid overrides this again with a height matching its
    // own row count -- see updateStimGridHeight.)
    view->setMinimumHeight(120);
}

}  // namespace

// One Target List row: the name plus its own rename / create / delete icons.
// BeamV0 puts those three actions in buttons beside the list that act on
// whatever is selected; per row they say which target they act on, which
// matters once a protocol has added targets of its own to the twelve.
void WorkflowWindow::addTargetListRow(const QString& name, int atRow) {
    auto* item = new QListWidgetItem;
    // The row widget draws the name. The item's own text must stay empty --
    // setItemWidget draws the widget over the item without suppressing its
    // text, so a name set here shows through behind the label, offset by the
    // widget's margin ("SCC2" reading as "SCCC2"). The name lives in a role
    // instead, which targetListName() reads.
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
    nameLabel->setStyleSheet(QStringLiteral("QLabel { color: #17313f; background: transparent; }"));
    rowLayout->addWidget(nameLabel, 1);
    // The row widget covers the item, so a click on it never reaches the list
    // and the current row does not change -- which is what left the grid's
    // X/Y/Z and Move To Target acting on a stale target. eventFilter selects
    // the row on press and lets the event through, so the icons still work.
    // (Qt::WA_TransparentForMouseEvents is the obvious alternative and the
    // wrong one: childAt skips a transparent widget together with its
    // children, so the icons would stop receiving clicks entirely.)
    row->installEventFilter(this);
    nameLabel->installEventFilter(this);


    item->setSizeHint(row->sizeHint());
    targetListWidget_->setItemWidget(item, row);

}

void WorkflowWindow::buildTreatmentPlanBody(const QString& acceptButtonStyle) {
    // Before anything that reads it: the Target List is seeded from the store,
    // and the protocol combo is filled from its protocol names.
    loadTreatmentTargetStore();

    // Layout mirrors app.SonicateTab as beam_app's port builds it: the
    // Sonications panel on the left, and to its right a column holding the
    // stimParamTable button row, the table itself, TabGroup3, and a status
    // line. It scrolls for the same reason beam_app's does -- the three
    // sub-tabs plus the grid want more height than the page has, the more so
    // here because the MRI viewers sit above it.
    auto* scrollArea = new QScrollArea(ui_->placeholderPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumHeight(kTreatmentBodyMinHeight);
    // A QScrollArea reports its scrolled widget's minimum as its own, so the
    // grid and the three sub-tabs would otherwise drive the whole window's
    // minimum height past the screen -- the window grew every time a tab was
    // first laid out. Ignored drops that hint; the explicit minimum above is
    // what the page actually reserves, and anything beyond it scrolls.
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    treatmentPlanBody_ = scrollArea;
    auto* bodyContent = new QWidget(scrollArea);
    scrollArea->setWidget(bodyContent);
    auto* bodyLayout = new QHBoxLayout(bodyContent);
    bodyLayout->setContentsMargins(0, 6, 0, 0);
    bodyLayout->setSpacing(8);

    // ---- app.SonicationsPanel ------------------------------------------
    auto* targetPanel = new QWidget(bodyContent);
    targetPanel->setMaximumWidth(220);
    auto* targetPanelLayout = new QVBoxLayout(targetPanel);
    targetPanelLayout->setContentsMargins(0, 0, 0, 0);
    auto* targetListLabel = new QLabel(QStringLiteral("Target List"), targetPanel);
    targetListLabel->setStyleSheet(QString::fromLatin1(kSectionLabelStyle));
    targetPanelLayout->addWidget(targetListLabel);
    moveToTargetButton_ = new QPushButton(QStringLiteral("Move to Selected Target"), targetPanel);
    moveToTargetButton_->setToolTip(
        QStringLiteral("Centre all three MRI planes on the shown sonication's position"));
    moveToTargetButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    targetPanelLayout->addWidget(moveToTargetButton_);
    targetListWidget_ = new QListWidget(targetPanel);
    applyItemViewPalette(targetListWidget_);
    // Each row carries its own rename / create / delete icons, so beam_app's
    // separate Rename, Create New Protocol and Remove Selected Protocol
    // buttons are gone -- they acted on the selected row, which is what the
    // per-row icons say explicitly.
    // The store is the Target List: data/treatment_targets.csv holds BeamV0's
    // own twelve names in its own order. kTargetNames stands in only when that
    // file could not be read.
    if (targetStore_.targets().empty()) {
        for (const char* name : kTargetNames) addTargetListRow(QString::fromLatin1(name));
    } else {
        for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
            addTargetListRow(QString::fromStdString(target.name));
    }
    targetListWidget_->setCurrentRow(0);
    // Expanding with a small minimum rather than a fixed height: it takes
    // whatever the panel has and scrolls inside itself for the rest. A fixed
    // height either wasted the space below it or, when taller than the body,
    // pushed its own bottom off the page.
    targetListWidget_->setMinimumHeight(120);
    targetListWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    targetListWidget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    targetListWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    targetPanelLayout->addWidget(targetListWidget_, 1);
    // Under the list it edits. It sat above it while the body still scrolled,
    // where a button below a full-height list fell past the visible area.
    manageTargetsButton_ = new QPushButton(QStringLiteral("Manage Target List..."), targetPanel);
    manageTargetsButton_->setToolTip(
        QStringLiteral("Add, rename, delete targets and edit the points they sonicate at"));
    manageTargetsButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    targetPanelLayout->addWidget(manageTargetsButton_);
    bodyLayout->addWidget(targetPanel);
    treatmentBodyRow_ = bodyLayout;

    // ---- app.stimParamTable and its button row --------------------------
    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(6);
    // No Sort / Add / Remove buttons: BeamV0 has one sonication row per target
    // and no UI to change that. createSonicationUpdateTable.m and
    // removeSonicationUpdateTable.m are unreferenced there, and
    // sortSonicationTable.m runs on its own rather than from a button. Several
    // rows per target is DiademV0's behaviour, which is a separate application.
    stimParamModel_ = new beam::gui_qt::StimParamTableModel(this);
    stimParamView_ = new QTableView(bodyContent);
    stimParamView_->setModel(stimParamModel_);
    configureModelTable(stimParamView_);
    // The sonication being fired is read off this one row, so it reads on
    // paper rather than in the dark palette the rest of the page uses.
    // The same paper the log table beside it uses, zebra stripe included, so
    // the two read as one surface rather than two different kinds of table.
    stimParamView_->setAlternatingRowColors(true);
    stimParamView_->setStyleSheet(QStringLiteral(
        "QTableView { background: #ffffff; alternate-background-color: #f2f4f7; "
        "color: #10232b; gridline-color: #b9c9d0; } "
        "QTableView::item:selected { background: #ffffff; color: #10232b; }"));
    // The selected row is painted from the palette's Highlight, which the
    // stylesheet rule above does not reach. The grid holds one row and it is
    // always selected, so highlighting it only tinted the whole table.
    {
        QPalette plain = stimParamView_->palette();
        plain.setColor(QPalette::Highlight, QColor(0xff, 0xff, 0xff));
        plain.setColor(QPalette::HighlightedText, QColor(0x10, 0x23, 0x2b));
        stimParamView_->setPalette(plain);
    }
    // The grid is exactly as tall as the rows it holds -- normally one -- so
    // the sub-tabs below start right under it instead of after a band of
    // empty white. updateStimGridHeight keeps that true as rows come and go.
    connect(stimParamModel_, &QAbstractItemModel::modelReset, this, [this] { updateStimGridHeight(); });
    connect(stimParamModel_, &QAbstractItemModel::rowsInserted, this, [this] { updateStimGridHeight(); });
    connect(stimParamModel_, &QAbstractItemModel::rowsRemoved, this, [this] { updateStimGridHeight(); });
    rightColumn->addWidget(stimParamView_);

    // ---- TabGroup3 ------------------------------------------------------
    treatmentTabs_ = new QTabWidget(bodyContent);
    treatmentTabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #356574; background: #173944; } "
        "QTabBar::tab { color: #dcebef; background: #244b58; padding: 7px 13px; } "
        "QTabBar::tab:selected { background: #176b87; }"));
    // A QStackedWidget takes the largest minimum of ALL its pages, so Pulse
    // Details and Targeting Examples imposed 284px while the operator was
    // looking at Treatment Protocol. That put the column above this scroll
    // area's viewport, so a wheel over the protocol table scrolled the whole
    // lower half of the page. Ignored drops the pages' hint; the floor below
    // is what the tabs actually reserve, and each tab scrolls inside it.
    treatmentTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    treatmentTabs_->setMinimumHeight(270);  // six schedule rows, header and tab bar

    // Treatment Protocol tab.
    auto* protocolTab = new QWidget(treatmentTabs_);
    auto* protocolLayout = new QVBoxLayout(protocolTab);
    auto* protocolSelectorRow = new QHBoxLayout;
    treatmentProtocolLabel_ = new QLabel(QStringLiteral("Protocol (the plan):"), protocolTab);
    treatmentProtocolCombo_ = new QComboBox(protocolTab);
    treatmentProtocolCombo_->addItems({QStringLiteral("Default"), QStringLiteral("Addiction"),
                                       QStringLiteral("PTSD"), QStringLiteral("PainACC"),
                                       QStringLiteral("PainSCCandAMCC"), QStringLiteral("PainAMCCandSCC")});
    visitNumberCombo_ = new QComboBox(protocolTab);
    // Fifteen, which is how many sessions BeamV0 keeps per protocol:
    // treatmentProtocolTables(i).sessions is a 15-element array, and
    // addTreatmentProtocolSession.m refuses to add past maxTreatmentSessions.
    // Three was a placeholder with nothing behind it.
    for (int visit = 1; visit <= kMaxTreatmentVisits; ++visit)
        visitNumberCombo_->addItem(QString::number(visit));
    // The table below is this visit's log, so switching visit reloads it.
    connect(visitNumberCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { loadTreatmentProtocol(currentProtocolName()); });
    newVisitButton_ = new QPushButton(QStringLiteral("New Visit"), protocolTab);
    newVisitButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    auto* visitLabel = new QLabel(QStringLiteral("Visit:"), protocolTab);
    for (QComboBox* combo : {treatmentProtocolCombo_, visitNumberCombo_}) applyItemViewPalette(combo);
    const auto makeProtocolButton = [&](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text, protocolTab);
        button->setToolTip(tip);
        button->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    };
    manageLibraryButton_ = makeProtocolButton(
        QStringLiteral("Manage Protocols..."),
        QStringLiteral("Add, rename, delete protocols and the targets each one schedules"));
    protocolSelectorRow->addWidget(treatmentProtocolLabel_);
    protocolSelectorRow->addWidget(treatmentProtocolCombo_);
    protocolSelectorRow->addWidget(manageLibraryButton_);
    saveLogButton_ = makeProtocolButton(QStringLiteral("Save Log..."),
                                        QStringLiteral("Write this visit's log to a file of your choosing"));
    connect(saveLogButton_, &QPushButton::clicked, this, [this] { saveTreatmentLogAs(); });
    protocolSelectorRow->addWidget(saveLogButton_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(visitLabel);
    protocolSelectorRow->addWidget(visitNumberCombo_);
    protocolSelectorRow->addWidget(newVisitButton_);

    // HydrogelSizeButtonGroup, as a combo rather than BeamV0's three radios:
    // this row now also carries the protocol selector, and three radios plus
    // their labels did not fit beside it.
    auto* hydrogelLabel = new QLabel(QStringLiteral("Hydrogel size:"), protocolTab);
    hydrogelSizeCombo_ = new QComboBox(protocolTab);
    hydrogelSizeCombo_->addItems({QStringLiteral("Small"), QStringLiteral("Medium"),
                                  QStringLiteral("Large")});
    hydrogelSizeCombo_->setCurrentIndex(0);  // HydrogelSizeButtonGroup's own default
    applyItemViewPalette(hydrogelSizeCombo_);
    auto* sonicationNumberLabel = new QLabel(QStringLiteral("Current Sonication Number:"), protocolTab);
    currentSonicationNumberEdit_ = new QSpinBox(protocolTab);
    currentSonicationNumberEdit_->setRange(1, 999);
    currentSonicationNumberEdit_->setMaximumWidth(70);
    applyItemViewPalette(currentSonicationNumberEdit_);
    for (QLabel* label : {treatmentProtocolLabel_, visitLabel, hydrogelLabel, sonicationNumberLabel})
        label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    // One row, not two: these five fields and the protocol selector together
    // fit the tab's width, and the row they used to occupy goes to the
    // schedule below.
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(hydrogelLabel);
    protocolSelectorRow->addWidget(hydrogelSizeCombo_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(sonicationNumberLabel);
    protocolSelectorRow->addWidget(currentSonicationNumberEdit_);
    // updateSonicateSettingsForCurrentSonication.m indexes the schedule with
    // this field, so typing a number here moves to that scheduled sonication.
    connect(currentSonicationNumberEdit_, &QSpinBox::valueChanged, this,
            [this](int value) { selectScheduledSonication(value); });
    protocolSelectorRow->addStretch(1);
    protocolLayout->addLayout(protocolSelectorRow);

    protocolModel_ = new beam::gui_qt::TreatmentProtocolTableModel(this);
    protocolView_ = new QTableView(protocolTab);
    protocolView_->setModel(protocolModel_);
    configureModelTable(protocolView_);
    // Explicit widths, not ResizeToContents: that sizes the view to the sum of
    // its content, and the view then demanded the whole row -- the Sonicate and
    // serial column was pushed off the page entirely. These fit the longest
    // label in each column; Notes takes what is left, and the view scrolls
    // sideways rather than growing if the window is narrow.
    protocolView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    protocolView_->horizontalHeader()->setStretchLastSection(true);
    protocolView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    protocolView_->setToolTip(QStringLiteral(
        "This visit's log: what was actually delivered. The protocol it started from is "
        "edited in Manage Protocols."));
    protocolView_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    {
        const int widths[] = {36, 78, 86, 86, 250, 104, 110, 142};
        for (int column = 0; column < static_cast<int>(std::size(widths)); ++column)
            protocolView_->setColumnWidth(column, widths[column]);
    }
    // Both tables carry one line of text per cell, so the default padding is
    // most of their height. The log in particular is a list to scan.
    protocolView_->horizontalHeader()->setFixedHeight(22);
    protocolView_->verticalHeader()->setDefaultSectionSize(19);
    stimParamView_->horizontalHeader()->setFixedHeight(22);
    stimParamView_->verticalHeader()->setDefaultSectionSize(21);
    protocolView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(protocolView_, &QTableView::customContextMenuRequested, this,
            [this](const QPoint& where) { showScheduleRowMenu(where); });
    // A protocol is 24-odd entries; at the default row height only a handful
    // fit and the rest pushed the page taller. Compact rows plus the table's
    // own scrollbar keep the schedule readable without growing the page.
    protocolView_->verticalHeader()->setDefaultSectionSize(22);
    protocolView_->verticalHeader()->setMinimumSectionSize(18);
    protocolView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    protocolView_->setMinimumHeight(120);
    protocolView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // The table takes the tab's leftover height, so there is no band of empty
    // space under it and no surplus for the layout to spread between the rows
    // above. Its own scrollbar handles a schedule longer than the space.
    protocolLayout->addWidget(protocolView_, 1);

    treatmentTabs_->addTab(protocolTab, QStringLiteral("Treatment Log"));

    // Pulse Details tab: BeamV0's axPulseWaveformPlot / axBurstWaveformPlot
    // / axTotalSonicationPlot. The two pulse-scale plots share a row so all
    // three fit without the tab needing the whole page.
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

    // Targeting Examples tab: BeamV0's three reference views for the chosen
    // region, side by side as the source shows them, plus the best-targets
    // ranking its setTreatmentProtocolTableData.m computes automatically.
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
    accFlagRow->addWidget(accFlagLabel);
    accFlagRow->addWidget(accFlagCombo_);
    accFlagRow->addWidget(computeBestTargetsButton_);
    accFlagRow->addStretch(1);
    targetingLayout->addLayout(accFlagRow);
    auto* imageRow = new QHBoxLayout;
    for (QLabel** imageSlot : {&exampleTargetImage_, &exampleTargetImage2_, &exampleTargetImage3_}) {
        *imageSlot = new QLabel(targetingTab);
        (*imageSlot)->setAlignment(Qt::AlignCenter);
        (*imageSlot)->setMinimumHeight(120);
        (*imageSlot)->setStyleSheet(QStringLiteral("QLabel { background: #101820; border: 1px solid #2b6172; }"));
        imageRow->addWidget(*imageSlot, 1);
    }
    targetingLayout->addLayout(imageRow, 1);
    exampleTargetText_ = new QLabel(targetingTab);
    exampleTargetText_->setWordWrap(true);
    exampleTargetText_->setStyleSheet(QStringLiteral("QLabel { color: #c8d9de; padding: 4px; }"));
    targetingLayout->addWidget(exampleTargetText_);
    bestTargetsList_ = new QListWidget(targetingTab);
    bestTargetsList_->setMaximumHeight(70);
    applyItemViewPalette(bestTargetsList_);
    targetingLayout->addWidget(bestTargetsList_);
    treatmentTabs_->addTab(targetingTab, QStringLiteral("Targeting Examples"));

    rightColumn->addWidget(treatmentTabs_, 1);

    bodyLayout->addLayout(rightColumn, 1);
    treatmentPlanBody_->setVisible(false);

    wireTreatmentPlanBody();
}

void WorkflowWindow::wireTreatmentPlanBody() {
    // ---- the sonication grid -------------------------------------------
    connect(stimParamModel_, &beam::gui_qt::StimParamTableModel::showFlagsChanged, this,
            [this] { updateSonicationPlots(); });
    connect(stimParamModel_, &QAbstractItemModel::dataChanged, this, [this] {
        saveCurrentTargetSonications();
        saveTreatmentTargetStore();
        updateSonicationPlots();
    });
    connect(stimParamView_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) updateSonicationPlots();
            });


    // ---- the target list ------------------------------------------------
    // Rename / create / delete live on each row; see addTargetListRow.
    // Move To Target is real here because the list carries positions: each
    // item's data() holds the RAS millimetres an MRI click stored.
    connect(moveToTargetButton_, &QPushButton::clicked, this, [this] {
        const QListWidgetItem* item = targetListWidget_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) {
            showMessage(QStringLiteral("That target has no position yet. Select a sonication and click a point "
                                       "in any MRI plane to give it one."),
                        true);
            return;
        }
        // BeamV0 moves to the sonication getCurrentShownSonication.m picks --
        // the last row with Show ticked -- not to a position held per target.
        const std::vector<beam::gui_qt::StimParamRow>& rows = stimParamModel_->rows();
        if (rows.empty()) return;
        const std::size_t shown = static_cast<std::size_t>(
            std::clamp(stimParamModel_->currentShownRow(), 1, static_cast<int>(rows.size()))) - 1;
        focusTreatmentViewsOn(Eigen::Vector3d(rows[shown].x, rows[shown].y, rows[shown].z));
    });
    // The grid row belongs to whichever target is selected, so switching
    // target loads that target's placed coordinates into X/Y/Z (zeros if it
    // has never been placed) and re-centres the planes on it.
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
            [this] { saveTreatmentTargetStore(); });
    connect(newVisitButton_, &QPushButton::clicked, this, [this] {
        // The next visit that has not run anything yet, so New Visit means
        // "move on" rather than "add a number past the end".
        const std::string protocol = currentProtocolName().toStdString();
        int next = 0;
        for (int visit = 1; visit <= kMaxTreatmentVisits && next == 0; ++visit)
            if (treatmentLog_.rowsFor(protocol, visit).empty()) next = visit;
        if (next == 0) {
            showMessage(QStringLiteral("All %1 visits of this protocol have been used.")
                            .arg(kMaxTreatmentVisits),
                        true);
            return;
        }
        visitNumberCombo_->setCurrentText(QString::number(next));
        showMessage(QStringLiteral("Visit %1 started.").arg(next), false);
        loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    });
    connect(computeBestTargetsButton_, &QPushButton::clicked, this, [this] { updateBestTargets(); });
    connect(accFlagCombo_, &QComboBox::currentTextChanged, this, [this] {
        updateBestTargets();
        updateExampleTargetImage();
    });
    connect(protocolModel_, &QAbstractItemModel::dataChanged, this, [this] { updateBestTargets(); });

    // Already loaded in buildTreatmentPlanBody -- the Target List needs it.
    refreshProtocolCombo(QStringLiteral("Default"));

    loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    updateExampleTargetImage();
}

// Port of BeamV0's per-protocol CSVs, unchanged from this application's
// earlier hand-built version except that the rows now land in the real
// TreatmentProtocolTableModel instead of a QTableWidget of strings.
// Where the operator's own protocol/target library lives. The copies under
// data/ in the install are the shipped seed and are never written: deleting
// the user's copy restores them.
QString WorkflowWindow::currentProtocolName() const {
    return treatmentProtocolCombo_ ? treatmentProtocolCombo_->currentText() : QString();
}

// data/runtime/ inside the repository: edits sit with the project rather than
// in a per-user folder, but NOT on top of data/*.csv itself. Those are the
// migrated BeamV0 fixtures that treatment_migration_tests pins, and writing
// the operator's edits over them makes the app fail its own tests.
// Falls back to Documents when the repository is not there -- an installed
// build, whose executable directory is not writable anyway.
QString WorkflowWindow::treatmentDataDirectory() const {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(executableDir).filePath(QStringLiteral("../../../data")),
        QDir(executableDir).filePath(QStringLiteral("../../data")),
        QDir(executableDir).filePath(QStringLiteral("data")),
        QDir::current().filePath(QStringLiteral("data"))};
    for (const QString& candidate : candidates) {
        const QDir directory(candidate);
        if (!directory.exists()) continue;
        const QString runtime = directory.absoluteFilePath(QStringLiteral("runtime"));
        if (QDir().mkpath(runtime)) return runtime;
    }
    const QString home =
        QDir(QDir::homePath()).filePath(QStringLiteral("Documents/BeamAI/treatment"));
    QDir().mkpath(home);
    return home;
}

void WorkflowWindow::loadTreatmentTargetStore() {
    const auto locate = [this](const QString& fileName) {
        const QString user = QDir(treatmentDataDirectory()).filePath(fileName);
        if (QFileInfo::exists(user)) return user;
        const QString executableDir = QCoreApplication::applicationDirPath();
        const QStringList seeds = {
            QDir(executableDir).filePath(QStringLiteral("../../../data/") + fileName),
            QDir(executableDir).filePath(QStringLiteral("../../data/") + fileName),
            QDir(executableDir).filePath(QStringLiteral("data/") + fileName),
            QDir::current().filePath(QStringLiteral("data/") + fileName)};
        for (const QString& seed : seeds)
            if (QFileInfo::exists(seed)) return seed;
        return QString();
    };

    const QString targetsPath = locate(QStringLiteral("treatment_targets.csv"));
    const QString protocolsPath = locate(QStringLiteral("treatment_protocols.csv"));
    if (targetsPath.isEmpty() || protocolsPath.isEmpty()) {
        showMessage(QStringLiteral("Treatment target/protocol data was not found; using built-in "
                                   "defaults."),
                    true);
        return;
    }
    try {
        targetStore_.loadTargets(targetsPath.toStdString());
        targetStore_.loadProtocols(protocolsPath.toStdString());
        // The log lives with the patient's data, not the install's seed.
        treatmentLog_.load(QDir(treatmentDataDirectory())
                               .filePath(QStringLiteral("treatment_log.csv"))
                               .toStdString());
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Treatment data could not be read: %1").arg(error.what()), true);
        return;
    }
    const std::vector<std::string> dangling = targetStore_.danglingTargetNames();
    if (!dangling.empty()) {
        showMessage(QStringLiteral("A protocol references %1 undefined target(s), starting with "
                                   "\"%2\".")
                        .arg(dangling.size())
                        .arg(QString::fromStdString(dangling.front())),
                    true);
    }
}

void WorkflowWindow::refreshProtocolCombo(const QString& select) {
    if (!treatmentProtocolCombo_ || targetStore_.protocols().empty()) return;
    const QSignalBlocker blocker(treatmentProtocolCombo_);
    treatmentProtocolCombo_->clear();
    for (const beam::gui::TreatmentProtocolDefinition& definition : targetStore_.protocols())
        treatmentProtocolCombo_->addItem(QString::fromStdString(definition.name));
    const int index = treatmentProtocolCombo_->findText(select);
    treatmentProtocolCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

// The protocol grid is the authority while it is on screen; this pushes it
// back so a create/delete/save sees the operator's edits.
void WorkflowWindow::writeProtocolGridToStore() {
    // Writes the table back into the VISIT LOG and nowhere else, which is all
    // setTreatmentProtocolTableData.m does: `sessions(visit).Data = table.Data`.
    // Nothing here touches the protocol or the Target List. Writing the table
    // into the plan is what renamed a scheduled target to whichever one had
    // been sonicated into that row.
    if (!protocolModel_ || !treatmentProtocolCombo_) return;
    const std::string protocol = treatmentProtocolCombo_->currentText().toStdString();
    const int visit = currentVisitNumber();
    for (std::size_t index = 0; index < protocolModel_->rows().size(); ++index) {
        const beam::gui_qt::TreatmentProtocolRow& source = protocolModel_->rows()[index];
        const int sequence = static_cast<int>(index) + 1;
        const beam::gui::TreatmentLogRow* existing = treatmentLog_.find(protocol, visit, sequence);
        if (existing == nullptr) continue;
        beam::gui::TreatmentLogRow row = *existing;
        row.responsePain = source.responsePain;
        row.responseMood = source.responseMood;
        row.notes = source.notes.toStdString();
        // A delivered row records what was sent; only the operator entries
        // above can change. One not yet fired is still the plan copy, so an
        // edit to it is the operator adjusting this visit.
        if (!beam::gui::TreatmentLog::delivered(row)) {
            row.target = source.target.toStdString();
            row.amplitude = source.amplitude;
            row.endTimeSeconds = row.startTimeSeconds + source.duration;
        }
        treatmentLog_.record(std::move(row));
    }
}

void WorkflowWindow::saveTreatmentTargetStore() {
    writeProtocolGridToStore();
    writeTargetPointsToStore();
    const QString directory = treatmentDataDirectory();
    try {
        targetStore_.saveTargets(
            QDir(directory).filePath(QStringLiteral("treatment_targets.csv")).toStdString());
        targetStore_.saveProtocols(
            QDir(directory).filePath(QStringLiteral("treatment_protocols.csv")).toStdString());
        treatmentLog_.save(
            QDir(directory).filePath(QStringLiteral("treatment_log.csv")).toStdString());
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Could not save treatment data: %1").arg(error.what()), true);
        return;
    }
}

void WorkflowWindow::loadTreatmentProtocol(const QString& protocolName) {
    // The table on this page is the VISIT LOG, not the protocol. BeamV0 draws
    // treatmentProtocolTables(i).sessions(visit).Data here, seeded from
    // blank.Data by initTreatmentProtocolTables.m and written back by
    // setTreatmentProtocolTableData.m -- never into blank. The protocol itself
    // is the model, and Manage Protocols is the only thing that edits it.
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentProtocolDefinition* definition =
        targetStore_.findProtocol(protocol);

    // What the visit would run if nothing had happened yet: the schedule, with
    // each target own parameters filled in from the Target List.
    std::vector<beam::gui::TreatmentLogRow> planned;
    if (definition != nullptr) {
        for (std::size_t index = 0; index < definition->entries.size(); ++index) {
            const beam::gui::TreatmentProtocolEntry& entry = definition->entries[index];
            const beam::gui::TreatmentTarget* target = targetStore_.findTarget(entry.target);
            const beam::gui::TreatmentTargetPoint* first =
                target != nullptr && !target->points.empty() ? &target->points.front() : nullptr;
            beam::gui::TreatmentLogRow row;
            row.sequence = static_cast<int>(index) + 1;
            row.target = entry.target;
            // The point amplitude, not the entry: BeamV0 stores a schedule
            // amplitude but never applies it, so the point is what is sent.
            row.amplitude = first != nullptr ? first->amplitude : entry.amplitude.value_or(0.75);
            row.startTimeSeconds = 0.0;
            row.endTimeSeconds =
                entry.durationSeconds.value_or(first != nullptr ? first->endTimeSeconds : 30.0);
            if (first != nullptr) {
                row.burstDurationSeconds = first->burstDurationSeconds;
                row.burstIntervalSeconds = first->burstIntervalSeconds;
                row.pulseDurationSeconds = first->pulseDurationSeconds;
                row.pulseIntervalSeconds = first->pulseIntervalSeconds;
            }
            planned.push_back(std::move(row));
        }
    }
    const bool frozen = treatmentLog_.visitFrozen(protocol, visit);
    treatmentLog_.seedVisit(visit, protocol, planned);
    // A frozen visit silently ignoring an edited protocol looks like the edit
    // was lost. Say which it is, and what to do about it.
    if (frozen) {
        const std::vector<beam::gui::TreatmentLogRow> current =
            treatmentLog_.rowsFor(protocol, visit);
        bool differs = current.size() != planned.size();
        for (std::size_t i = 0; !differs && i < planned.size(); ++i)
            differs = current[i].target != planned[i].target;
        if (differs) {
            showMessage(QStringLiteral("Visit %1 of \"%2\" has sonications inserted or skipped by "
                                       "hand, so it keeps its own order. Protocol edits reach a "
                                       "visit that still follows the plan.")
                            .arg(visit)
                            .arg(protocolName),
                        false);
        }
    }

    std::vector<beam::gui_qt::TreatmentProtocolRow> protocolRows;
    QStringList targetNames;
    for (const beam::gui::TreatmentLogRow& logged : treatmentLog_.rowsFor(protocol, visit)) {
        beam::gui_qt::TreatmentProtocolRow row;
        row.number = logged.sequence;
        row.target = QString::fromStdString(logged.target);
        row.duration = logged.endTimeSeconds - logged.startTimeSeconds;
        row.amplitude = logged.amplitude;
        row.parameters =
            QString::fromStdString(beam::gui::TreatmentLog::parameterString(logged));
        const QDateTime ended = QDateTime::fromString(
            QString::fromStdString(logged.endedAt), Qt::ISODate);
        row.completed =
            ended.isValid() ? ended.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
        row.responsePain = logged.responsePain.value_or(0.0);
        row.responseMood = logged.responseMood.value_or(0.0);
        row.notes = QString::fromStdString(logged.notes);
        protocolRows.push_back(row);
        if (!targetNames.contains(row.target)) targetNames.push_back(row.target);
    }

    const double defaultAmplitude = protocolRows.empty() ? 0.75 : protocolRows.front().amplitude;
    const double defaultDuration = protocolRows.empty() ? 30.0 : protocolRows.front().duration;
    protocolModel_->setRows(std::move(protocolRows));

    beam::gui_qt::StimParamRow stimRow;
    stimRow.order = 1;
    stimRow.show = true;
    stimRow.amplitude = defaultAmplitude;
    stimRow.startTime = 0.0;
    stimRow.endTime = defaultDuration;
    stimParamModel_->setRows({stimRow});

    // A different protocol is a different set of sonications, so the
    // per-target records start over rather than carrying the last one across.
    targetSonications_.clear();
    currentTargetId_ = -1;

    // The Target List is BeamV0's twelve and does not grow to fit a protocol.
    // The BEAM protocol CSVs schedule NACC_*, VPL_* and T1-T6, which no Target
    // List defines; a protocol naming them cannot be run, and saying so beats
    // inventing a row that has no placement and no parameters.
    QStringList undefined;
    for (const QString& name : targetNames) {
        bool known = false;
        for (int i = 0; i < targetListWidget_->count() && !known; ++i)
            known = targetListName(targetListWidget_->item(i)) == name;
        if (!known) undefined.push_back(name);
    }
    if (!undefined.isEmpty()) {
        showMessage(QStringLiteral("Protocol \"%1\" schedules %2 target(s) the Target List does not "
                                   "define (%3); those sonications cannot be placed.")
                        .arg(protocolName)
                        .arg(undefined.size())
                        .arg(undefined.join(QStringLiteral(", "))),
                    true);
    }

    selectSonicationRow(0);
    switchToSelectedTarget();
    // A protocol opens on its first scheduled sonication. Reloading the SAME
    // protocol keeps where the visit has got to: this runs again after every
    // delivery, to show the row that was just logged, and resetting to 1 there
    // made every Sonicate advance to 2 and stop.
    const bool sameProtocol = protocolName == lastLoadedProtocol_;
    const int keep = sameProtocol && currentSonicationNumberEdit_
                         ? currentSonicationNumberEdit_->value()
                         : 1;
    lastLoadedProtocol_ = protocolName;
    selectScheduledSonication(keep);
    updateBestTargets();
}

void WorkflowWindow::selectSonicationRow(int row) {
    if (!stimParamView_ || stimParamModel_->rows().empty()) return;
    const int clamped = std::clamp(row, 0, static_cast<int>(stimParamModel_->rows().size()) - 1);
    stimParamView_->selectRow(clamped);
    updateSonicationPlots();
}

int WorkflowWindow::selectedSonicationRow() const {
    if (!stimParamView_ || stimParamModel_->rows().empty()) return -1;
    const QModelIndex current = stimParamView_->selectionModel()->currentIndex();
    if (!current.isValid()) return 0;
    return std::clamp(current.row(), 0, static_cast<int>(stimParamModel_->rows().size()) - 1);
}

// The sonication grid holds one row in normal use, so a fixed slice of the
// body left a band of empty white under it and pushed the sub-tabs down.
// Sizing it to its own rows closes that gap; a plan with several sonications
// grows it up to a cap, past which the grid scrolls.
void WorkflowWindow::updateStimGridHeight() {
    if (!stimParamView_) return;
    const int rows = stimParamModel_->rowCount();
    const int rowHeight = rows > 0 ? stimParamView_->rowHeight(0) : 26;
    const int wanted = stimParamView_->horizontalHeader()->height() + rows * rowHeight +
                       2 * stimParamView_->frameWidth();
    stimParamView_->setFixedHeight(std::clamp(wanted, 56, 220));
}

// Copies the selected target's placed position into the grid row's X/Y/Z,
// so the grid always shows the coordinates of the target being worked on.
// An unplaced target reads as zeros, which is what BeamV0's grid starts at.
// Port of app.sys.protocolTables: every target owns its own stimParamTable
// data, and selecting a target swaps the grid to it
// (setProtocolTableWithStimParamTable.m saves the grid back into the selected
// target's slot, setProtocolListBox.m rebuilds the list from those records).
// Beam's C++ port left this model out -- known_gaps_gui.md calls it out, and
// it is why its own Move To Target has no per-target position to move to.
// Without it every target showed the same row, so switching target appeared
// to do nothing.
void WorkflowWindow::switchToSelectedTarget() {
    if (!stimParamModel_ || !targetListWidget_) return;
    saveCurrentTargetSonications();

    const QListWidgetItem* item = targetListWidget_->currentItem();
    currentTargetId_ = targetListId(item);
    if (currentTargetId_ < 0) return;

    const auto stored = targetSonications_.constFind(currentTargetId_);
    if (stored != targetSonications_.constEnd() && !stored.value().empty()) {
        stimParamModel_->setRows(stored.value());
    } else {
        // First visit to this target: every point the Target List defines for
        // it, not just one. The grid is that target's own stimParamTable, and
        // each row keeps its own parameters -- a target with three points must
        // arrive with three rows or two of them are silently lost.
        //
        // Each sits at the array centre until the operator places it. That
        // centre is drawROIs.m's `centerArrayMM =
        // mean(arrayData.arrayTotal.rect(17:19,:),2)*1000`, which targetMm_ is
        // this application's copy of.
        const QString name = targetListName(item);

        // The protocol contributes the duration and nothing else:
        // updateSonicateSettingsForCurrentSonication.m sets startTime and
        // endTime down the whole column and leaves Amplitude to the target.
        std::optional<double> scheduledDuration;
        for (const beam::gui_qt::TreatmentProtocolRow& protocolRow : protocolModel_->rows()) {
            if (protocolRow.target == name) {
                scheduledDuration = protocolRow.duration;
                break;
            }
        }

        std::vector<beam::gui_qt::StimParamRow> rows;
        // No entry means this protocol does not schedule this target. Clamping
        // The target's own rows. scheduledDuration above is the protocol's
        // only contribution, exactly as updateSonicateSettingsForCurrent-
        // Sonication.m has it: the schedule sets how long, the target says
        // everything else.
        for (const beam::gui::TreatmentTargetPoint& point :
             targetStore_.pointsForSonication(name.toStdString(), scheduledDuration)) {
            beam::gui_qt::StimParamRow row;
            row.order = point.order;
            row.show = point.show;
            row.amplitude = point.amplitude;
            row.startTime = point.startTimeSeconds;
            row.endTime = point.endTimeSeconds;
            row.bd = point.burstDurationSeconds;
            row.bi = point.burstIntervalSeconds;
            row.pd = point.pulseDurationSeconds;
            row.pi = point.pulseIntervalSeconds;
            row.x = targetMm_.x();
            row.y = targetMm_.y();
            row.z = targetMm_.z();
            rows.push_back(row);
        }
        // Either the Target List does not define this target, or this protocol
        // does not schedule it. One blank row keeps the grid usable rather than
        // empty; the label above it says which case this is.
        if (rows.empty()) {
            beam::gui_qt::StimParamRow row;
            row.order = 1;
            row.show = true;
            if (scheduledDuration) row.endTime = *scheduledDuration;
            row.x = targetMm_.x();
            row.y = targetMm_.y();
            row.z = targetMm_.z();
            rows.push_back(row);
        }
        stimParamModel_->setRows(std::move(rows));
    }
    // A target the operator has never placed follows the array centre, which
    // moves as registration and targeting proceed -- including from zero, when
    // the record was first created before any MRI was loaded. Placed targets
    // (those carrying a position on their list item) keep their own.
    if (item && !item->data(Qt::UserRole).isValid() && !targetMm_.isZero()) {
        std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
        // Every point of an unplaced target, not just the first -- the list
        // item carries one position, so they all start from the same centre.
        for (beam::gui_qt::StimParamRow& row : rows) {
            row.x = targetMm_.x();
            row.y = targetMm_.y();
            row.z = targetMm_.z();
        }
        stimParamModel_->setRows(std::move(rows));
    }
    stimParamView_->selectRow(0);
    saveCurrentTargetSonications();
    updateSonicationPlots();
}

// Port of registerCurrentTransducerPostion.m's tail: once the array is
// registered to the current lock position, every target's sonication -- not
// just the selected one, and not just the unplaced ones -- takes the
// registered array centre as its X/Y/Z. The operator can then move individual
// targets off it; until they do, the plan reflects where the array actually
// points.
void WorkflowWindow::applyArrayCentreToAllTargets(const Eigen::Vector3d& centreMm) {
    if (!stimParamModel_ || !targetListWidget_) return;
    for (auto it = targetSonications_.begin(); it != targetSonications_.end(); ++it) {
        for (beam::gui_qt::StimParamRow& row : it.value()) {
            row.x = centreMm.x();
            row.y = centreMm.y();
            row.z = centreMm.z();
        }
    }
    // The list items carry the same position for Move To Target.
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        targetListWidget_->item(i)->setData(
            Qt::UserRole, QList<QVariant>{centreMm.x(), centreMm.y(), centreMm.z()});
    }
    // And the grid currently on screen, which may not have been saved yet.
    if (!stimParamModel_->rows().empty()) {
        std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
        for (beam::gui_qt::StimParamRow& row : rows) {
            row.x = centreMm.x();
            row.y = centreMm.y();
            row.z = centreMm.z();
        }
        stimParamModel_->setRows(std::move(rows));
        saveCurrentTargetSonications();
    }
}

void WorkflowWindow::saveCurrentTargetSonications() {
    if (currentTargetId_ >= 0 && stimParamModel_ && !stimParamModel_->rows().empty())
        targetSonications_.insert(currentTargetId_, stimParamModel_->rows());
}

void WorkflowWindow::updateSonicationPlots() {
    if (!pulsePlotView_ || stimParamModel_->rows().empty()) return;
    // getCurrentShownSonication is 1-based, and is what BeamV0 plots.
    const int shown = std::clamp(stimParamModel_->currentShownRow(), 1,
                                 static_cast<int>(stimParamModel_->rows().size()));
    const beam::gui_qt::StimParamRow& r = stimParamModel_->rows()[static_cast<std::size_t>(shown - 1)];

    pulsePlotView_->setData(beam::gui::computePulseWaveformPlot(r.pd, r.pi, r.amplitude),
                            QStringLiteral("Pulse waveform"), QStringLiteral("Time (s)"));
    burstPlotView_->setData(beam::gui::computeBurstWaveformPlot(r.bd, r.bi, r.pd, r.pi, r.amplitude),
                            QStringLiteral("Burst waveform"), QStringLiteral("Time (s)"));

    // An infeasible row (duration shorter than one burst) throws rather
    // than producing a timeline; show an empty plot instead of failing.
    beam::stimulation::SonicationSchedule schedule;
    schedule.startTime = r.startTime;
    schedule.endTime = r.endTime;
    schedule.bi = r.bi;
    schedule.bd = r.bd;
    schedule.pi = r.pi;
    schedule.pd = r.pd;
    try {
        timelineView_->setData(beam::stimulation::computeSonicationEventTimeline({schedule}), r.amplitude,
                               r.endTime - r.startTime);
    } catch (const std::exception&) {
        timelineView_->setData({}, r.amplitude, r.endTime - r.startTime);
    }
}

void WorkflowWindow::updateBestTargets() {
    if (!bestTargetsList_ || !protocolModel_) return;
    const beam::gui::AccFlag flag = accFlagCombo_->currentIndex() == 1   ? beam::gui::AccFlag::kScc
                                    : accFlagCombo_->currentIndex() == 2 ? beam::gui::AccFlag::kAmcc
                                                                          : beam::gui::AccFlag::kOther;
    const beam::gui::BestTargets best = protocolModel_->computeBestTargets(flag);
    bestTargetsList_->clear();
    for (const std::string& name : best.name)
        bestTargetsList_->addItem(QString::fromStdString(name));
}

void WorkflowWindow::updateExampleTargetImage() {
    if (!exampleTargetImage_) return;
    // BeamV0.mlapp's own SonicationTab/ExampleTargets images, compiled in
    // through designer/resources/example_targets.qrc. The source shows three
    // views of the chosen region at once, so all three labels are filled.
    const QString stem = accFlagCombo_->currentIndex() == 1   ? QStringLiteral("SCC1")
                         : accFlagCombo_->currentIndex() == 2 ? QStringLiteral("aMCC1")
                                                              : QStringLiteral("ACC");
    // ACC's own third view is the white-matter reference rather than a
    // coronal one -- the file set differs per region.
    const QStringList suffixes = stem == QStringLiteral("ACC")
                                     ? QStringList{QStringLiteral("Sag"), QStringLiteral("Axial"),
                                                   QStringLiteral("WhiteMatter")}
                                     : QStringList{QStringLiteral("Sag"), QStringLiteral("Cor"),
                                                   QStringLiteral("Axial")};
    QLabel* const imageSlots[] = {exampleTargetImage_, exampleTargetImage2_, exampleTargetImage3_};
    for (int i = 0; i < 3; ++i) {
        const QPixmap image(QStringLiteral(":/example_targets/%1%2.bmp").arg(stem, suffixes.value(i)));
        if (image.isNull()) {
            imageSlots[i]->setPixmap(QPixmap());
            imageSlots[i]->setText(QStringLiteral("%1 %2\nnot bundled").arg(stem, suffixes.value(i)));
            continue;
        }
        imageSlots[i]->setText(QString());
        // A fixed box rather than the label's own width: this also runs
        // during construction, when the label has not been laid out yet and
        // would report its default 100px.
        imageSlots[i]->setPixmap(image.scaled(420, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        imageSlots[i]->setToolTip(QStringLiteral("%1 — %2").arg(stem, suffixes.value(i)));
    }
    exampleTargetText_->setText(QString::fromStdString(
        beam::gui::exampleTargetHelpText(accFlagCombo_->currentText().toStdString())));
}

// ---------------------------------------------------------------------------
// Treatment execution: the half of BeamV0's Sonicate tab that actually fires.
//
// In BeamV0 these sit in a narrow right-hand column of the same tab that
// holds the plan, so an operator can sonicate at any time. Here they are a
// separate workflow stage, unreachable until the plan is accepted and Safety
// Review is complete -- the gate is the stage model, not a button's enabled
// state. The controls themselves are the .mlapp's own, in its own top-to-
// bottom order: Serial Port, Serial Connect, Connected lamp, Trigger, Sham,
// Sonicate, Abort Sonication. Get Params is not carried: its source action
// only exposes MATLAB's workspace for debugging.
// ---------------------------------------------------------------------------

namespace {

// BeamV0 loads its masking track from sonicationSound.mat, which is not
// redistributable and not bundled here; prepareShamSonication takes the
// caller's own buffer, so this synthesises the same 200 Hz burst beam_app
// uses (startup_data.cpp's syntheticShamBurstSound).
constexpr double kShamSoundFs = 44100.0;

std::vector<double> shamBurstSound() {
    constexpr double kToneHz = 200.0;
    constexpr double kBurstSeconds = 0.05;
    const auto n = static_cast<std::size_t>(kBurstSeconds * kShamSoundFs);
    std::vector<double> sound(n);
    for (std::size_t i = 0; i < n; ++i)
        sound[i] = std::sin(2.0 * 3.14159265358979323846 * kToneHz * static_cast<double>(i) / kShamSoundFs);
    return sound;
}

}  // namespace

void WorkflowWindow::buildTreatmentExecutionBody(const QString& acceptButtonStyle) {
    treatmentExecutionBody_ = new QWidget(ui_->placeholderPage);
    // Only as tall as the one status line it holds. Left Expanding it was
    // given 83px for 44px of content and centred it, which is the dark band
    // above and below the last line of the page.
    treatmentExecutionBody_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* outer = new QVBoxLayout(treatmentExecutionBody_);
    outer->setContentsMargins(0, 0, 0, 0);

    // No heading or blurb: execution is part of this page now, not a stage of
    // its own, and the Sonicate controls name themselves.

    auto* row = new QHBoxLayout;

    // One line, not three stacked: the countdown reads at the left, the
    // progress bar takes the middle, and Finish sits at the right where an
    // end-of-visit action belongs. Stacked, these three cost the MRI viewers
    // about 70px of height for text that fits on one row.
    auto* statusColumn = new QHBoxLayout;
    statusColumn->setSpacing(8);
    sonicationCountdownLabel_ = new QLabel(QStringLiteral("No sonication running."), treatmentExecutionBody_);
    sonicationCountdownLabel_->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    treatmentExecutionProgressBar_ = new QProgressBar(treatmentExecutionBody_);
    treatmentExecutionProgressBar_->setRange(0, 100);
    treatmentExecutionProgressBar_->setValue(0);
    treatmentExecutionProgressBar_->setTextVisible(false);
    treatmentExecutionProgressBar_->setMinimumHeight(24);
    treatmentExecutionProgressBar_->setMinimumWidth(420);
    treatmentExecutionProgressBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // The bar takes the larger share of the row: it is what the operator
    // watches while a sonication runs, and the label beside it holds one line.
    statusColumn->addWidget(sonicationCountdownLabel_, 1);
    statusColumn->addWidget(treatmentExecutionProgressBar_, 2);
    row->addLayout(statusColumn, 1);

    // Right: the .mlapp's own narrow control column.
    auto* controlColumn = new QVBoxLayout;
    // Tight, so the whole column clears the body that holds it: at the
    // default spacing Abort sonication fell past the bottom edge.
    controlColumn->setSpacing(4);
    controlColumn->setContentsMargins(0, 0, 0, 0);
    auto* controlPanel = new QWidget(treatmentExecutionBody_);
    // Wider than the target list: this column holds a port name, a trigger
    // mode and four buttons, and at 220 the combos truncated and the whole
    // thing read as squeezed into the corner. The grid and the schedule give
    // up the width -- their columns stretch and have it to spare.
    controlPanel->setMinimumWidth(320);
    controlPanel->setMaximumWidth(400);
    controlPanel->setLayout(controlColumn);
    const auto columnLabel = [&](const QString& text) {
        auto* label = new QLabel(text, controlPanel);
        label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
        return label;
    };
    serialPortCombo_ = new QComboBox(controlPanel);
    serialPortCombo_->setStyleSheet(QStringLiteral(
        "QComboBox { background: #ffffff; color: #17313f; padding: 4px 8px; "
        "border: 1px solid #7c9da8; border-radius: 4px; }"));
    for (const std::string& port : beam::serialcom::listAvailableComPorts())
        serialPortCombo_->addItem(QString::fromStdString(port));
    if (serialPortCombo_->count() == 0) serialPortCombo_->addItem(QStringLiteral("(no port detected)"));
    serialConnectButton_ = new QPushButton(QStringLiteral("Serial connect"), controlPanel);
    serialConnectedLamp_ = new QLabel(controlPanel);
    serialConnectedLamp_->setFixedSize(20, 20);
    serialConnectedLamp_->setAutoFillBackground(true);
    triggerModeCombo_ = new QComboBox(controlPanel);
    triggerModeCombo_->addItems({QStringLiteral("Immediate"), QStringLiteral("External")});
    triggerModeCombo_->setStyleSheet(serialPortCombo_->styleSheet());
    shamButton_ = new QPushButton(QStringLiteral("Sham"), controlPanel);
    startTreatmentButton_ = new QPushButton(QStringLiteral("Sonicate"), controlPanel);
    startTreatmentButton_->setMinimumHeight(40);
    startTreatmentButton_->setStyleSheet(acceptButtonStyle);
    abortTreatmentButton_ = new QPushButton(QStringLiteral("Abort sonication"), controlPanel);
    abortTreatmentButton_->setMinimumHeight(36);
    abortTreatmentButton_->setEnabled(false);
    // A plan holds many sonications and BeamV0 fires them one at a time, so
    // finishing the stage is a separate, deliberate action -- completing it
    // when the first countdown ends would skip the rest of the plan.
    finishTreatmentButton_ = new QPushButton(QStringLiteral("Finish treatment and continue →"), controlPanel);
    finishTreatmentButton_->setMinimumHeight(36);
    finishTreatmentButton_->setEnabled(false);
    for (QPushButton* button : {serialConnectButton_, shamButton_, abortTreatmentButton_, finishTreatmentButton_})
        button->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));

    auto* connectedRow = new QHBoxLayout;
    connectedRow->addWidget(columnLabel(QStringLiteral("Connected")));
    connectedRow->addWidget(serialConnectedLamp_);
    connectedRow->addStretch(1);
    controlColumn->addWidget(columnLabel(QStringLiteral("Serial port:")));
    controlColumn->addWidget(serialPortCombo_);
    controlColumn->addWidget(serialConnectButton_);
    controlColumn->addLayout(connectedRow);
    controlColumn->addWidget(columnLabel(QStringLiteral("Trigger:")));
    controlColumn->addWidget(triggerModeCombo_);
    controlColumn->addSpacing(6);
    controlColumn->addWidget(shamButton_);
    controlColumn->addWidget(startTreatmentButton_);
    controlColumn->addWidget(abortTreatmentButton_);
    // Slack below, or the box layout spreads the controls apart.
    controlColumn->addStretch(1);
    // Finish is not a firing control, so it stays out of the Sonicate column;
    // it ends the status line instead, at the far right.
    finishTreatmentButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Wide: it ends the visit, and on a row of status text a button sized to
    // its own label reads as one more label.
    finishTreatmentButton_->setMinimumWidth(420);
    statusColumn->addWidget(finishTreatmentButton_);
    // Beside the plan rather than under it. Stacked below, this column began
    // around 1150 px down a maximised window -- off the bottom of a 1080p
    // screen, so the operator could not see Sonicate at all.
    if (treatmentBodyRow_) {
        controlPanel->setParent(nullptr);
        treatmentBodyRow_->addWidget(controlPanel);
    } else {
        row->addWidget(controlPanel);
    }
    // No stretch: this row is one line of status and a button. Given a share of
    // the leftover height it centred them in a tall empty band at the foot of
    // the page. The Sonicate body above takes the slack instead.
    outer->addLayout(row);
    treatmentExecutionBody_->setVisible(false);

    setSerialConnectedLamp(false);

    // startStandaloneCountdown.m's own hardcoded 2 s tick period.
    sonicationCountdownTimer_ = new QTimer(this);
    sonicationCountdownTimer_->setInterval(2000);
    connect(sonicationCountdownTimer_, &QTimer::timeout, this, [this] {
        ++sonicationCountdownTicks_;
        const int left = beam::gui::countdownTimeLeftSeconds(sonicationCountdownSeconds_, sonicationCountdownTicks_);
        if (beam::gui::isCountdownDone(left)) {
            sonicationCountdownTimer_->stop();
            sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
            closeOpenLogRow();
            // Back to empty rather than sitting at 100%: the Completed column
            // now says the sonication finished, and a full bar left on screen
            // reads as one still in progress.
            treatmentExecutionProgressBar_->setValue(0);
            abortTreatmentButton_->setEnabled(false);
            startTreatmentButton_->setEnabled(true);
            shamButton_->setEnabled(true);
            finishTreatmentButton_->setEnabled(true);
            return;
        }
        sonicationCountdownLabel_->setText(QString::fromStdString(beam::gui::countdownDisplayText(left)));
        if (sonicationCountdownSeconds_ > 0) {
            const int done = sonicationCountdownSeconds_ - left;
            treatmentExecutionProgressBar_->setValue(
                std::clamp(done * 100 / sonicationCountdownSeconds_, 0, 100));
        }
    });

    connect(serialConnectButton_, &QPushButton::clicked, this, [this] {
        const QString port = serialPortCombo_->currentText();
        setSerialConnectedLamp(false);
        try {
            serialLink_ = std::make_unique<beam::serialcom::SerialPort>(port.toStdString());
            setSerialConnectedLamp(true);
            setExecutionStatus(QStringLiteral("Connected to %1.").arg(port));
        } catch (const std::exception& e) {
            serialLink_.reset();
            setExecutionStatus(
                QStringLiteral("Connection failed: %1").arg(QString::fromStdString(e.what())), true);
        }
    });
    connect(shamButton_, &QPushButton::clicked, this, [this] { runSonication(/*sham=*/true); });
    connect(startTreatmentButton_, &QPushButton::clicked, this, [this] { runSonication(/*sham=*/false); });
    connect(abortTreatmentButton_, &QPushButton::clicked, this, [this] { abortSonication(); });
    connect(finishTreatmentButton_, &QPushButton::clicked, this, [this] { finishTreatmentStage(); });
}

void WorkflowWindow::setSerialConnectedLamp(bool connected) {
    if (!serialConnectedLamp_) return;
    serialConnectedLamp_->setStyleSheet(
        connected ? QStringLiteral("QLabel { background: #3fbf6f; border: 1px solid #2c8a50; border-radius: 10px; }")
                  : QStringLiteral("QLabel { background: #7b8c93; border: 1px solid #55646a; border-radius: 10px; }"));
}

// generalSonicateMaster.m's decision path: build stimParams from the shown
// row, run the two last-moment checks, and only then send. The transmission
// figure is the one the Correction stage actually measured.
int WorkflowWindow::currentVisitNumber() const {

    bool ok = false;
    const int visit = visitNumberCombo_ ? visitNumberCombo_->currentText().toInt(&ok) : 0;
    return ok && visit > 0 ? visit : 1;
}

// One row per delivery, stamped as it starts. BeamV0 means to record this --
// setLogInfoToCSV.m parses a Parameters string and names SonicationStartDTM
// columns -- but nothing there ever writes either, so the values are taken
// here, from the row actually sent.
void WorkflowWindow::recordSonicationStart(const beam::gui_qt::StimParamRow& row, bool sham,
                                           std::optional<double> attenuation,
                                           std::optional<double> dutyCycle) {
    beam::gui::TreatmentLogRow entry;
    entry.visit = currentVisitNumber();
    entry.protocol = currentProtocolName().toStdString();
    const QListWidgetItem* item = targetListWidget_ ? targetListWidget_->currentItem() : nullptr;
    entry.target = item != nullptr ? targetListName(item).toStdString() : std::string();
    entry.xMm = row.x;
    entry.yMm = row.y;
    entry.zMm = row.z;
    entry.amplitude = row.amplitude;
    entry.startTimeSeconds = row.startTime;
    entry.endTimeSeconds = row.endTime;
    entry.burstDurationSeconds = row.bd;
    entry.burstIntervalSeconds = row.bi;
    entry.pulseDurationSeconds = row.pd;
    entry.pulseIntervalSeconds = row.pi;
    entry.sham = sham;
    entry.attenuation = attenuation;
    entry.dutyCycle = dutyCycle;
    entry.startedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    // BeamV0 writes into treatmentProtocolTable row i, where i is the Current
    // Sonication Number, so stepping back and re-firing replaces that row.
    entry.sequence = currentSonicationNumberEdit_ ? currentSonicationNumberEdit_->value() : 1;
    openLogSequence_ = entry.sequence;
    treatmentLog_.record(std::move(entry));
    // Persist now -- a row that is never closed still has to survive a crash
    // mid-delivery -- but do not rebuild the table here: that reloads the
    // sonication grid out from under the row currently being fired. The
    // table catches up when the countdown closes the row.
    saveTreatmentTargetStore();
}

// The countdown reaching zero, or an abort, ends the row. An abort still leaves
// a row: it was delivered, just not for as long as planned.
void WorkflowWindow::closeOpenLogRow() {
    if (openLogSequence_ <= 0) return;
    treatmentLog_.close(currentProtocolName().toStdString(), currentVisitNumber(), openLogSequence_,
                        QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
    openLogSequence_ = 0;
    saveTreatmentTargetStore();
    loadTreatmentProtocol(currentProtocolName());
    advanceToNextScheduledSonication();
}

// Where the visit has got to: one past the last delivery, which is BeamV0's
// own numbering -- sessions(visit).Data is the schedule in order and a row is
// filled in as it is delivered. Moves the schedule selection and the target
// with it, so the next Sonicate fires the next scheduled target rather than
// repeating the last one.
// One step along the schedule, which is BeamV0's own
// `CurrentSonicationNumberEditField.Value + 1` in SonicateButtonPushed. Taken
// from the field rather than from the log's highest sequence, so stepping back
// to re-fire a row and letting it finish advances to the row after THAT one.
void WorkflowWindow::advanceToNextScheduledSonication() {
    if (!currentSonicationNumberEdit_) return;
    selectScheduledSonication(currentSonicationNumberEdit_->value() + 1);
}

// Moves to one scheduled sonication: the number field, the highlighted row,
// and the target the grid is showing, which is what Sonicate sends.
void WorkflowWindow::selectScheduledSonication(int oneBased) {
    if (!protocolModel_ || !currentSonicationNumberEdit_) return;
    const int scheduled = static_cast<int>(protocolModel_->rows().size());
    if (scheduled == 0) return;
    const int position = std::clamp(oneBased, 1, scheduled);

    {
        const QSignalBlocker blocker(currentSonicationNumberEdit_);
        currentSonicationNumberEdit_->setRange(1, scheduled);
        currentSonicationNumberEdit_->setValue(position);
    }
    if (protocolView_) protocolView_->selectRow(position - 1);

    const QString target = protocolModel_->rows()[static_cast<std::size_t>(position) - 1].target;
    if (!targetListWidget_) return;
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        if (targetListName(targetListWidget_->item(i)) != target) continue;
        if (targetListWidget_->currentRow() != i) targetListWidget_->setCurrentRow(i);
        return;
    }
}

// Right-click a row to change what this VISIT runs, without touching the
// protocol: skip one that has not started, or add one after it. BeamV0 has no
// equivalent -- its table is fixed at 24 rows -- but a visit that has to drop
// or add a sonication otherwise has nowhere to record that it did.
void WorkflowWindow::showScheduleRowMenu(const QPoint& where) {
    if (!protocolView_ || !protocolModel_) return;
    const QModelIndex index = protocolView_->indexAt(where);
    if (!index.isValid()) return;
    const int sequence = index.row() + 1;
    const std::string protocol = currentProtocolName().toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row = treatmentLog_.find(protocol, visit, sequence);
    if (row == nullptr) return;

    QMenu menu(this);
    const bool fired = beam::gui::TreatmentLog::delivered(*row);
    QAction* insert = menu.addAction(QStringLiteral("Insert sonication after %1...").arg(sequence));
    QAction* skip = menu.addAction(QStringLiteral("Skip sonication %1").arg(sequence));
    insert->setEnabled(!targetStore_.targets().empty());
    skip->setEnabled(!fired);
    if (fired) skip->setToolTip(QStringLiteral("This sonication has already been delivered."));

    QAction* chosen = menu.exec(protocolView_->viewport()->mapToGlobal(where));
    if (chosen == nullptr) return;

    if (chosen == skip) {
        if (!treatmentLog_.removeAt(protocol, visit, sequence)) {
            showMessage(QStringLiteral("Sonication %1 cannot be skipped: it or one below it has "
                                       "already been delivered.")
                            .arg(sequence),
                        true);
            return;
        }
        showMessage(QStringLiteral("Sonication %1 skipped for this visit. The protocol is "
                                   "unchanged.")
                        .arg(sequence),
                    false);
    } else if (chosen == insert) {
        QStringList names;
        for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
            names << QString::fromStdString(target.name);
        bool accepted = false;
        const QString picked =
            QInputDialog::getItem(this, QStringLiteral("Insert sonication"),
                                  QStringLiteral("Target:"), names, 0, false, &accepted);
        if (!accepted || picked.isEmpty()) return;

        // The added sonication takes the target's own parameters, the same way
        // a seeded row does.
        const beam::gui::TreatmentTarget* target = targetStore_.findTarget(picked.toStdString());
        if (target == nullptr || target->points.empty()) return;
        const beam::gui::TreatmentTargetPoint& point = target->points.front();
        beam::gui::TreatmentLogRow added;
        added.target = target->name;
        added.amplitude = point.amplitude;
        added.startTimeSeconds = 0.0;
        added.endTimeSeconds = point.endTimeSeconds;
        added.burstDurationSeconds = point.burstDurationSeconds;
        added.burstIntervalSeconds = point.burstIntervalSeconds;
        added.pulseDurationSeconds = point.pulseDurationSeconds;
        added.pulseIntervalSeconds = point.pulseIntervalSeconds;
        if (!treatmentLog_.insertAfter(protocol, visit, sequence, std::move(added))) {
            showMessage(QStringLiteral("Nothing can be inserted here: a sonication below %1 has "
                                       "already been delivered.")
                            .arg(sequence),
                        true);
            return;
        }
        showMessage(QStringLiteral("%1 added after sonication %2 for this visit. The protocol is "
                                   "unchanged.")
                        .arg(picked)
                        .arg(sequence),
                    false);
    }
    // Reload first: saveTreatmentTargetStore writes the displayed rows back
    // into the log by position, and the table still holds the sequence from
    // before the insert. Saving first wrote the old row over the new one --
    // which is why an inserted SCC3 came back as the row above it.
    loadTreatmentProtocol(currentProtocolName());
    saveTreatmentTargetStore();
}

// Writes the visit log wherever the operator asks, on top of the copy kept
// beside the plan. setLogInfoToCSV.m does the same thing to its own
// participantLogFilesBeam folder after every sonication.
void WorkflowWindow::saveTreatmentLogAs() {
    const QString suggested =
        QDir(treatmentDataDirectory())
            .filePath(QStringLiteral("treatment_log_%1.csv")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"))));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save treatment log"),
                                                      suggested, QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    writeProtocolGridToStore();  // anything typed and not yet committed
    try {
        treatmentLog_.save(path.toStdString());
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Could not save the log: %1").arg(error.what()), true);
        return;
    }
    showMessage(QStringLiteral("Treatment log saved to %1.").arg(path), false);
}

void WorkflowWindow::runSonication(bool sham) {


    std::string reason;
    if (!workflow_.begin(beam::gui::WorkflowStage::Treatment, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    if (!stimParamModel_ || stimParamModel_->rows().empty()) {
        setExecutionStatus(QStringLiteral("No sonication is planned."), true);
        return;
    }
    const int shown = std::clamp(stimParamModel_->currentShownRow(), 1,
                                 static_cast<int>(stimParamModel_->rows().size()));
    const beam::gui_qt::StimParamRow& r = stimParamModel_->rows()[static_cast<std::size_t>(shown - 1)];
    const beam::safety::SonicationSafetyParams params =
        beam::gui::stimParamsFromTableRow(r.pd, r.pi, r.bd, r.bi, r.amplitude, r.startTime, r.endTime);

    if (sham) {
        const beam::gui::ShamSonicationOutcome outcome =
            beam::gui::prepareShamSonication(params, shamBurstSound(), kShamSoundFs);
        // A sham sends nothing, so there is no transmission or duty cycle.
        recordSonicationStart(r, true, std::nullopt, std::nullopt);
        startSonicationCountdown(static_cast<int>(outcome.durationSeconds));
        setExecutionStatus(
            QStringLiteral("Sham sonication %1: masking audio ready (%2 samples, %3 s). No ultrasound was sent.")
                .arg(shown)
                .arg(outcome.maskingAudio.size())
                .arg(outcome.durationSeconds, 0, 'f', 1));
        return;
    }

    const beam::gui::SonicateOutcome outcome = beam::gui::prepareSonication(
        params, transmissionAmplitude_, kCouplingThreshold, triggerModeCombo_->currentIndex() == 1);
    if (!outcome.started) {
        QString message = QStringLiteral("Not started: ");
        for (std::size_t i = 0; i < outcome.messages.size(); ++i) {
            if (i != 0) message += QStringLiteral("; ");
            message += QString::fromStdString(outcome.messages[i]);
        }
        setExecutionStatus(message, true);
        workflow_.block(beam::gui::WorkflowStage::Treatment, message.toStdString());
        refresh();
        return;
    }

    const beam::serialcom::SonicationTiming timing{r.pd, r.pi, r.bd, r.bi, r.startTime, r.endTime};
    const std::string command = beam::serialcom::setSerialCommandFromStimParams(timing, outcome.dutyCycle);
    QString status = QStringLiteral("Sonication %1: duty cycle %2%3, %4 s. Command: %5")
                         .arg(shown)
                         .arg(outcome.dutyCycle, 0, 'f', 3)
                         .arg(outcome.waitForTrigger ? QStringLiteral(" (waiting for external trigger)") : QString())
                         .arg(outcome.durationSeconds, 0, 'f', 1)
                         .arg(QString::fromStdString(command));
    if (!serialLink_) {
        status += QStringLiteral(" | No serial connection -- command not sent.");
    } else {
        try {
            const bool sent = beam::serialcom::sendSerialCommand(*serialLink_, command);
            const std::string reply = serialLink_->readLine();
            beam::serialcom::clearSerial(*serialLink_);
            status += sent ? QStringLiteral(" | Sent, reply: \"%1\"").arg(QString::fromStdString(reply))
                           : QStringLiteral(" | Link closed before send.");
        } catch (const std::exception& e) {
            status += QStringLiteral(" | Send failed: %1").arg(QString::fromStdString(e.what()));
        }
    }
    setExecutionStatus(status);
    recordSonicationStart(r, false, transmissionAmplitude_, outcome.dutyCycle);
    startSonicationCountdown(static_cast<int>(outcome.durationSeconds));
}

// The status panel earns its space only when there is something to report, so
// it stays out of the page until a sonication says something and goes again
// when the text is cleared.
// The page had two: this one and the workflow banner, saying the same thing
// after a failed sonication. The banner is every other stage's channel, so it
// is the one kept.
void WorkflowWindow::setExecutionStatus(const QString& text, bool isError) {
    if (text.trimmed().isEmpty()) return;
    showMessage(text, isError);
}

void WorkflowWindow::startSonicationCountdown(int durationSeconds) {
    sonicationCountdownSeconds_ = std::max(durationSeconds, 0);
    sonicationCountdownTicks_ = 0;
    treatmentExecutionProgressBar_->setValue(0);
    startTreatmentButton_->setEnabled(false);
    shamButton_->setEnabled(false);
    abortTreatmentButton_->setEnabled(true);
    sonicationCountdownLabel_->setText(
        QString::fromStdString(beam::gui::countdownDisplayText(sonicationCountdownSeconds_)));
    sonicationCountdownTimer_->start();
}

// Port of the Abort Sonication action: the source sends the same
// "Correction" command the correction measurement uses, which stops output.
void WorkflowWindow::abortSonication() {
    sonicationCountdownTimer_->stop();
    sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
    closeOpenLogRow();
    abortTreatmentButton_->setEnabled(false);
    startTreatmentButton_->setEnabled(true);
    shamButton_->setEnabled(true);
    QString status = QStringLiteral("Sonication aborted by operator.");
    if (serialLink_) {
        try {
            beam::serialcom::sendSerialCommand(*serialLink_, "Correction");
            status += QStringLiteral(" Abort command sent.");
        } catch (const std::exception& e) {
            status += QStringLiteral(" Abort command failed: %1").arg(QString::fromStdString(e.what()));
        }
    } else {
        status += QStringLiteral(" No serial connection -- no command sent.");
    }
    workflow_.block(beam::gui::WorkflowStage::Treatment, status.toStdString());
    setExecutionStatus(status);
    showMessage(status, true);
    refresh();
}

void WorkflowWindow::finishTreatmentStage() {
    std::string reason;
    if (workflow_.state(beam::gui::WorkflowStage::Treatment).status == beam::gui::WorkflowStatus::Complete) return;
    if (!workflow_.complete(beam::gui::WorkflowStage::Treatment, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    showMessage(QStringLiteral("Sonication complete. Continuing to Report."), false);
    refresh();
    ui_->stageList->setCurrentRow(static_cast<int>(workflow_.nextStage()));
}

void WorkflowWindow::openTreatmentLibrary(TreatmentLibraryDialog::Page page) {
    // The dialog edits a copy, and applying it replaces the store wholesale, so
    // anything still only in the grid has to reach the store first or it is lost.
    writeProtocolGridToStore();
    writeTargetPointsToStore();
    TreatmentLibraryDialog dialog(targetStore_, page, this);
    // Applied on each Save rather than once at the end, so the dialog can stay
    // open for the next edit.
    dialog.setSaveHandler([this](const beam::gui::TreatmentTargetStore& edited) {
        applyTreatmentLibrary(edited);
    });
    dialog.exec();
}

void WorkflowWindow::applyTreatmentLibrary(const beam::gui::TreatmentTargetStore& edited) {
    const QString protocol = treatmentProtocolCombo_->currentText();
    targetStore_ = edited;

    targetListWidget_->clear();
    targetSonications_.clear();
    currentTargetId_ = -1;
    for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
        addTargetListRow(QString::fromStdString(target.name));
    if (targetListWidget_->count() > 0) targetListWidget_->setCurrentRow(0);

    const bool kept = targetStore_.findProtocol(protocol.toStdString()) != nullptr;
    refreshProtocolCombo(kept ? protocol : QString());
    if (!treatmentProtocolCombo_->currentText().isEmpty())
        loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    saveTreatmentTargetStore();
}


// X/Y/Z stay out: the store holds no placement.
void WorkflowWindow::writeTargetPointsToStore() {
    if (targetListWidget_ == nullptr) return;
    saveCurrentTargetSonications();
    for (int row = 0; row < targetListWidget_->count(); ++row) {
        const QListWidgetItem* item = targetListWidget_->item(row);
        const auto cached = targetSonications_.constFind(targetListId(item));
        if (cached == targetSonications_.constEnd() || cached.value().empty()) continue;
        std::vector<beam::gui::TreatmentTargetPoint> points;
        points.reserve(cached.value().size());
        for (const beam::gui_qt::StimParamRow& grid : cached.value()) {
            beam::gui::TreatmentTargetPoint point;
            point.order = grid.order;
            point.show = grid.show;
            point.amplitude = grid.amplitude;
            point.startTimeSeconds = grid.startTime;
            point.endTimeSeconds = grid.endTime;
            point.burstDurationSeconds = grid.bd;
            point.burstIntervalSeconds = grid.bi;
            point.pulseDurationSeconds = grid.pd;
            point.pulseIntervalSeconds = grid.pi;
            points.push_back(point);
        }
        // Straight onto the target. A protocol carries no parameters, so this
        // is visible from every protocol that schedules it.
        targetStore_.setTargetPoints(targetListName(item).toStdString(), std::move(points));
    }
}

}  // namespace beam::app
