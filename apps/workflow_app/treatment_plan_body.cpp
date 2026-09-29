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

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <QComboBox>
#include <QCoreApplication>
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
// works from. beam_app's Sonicate tab pre-populates the Target List with
// exactly these, and so does this one -- the list is a list of targets, not of
// the sonications that visit them.
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

QString targetListName(const QListWidgetItem* item) {
    return item ? item->data(kTargetNameRole).toString() : QString();
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

    const auto addIcon = [&](QChar glyph, const QString& tip) {
        auto* button = new QToolButton(row);
        button->setText(QString(glyph));
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QString::fromLatin1(kRowIconStyle));
        // Pressing an icon also selects its row, so the icon and the grid
        // below always refer to the same target.
        button->installEventFilter(this);
        rowLayout->addWidget(button);
        return button;
    };
    QToolButton* renameButton = addIcon(QChar(0x270E), QStringLiteral("Rename this target"));
    QToolButton* createButton = addIcon(QChar(0x002B), QStringLiteral("Create a new target below this one"));
    QToolButton* deleteButton = addIcon(QChar(0x2715), QStringLiteral("Delete this target"));

    item->setSizeHint(row->sizeHint());
    targetListWidget_->setItemWidget(item, row);

    connect(renameButton, &QToolButton::clicked, this, [this, item, nameLabel] {
        bool ok = false;
        const QString renamed = QInputDialog::getText(this, QStringLiteral("Rename target"),
                                                       QStringLiteral("Name:"), QLineEdit::Normal,
                                                       targetListName(item), &ok);
        if (!ok || renamed.isEmpty()) return;
        item->setData(kTargetNameRole, renamed);
        nameLabel->setText(renamed);
        updateTreatmentPlanSummary();
    });
    connect(createButton, &QToolButton::clicked, this, [this, item] {
        std::vector<std::string> existing;
        for (int i = 0; i < targetListWidget_->count(); ++i)
            existing.push_back(targetListName(targetListWidget_->item(i)).toStdString());
        const int insertAt = targetListWidget_->row(item) + 1;
        addTargetListRow(QString::fromStdString(beam::gui::newProtocolName(existing)), insertAt);
        targetListWidget_->setCurrentRow(insertAt);
    });
    connect(deleteButton, &QToolButton::clicked, this, [this, item] {
        if (targetListWidget_->count() <= 1) {
            showMessage(QStringLiteral("The last target cannot be deleted."), true);
            return;
        }
        // Deferred: removing the item destroys the row widget this button
        // lives in, which must not happen while its own signal is emitting.
        QTimer::singleShot(0, this, [this, item] {
            const int row = targetListWidget_->row(item);
            if (row >= 0) delete targetListWidget_->takeItem(row);
        });
    });
}

