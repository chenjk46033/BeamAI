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
    const auto makeProtocolButton = [&](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text, protocolTab);
        button->setToolTip(tip);
        button->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    };
    newProtocolButton_ = makeProtocolButton(QStringLiteral("New"), QStringLiteral("Create a new, empty protocol"));
    renameProtocolButton_ = makeProtocolButton(QStringLiteral("Rename"), QStringLiteral("Rename the selected protocol"));
    deleteProtocolButton_ = makeProtocolButton(QStringLiteral("Delete"), QStringLiteral("Delete the selected protocol"));
    saveProtocolsButton_ = makeProtocolButton(QStringLiteral("Save protocols"),
                                               QStringLiteral("Write protocols and target parameters to your own copy"));
    protocolSelectorRow->addWidget(treatmentProtocolLabel_);
    protocolSelectorRow->addWidget(treatmentProtocolCombo_);
    protocolSelectorRow->addWidget(newProtocolButton_);
    protocolSelectorRow->addWidget(renameProtocolButton_);
    protocolSelectorRow->addWidget(deleteProtocolButton_);
    protocolSelectorRow->addWidget(saveProtocolsButton_);
    protocolSelectorRow->addSpacing(18);
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

    auto* protocolEntryRow = new QHBoxLayout;
    addProtocolEntryButton_ = makeProtocolButton(
        QStringLiteral("Add Entry"), QStringLiteral("Append a sonication of the selected Target List target"));
    removeProtocolEntryButton_ = makeProtocolButton(
        QStringLiteral("Remove Selected Rows"), QStringLiteral("Delete the selected rows from this protocol"));
    protocolEntryRow->addWidget(addProtocolEntryButton_);
    protocolEntryRow->addWidget(removeProtocolEntryButton_);
    protocolEntryRow->addStretch(1);
    protocolLayout->addLayout(protocolEntryRow);
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
    connect(stimParamModel_, &QAbstractItemModel::dataChanged, this, [this] {
        saveCurrentTargetSonications();
        updateSonicationPlots();
        updateTreatmentPlanSummary();
    });
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

    connect(newProtocolButton_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New protocol"),
                                                    QStringLiteral("Name:"), QLineEdit::Normal,
                                                    QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        writeProtocolGridToStore();
        if (!targetStore_.addProtocol(name.trimmed().toStdString())) {
            showMessage(QStringLiteral("A protocol named \"%1\" already exists.").arg(name.trimmed()), true);
            return;
        }
        setTargetStoreDirty(true);
        refreshProtocolCombo(name.trimmed());
        loadTreatmentProtocol(name.trimmed());
        showMessage(QStringLiteral("Protocol \"%1\" created. Use Add Entry to schedule targets.")
                        .arg(name.trimmed()), false);
    });

    connect(renameProtocolButton_, &QPushButton::clicked, this, [this] {
        const QString current = treatmentProtocolCombo_->currentText();
        if (current.isEmpty()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename protocol"),
                                                    QStringLiteral("Name:"), QLineEdit::Normal,
                                                    current, &ok);
        if (!ok || name.trimmed().isEmpty() || name.trimmed() == current) return;
        writeProtocolGridToStore();
        if (!targetStore_.renameProtocol(current.toStdString(), name.trimmed().toStdString())) {
            showMessage(QStringLiteral("Cannot rename to \"%1\"; that name is taken.").arg(name.trimmed()), true);
            return;
        }
        setTargetStoreDirty(true);
        refreshProtocolCombo(name.trimmed());
        loadTreatmentProtocol(name.trimmed());
    });

    connect(deleteProtocolButton_, &QPushButton::clicked, this, [this] {
        const QString current = treatmentProtocolCombo_->currentText();
        if (current.isEmpty()) return;
        if (targetStore_.protocols().size() <= 1) {
            showMessage(QStringLiteral("The last protocol cannot be deleted."), true);
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete protocol"),
                                  QStringLiteral("Delete protocol \"%1\"? Its schedule is lost.")
                                      .arg(current)) != QMessageBox::Yes)
            return;
        targetStore_.removeProtocol(current.toStdString());
        setTargetStoreDirty(true);
        refreshProtocolCombo(QString());
        loadTreatmentProtocol(treatmentProtocolCombo_->currentText());
        showMessage(QStringLiteral("Protocol \"%1\" deleted.").arg(current), false);
    });

    connect(addProtocolEntryButton_, &QPushButton::clicked, this, [this] {
        const QString protocol = treatmentProtocolCombo_->currentText();
        const QString target = targetListName(targetListWidget_->currentItem());
        if (protocol.isEmpty() || target.isEmpty()) {
            showMessage(QStringLiteral("Select a target in the Target List first."), true);
            return;
        }
        writeProtocolGridToStore();
        beam::gui::TreatmentProtocolEntry entry;
        entry.target = target.toStdString();
        if (!targetStore_.appendProtocolEntry(protocol.toStdString(), entry)) {
            showMessage(QStringLiteral("Could not add \"%1\" to \"%2\".").arg(target, protocol), true);
            return;
        }
        setTargetStoreDirty(true);
        loadTreatmentProtocol(protocol);
        if (protocolModel_->rowCount() > 0) protocolView_->selectRow(protocolModel_->rowCount() - 1);
    });

    connect(removeProtocolEntryButton_, &QPushButton::clicked, this, [this] {
        const QString protocol = treatmentProtocolCombo_->currentText();
        const QModelIndexList selected = protocolView_->selectionModel()->selectedRows();
        if (protocol.isEmpty() || selected.isEmpty()) {
            showMessage(QStringLiteral("Select one or more protocol rows to remove."), true);
            return;
        }
        writeProtocolGridToStore();
        const beam::gui::TreatmentProtocolDefinition* definition =
            targetStore_.findProtocol(protocol.toStdString());
        if (definition == nullptr) return;
        std::vector<bool> drop(definition->entries.size(), false);
        for (const QModelIndex& index : selected) {
            if (index.row() >= 0 && index.row() < static_cast<int>(drop.size()))
                drop[static_cast<std::size_t>(index.row())] = true;
        }
        std::vector<beam::gui::TreatmentProtocolEntry> kept;
        for (std::size_t i = 0; i < definition->entries.size(); ++i)
            if (!drop[i]) kept.push_back(definition->entries[i]);
        const std::size_t removed = definition->entries.size() - kept.size();
        targetStore_.setProtocolEntries(protocol.toStdString(), std::move(kept));
        setTargetStoreDirty(true);
        loadTreatmentProtocol(protocol);
        showMessage(QStringLiteral("Removed %1 row(s) from \"%2\".").arg(removed).arg(protocol), false);
    });

    connect(saveProtocolsButton_, &QPushButton::clicked, this, [this] { saveTreatmentTargetStore(); });
    connect(protocolModel_, &QAbstractItemModel::dataChanged, this,
            [this] { setTargetStoreDirty(true); });
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
QString WorkflowWindow::treatmentDataDirectory() const {
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
    setTargetStoreDirty(false);
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
    if (!protocolModel_ || !treatmentProtocolCombo_) return;
    const std::string name = treatmentProtocolCombo_->currentText().toStdString();
    if (targetStore_.findProtocol(name) == nullptr) return;
    std::vector<beam::gui::TreatmentProtocolEntry> entries;
    for (const beam::gui_qt::TreatmentProtocolRow& row : protocolModel_->rows()) {
        beam::gui::TreatmentProtocolEntry entry;
        entry.target = row.target.toStdString();
        entry.amplitude = row.amplitude;
        entry.durationSeconds = row.duration;
        entries.push_back(std::move(entry));
    }
    targetStore_.setProtocolEntries(name, std::move(entries));
}