void WorkflowWindow::buildTreatmentPlanBody(const QString& acceptButtonStyle) {
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
    targetListWidget_ = new QListWidget(targetPanel);
    applyItemViewPalette(targetListWidget_);
    // Each row carries its own rename / create / delete icons, so beam_app's
    // separate Rename, Create New Protocol and Remove Selected Protocol
    // buttons are gone -- they acted on the selected row, which is what the
    // per-row icons say explicitly.
    for (const char* name : kTargetNames) addTargetListRow(QString::fromLatin1(name));
    targetListWidget_->setCurrentRow(0);
    // Bounded, so the button below sits directly under the list instead of at
    // the foot of a panel as tall as the whole (scrolling) body, where it was
    // off-screen. Sized to show most of the twelve targets at once while
    // leaving the button inside the body's own height; the list scrolls past it.
    targetListWidget_->setFixedHeight(430);
    targetPanelLayout->addWidget(targetListWidget_);
    moveToTargetButton_ = new QPushButton(QStringLiteral("Move to Selected Target"), targetPanel);
    // Unlike beam_app's, this one is connected: the MRI views give each
    // target a real position to move to.
    moveToTargetButton_->setToolTip(
        QStringLiteral("Centre all three MRI planes on the selected target's placed position"));
    targetPanelLayout->addWidget(moveToTargetButton_);
    targetPanelLayout->addStretch(1);
    moveToTargetButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    bodyLayout->addWidget(targetPanel);

    // ---- app.stimParamTable and its button row --------------------------
    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(6);
    sortByOrderButton_ = new QPushButton(QStringLiteral("Sort by #"), bodyContent);
    addSonicationButton_ = new QPushButton(QStringLiteral("Add Sonication"), bodyContent);
    removeSonicationButton_ = new QPushButton(QStringLiteral("Remove Marked"), bodyContent);
    removeSonicationButton_->setToolTip(
        QStringLiteral("Removes every sonication whose Show box is ticked. Row 1 is never removed -- "
                       "BeamV0's own removeSonicationUpdateTable.m clears its flag first."));
    for (QPushButton* button : {sortByOrderButton_, addSonicationButton_, removeSonicationButton_})
        button->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    auto* gridButtonRow = new QHBoxLayout;
    gridButtonRow->addWidget(sortByOrderButton_);
    gridButtonRow->addWidget(addSonicationButton_);
    gridButtonRow->addWidget(removeSonicationButton_);
    gridButtonRow->addStretch(1);
    rightColumn->addLayout(gridButtonRow);

    stimParamModel_ = new beam::gui_qt::StimParamTableModel(this);
    stimParamView_ = new QTableView(bodyContent);
    stimParamView_->setModel(stimParamModel_);
    configureModelTable(stimParamView_);
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

    // Treatment Protocol tab.
    auto* protocolTab = new QWidget(treatmentTabs_);
    auto* protocolLayout = new QVBoxLayout(protocolTab);
    auto* protocolSelectorRow = new QHBoxLayout;
    treatmentProtocolLabel_ = new QLabel(QStringLiteral("Treatment protocol:"), protocolTab);
    treatmentProtocolCombo_ = new QComboBox(protocolTab);
    treatmentProtocolCombo_->addItems({QStringLiteral("Default"), QStringLiteral("Addiction"),
                                       QStringLiteral("PTSD"), QStringLiteral("PainACC"),
                                       QStringLiteral("PainSCCandAMCC"), QStringLiteral("PainAMCCandSCC")});
    visitNumberCombo_ = new QComboBox(protocolTab);
    visitNumberCombo_->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")});
    newVisitButton_ = new QPushButton(QStringLiteral("New Visit"), protocolTab);
    newVisitButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    auto* visitLabel = new QLabel(QStringLiteral("Visit:"), protocolTab);
    for (QComboBox* combo : {treatmentProtocolCombo_, visitNumberCombo_}) applyItemViewPalette(combo);
    protocolSelectorRow->addWidget(treatmentProtocolLabel_);
    protocolSelectorRow->addWidget(treatmentProtocolCombo_);
    protocolSelectorRow->addWidget(visitLabel);
    protocolSelectorRow->addWidget(visitNumberCombo_);
    protocolSelectorRow->addWidget(newVisitButton_);
    protocolSelectorRow->addStretch(1);
    protocolLayout->addLayout(protocolSelectorRow);

    // HydrogelSizeButtonGroup + CurrentSonicationNumberEditField. Three
    // sibling radios in one parent are naturally exclusive, so no
    // QButtonGroup is needed -- same as beam_app's port.
    auto* hydrogelRow = new QHBoxLayout;
    auto* hydrogelLabel = new QLabel(QStringLiteral("Hydrogel Size:"), protocolTab);
    hydrogelSmallRadio_ = new QRadioButton(QStringLiteral("Small"), protocolTab);
    hydrogelMediumRadio_ = new QRadioButton(QStringLiteral("Medium"), protocolTab);
    hydrogelLargeRadio_ = new QRadioButton(QStringLiteral("Large"), protocolTab);
    hydrogelSmallRadio_->setChecked(true);  // HydrogelSizeButtonGroup's own default
    for (QRadioButton* radio : {hydrogelSmallRadio_, hydrogelMediumRadio_, hydrogelLargeRadio_})
        radio->setStyleSheet(QStringLiteral("QRadioButton { color: #dcebef; padding: 2px 6px; }"));
    auto* sonicationNumberLabel = new QLabel(QStringLiteral("Current Sonication Number:"), protocolTab);
    currentSonicationNumberEdit_ = new QSpinBox(protocolTab);
    currentSonicationNumberEdit_->setRange(1, 999);
    currentSonicationNumberEdit_->setMaximumWidth(70);
    applyItemViewPalette(currentSonicationNumberEdit_);
    for (QLabel* label : {treatmentProtocolLabel_, visitLabel, hydrogelLabel, sonicationNumberLabel})
        label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    hydrogelRow->addWidget(hydrogelLabel);
    hydrogelRow->addWidget(hydrogelSmallRadio_);
    hydrogelRow->addWidget(hydrogelMediumRadio_);
    hydrogelRow->addWidget(hydrogelLargeRadio_);
    hydrogelRow->addSpacing(18);
    hydrogelRow->addWidget(sonicationNumberLabel);
    hydrogelRow->addWidget(currentSonicationNumberEdit_);
    hydrogelRow->addStretch(1);
    protocolLayout->addLayout(hydrogelRow);

    protocolModel_ = new beam::gui_qt::TreatmentProtocolTableModel(this);
    protocolView_ = new QTableView(protocolTab);
    protocolView_->setModel(protocolModel_);
    configureModelTable(protocolView_);
    protocolLayout->addWidget(protocolView_, 1);
    treatmentTabs_->addTab(protocolTab, QStringLiteral("Treatment Protocol"));

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

    // beam_app's own statusRow, in the same place: full width under the tabs.
    treatmentPlanTargetLabel_ = new QLabel(QStringLiteral("Selected sonication: none"), bodyContent);
    treatmentPlanTargetLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 8px; }"));
    treatmentPlanTargetLabel_->setWordWrap(true);
    rightColumn->addWidget(treatmentPlanTargetLabel_);

    acceptTreatmentPlanButton_ =
        new QPushButton(QStringLiteral("Accept treatment plan and continue →"), bodyContent);
    acceptTreatmentPlanButton_->setMinimumHeight(40);
    acceptTreatmentPlanButton_->setStyleSheet(acceptButtonStyle);
    rightColumn->addWidget(acceptTreatmentPlanButton_);

    bodyLayout->addLayout(rightColumn, 1);
    treatmentPlanBody_->setVisible(false);

    wireTreatmentPlanBody();
}

void WorkflowWindow::wireTreatmentPlanBody() {
    // ---- the sonication grid -------------------------------------------
    connect(sortByOrderButton_, &QPushButton::clicked, this, [this] {
        stimParamModel_->sortByOrderColumn();
        updateSonicationPlots();
    });
    connect(addSonicationButton_, &QPushButton::clicked, this, [this] {
        if (stimParamModel_->rows().empty()) {
            // addSonicationRow duplicates the last row, so seed the first.
            stimParamModel_->setRows({beam::gui_qt::StimParamRow{}});
        } else {
            stimParamModel_->addSonicationRow();
        }
        selectSonicationRow(static_cast<int>(stimParamModel_->rows().size()) - 1);
    });
    connect(removeSonicationButton_, &QPushButton::clicked, this, [this] {
        stimParamModel_->removeMarkedRows();
        selectSonicationRow(0);
    });
    connect(stimParamModel_, &beam::gui_qt::StimParamTableModel::showFlagsChanged, this,
            [this] { updateSonicationPlots(); });
    connect(stimParamModel_, &QAbstractItemModel::dataChanged, this,
            [this] { updateSonicationPlots(); updateTreatmentPlanSummary(); });
    connect(stimParamView_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) updateTreatmentPlanSummary();
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
        const QList<QVariant> ras = item->data(Qt::UserRole).toList();
        if (ras.size() != 3) return;
        focusTreatmentViewsOn(Eigen::Vector3d(ras.at(0).toDouble(), ras.at(1).toDouble(), ras.at(2).toDouble()));
    });
    // The grid row belongs to whichever target is selected, so switching
    // target loads that target's placed coordinates into X/Y/Z (zeros if it
    // has never been placed) and re-centres the planes on it.
    connect(targetListWidget_, &QListWidget::currentRowChanged, this, [this](int) {
        loadSelectedTargetPosition();
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
    connect(newVisitButton_, &QPushButton::clicked, this, [this] {
        const int next = visitNumberCombo_->count() + 1;
        visitNumberCombo_->addItem(QString::number(next));
        visitNumberCombo_->setCurrentIndex(visitNumberCombo_->count() - 1);
        showMessage(QStringLiteral("Visit %1 started. The protocol table was reset for the new visit.").arg(next),
                    false);
        loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    });
    connect(computeBestTargetsButton_, &QPushButton::clicked, this, [this] { updateBestTargets(); });
    connect(accFlagCombo_, &QComboBox::currentTextChanged, this, [this] {
        updateBestTargets();
        updateExampleTargetImage();
    });
    connect(protocolModel_, &QAbstractItemModel::dataChanged, this, [this] { updateBestTargets(); });

    loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
    updateExampleTargetImage();
}

// Port of BeamV0's per-protocol CSVs, unchanged from this application's
// earlier hand-built version except that the rows now land in the real
// TreatmentProtocolTableModel instead of a QTableWidget of strings.
void WorkflowWindow::loadTreatmentProtocol(const QString& protocolName) {
    const auto parseCsvLine = [](const QString& line) {
        QStringList fields;
        QString field;
        bool quoted = false;
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QLatin1Char('"')) {
                if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = !quoted;
                }
            } else if (ch == QLatin1Char(',') && !quoted) {
                fields.push_back(field.trimmed());
                field.clear();
            } else {
                field += ch;
            }
        }
        fields.push_back(field.trimmed());
        return fields;
    };

    const QString relative =
        QStringLiteral("BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/TreatmentProtocols/") + protocolName +
        QStringLiteral(".csv");
    const QStringList candidates = {
        QDir::current().filePath(relative),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../../") + relative),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../") + relative)};
    QString selectedPath;
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            selectedPath = candidate;
            break;
        }
    }

    QList<QStringList> sourceRows;
    if (!selectedPath.isEmpty()) {
        QFile file(selectedPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            while (!stream.atEnd()) {
                const QString line = stream.readLine();
                if (line.trimmed().isEmpty()) continue;
                const QStringList fields = parseCsvLine(line);
                if (!fields.isEmpty() && fields.first().compare(QStringLiteral("Number"), Qt::CaseInsensitive) != 0)
                    sourceRows.push_back(fields);
            }
        }
    }
    // Keep offline/release builds usable when the BeamV0 checkout is not
    // beside the executable. These defaults mirror its table schema.
    if (sourceRows.isEmpty()) {
        const int rows = protocolName == QStringLiteral("Default")
                             ? 1
                             : ((protocolName == QStringLiteral("PainACC") ||
                                 protocolName.contains(QStringLiteral("SCCandAMCC")) ||
                                 protocolName.contains(QStringLiteral("AMCCandSCC")))
                                    ? 12
                                    : 6);
        const bool amccFirst = protocolName.contains(QStringLiteral("AMCCandSCC"));
        for (int row = 0; row < rows; ++row) {
            const bool amcc = rows > 1 && ((row % 2 == 0) == amccFirst);
            sourceRows.push_back({QString::number(row + 1),
                                  QStringLiteral("%1%2").arg(amcc ? QStringLiteral("aMCC") : QStringLiteral("SCC"))
                                      .arg(row / 2 + 1),
                                  QStringLiteral("30"), QStringLiteral("0.75"),
                                  QStringLiteral("X:0,Y:0,Z:0,0.03,0.7,0.005,0.010"), QStringLiteral("N"),
                                  QStringLiteral("notes")});
        }
    }

    std::vector<beam::gui_qt::TreatmentProtocolRow> protocolRows;
    QStringList targetNames;
    protocolRows.reserve(static_cast<std::size_t>(sourceRows.size()));
    for (int row = 0; row < sourceRows.size(); ++row) {
        const QStringList source = sourceRows.at(row);
        const QString responseField = source.value(5);
        const double response =
            responseField.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0 ? 0.0 : responseField.toDouble();

        beam::gui_qt::TreatmentProtocolRow protocolRow;
        protocolRow.number = source.value(0).toInt();
        protocolRow.target = source.value(1);
        protocolRow.duration = source.value(2).toDouble();
        protocolRow.amplitude = source.value(3).toDouble();
        protocolRow.parameters = source.value(4);
        protocolRow.responsePain = response;
        protocolRow.responseMood = response;
        protocolRow.notes = source.value(6);
        protocolRows.push_back(protocolRow);


        if (!targetNames.contains(protocolRow.target)) targetNames.push_back(protocolRow.target);
    }

    // The sonication grid describes the sonication being placed right now --
    // one row, the selected target's. The protocol's 24-odd entries are the
    // visit's schedule and belong to the Treatment Protocol tab, not here;
    // seeding one grid row per protocol entry (as this first did) filled the
    // grid with rows that had no target and could never get one.
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

    // The Target List keeps BeamV0's own fixed names; a protocol that names
    // a target this build does not know about is appended rather than
    // replacing the list, so the operator can still place it.
    for (const QString& name : targetNames) {
        bool known = false;
        for (int i = 0; i < targetListWidget_->count() && !known; ++i)
            known = targetListName(targetListWidget_->item(i)) == name;
        if (!known) addTargetListRow(name);
    }
    currentSonicationNumberEdit_->setRange(1, std::max<int>(1, static_cast<int>(stimParamModel_->rows().size())));

    selectSonicationRow(0);
    loadSelectedTargetPosition();
    updateBestTargets();
}