void WorkflowWindow::saveTreatmentTargetStore() {
    writeProtocolGridToStore();
    const QString directory = treatmentDataDirectory();
    try {
        targetStore_.saveTargets(
            QDir(directory).filePath(QStringLiteral("treatment_targets.csv")).toStdString());
        targetStore_.saveProtocols(
            QDir(directory).filePath(QStringLiteral("treatment_protocols.csv")).toStdString());
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Could not save treatment data: %1").arg(error.what()), true);
        return;
    }
    setTargetStoreDirty(false);
    showMessage(QStringLiteral("Protocols saved to %1.").arg(directory), false);
}

void WorkflowWindow::setTargetStoreDirty(bool dirty) {
    targetStoreDirty_ = dirty;
    if (saveProtocolsButton_) saveProtocolsButton_->setEnabled(dirty);
    if (treatmentProtocolLabel_) {
        treatmentProtocolLabel_->setText(dirty ? QStringLiteral("Treatment protocol: *")
                                               : QStringLiteral("Treatment protocol:"));
    }
}

void WorkflowWindow::loadTreatmentProtocol(const QString& protocolName) {
    // From BeamAI's own store, not BeamV0's TreatmentProtocols/*.csv. Each
    // entry references a Target List member by name; the target's points own
    // the burst/pulse parameters, the entry carries the schedule's duration.
    const beam::gui::TreatmentProtocolDefinition* definition =
        targetStore_.findProtocol(protocolName.toStdString());
    QList<QStringList> sourceRows;
    if (definition != nullptr) {
        for (const beam::gui::TreatmentProtocolEntry& entry : definition->entries) {
            const beam::gui::TreatmentTarget* target = targetStore_.findTarget(entry.target);
            // Where the entry says nothing, the target's first point stands in
            // -- it is the row the schedule would have been written from.
            const beam::gui::TreatmentTargetPoint* first =
                target != nullptr && !target->points.empty() ? &target->points.front() : nullptr;
            const double amplitude =
                entry.amplitude.value_or(first != nullptr ? first->amplitude : 0.75);
            const double duration =
                entry.durationSeconds.value_or(first != nullptr ? first->endTimeSeconds : 30.0);
            sourceRows.push_back({QString::number(entry.order),
                                  QString::fromStdString(entry.target),
                                  QString::number(duration), QString::number(amplitude),
                                  QStringLiteral("X:0,Y:0,Z:0"), QStringLiteral("N"),
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
    currentSonicationNumberEdit_->setRange(1, std::max<int>(1, static_cast<int>(stimParamModel_->rows().size())));

    selectSonicationRow(0);
    switchToSelectedTarget();
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
        // A target the Target List does not define -- a protocol naming an
        // undefined target already warned about it; one blank row keeps the
        // grid usable rather than empty.
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
    currentSonicationNumberEdit_->setRange(1, std::max<int>(1, static_cast<int>(stimParamModel_->rows().size())));
    saveCurrentTargetSonications();
    updateSonicationPlots();
    updateTreatmentPlanSummary();
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
        updateTreatmentPlanSummary();
    }
}

void WorkflowWindow::saveCurrentTargetSonications() {
    if (currentTargetId_ >= 0 && stimParamModel_ && !stimParamModel_->rows().empty())
        targetSonications_.insert(currentTargetId_, stimParamModel_->rows());
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

    // The placed position also belongs to whichever target is selected: the
    // grid row goes into that target's own record, and the position onto the
    // list item for Move To Target.
    if (QListWidgetItem* item = targetListWidget_->currentItem())
        item->setData(Qt::UserRole, QList<QVariant>{positionMm.x(), positionMm.y(), positionMm.z()});
    saveCurrentTargetSonications();
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