void WorkflowWindow::selectSonicationRow(int row) {
    if (!stimParamView_ || stimParamModel_->rows().empty()) return;
    const int clamped = std::clamp(row, 0, static_cast<int>(stimParamModel_->rows().size()) - 1);
    stimParamView_->selectRow(clamped);
    currentSonicationNumberEdit_->setValue(clamped + 1);
    updateSonicationPlots();
    updateTreatmentPlanSummary();
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
void WorkflowWindow::loadSelectedTargetPosition() {
    if (!stimParamModel_ || stimParamModel_->rows().empty()) return;
    const QListWidgetItem* item = targetListWidget_->currentItem();
    // A target the operator has not placed yet sits at the array's own centre,
    // not at the origin. That is what BeamV0 shows: drawROIs.m computes
    // `centerArrayMM = mean(arrayData.arrayTotal.rect(17:19,:),2)*1000` and
    // assigns it as every shown target's position, and Beam's C++ port carries
    // the same value through setArrayCenterMm/refreshTargetCrosshairs.
    // targetMm_ is this application's copy of that centre.
    Eigen::Vector3d placed = targetMm_;
    if (item && item->data(Qt::UserRole).isValid()) {
        const QList<QVariant> ras = item->data(Qt::UserRole).toList();
        if (ras.size() == 3)
            placed = Eigen::Vector3d(ras.at(0).toDouble(), ras.at(1).toDouble(), ras.at(2).toDouble());
    }
    const int row = std::max(selectedSonicationRow(), 0);
    std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
    rows[static_cast<std::size_t>(row)].x = placed.x();
    rows[static_cast<std::size_t>(row)].y = placed.y();
    rows[static_cast<std::size_t>(row)].z = placed.z();
    stimParamModel_->setRows(std::move(rows));
    stimParamView_->selectRow(row);
    updateTreatmentPlanSummary();
}

// Writes an MRI click into the selected sonication. This is the piece
// BeamV0 has no equivalent for: its Sonicate tab has no MRI view, so X/Y/Z
// can only be typed into the grid.
void WorkflowWindow::setSonicationTargetFromMri(const Eigen::Vector3d& positionMm) {
    const int row = selectedSonicationRow();
    if (row < 0) return;
    std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
    rows[static_cast<std::size_t>(row)].x = positionMm.x();
    rows[static_cast<std::size_t>(row)].y = positionMm.y();
    rows[static_cast<std::size_t>(row)].z = positionMm.z();
    stimParamModel_->setRows(std::move(rows));
    stimParamView_->selectRow(row);

    // The placed position also belongs to whichever target is selected, so
    // Move To Target can come back to it later.
    if (QListWidgetItem* item = targetListWidget_->currentItem())
        item->setData(Qt::UserRole, QList<QVariant>{positionMm.x(), positionMm.y(), positionMm.z()});
    updateTreatmentPlanSummary();
}

void WorkflowWindow::updateTreatmentPlanSummary() {
    if (!treatmentPlanTargetLabel_) return;
    const int row = selectedSonicationRow();
    if (row < 0) {
        treatmentPlanTargetLabel_->setText(QStringLiteral("Selected sonication: none"));
        return;
    }
    const beam::gui_qt::StimParamRow& r = stimParamModel_->rows()[static_cast<std::size_t>(row)];
    const QString targetName = targetListWidget_->currentItem() ? targetListName(targetListWidget_->currentItem())
                                                                : QStringLiteral("no target selected");
    treatmentPlanTargetLabel_->setText(
        QStringLiteral("Sonication %1 (%2) — RAS LR %3, AP %4, IS %5 mm · %6 MPa · %7–%8 s · "
                       "BD %9 s, BI %10 s, PD %11 s, PI %12 s")
            .arg(row + 1)
            .arg(targetName)
            .arg(r.x, 0, 'f', 2)
            .arg(r.y, 0, 'f', 2)
            .arg(r.z, 0, 'f', 2)
            .arg(r.amplitude, 0, 'f', 2)
            .arg(r.startTime, 0, 'f', 1)
            .arg(r.endTime, 0, 'f', 1)
            .arg(r.bd, 0, 'f', 3)
            .arg(r.bi, 0, 'f', 3)
            .arg(r.pd, 0, 'f', 4)
            .arg(r.pi, 0, 'f', 4));
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
    auto* outer = new QVBoxLayout(treatmentExecutionBody_);
    outer->setContentsMargins(0, 0, 0, 0);

    treatmentExecutionTitleLabel_ = new QLabel(QStringLiteral("Treatment execution"), treatmentExecutionBody_);
    treatmentExecutionTitleLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: #f2f7f8; font-size: 20px; font-weight: 700; }"));
    treatmentExecutionDescriptionLabel_ = new QLabel(
        QStringLiteral("Fire the accepted sonication plan. Each sonication runs BeamV0's own last-moment checks "
                       "(coupling transmission and computed duty cycle) before any command is sent."),
        treatmentExecutionBody_);
    treatmentExecutionDescriptionLabel_->setWordWrap(true);
    treatmentExecutionDescriptionLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: #c8d9de; padding: 4px 0 10px 0; }"));
    outer->addWidget(treatmentExecutionTitleLabel_);
    outer->addWidget(treatmentExecutionDescriptionLabel_);

    auto* row = new QHBoxLayout;

    // Left: what is about to be fired, and how it is going.
    auto* statusColumn = new QVBoxLayout;
    treatmentExecutionStatusLabel_ = new QLabel(QStringLiteral("Treatment has not started."),
                                                 treatmentExecutionBody_);
    treatmentExecutionStatusLabel_->setWordWrap(true);
    treatmentExecutionStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #dcebef; background: #183944; border: 1px solid #2b6172; border-radius: 5px; padding: 12px; }"));
    treatmentExecutionProgressBar_ = new QProgressBar(treatmentExecutionBody_);
    treatmentExecutionProgressBar_->setRange(0, 100);
    treatmentExecutionProgressBar_->setValue(0);
    treatmentExecutionProgressBar_->setFormat(QStringLiteral("Treatment progress: %p%"));
    treatmentExecutionProgressBar_->setMinimumHeight(24);
    sonicationCountdownLabel_ = new QLabel(QStringLiteral("No sonication running."), treatmentExecutionBody_);
    sonicationCountdownLabel_->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    statusColumn->addWidget(treatmentExecutionStatusLabel_, 1);
    statusColumn->addWidget(treatmentExecutionProgressBar_);
    statusColumn->addWidget(sonicationCountdownLabel_);
    row->addLayout(statusColumn, 1);

    // Right: the .mlapp's own narrow control column.
    auto* controlColumn = new QVBoxLayout;
    auto* controlPanel = new QWidget(treatmentExecutionBody_);
    controlPanel->setMaximumWidth(220);
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
    controlColumn->addSpacing(10);
    controlColumn->addWidget(shamButton_);
    controlColumn->addWidget(startTreatmentButton_);
    controlColumn->addWidget(abortTreatmentButton_);
    controlColumn->addStretch(1);
    controlColumn->addWidget(finishTreatmentButton_);
    row->addWidget(controlPanel);
    outer->addLayout(row, 1);
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
            treatmentExecutionProgressBar_->setValue(100);
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
            treatmentExecutionStatusLabel_->setText(QStringLiteral("Connected to %1.").arg(port));
        } catch (const std::exception& e) {
            serialLink_.reset();
            treatmentExecutionStatusLabel_->setText(
                QStringLiteral("Connection failed: %1").arg(QString::fromStdString(e.what())));
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
void WorkflowWindow::runSonication(bool sham) {
    std::string reason;
    if (!workflow_.begin(beam::gui::WorkflowStage::Treatment, &reason)) {
        showMessage(QString::fromStdString(reason), true);
        return;
    }
    if (!stimParamModel_ || stimParamModel_->rows().empty()) {
        treatmentExecutionStatusLabel_->setText(QStringLiteral("No sonication is planned."));
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
        startSonicationCountdown(static_cast<int>(outcome.durationSeconds));
        treatmentExecutionStatusLabel_->setText(
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
        treatmentExecutionStatusLabel_->setText(message);
        showMessage(message, true);
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
    treatmentExecutionStatusLabel_->setText(status);
    startSonicationCountdown(static_cast<int>(outcome.durationSeconds));
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
    treatmentExecutionStatusLabel_->setText(status);
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

}  // namespace beam::app
