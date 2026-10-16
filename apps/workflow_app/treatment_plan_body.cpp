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

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include "workflow_window.hpp"

#include "treatment_library_dialog.hpp"
#include "example_image_view.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QFileInfo>
#include <set>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QScreen>
#include <QWheelEvent>
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
#include <QListView>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpacerItem>
#include <QSpinBox>
#include <QIcon>
#include <QPainter>
#include <QStyledItemDelegate>
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

// A pane heading, painted exactly as kHeaderStyle paints a table column
// heading: same ink, same fill, same rule beneath. The Target List and the
// serial column head their panes the way Target or Parameters heads a column,
// so the three read as parts of one table rather than three unrelated boxes.
const char* const kPaneHeadingStyle =
    "QLabel { color: #17313f; background: #dcebef; padding: 4px; font-weight: 600; "
    "border-bottom: 1px solid #b7cbd1; }";

// The height kHeaderStyle's sections are pinned to, so a heading beside a
// table lines up with that table's own heading row.
constexpr int kHeadingHeight = 22;

// Where a Target List row keeps its name: see addTargetListRow for why it
// cannot be the item's own text.
constexpr int kTargetNameRole = Qt::UserRole + 1;
// A stable key for the target's own sonications, so renaming a target or
// inserting one above it does not lose them.
constexpr int kTargetIdRole = Qt::UserRole + 2;
// Set on the Target List row the Current Sonication Number points at.
constexpr int kCurrentTargetRole = Qt::UserRole + 3;

// The marker table's own selection fill, reused so a clicked row looks the
// same on both pages. The dark one marks the current sonication and the
// target it fires at -- a different thing from what the operator clicked.
const QColor kRowSelectedFill(190, 235, 245);
const QColor kCurrentRowFill(0x1f, 0x4e, 0x79);

// The countdown bar takes the colour of the tick the row will end up with:
// green while ultrasound is going out, amber while a sham is only playing its
// mask. Nothing else on the page distinguishes the two while they run.
QString progressBarStyle(const QColor& chunk) {
    return QStringLiteral("QProgressBar { background: #16303a; border: 1px solid #2b6172; "
                          "border-radius: 4px; } "
                          "QProgressBar::chunk { background: %1; border-radius: 3px; }")
        .arg(chunk.name());
}

// The in-row Sonicate button, and what it becomes while a sonication is
// actually being sent: red, because a greyed-out button reads as "not
// available" rather than "running right now".
// The two states of the in-row control, drawn rather than loaded: a resource
// can fail to deploy, and these have to scale with the row height.
//
// Sonicate: the transducer firing -- a solid emitter with the pressure wave
// leaving it. Stop: the square every machine uses to mean stop, and nothing
// else, so it cannot be read as "pause" or "next".
// The transport vocabulary a media player uses, because everyone already reads
// it: a solid triangle starts, a square stops. Sonicate is play.
QPolygonF playTriangle(double unit) {
    QPolygonF play;
    play << QPointF(4.5 * unit, 2.8 * unit) << QPointF(4.5 * unit, 13.2 * unit)
         << QPointF(13.0 * unit, 8.0 * unit);
    return play;
}

QIcon makeSonicateIcon(int side, const QColor& ink) {
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double unit = side / 16.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawPolygon(playTriangle(unit));
    return QIcon(canvas);
}

// The sham: play, hollow. The same shape and the same motions -- a countdown
// runs and the mask plays -- with nothing inside it, because no ultrasound
// leaves the transducer. Winamp has no icon for this; an outlined play is the
// nearest honest thing.
QIcon makeShamIcon(int side, const QColor& ink) {
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double unit = side / 16.0;
    QPen outline(ink, std::max(1.6, unit * 1.6));
    outline.setJoinStyle(Qt::MiterJoin);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(playTriangle(unit));
    return QIcon(canvas);
}

QIcon makeStopIcon(int side, const QColor& ink) {
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double unit = side / 16.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawRoundedRect(QRectF(4.0 * unit, 4.0 * unit, 8.0 * unit, 8.0 * unit), unit, unit);
    return QIcon(canvas);
}

const char* const kSonicateStyle =
    "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "stop:0 #2aa3c4, stop:1 #15718f); color: #ffffff; border: 1px solid #0b4a5c; "
    "border-radius: 4px; padding: 0 4px; font-size: 11px; font-weight: 700; "
    "letter-spacing: 0.4px; } "
    "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "stop:0 #38bcdf, stop:1 #1b86a8); } "
    "QPushButton:pressed { background: #0f5269; } "
    "QPushButton:disabled { background: #3b4a52; color: #93a3ab; border-color: #2b373d; }";

// Yellow, not amber: it has to be told apart from the blue beside it and the
// red beyond it at a glance, and dark ink on yellow is what reads.
// The ON lamp, in the manner of a panel indicator: a dot that lights beside
// the word, both always present and dark until something is actually going
// out. Dark-but-present is what makes a lit lamp mean something -- a label
// that appears and disappears reads as the layout moving, not as a state.
QString lampText(const QString& dot, const QString& word) {
    return QStringLiteral("<span style='color:%1'>&#9679;</span>"
                          "<span style='color:%2'> ON</span>")
        .arg(dot, word);
}
const char* const kLampOffStyle =
    "QLabel { background: #0d1418; color: #27343a; border: 1px solid #27343a; "
    "border-radius: 3px; font-family: 'Consolas','Courier New',monospace; "
    "font-size: 10px; font-weight: 700; letter-spacing: 1px; padding: 0 2px; }";

const char* const kLampSonicatingStyle =
    "QLabel { background: #04210f; color: #3dff7a; border: 1px solid #1f7a3a; "
    "border-radius: 3px; font-family: 'Consolas','Courier New',monospace; "
    "font-size: 10px; font-weight: 700; letter-spacing: 1px; padding: 0 2px; }";

const char* const kLampShamStyle =
    "QLabel { background: #2a2206; color: #ffd83d; border: 1px solid #b8912a; "
    "border-radius: 3px; font-family: 'Consolas','Courier New',monospace; "
    "font-size: 10px; font-weight: 700; letter-spacing: 1px; padding: 0 2px; }";

// What each icon is drawn in, so it reads against the fill under it.
const QColor kInkOnBlue(0xff, 0xff, 0xff);
const QColor kInkOnBrightBlue(0x06, 0x30, 0x3d);
const QColor kInkOnDimBlue(0x9f, 0xd7, 0xe6);
const QColor kInkOnYellow(0x3a, 0x2c, 0x00);
const QColor kInkOnDimYellow(0xff, 0xee, 0xa8);
const QColor kInkOnRed(0xff, 0xff, 0xff);
const QColor kInkOnDead(0x93, 0xa3, 0xab);

const char* const kShamStyle =
    "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "stop:0 #ffe066, stop:1 #e8bf1c); color: #3a2c00; border: 1px solid #b8912a; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "stop:0 #fff0a0, stop:1 #f5d130); } "
    "QPushButton:pressed { background: #c9a115; } "
    "QPushButton:disabled { background: #3b4a52; color: #93a3ab; border-color: #2b373d; }";

// Red, and red is on this row for nothing else: it is the one control that
// interrupts a sonication. A button that is merely busy must not borrow it --
// that reads as "press me to stop".
const char* const kStopStyle =
    "QPushButton { background: #d32f2f; color: #ffffff; border: 1px solid #8e1b1b; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:hover { background: #e14343; } "
    "QPushButton:pressed { background: #a81f1f; } "
    "QPushButton:disabled { background: #4a3a3a; color: #9a8686; border-color: #5c4545; }";

// A running button pulses between these two, in the colour of the button it
// already is -- blue for a sonication, yellow for a sham. One colour at two
// strengths, because a button that alternates hue reads as two states rather
// than one thing working.
const char* const kSonicatingStyle =
    "QPushButton { background: #35c6ef; color: #06303d; border: 1px solid #0b4a5c; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:disabled { background: #35c6ef; color: #06303d; border-color: #0b4a5c; }";

const char* const kSonicatingDimStyle =
    "QPushButton { background: #0d3f52; color: #9fd7e6; border: 1px solid #0b4a5c; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:disabled { background: #0d3f52; color: #9fd7e6; border-color: #0b4a5c; }";

const char* const kShamRunningStyle =
    "QPushButton { background: #ffd83d; color: #3a2c00; border: 1px solid #b8912a; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:disabled { background: #ffd83d; color: #3a2c00; border-color: #b8912a; }";

const char* const kShamRunningDimStyle =
    "QPushButton { background: #7d6410; color: #ffeea8; border: 1px solid #6d5a0c; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:disabled { background: #7d6410; color: #ffeea8; border-color: #6d5a0c; }";

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

// The selection highlight paints over whatever background the model asked for,
// so selecting the current sonication hid the grey that marks it. The grey says
// where the visit has got to, which outranks saying the row is selected -- the
// two are the same row almost always anyway.
class CurrentRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!index.data(beam::gui_qt::TreatmentProtocolTableModel::kCurrentRowRole).toBool())
            return;
        option->state &= ~QStyle::State_Selected;
        const QVariant background = index.data(Qt::BackgroundRole);
        if (background.canConvert<QColor>())
            option->backgroundBrush = QBrush(background.value<QColor>());
    }
};

// The Target List row the current sonication fires at, in the same dark blue
// as that row in the log, so the two read as one statement about where the
// visit is. The selection highlight is dropped for it the same way.
class CurrentTargetDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!index.data(kCurrentTargetRole).toBool()) return;
        option->state &= ~QStyle::State_Selected;
        option->backgroundBrush = QBrush(kCurrentRowFill);
        option->palette.setColor(QPalette::Text, QColor(0xff, 0xff, 0xff));
        option->palette.setColor(QPalette::WindowText, QColor(0xff, 0xff, 0xff));
    }
};

// Response Pain and Response Mood run -2..2 (getResponseFromTreatmentProtocolData.m
// rounds and clamps to that, and writes the clamped value back). Typing 3 and
// having it silently become 2 reads as the table corrupting the entry, so the
// editor only offers what the scale holds. It keeps CurrentRowDelegate's
// painting, or these two cells would lose the current-row marking.
class ResponseDelegate final : public CurrentRowDelegate {
public:
    using CurrentRowDelegate::CurrentRowDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        auto* editor = new QSpinBox(parent);
        editor->setRange(-2, 2);
        editor->setToolTip(QStringLiteral("-2 to 2"));
        return editor;
    }
};

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
    view->setItemDelegate(new CurrentRowDelegate(view));
    QPalette palette = view->palette();
    palette.setColor(QPalette::Highlight, kRowSelectedFill);
    palette.setColor(QPalette::HighlightedText, QColor(0x17, 0x31, 0x3f));
    view->setPalette(palette);
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
    // Named so the current-sonication marking can recolour it: the row widget
    // draws the text, so a delegate cannot reach it.
    nameLabel->setObjectName(QStringLiteral("targetNameLabel"));
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
    // Narrow: it holds short target names and small row buttons, and every pixel
    // it gives up goes to the log table, where Notes was being cut.
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
    // Shorter, so the full-width Finish Treatment at the foot has room without
    // pushing the MRI viewers off the page. The list scrolls.
    targetListWidget_->setMinimumHeight(96);
    targetListWidget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    targetListWidget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    targetListWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    targetPanelLayout->addWidget(targetListWidget_, 1);
    // Under the list, not above it: it acts on the row that is selected there,
    // so it reads as the list's own action rather than a pane-wide one.
    targetPanelLayout->addWidget(moveToTargetButton_);
    // Under the list it edits. It sat above it while the body still scrolled,
    // where a button below a full-height list fell past the visible area.
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
    // No Sort / Add / Remove buttons: BeamV0 has one sonication row per target
    // and no UI to change that. createSonicationUpdateTable.m and
    // removeSonicationUpdateTable.m are unreferenced there, and
    // sortSonicationTable.m runs on its own rather than from a button. Several
    // rows per target is DiademV0's behaviour, which is a separate application.
    stimParamModel_ = new beam::gui_qt::StimParamTableModel(this);
    stimParamView_ = new QTableView(bodyContent);
    stimParamView_->setModel(stimParamModel_);
    configureModelTable(stimParamView_);
    // Show is how getCurrentShownSonication.m picks the row to fire, so the
    // flag stays and only the column goes: with one row per target there is
    // nothing to choose between, and a checkbox implying otherwise misleads.
    stimParamView_->setColumnHidden(beam::gui_qt::StimParamTableModel::kShow, true);
    // Order numbers a column of one: every BeamV0 target carries exactly one
    // stimParamTable row (createStimParamTable.m's own N = 1), so the number is
    // always 1 and says nothing.
    stimParamView_->setColumnHidden(beam::gui_qt::StimParamTableModel::kOrder, true);
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
    // Off the page: a plan is a visit, filled by Add Row and Add Protocol to
    // Plan, so there is no protocol for the operator to pick here. The widget
    // stays because the log is keyed by protocol name and this is where that
    // name comes from.
    treatmentProtocolLabel_->setVisible(false);
    treatmentProtocolCombo_->setVisible(false);
    visitNumberCombo_ = new QComboBox(protocolTab);
    visitNumberCombo_->setObjectName(QStringLiteral("visitNumberCombo"));
    // Filled by refreshVisitCombo from the log: only visits that exist are
    // offered. BeamV0 keeps fifteen sessions per protocol and lists all of
    // them, but an empty visit 12 is not something an operator can mean to
    // choose; Create New Visit is what brings the next one into being.
    // A QListView popup with room for every visit: the default popup shows ten
    // rows and scrolls the rest, so visits 10 and up sat below the fold in what
    // looked like empty space. Styles are free to ignore maxVisibleItems on a
    // native popup, which setting the view explicitly prevents.
    visitNumberCombo_->setView(new QListView(visitNumberCombo_));
    visitNumberCombo_->setMaxVisibleItems(kMaxTreatmentVisits);
    // Wide enough for "Visit 12 -- 3 of 24 delivered" and fixed there, so the
    // buttons beside it cannot squeeze it until its text is unreadable.
    visitNumberCombo_->setMinimumWidth(188);
    visitNumberCombo_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Nothing but the create item until a visit exists: an item reading
    // "Visit 1" says a visit 1 has been created. refreshVisitCombo fills in
    // the ones that have.
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
        QStringLiteral("Manage Protocols..."),
        QStringLiteral("Add, rename, delete protocols and the targets each one schedules"));
    manageTargetsButton_->setParent(protocolTab);
    manageTargetsButton_->setVisible(true);
    manageTargetsButton_->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));
    manageTargetsButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
    currentSonicationNumberLabel_ = new QLabel(protocolTab);
    currentSonicationNumberLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; background: #1f4e79; border: 1px solid #163c5e; "
        "border-radius: 3px; padding: 2px 10px; font-weight: 700; }"));
    for (QLabel* label : {treatmentProtocolLabel_, visitLabel, hydrogelLabel, sonicationNumberLabel})
        label->setStyleSheet(QStringLiteral("QLabel { color: #dcebef; }"));
    // One row, not two: the row these fields used to occupy goes to the
    // schedule below. Reading left to right: which visit, where it has got
    // to, the coupling setting -- then the stretch, which holds the two
    // dialogs against the right edge, away from what is read.
    protocolSelectorRow->addWidget(visitNumberCombo_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(sonicationNumberLabel);
    protocolSelectorRow->addWidget(currentSonicationNumberLabel_);
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(hydrogelLabel);
    protocolSelectorRow->addWidget(hydrogelSizeCombo_);
    protocolSelectorRow->addStretch(1);
    protocolSelectorRow->addWidget(manageLibraryButton_);
    protocolSelectorRow->addWidget(manageTargetsButton_);
    protocolLayout->addLayout(protocolSelectorRow);

    protocolModel_ = new beam::gui_qt::TreatmentProtocolTableModel(this);
    protocolView_ = new QTableView(protocolTab);
    protocolView_->setObjectName(QStringLiteral("treatmentPlanView"));
    // Drag or shift-click across rows to take several at once; the row menu
    // then acts on what is selected.
    protocolView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    protocolView_->setModel(protocolModel_);
    configureModelTable(protocolView_);
    // Explicit widths, not ResizeToContents: that sizes the view to the sum of
    // its content, and the view then demanded the whole row -- the Sonicate and
    // serial column was pushed off the page entirely. These fit the longest
    // label in each column; Notes takes what is left, and the view scrolls
    // sideways rather than growing if the window is narrow.
    protocolView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Notes takes the slack. Parameters is sized to the string it holds --
    // stretching it left a wide column with the text ending halfway across --
    // and Notes is free text, which can always use whatever is left.
    protocolView_->horizontalHeader()->setStretchLastSection(true);
    protocolView_->horizontalHeader()->setMinimumSectionSize(48);
    protocolView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (int column : {beam::gui_qt::TreatmentProtocolTableModel::kResponsePain,
                       beam::gui_qt::TreatmentProtocolTableModel::kResponseMood})
        protocolView_->setItemDelegateForColumn(column, new ResponseDelegate(protocolView_));
    // A value that still arrives out of range -- pasted, or from a file -- is
    // clamped by the model. Say so, or the number appears to change itself.
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
        // Each the width of what it holds: Parameters fits its whole string
        // (X, Y, Z and the four timings) and stops there, the two response
        // columns fit their full names, and # fits two digits.
        const int widths[] = {158, 28, 78, 68, 72, 300, 100, 106, 86, 86, 56, 200};
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
    protocolView_->setMinimumHeight(96);
    protocolView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // The table takes the tab's leftover height, so there is no band of empty
    // space under it and no surplus for the layout to spread between the rows
    // above. Its own scrollbar handles a schedule longer than the space.
    protocolLayout->addWidget(protocolView_, 1);
    // The table takes the slack in this column, and nothing else does. With
    // the table hidden there was nothing to take it, so the layout spread it
    // over the row above and the controls sat a few lines down the page until
    // the first visit brought the table back. This takes it instead.
    protocolEmptySpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
    protocolLayout->addItem(protocolEmptySpacer_);

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
    // The last item of the visit list makes a visit; every other item is one.
    connect(visitNumberCombo_, &QComboBox::activated, this, [this](int index) {
        const int chosen = visitNumberCombo_->itemData(index).toInt();
        // Queued, and by the visit number rather than the item position: both
        // handlers below rebuild this list, and rebuilding a combo from inside
        // its own signal leaves Qt holding a model index that no longer exists
        // -- which is why Create New Visit often did nothing at all. Let the
        // combo finish, then act.
        QMetaObject::invokeMethod(this, [this, chosen] {
            if (chosen == kNewVisitItem) {
                startNewVisit();
                return;
            }
            selectVisit(chosen);
            loadTreatmentProtocol(currentProtocolName());
        }, Qt::QueuedConnection);
    });
    connect(computeBestTargetsButton_, &QPushButton::clicked, this, [this] { updateBestTargets(); });
    connect(accFlagCombo_, &QComboBox::currentTextChanged, this, [this] {
        updateBestTargets();
        updateExampleTargetImage();
    });
    connect(protocolModel_, &QAbstractItemModel::dataChanged, this, [this] { updateBestTargets(); });

    // Before the first load: it decides which sonication each visit opens on.
    loadVisitState();
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
    // An unanswered response shows as 0 in the grid, so a 0 coming back from a
    // row that was never answered is that blank, not an entered zero.
    const auto entered = [](std::optional<double> stored, double shown) -> std::optional<double> {
        if (!stored.has_value() && shown == 0.0) return std::nullopt;
        return shown;
    };
    for (std::size_t index = 0; index < protocolModel_->rows().size(); ++index) {
        const beam::gui_qt::TreatmentProtocolRow& source = protocolModel_->rows()[index];
        const int sequence = static_cast<int>(index) + 1;
        const beam::gui::TreatmentLogRow* existing = treatmentLog_.find(protocol, visit, sequence);
        if (existing == nullptr) continue;
        // Not through record(): that keeps the stored response on purpose, so a
        // response handed to it is dropped and the operator's edit never lands.
        treatmentLog_.setResponse(protocol, visit, sequence,
                                  entered(existing->responsePain, source.responsePain),
                                  entered(existing->responseMood, source.responseMood));
        treatmentLog_.setNotes(protocol, visit, sequence, source.notes.toStdString());

        // A delivered row records what was sent, so only the operator entries
        // above can change it. One not yet fired is still the plan copy, and an
        // edit to it is the operator adjusting this visit.
        if (beam::gui::TreatmentLog::delivered(*existing)) continue;
        beam::gui::TreatmentLogRow row = *existing;
        row.target = source.target.toStdString();
        row.amplitude = source.amplitude;
        row.endTimeSeconds = row.startTimeSeconds + source.duration;
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
        const QString logPath = QDir(directory).filePath(QStringLiteral("treatment_log.csv"));
        treatmentLog_.save(logPath.toStdString());
        // The log writes itself on every change, so there is no Save button to
        // press and no way to tell whether it happened. This is that telling.
        // The file first, then what happened to it: the operator is checking
        // which log this went into, not reading a headline.
        showMessage(QStringLiteral("%1 updated at %2")
                        .arg(QDir::toNativeSeparators(logPath))
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))),
                    false);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("Could not save treatment data: %1").arg(error.what()), true);
        return;
    }
}

beam::gui::TreatmentLogRow WorkflowWindow::rowForTarget(
    const std::string& target, std::optional<double> durationSeconds) const {
    const beam::gui::TreatmentTarget* defined = targetStore_.findTarget(target);
    const beam::gui::TreatmentTargetPoint* first =
        defined != nullptr && !defined->points.empty() ? &defined->points.front() : nullptr;
    beam::gui::TreatmentLogRow row;
    row.target = target;
    // The point amplitude, not the schedule's: BeamV0 stores a schedule
    // amplitude but never applies it, so the point is what is sent.
    row.amplitude = first != nullptr ? first->amplitude : 0.75;
    row.startTimeSeconds = 0.0;
    row.endTimeSeconds =
        durationSeconds.value_or(first != nullptr ? first->endTimeSeconds : 30.0);
    if (first != nullptr) {
        row.burstDurationSeconds = first->burstDurationSeconds;
        row.burstIntervalSeconds = first->burstIntervalSeconds;
        row.pulseDurationSeconds = first->pulseDurationSeconds;
        row.pulseIntervalSeconds = first->pulseIntervalSeconds;
    }
    row.fromPlan = false;
    return row;
}

// Adding a protocol works the way adding a sonication works: right-click the
// row it should follow and choose one. Nothing is replaced -- a visit can take
// a protocol twice, or two different protocols, and what is already in it
// keeps its place.
void WorkflowWindow::addProtocolToVisit(int afterSequence) {
    QStringList names;
    for (const beam::gui::TreatmentProtocolDefinition& known : targetStore_.protocols())
        names << QString::fromStdString(known.name);
    if (names.isEmpty()) {
        showMessage(QStringLiteral("There are no protocols to add."), true);
        return;
    }
    bool accepted = false;
    const QString picked = QInputDialog::getItem(
        this, QStringLiteral("Add protocol to plan"), QStringLiteral("Protocol:"), names,
        std::max(0, static_cast<int>(names.indexOf(currentProtocolName()))), false, &accepted);
    if (!accepted || picked.isEmpty()) return;
    const beam::gui::TreatmentProtocolDefinition* definition =
        targetStore_.findProtocol(picked.toStdString());
    if (definition == nullptr || definition->entries.empty()) {
        showMessage(QStringLiteral("\"%1\" schedules nothing to add.").arg(picked), true);
        return;
    }

    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    int after = afterSequence;
    for (const beam::gui::TreatmentProtocolEntry& entry : definition->entries) {
        beam::gui::TreatmentLogRow row = rowForTarget(entry.target, entry.durationSeconds);
        row.protocol = protocol;
        row.visit = visit;
        if (after >= 1 && treatmentLog_.canInsertAfter(protocol, visit, after)) {
            treatmentLog_.insertAfter(protocol, visit, after, std::move(row));
            ++after;  // the next one follows the one just placed
        } else {
            treatmentLog_.append(std::move(row));
            after = 0;  // the rest follow it at the end
        }
    }
    markVisitEdited(protocolName, visit);
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
    showMessage(QStringLiteral("%1 sonication(s) of \"%2\" added to visit %3.")
                    .arg(definition->entries.size())
                    .arg(picked)
                    .arg(visit),
                false);
}

void WorkflowWindow::addRowToVisit() {
    QStringList names;
    for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
        names << QString::fromStdString(target.name);
    if (names.isEmpty()) {
        showMessage(QStringLiteral("The Target List is empty; there is nothing to add."), true);
        return;
    }
    bool accepted = false;
    const QString picked = QInputDialog::getItem(this, QStringLiteral("Add row"),
                                                 QStringLiteral("Target:"), names, 0, false,
                                                 &accepted);
    if (!accepted || picked.isEmpty()) return;
    const QString protocolName = currentProtocolName();
    const int visit = currentVisitNumber();
    beam::gui::TreatmentLogRow row = rowForTarget(picked.toStdString(), std::nullopt);
    row.protocol = protocolName.toStdString();
    row.visit = visit;
    treatmentLog_.append(std::move(row));
    markVisitEdited(protocolName, visit);
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
    showMessage(QStringLiteral("%1 added to visit %2.").arg(picked).arg(visit), false);
}

void WorkflowWindow::loadTreatmentProtocol(const QString& protocolName) {
    // The table on this page is the VISIT LOG, not the protocol. BeamV0 draws
    // treatmentProtocolTables(i).sessions(visit).Data here, seeded from
    // blank.Data by initTreatmentProtocolTables.m and written back by
    // setTreatmentProtocolTableData.m -- never into blank. The protocol itself
    // is the model, and Manage Protocols is the only thing that edits it.
    const std::string protocol = protocolName.toStdString();
    // Visit numbers belong to a protocol. Carrying the last one across meant
    // arriving at a protocol on a visit it had never had -- and seeding that
    // visit into its log on the way in, which is how an empty protocol came to
    // open on visit 15.
    if (protocolName != lastLoadedProtocol_ && visitNumberCombo_ != nullptr) {
        const int opening = visitForProtocol(protocol);
        selectVisit(opening);
    }
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentProtocolDefinition* definition =
        targetStore_.findProtocol(protocol);

    // Nothing is seeded here. A visit holds exactly the rows it was given:
    // created empty by New Visit, filled by Add Protocol or by inserting rows,
    // and changed only by the operator afterwards. The old model derived the
    // rows from the protocol on every load and had to keep proving the operator
    // had not touched them -- which is why a visit emptied by hand refilled
    // itself, and why deleting the last row could not work.
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
        // Time of day to the millisecond in the cell, the whole stamp in the
        // tooltip: a visit runs inside an hour, so the date is noise in a column
        // and worth keeping only where it can be asked for.
        const auto timeOfDay = [](const std::string& stamp) {
            const QDateTime when =
                QDateTime::fromString(QString::fromStdString(stamp), Qt::ISODateWithMs);
            return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss.zzz")) : QString();
        };
        row.startedAt = timeOfDay(logged.startedAt);
        row.endedAt = timeOfDay(logged.endedAt);
        row.startedAtFull = QString::fromStdString(logged.startedAt);
        row.endedAtFull = QString::fromStdString(logged.endedAt);
        row.statusCode = logged.statusCode;
        row.responsePain = logged.responsePain.value_or(0.0);
        row.responseMood = logged.responseMood.value_or(0.0);
        row.notes = QString::fromStdString(logged.notes);
        row.delivered = beam::gui::TreatmentLog::delivered(logged);
        row.sham = logged.sham;
        row.aborted = beam::gui::TreatmentLog::stopped(logged);
        protocolRows.push_back(row);
        if (!targetNames.contains(row.target)) targetNames.push_back(row.target);
    }

    const double defaultAmplitude = protocolRows.empty() ? 0.75 : protocolRows.front().amplitude;
    const double defaultDuration = protocolRows.empty() ? 30.0 : protocolRows.front().duration;
    protocolModel_->setRows(std::move(protocolRows));

    // Before a visit is created there is nothing to show: not rows, not the
    // blank row the model draws for a visit that has none, not the header. The
    // list beside it offers Create New Visit, and the table arrives with it.
    const bool visitCreated = visitExists(protocolName, visit);
    if (protocolView_ != nullptr) {
        protocolView_->setVisible(visitCreated);
        if (protocolEmptySpacer_ != nullptr) {
            protocolEmptySpacer_->changeSize(0, 0, QSizePolicy::Minimum,
                                             visitCreated ? QSizePolicy::Minimum
                                                          : QSizePolicy::Expanding);
            if (protocolView_->parentWidget() != nullptr &&
                protocolView_->parentWidget()->layout() != nullptr)
                protocolView_->parentWidget()->layout()->invalidate();
        }
    }

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
    gridTargetId_ = -1;

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
    refreshVisitCombo();
    // Where this visit had got to. A reload of the same protocol and visit
    // keeps the operator's place; a different one resumes where that one was
    // left, falling back to its first sonication.
    const QString key = visitKey(protocolName, visit);
    const bool sameplace = protocolName == lastLoadedProtocol_ && visit == lastLoadedVisit_;
    int keep = 1;
    if (sameplace) {
        keep = currentSonication_;
    } else {
        // Where the visit actually got to. rowsFor() is in sequence order, so
        // the last delivered row is the furthest it reached, and the visit
        // picks up after it rather than back at the top.
        int delivered = 0;
        int firstFree = 0;
        for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rowsFor(protocol, visit)) {
            if (beam::gui::TreatmentLog::delivered(row)) delivered = row.sequence;
            else if (firstFree == 0) firstFree = row.sequence;
        }
        // A visit that has delivered nothing starts at its first sonication,
        // whatever was remembered against it. The remembered number outlives
        // the log it describes -- the log can be deleted, and a new protocol
        // can take the name of an old one -- and a visit that has not run must
        // not open part-way through itself.
        if (delivered == 0) keep = 1;
        else if (visitSonication_.contains(key)) keep = visitSonication_.value(key);
        // The first row still to run, which is not always one past the last
        // delivered: re-firing an earlier row leaves later ones outstanding.
        else keep = firstFree > 0 ? firstFree : delivered + 1;
    }
    // A sonication in progress keeps the number, whatever else this reload
    // concluded. Its row is stamped as started the moment it goes out, which
    // reads here as delivered: inserting a row while row 2 was running sent
    // the number and the highlight to the new row 3 while row 2 was still
    // going out.
    if (openLogSequence_ > 0) {
        keep = openLogSequence_;
    } else {
        // Wherever that landed, it must not be a row that has already run
        // while another has not. A sonication added to a finished visit is
        // the case that matters: the operator inserts it, the number is still
        // sitting on the last delivered row, and the new row -- the only one
        // left to run -- gets no buttons because the current row is not it.
        const std::vector<beam::gui::TreatmentLogRow> rows =
            treatmentLog_.rowsFor(protocol, visit);
        const std::size_t at = static_cast<std::size_t>(keep) - 1;
        if (keep >= 1 && at < rows.size() && beam::gui::TreatmentLog::delivered(rows[at])) {
            for (const beam::gui::TreatmentLogRow& row : rows) {
                if (beam::gui::TreatmentLog::delivered(row)) continue;
                keep = row.sequence;
                break;
            }
        }
    }
    lastLoadedProtocol_ = protocolName;
    lastLoadedVisit_ = visit;
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
    if (currentTargetId_ < 0) {
        gridTargetId_ = -1;
        stimParamModel_->setTargetName(QString());
        return;
    }

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
    // The grid now holds this target, so it may be cached against it -- and
    // only now may the name be set, since that emits dataChanged and the
    // handler for it saves.
    gridTargetId_ = currentTargetId_;
    stimParamModel_->setTargetName(targetListName(item));
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
    // gridTargetId_ guards the window between choosing a target and the grid
    // being refilled for it: anything that reaches here in between would cache
    // the previous target's rows against the new one, and the store write that
    // follows would make that permanent.
    if (currentTargetId_ >= 0 && currentTargetId_ == gridTargetId_ && stimParamModel_ &&
        !stimParamModel_->rows().empty()) {
        targetSonications_.insert(currentTargetId_, stimParamModel_->rows());
    }
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
    ExampleImageView* const imageSlots[] = {exampleTargetImage_, exampleTargetImage2_,
                                            exampleTargetImage3_};
    for (int i = 0; i < 3; ++i) {
        const QString path = QStringLiteral(":/example_targets/%1%2.bmp").arg(stem, suffixes.value(i));
        const QPixmap image(path);
        if (image.isNull()) {
            imageSlots[i]->setPlaceholder(
                QStringLiteral("%1 %2 not bundled").arg(stem, suffixes.value(i)));
            continue;
        }
        imageSlots[i]->setImage(image);
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
// BeamV0 tiles a recorded burst that it loads from sonicationSound.mat. That
// file ships with BeamV0, not with this application, and reading it would tie a
// sham to both a MATLAB container and a sibling checkout -- so it was converted
// once into data/sham_burst.wav and this reads the audio file. A synthesised
// 200 Hz burst stands in when the file is absent: the mask still masks, it just
// does not sound the same.
// Every button in the Sonicate column is this tall, so the column reads as
// one set of controls.
constexpr int kControlButtonHeight = 38;

constexpr double kSyntheticShamFs = 44100.0;

struct ShamBurst {
    std::vector<double> samples;
    double sampleRateHz = kSyntheticShamFs;
    bool recorded = false;
};

ShamBurst synthesiseShamBurst() {
    constexpr double kToneHz = 200.0;
    constexpr double kBurstSeconds = 0.05;
    ShamBurst burst;
    const auto n = static_cast<std::size_t>(kBurstSeconds * kSyntheticShamFs);
    burst.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        burst.samples[i] = std::sin(2.0 * 3.14159265358979323846 * kToneHz *
                                    static_cast<double>(i) / kSyntheticShamFs);
    }
    return burst;
}

// Enough WAV to read the one file this ships: 16-bit PCM, mono, uncompressed.
// Anything else returns nothing and the caller falls back, rather than playing
// a misread buffer as noise at a participant.
ShamBurst readWavBurst(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || !bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE") return {};

    QDataStream in(bytes);
    in.setByteOrder(QDataStream::LittleEndian);
    in.skipRawData(20);
    quint16 format = 0, channels = 0, bitsPerSample = 0, blockAlign = 0;
    quint32 sampleRate = 0, byteRate = 0;
    in >> format >> channels >> sampleRate >> byteRate >> blockAlign >> bitsPerSample;
    if (format != 1 || channels != 1 || bitsPerSample != 16 || sampleRate == 0) return {};

    // Walk the chunks rather than assuming data starts at 36: a file written by
    // another tool may carry LIST or fact before it.
    int offset = 12;
    while (offset + 8 <= bytes.size()) {
        const QByteArray id = bytes.mid(offset, 4);
        const quint32 size = static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 4])) |
                             (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 5])) << 8) |
                             (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 6])) << 16) |
                             (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 7])) << 24);
        if (id == "data") {
            const int start = offset + 8;
            const int count = static_cast<int>(std::min<quint32>(size, bytes.size() - start)) / 2;
            if (count <= 0) return {};
            ShamBurst burst;
            burst.sampleRateHz = sampleRate;
            burst.recorded = true;
            burst.samples.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                const auto low = static_cast<unsigned char>(bytes[start + i * 2]);
                const auto high = static_cast<unsigned char>(bytes[start + i * 2 + 1]);
                burst.samples.push_back(static_cast<qint16>(low | (high << 8)) / 32768.0);
            }
            return burst;
        }
        offset += 8 + static_cast<int>(size) + (size % 2);  // chunks are word aligned
    }
    return {};
}

// Read once: the file does not change while the application runs, and a sham
// should not stall on disk at the moment it is triggered.
const ShamBurst& shamBurst() {
    static const ShamBurst burst = [] {
        const QString executableDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            QDir(executableDir).filePath(QStringLiteral("../../../data/sham_burst.wav")),
            QDir(executableDir).filePath(QStringLiteral("../../data/sham_burst.wav")),
            QDir(executableDir).filePath(QStringLiteral("data/sham_burst.wav")),
            QDir::current().filePath(QStringLiteral("data/sham_burst.wav")),
        };
        for (const QString& candidate : candidates) {
            if (!QFileInfo::exists(candidate)) continue;
            const ShamBurst found = readWavBurst(candidate);
            if (!found.samples.empty()) return found;
        }
        return synthesiseShamBurst();
    }();
    return burst;
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
    treatmentExecutionProgressBar_->setMinimumWidth(210);
    treatmentExecutionProgressBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // The bar takes the larger share of the row: it is what the operator
    // watches while a sonication runs, and the label beside it holds one line.
    statusColumn->addWidget(sonicationCountdownLabel_, 1);
    statusColumn->addWidget(treatmentExecutionProgressBar_, 1);
    row->addLayout(statusColumn, 1);

    // Right: the .mlapp's own narrow control column.
    auto* controlColumn = new QVBoxLayout;
    // Tight, so the whole column clears the body that holds it: at the
    // default spacing Abort sonication fell past the bottom edge.
    controlColumn->setSpacing(4);
    controlColumn->setContentsMargins(0, 0, 0, 0);
    auto* controlPanel = new QWidget(treatmentExecutionBody_);
    // The same width as the Target List on the other side, so the page reads as
    // a table between two matching columns. It can afford it: Sonicate, Sham and
    // Abort have all moved onto the log row, leaving a port, a trigger and one
    // button here.
    controlPanel->setMinimumWidth(140);
    controlPanel->setMaximumWidth(140);
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
    // A plan holds many sonications and BeamV0 fires them one at a time, so
    // finishing the stage is a separate, deliberate action -- completing it
    // when the first countdown ends would skip the rest of the plan.
    // Full width at the foot of the page: it ends the visit, it is pressed
    // once, and in the narrow column it read as one more serial-port control.
    finishTreatmentButton_ = new QPushButton(QStringLiteral("Finish Treatment"),
                                             treatmentExecutionBody_);
    finishTreatmentButton_->setMinimumHeight(40);
    finishTreatmentButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    finishTreatmentButton_->setEnabled(false);
    for (QPushButton* button : {serialConnectButton_})
        button->setStyleSheet(QString::fromLatin1(kPanelButtonStyle));

    auto* connectedRow = new QHBoxLayout;
    connectedRow->addWidget(columnLabel(QStringLiteral("Connected")));
    connectedRow->addWidget(serialConnectedLamp_);
    connectedRow->addStretch(1);
    auto* serialHeading = new QLabel(QStringLiteral("Serial Port"), controlPanel);
    serialHeading->setStyleSheet(QString::fromLatin1(kPaneHeadingStyle));
    serialHeading->setFixedHeight(kHeadingHeight);
    controlColumn->addWidget(serialHeading);
    controlColumn->addWidget(serialPortCombo_);
    controlColumn->addWidget(serialConnectButton_);
    controlColumn->addLayout(connectedRow);
    controlColumn->addWidget(columnLabel(QStringLiteral("Trigger:")));
    controlColumn->addWidget(triggerModeCombo_);
    controlColumn->addSpacing(6);
    // Slack below, or the box layout spreads the controls apart.
    // No trailing stretch: the column is as tall as its controls now, and the
    // space below belongs to the page, not to an empty pane.

    // One size for the whole column. Sonicate was taller than the rest and
    // Finish a different shape again, which made the column read as four
    // unrelated controls rather than one set.
    for (QPushButton* button : {serialConnectButton_}) {
        // Last, so it wins over whatever each button's own sheet set. The
        // min-height matters most: Sonicate's sheet asks for 34px plus padding,
        // and a stylesheet minimum beats any maximum set in code.
        button->setStyleSheet(button->styleSheet() +
                              QStringLiteral(" QPushButton { padding: 4px 10px; min-height: 28px; max-height: 28px; }"));
        button->setMinimumHeight(kControlButtonHeight);
        button->setMaximumHeight(kControlButtonHeight);
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    // Beside the plan rather than under it. Stacked below, this column began
    // around 1150 px down a maximised window -- off the bottom of a 1080p
    // screen, so the operator could not see Sonicate at all.
    // Top-aligned and only as tall as what it holds: given the row height it
    // grew a long empty box below Finish treatment.
    controlPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (treatmentBodyRow_) {
        controlPanel->setParent(nullptr);
        treatmentBodyRow_->addWidget(controlPanel, 0, Qt::AlignTop);
    } else {
        row->addWidget(controlPanel, 0, Qt::AlignTop);
    }
    // No stretch: this row is one line of status and a button. Given a share of
    // the leftover height it centred them in a tall empty band at the foot of
    // the page. The Sonicate body above takes the slack instead.
    finishTreatmentButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #176b87; color: #ffffff; border: 1px solid #0f5269; "
        "border-radius: 5px; font-weight: 700; letter-spacing: 0.5px; } "
        "QPushButton:hover { background: #2083a3; } "
        "QPushButton:pressed { background: #0f5269; } "
        "QPushButton:disabled { background: #3b4a52; color: #93a3ab; border-color: #2b373d; }"));
    outer->addWidget(finishTreatmentButton_);
    // The countdown and its bar read below the button, so the last thing on
    // the page is what is happening now rather than a control.
    outer->addLayout(row);
    // Its own height, held: the body is Fixed, but a page too short for
    // everything above it squeezed this to nothing and took the countdown and
    // the progress bar with it. The plan above scrolls; this does not.
    treatmentExecutionBody_->setMinimumHeight(outer->sizeHint().height());
    treatmentExecutionBody_->setVisible(false);

    setSerialConnectedLamp(false);

    // Not startStandaloneCountdown.m's 2 s tick arithmetic: that runs two
    // ticks past zero, so a 30 s sonication closed its log row at 34 s and the
    // Start and End stamps said so. The row ends when the time it asked for
    // has passed, and the timer only decides how often that is checked.
    sonicationCountdownTimer_ = new QTimer(this);
    sonicationCountdownTimer_->setInterval(100);
    connect(sonicationCountdownTimer_, &QTimer::timeout, this, [this] {
        const qint64 remainingMs = sonicationDeadlineMs_ - QDateTime::currentMSecsSinceEpoch();
        const int left = static_cast<int>((remainingMs + 999) / 1000);
        if (remainingMs <= 0) {
            sonicationCountdownTimer_->stop();
            if (runningFlashTimer_ != nullptr) runningFlashTimer_->stop();
            sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
            closeOpenLogRow();
            stopShamAudio();
            // Back to empty rather than sitting at 100%: the Completed column
            // now says the sonication finished, and a full bar left on screen
            // reads as one still in progress.
            treatmentExecutionProgressBar_->setValue(0);
            setSonicateEnabled(true);
            finishTreatmentButton_->setEnabled(true);
            return;
        }
        sonicationCountdownLabel_->setText(
            QString::fromStdString(beam::gui::countdownDisplayText(std::max(left, 0))));
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

QString WorkflowWindow::visitStatePath() const {
    return QDir(treatmentDataDirectory()).filePath(QStringLiteral("treatment_visit_state.csv"));
}

// Where each visit was left. The log holds what was delivered, which is not the
// same thing: the operator can point the visit at a sonication without firing
// it, and that choice has to survive a restart like everything else on the page.
void WorkflowWindow::saveVisitState() const {
    QFile file(visitStatePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    QTextStream out(&file);
    out << "Protocol,Visit,CurrentSonication,Edited\n";
    for (auto entry = visitSonication_.constBegin(); entry != visitSonication_.constEnd();
         ++entry) {
        const int split = entry.key().lastIndexOf(QLatin1Char('|'));
        if (split <= 0) continue;
        QString protocol = entry.key().left(split);
        protocol.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        out << '"' << protocol << "\"," << entry.key().mid(split + 1) << ',' << entry.value()
            << ',' << (editedVisits_.contains(entry.key()) ? 1 : 0) << '\n';
    }
}

// The operator has changed this visit's rows, so the schedule no longer
// describes it and must not be seeded into it again.
void WorkflowWindow::markVisitEdited(const QString& protocol, int visit) {
    editedVisits_.insert(visitKey(protocol, visit));
    saveVisitState();
}

void WorkflowWindow::loadVisitState() {
    QFile file(visitStatePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    in.readLine();  // header
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        // The protocol is quoted and may hold a comma; the two numbers after it
        // cannot, so splitting from the right is enough.
        // Protocol,Visit,CurrentSonication[,Edited]. Edited is read off the
        // end when present, so a file written before the column still loads.
        QString body = line;
        bool edited = false;
        if (body.count(QLatin1Char(',')) >= 3) {
            const int tail = body.lastIndexOf(QLatin1Char(','));
            edited = body.mid(tail + 1).trimmed() == QLatin1String("1");
            body = body.left(tail);
        }
        const int lastComma = body.lastIndexOf(QLatin1Char(','));
        const int firstComma = body.lastIndexOf(QLatin1Char(','), lastComma - 1);
        if (firstComma <= 0) continue;
        QString protocol = body.left(firstComma).trimmed();
        if (protocol.startsWith(QLatin1Char('"')) && protocol.endsWith(QLatin1Char('"')))
            protocol = protocol.mid(1, protocol.size() - 2).replace(QStringLiteral("\"\""),
                                                                    QStringLiteral("\""));
        bool visitOk = false;
        bool positionOk = false;
        const int visit = body.mid(firstComma + 1, lastComma - firstComma - 1).toInt(&visitOk);
        const int position = body.mid(lastComma + 1).toInt(&positionOk);
        if (!visitOk || !positionOk || visit <= 0 || position <= 0) continue;
        visitSonication_.insert(visitKey(protocol, visit), position);
        if (edited) editedVisits_.insert(visitKey(protocol, visit));
    }
}

// One key per visit of a protocol: visits are numbered per protocol, so the
// same number means different things under two of them.
QString WorkflowWindow::visitKey(const QString& protocol, int visit) {
    return QStringLiteral("%1|%2").arg(protocol).arg(visit);
}

// Only the visits that exist. A visit exists once it has been seeded, which
// happens when it is first selected, so the list is what has been opened rather
// than every number the log could hold.
// The visit a protocol opens on: where it was left if it has run at all,
// otherwise its first. Never a number carried over from another protocol.
int WorkflowWindow::visitForProtocol(const std::string& protocol) const {
    int latest = 0;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rows())
        if (row.protocol == protocol) latest = std::max(latest, row.visit);
    // An empty visit is still a visit. Looking only at rows walked past the
    // one the operator had just created and emptied, back to an older one.
    const QString prefix = QString::fromStdString(protocol) + QLatin1Char('|');
    for (const QString& key : editedVisits_) {
        if (!key.startsWith(prefix)) continue;
        bool ok = false;
        const int visit = key.mid(prefix.size()).toInt(&ok);
        if (ok) latest = std::max(latest, visit);
    }
    return latest > 0 ? latest : 1;
}

// The list is the visit history: every visit that exists, what state it is
// in, and one item that makes the next one. Two controls side by side could
// not say which numbers already existed.
void WorkflowWindow::refreshVisitCombo() {
    if (!visitNumberCombo_) return;
    const std::string protocol = currentProtocolName().toStdString();
    std::set<int> existing;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rows())
        if (row.protocol == protocol) existing.insert(row.visit);
    // A visit emptied by hand has no rows to be found by, and dropping it from
    // the list would leave the operator no way back to it.
    const QString prefix = currentProtocolName() + QLatin1Char('|');
    for (const QString& key : editedVisits_) {
        if (!key.startsWith(prefix)) continue;
        bool ok = false;
        const int visit = key.mid(prefix.size()).toInt(&ok);
        if (ok && visit > 0) existing.insert(visit);
    }
    const int current = currentVisitNumber();

    const QSignalBlocker blocker(visitNumberCombo_);
    visitNumberCombo_->clear();
    for (const int visit : existing) {
        const std::vector<beam::gui::TreatmentLogRow> rows =
            treatmentLog_.rowsFor(protocol, visit);
        int delivered = 0;
        for (const beam::gui::TreatmentLogRow& row : rows)
            if (beam::gui::TreatmentLog::delivered(row)) ++delivered;
        // Nothing in it yet is simply "Visit 3": saying it is empty tells the
        // operator what the blank table already shows them.
        const QString state =
            rows.empty() ? QString()
            : delivered == 0
                ? QStringLiteral(" -- %1 planned").arg(rows.size())
                : QStringLiteral(" -- %1 of %2 delivered").arg(delivered).arg(rows.size());
        visitNumberCombo_->addItem(QStringLiteral("Visit %1%2").arg(visit).arg(state), visit);
    }
    visitNumberCombo_->addItem(QStringLiteral("Create New Visit"), kNewVisitItem);
    const int index = visitNumberCombo_->findData(current);
    visitNumberCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

int WorkflowWindow::currentVisitNumber() const {
    if (visitNumberCombo_ == nullptr) return 1;
    bool ok = false;
    const int visit = visitNumberCombo_->currentData().toInt(&ok);
    return ok && visit > 0 ? visit : 1;
}

// A new visit is empty. It is filled by Add Protocol to Plan or Add Row, and
// that is the whole model: nothing creates rows behind the operator, so
// nothing has to guess later whether they were meant.
void WorkflowWindow::startNewVisit() {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    int next = 0;
    for (int visit = 1; visit <= kMaxTreatmentVisits && next == 0; ++visit)
        if (treatmentLog_.rowsFor(protocol, visit).empty() &&
            !editedVisits_.contains(visitKey(protocolName, visit)))
            next = visit;
    if (next == 0) {
        showMessage(QStringLiteral("All %1 visits of this protocol have been used.")
                        .arg(kMaxTreatmentVisits),
                    true);
        refreshVisitCombo();  // put the list back on the visit it was showing
        return;
    }
    // Known from here on, so it keeps its place in the list even while empty.
    markVisitEdited(protocolName, next);
    selectVisit(next);
    loadTreatmentProtocol(protocolName);
}

bool WorkflowWindow::visitExists(const QString& protocol, int visit) const {
    return !treatmentLog_.rowsFor(protocol.toStdString(), visit).empty() ||
           editedVisits_.contains(visitKey(protocol, visit));
}

// A visit that has delivered nothing is a plan, not a record, and a plan can
// be thrown away whole rather than a row at a time. The log refuses the
// moment anything in it has been fired.
void WorkflowWindow::deleteVisit(int visit) {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int rows = static_cast<int>(treatmentLog_.rowsFor(protocol, visit).size());
    if (!treatmentLog_.canRemoveVisit(protocol, visit)) {
        showMessage(QStringLiteral("Visit %1 has delivered sonications and cannot be deleted.")
                        .arg(visit),
                    true);
        return;
    }
    // Nothing here can be undone, and the whole visit goes at once.
    const QString question =
        rows == 0 ? QStringLiteral("Delete visit %1?").arg(visit)
                  : QStringLiteral("Delete visit %1 and its %2 planned sonication(s)?")
                        .arg(visit)
                        .arg(rows);
    if (QMessageBox::question(this, QStringLiteral("Delete visit"), question) !=
        QMessageBox::Yes)
        return;

    treatmentLog_.removeVisit(protocol, visit);
    const QString key = visitKey(protocolName, visit);
    editedVisits_.remove(key);
    visitSonication_.remove(key);
    saveVisitState();
    refreshVisitCombo();
    // Whatever is left of this protocol, or nothing -- and then the list is
    // back to its create row and the table is gone with the visit.
    selectVisit(visitForProtocol(protocol));
    loadTreatmentProtocol(protocolName);
    // Reload first: saving writes the table back into the log, and the table
    // still held the deleted visit until that reload.
    saveTreatmentTargetStore();
    showMessage(QStringLiteral("Visit %1 deleted.").arg(visit), false);
}

void WorkflowWindow::selectVisit(int visit) {
    if (visitNumberCombo_ == nullptr) return;
    const QSignalBlocker blocker(visitNumberCombo_);
    int index = visitNumberCombo_->findData(visit);
    // A visit just created is not in the list yet -- the rebuild that follows
    // puts it in its place. One that does not exist is not added at all.
    if (index < 0 && visitExists(currentProtocolName(), visit)) {
        visitNumberCombo_->insertItem(0, QStringLiteral("Visit %1").arg(visit), visit);
        index = 0;
    }
    if (index >= 0) visitNumberCombo_->setCurrentIndex(index);
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
    entry.startedAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
    entry.statusCode = beam::gui::TreatmentLog::kRunning;
    // BeamV0 writes into treatmentProtocolTable row i, where i is the Current
    // Sonication Number, so stepping back and re-firing replaces that row.
    entry.sequence = currentSonication_;
    openLogSequence_ = entry.sequence;
    const int shownSequence = entry.sequence;
    const QString sentParameters =
        QString::fromStdString(beam::gui::TreatmentLog::parameterString(entry));
    const QString sentTarget = QString::fromStdString(entry.target);
    const QString startedStamp = QString::fromStdString(entry.startedAt);
    treatmentLog_.record(std::move(entry));
    // The row said what was planned; from here it says what went out. Written
    // straight into the model rather than by reloading, which would rebuild
    // the sonication grid out from under the delivery in progress.
    if (protocolModel_ != nullptr) {
        std::vector<beam::gui_qt::TreatmentProtocolRow> shown = protocolModel_->rows();
        const std::size_t at = static_cast<std::size_t>(shownSequence) - 1;
        if (shownSequence >= 1 && at < shown.size()) {
            shown[at].parameters = sentParameters;
            shown[at].target = sentTarget;
            shown[at].amplitude = row.amplitude;
            shown[at].duration = row.endTime - row.startTime;
            // Start fills in now, not when the row closes: the operator is
            // watching this sonication run, and an empty cell until the end
            // reads as a row that never started.
            const QDateTime started = QDateTime::fromString(startedStamp, Qt::ISODateWithMs);
            shown[at].startedAt =
                started.isValid() ? started.toString(QStringLiteral("HH:mm:ss.zzz")) : QString();
            shown[at].startedAtFull = startedStamp;
            shown[at].endedAt.clear();
            shown[at].endedAtFull.clear();
            shown[at].statusCode = beam::gui::TreatmentLog::kRunning;
            protocolModel_->setRows(std::move(shown));
            protocolModel_->setCurrentSonication(shownSequence);
            placeSonicateButton(shownSequence);
        }
    }
    // Persist now -- a row that is never closed still has to survive a crash
    // mid-delivery -- but do not rebuild the table here: that reloads the
    // sonication grid out from under the row currently being fired. The
    // table catches up when the countdown closes the row.
    saveTreatmentTargetStore();
}

// The countdown reaching zero, or an abort, ends the row. An abort still leaves
// a row: it was delivered, just not for as long as planned.
// aborted: the operator stopped it part-way. The row stays -- energy went out
// -- and is marked, because what it delivered is not what it says it would.
void WorkflowWindow::closeOpenLogRow(bool aborted) {
    const std::string protocol = currentProtocolName().toStdString();
    const int visit = currentVisitNumber();
    if (openLogSequence_ > 0) {
        treatmentLog_.setStatus(protocol, visit, openLogSequence_,
                                aborted ? beam::gui::TreatmentLog::kStopped
                                        : beam::gui::TreatmentLog::kCompleted);
        // The status column carries the code; Notes carries what it means, so
        // the warning survives into the exported log where nobody is hovering
        // over a cell to read a tooltip. Prepended to whatever the operator
        // wrote rather than replacing it, and only once per row.
        if (aborted) {
            const beam::gui::TreatmentLogRow* row =
                treatmentLog_.find(protocol, visit, openLogSequence_);
            const std::string warning = "Stopped by operator before it finished (499).";
            if (row != nullptr && row->notes.find(warning) == std::string::npos) {
                const std::string kept = row->notes;
                treatmentLog_.setNotes(protocol, visit, openLogSequence_,
                                       kept.empty() ? warning : warning + " " + kept);
            }
        }
    }
    if (openLogSequence_ <= 0) return;
    treatmentLog_.close(protocol, visit, openLogSequence_,
                        QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString());
    openLogSequence_ = 0;
    // Reload before saving, not after. saveTreatmentTargetStore writes the
    // table's cells back into the log, so saving first pushed the table's empty
    // Notes over the warning set above; reloading first puts the warning into
    // the table, and the save then writes it back unchanged.
    loadTreatmentProtocol(currentProtocolName());
    saveTreatmentTargetStore();
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
// The next one to run, which is the next that has NOT been delivered --
// BeamV0's plain +1 steps onto a row that has already happened as soon as the
// operator has gone back and re-fired one.
void WorkflowWindow::advanceToNextScheduledSonication() {
    if (!protocolModel_) return;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& rows = protocolModel_->rows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].delivered) continue;
        selectScheduledSonication(static_cast<int>(i) + 1);
        return;
    }
    // Nothing left: the visit is done. Stay on the last row so the table does
    // not jump, and let selectScheduledSonication say so.
    selectScheduledSonication(static_cast<int>(rows.size()));
}

// Moves to one scheduled sonication: the number field, the highlighted row,
// and the target the grid is showing, which is what Sonicate sends.
// The row that fires next carries the control that fires it, so there is no
// reading across from a number to a row. It runs the same gated path as the
// Sonicate button on the Treatment stage -- runSonication refuses until the
// workflow allows treatment to begin -- so this is a second way to reach it,
// not a way around it.
// The gate the pane button used to carry: the stage model decides whether
// firing is available at all, and a sonication already running closes it.
// Half of the pulse. Called on each tick of the flash timer and whenever the
// button is rebuilt, so a row that changes under a running sonication comes
// back mid-pulse rather than going dark.
void WorkflowWindow::paintRunningButton() {
    QPushButton* const performing =
        runningSham_ ? inRowShamButton_.data() : inRowSonicateButton_.data();
    if (performing == nullptr) return;
    const char* const bright = runningSham_ ? kShamRunningStyle : kSonicatingStyle;
    const char* const dim = runningSham_ ? kShamRunningDimStyle : kSonicatingDimStyle;
    performing->setStyleSheet(QString::fromLatin1(runningFlashOn_ ? bright : dim));
    // The icon changes ink with the fill, or half the pulse is an empty button.
    const QColor ink = runningSham_
                           ? (runningFlashOn_ ? kInkOnYellow : kInkOnDimYellow)
                           : (runningFlashOn_ ? kInkOnBrightBlue : kInkOnDimBlue);
    performing->setIcon(runningSham_ ? makeShamIcon(18, ink) : makeSonicateIcon(18, ink));
}

// Three buttons with fixed jobs. Idle: send and sham are live, stop is dead.
// Running: the one being carried out pulses and is dead to the touch -- it is
// a readout now -- the other way of starting is dead too, and stop is the only
// thing that can be pressed.
void WorkflowWindow::setSonicateEnabled(bool enabled) {
    sonicateEnabled_ = enabled;
    if (inRowSonicateButton_.isNull()) return;
    const bool running =
        sonicationCountdownTimer_ != nullptr && sonicationCountdownTimer_->isActive();
    const bool live = !running && enabled;
    inRowSonicateButton_->setEnabled(live);
    if (!running)
        inRowSonicateButton_->setIcon(makeSonicateIcon(18, live ? kInkOnBlue : kInkOnDead));
    inRowSonicateButton_->setToolTip(running && !runningSham_
                                         ? QStringLiteral("Sonication in progress")
                                         : QStringLiteral("Sonicate this row"));
    if (!inRowShamButton_.isNull()) {
        inRowShamButton_->setEnabled(live);
        if (!running)
            inRowShamButton_->setIcon(makeShamIcon(18, live ? kInkOnYellow : kInkOnDead));
        inRowShamButton_->setToolTip(
            running && runningSham_
                ? QStringLiteral("Sham in progress")
                : QStringLiteral("Sham -- masking audio only, no ultrasound sent"));
    }
    if (!inRowStopButton_.isNull()) {
        inRowStopButton_->setEnabled(running);
        inRowStopButton_->setIcon(makeStopIcon(18, running ? kInkOnRed : kInkOnDead));
        inRowStopButton_->setToolTip(running ? QStringLiteral("Stop the sonication in progress")
                                             : QStringLiteral("Nothing is running"));
    }
    if (!inRowOnLamp_.isNull()) {
        inRowOnLamp_->setStyleSheet(QString::fromLatin1(
            !running ? kLampOffStyle
                     : (runningSham_ ? kLampShamStyle : kLampSonicatingStyle)));
        inRowOnLamp_->setText(
            !running ? lampText(QStringLiteral("#27343a"), QStringLiteral("#27343a"))
            : runningSham_ ? lampText(QStringLiteral("#ffd83d"), QStringLiteral("#ffeea8"))
                           : lampText(QStringLiteral("#3dff7a"), QStringLiteral("#a8ffc4")));
        inRowOnLamp_->setToolTip(running ? (runningSham_
                                               ? QStringLiteral("Sham in progress")
                                               : QStringLiteral("Sonication in progress"))
                                         : QStringLiteral("Nothing is being delivered"));
    }
    // Qt drops a mouse event aimed at a disabled widget before any policy or
    // filter sees it, so a right-click on a greyed-out button reached nothing
    // at all and the row under it could not be given a menu. Transparent while
    // disabled, the click falls through to the row, which is what a disabled
    // button should do anyway.
    const auto passThroughWhileDisabled = [](QPushButton* button) {
        if (button != nullptr)
            button->setAttribute(Qt::WA_TransparentForMouseEvents, !button->isEnabled());
    };
    passThroughWhileDisabled(inRowSonicateButton_);
    passThroughWhileDisabled(inRowShamButton_);
    passThroughWhileDisabled(inRowStopButton_);
    if (running) {
        paintRunningButton();
    } else {
        inRowSonicateButton_->setStyleSheet(QString::fromLatin1(kSonicateStyle));
        if (!inRowShamButton_.isNull())
            inRowShamButton_->setStyleSheet(QString::fromLatin1(kShamStyle));
    }
}

void WorkflowWindow::placeSonicateButton(int oneBased) {
    if (protocolView_ == nullptr || protocolModel_ == nullptr) return;
    const int rows = static_cast<int>(protocolModel_->rows().size());
    for (int row = 0; row < rows; ++row) {
        const QModelIndex cell =
            protocolModel_->index(row, beam::gui_qt::TreatmentProtocolTableModel::kAction);
        const bool wanted = row == oneBased - 1 &&
                            !protocolModel_->rows()[static_cast<std::size_t>(row)].delivered;
        if (!wanted) {
            if (protocolView_->indexWidget(cell) != nullptr) {
                // The pair widget owns both buttons, so dropping it takes them.
                protocolView_->setIndexWidget(cell, nullptr);
            }
            continue;
        }
        if (protocolView_->indexWidget(cell) != nullptr) continue;
        // Both ways of running this sonication, side by side on the row that is
        // about to run: ultrasound, or the mask alone.
        auto* pair = new QWidget(protocolView_);
        auto* row_layout = new QHBoxLayout(pair);
        row_layout->setContentsMargins(1, 0, 1, 0);
        row_layout->setSpacing(2);
        const auto makeRowButton = [this, pair](const char* style) {
            auto* button = new QPushButton(pair);
            button->setIconSize(QSize(18, 18));
            button->setFixedSize(32, 22);
            button->setCursor(Qt::PointingHandCursor);
            button->setStyleSheet(QString::fromLatin1(style));
            return button;
        };
        // Three buttons, each with one job it never swaps: send ultrasound,
        // play the mask alone, stop whichever is running. A button that
        // changes what it does is a button that gets pressed by mistake.
        auto* fire = makeRowButton(kSonicateStyle);
        auto* mask = makeRowButton(kShamStyle);
        auto* halt = makeRowButton(kStopStyle);
        fire->setIcon(makeSonicateIcon(18, kInkOnBlue));
        mask->setIcon(makeShamIcon(18, kInkOnYellow));
        halt->setIcon(makeStopIcon(18, kInkOnRed));
        connect(fire, &QPushButton::clicked, this, [this] { runSonication(/*sham=*/false); });
        connect(mask, &QPushButton::clicked, this, [this] { runSonication(/*sham=*/true); });
        connect(halt, &QPushButton::clicked, this, [this] { abortSonication(); });
        auto* lamp = new QLabel(pair);
        lamp->setTextFormat(Qt::RichText);
        lamp->setText(lampText(QStringLiteral("#27343a"), QStringLiteral("#27343a")));
        lamp->setAlignment(Qt::AlignCenter);
        lamp->setFixedSize(40, 22);
        lamp->setStyleSheet(QString::fromLatin1(kLampOffStyle));
        // Nothing to click: it reports, so clicks belong to the row behind it.
        lamp->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        row_layout->addWidget(fire);
        row_layout->addWidget(mask);
        row_layout->addWidget(halt);
        row_layout->addWidget(lamp);
        // These sit on top of the row's first cell, so without this a
        // right-click on them is swallowed by the button and the row that is
        // about to run is the one row with no menu.
        for (QWidget* over : {static_cast<QWidget*>(pair), static_cast<QWidget*>(fire),
                              static_cast<QWidget*>(mask), static_cast<QWidget*>(halt),
                              static_cast<QWidget*>(lamp)}) {
            over->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(over, &QWidget::customContextMenuRequested, this,
                    [this, over](const QPoint& at) {
                        showScheduleRowMenu(protocolView_->viewport()->mapFromGlobal(
                            over->mapToGlobal(at)));
                    });
        }
        protocolView_->setIndexWidget(cell, pair);
        protocolView_->setRowHeight(row, 27);
        inRowSonicateButton_ = fire;
        inRowShamButton_ = mask;
        inRowStopButton_ = halt;
        inRowOnLamp_ = lamp;
        setSonicateEnabled(sonicateEnabled_);  // paints them into whichever state applies
    }
}

void WorkflowWindow::selectScheduledSonication(int oneBased) {
    if (!protocolModel_) return;
    const int scheduled = static_cast<int>(protocolModel_->rows().size());
    if (scheduled == 0) {
        // An empty plan is a legitimate state, not a reason to leave the page
        // showing the last one: no row is current, nothing can be fired, and
        // the readout says what to do instead of a stale "2 of 3".
        currentSonication_ = 0;
        protocolModel_->setCurrentSonication(0);
        placeSonicateButton(0);
        if (currentSonicationNumberLabel_ != nullptr)
            currentSonicationNumberLabel_->setText(QStringLiteral("--"));
        return;
    }
    const int position = std::clamp(oneBased, 1, scheduled);

    currentSonication_ = position;
    if (protocolView_) protocolView_->selectRow(position - 1);
    // Every row delivered means there is no "next" one: marking a finished row
    // as current, and offering to fire it again, both say the visit still has
    // something to do.
    bool anyLeft = false;
    for (const beam::gui_qt::TreatmentProtocolRow& row : protocolModel_->rows())
        anyLeft = anyLeft || !row.delivered;
    // Marked the way setTreatmentProtocolTableData.m marks it; 0 marks nothing.
    protocolModel_->setCurrentSonication(anyLeft ? position : 0);
    placeSonicateButton(anyLeft ? position : 0);
    if (currentSonicationNumberLabel_ != nullptr)
        currentSonicationNumberLabel_->setText(
            anyLeft ? QStringLiteral("%1 of %2").arg(position).arg(scheduled)
                    : QStringLiteral("all %1 delivered").arg(scheduled));
    if (!anyLeft && !protocolModel_->rows().empty())
        setExecutionStatus(QStringLiteral("Visit %1 of \"%2\" is complete: all %3 sonications "
                                          "delivered. Add a sonication to continue, or finish "
                                          "the treatment.")
                               .arg(currentVisitNumber())
                               .arg(currentProtocolName())
                               .arg(protocolModel_->rows().size()));
    // Remembered against the visit, so coming back to one resumes where it was
    // rather than at its first row.
    visitSonication_.insert(visitKey(currentProtocolName(), currentVisitNumber()), position);
    saveVisitState();
    const QString target = protocolModel_->rows()[static_cast<std::size_t>(position) - 1].target;
    if (!targetListWidget_) return;
    int found = -1;
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        QListWidgetItem* item = targetListWidget_->item(i);
        const bool matches = found < 0 && targetListName(item) == target;
        item->setData(kCurrentTargetRole, matches);
        if (matches) found = i;
        if (QWidget* rowWidget = targetListWidget_->itemWidget(item)) {
            if (QLabel* label =
                    rowWidget->findChild<QLabel*>(QStringLiteral("targetNameLabel")))
                label->setStyleSheet(
                    matches ? QStringLiteral("QLabel { color: #ffffff; background: transparent; }")
                            : QStringLiteral("QLabel { color: #17313f; background: transparent; }"));
        }
    }
    targetListWidget_->viewport()->update();
    if (found >= 0 && targetListWidget_->currentRow() != found)
        targetListWidget_->setCurrentRow(found);
}

// Right-click a row to change what this VISIT runs, without touching the
// protocol: skip one that has not started, or add one after it. BeamV0 has no
// equivalent -- its table is fixed at 24 rows -- but a visit that has to drop
// or add a sonication otherwise has nowhere to record that it did.
void WorkflowWindow::showScheduleRowMenu(const QPoint& where) {
    if (!protocolView_ || !protocolModel_) return;
    // The position arrives in the VIEWPORT's coordinates, which is the frame
    // indexAt reads: a scroll area hands its viewport's context-menu event to
    // the view without translating it. Taking it for the view's and mapping it
    // put every menu on the row above the one clicked -- and on the last row of
    // a finished visit, the one row that can still take an insert, that looked
    // like a menu with nothing in it. The other reading is tried only when this
    // one lands nowhere.
    QPoint inViewport = where;
    QModelIndex index = protocolView_->indexAt(inViewport);
    if (!index.isValid()) {
        const QPoint mapped = protocolView_->viewport()->mapFrom(protocolView_, where);
        if (protocolView_->indexAt(mapped).isValid()) {
            inViewport = mapped;
            index = protocolView_->indexAt(mapped);
        }
    }
    // Two cases land here: the blank row an empty plan draws, and the space
    // below the last row of a plan that has some. Both mean the same thing --
    // put a row at the end -- and returning without a menu for the second was
    // why a one-row plan could not be given a second row: with one short row,
    // nearly all of the table is that space.
    if (!index.isValid() || protocolModel_->rows().empty()) {
        QMenu blank(this);
        QAction* const add = blank.addAction(QStringLiteral("Add Row"));
        QAction* const wholeProtocol =
            targetStore_.protocols().empty()
                ? nullptr
                : blank.addAction(QStringLiteral("Add Protocol to Plan"));
        const int here = currentVisitNumber();
        QAction* dropVisit = nullptr;
        if (visitExists(currentProtocolName(), here) &&
            treatmentLog_.canRemoveVisit(currentProtocolName().toStdString(), here)) {
            blank.addSeparator();
            dropVisit = blank.addAction(QStringLiteral("Delete Visit"));
        }
        QAction* const picked = blank.exec(protocolView_->viewport()->mapToGlobal(inViewport));
        if (picked == add) addRowToVisit();
        else if (picked != nullptr && picked == wholeProtocol) addProtocolToVisit(0);
        else if (picked != nullptr && picked == dropVisit) deleteVisit(here);
        return;
    }
    const int sequence = index.row() + 1;
    const std::string protocol = currentProtocolName().toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row = treatmentLog_.find(protocol, visit, sequence);
    if (row == nullptr) return;

    // A menu opened inside a selection acts on the selection and says how
    // much it is about to take, the way a document editor does. Opened on a
    // row outside it, it acts on that row alone.
    std::vector<int> block;
    if (protocolView_->selectionModel() != nullptr)
        for (const QModelIndex& picked : protocolView_->selectionModel()->selectedRows())
            block.push_back(picked.row() + 1);
    std::sort(block.begin(), block.end());
    block.erase(std::unique(block.begin(), block.end()), block.end());
    const bool wholeBlock =
        block.size() > 1 && std::find(block.begin(), block.end(), sequence) != block.end();

    QMenu menu(this);
    // Ask the log what it would accept rather than guessing: a row below this
    // one that has already run cannot be moved, so neither action is possible
    // even though THIS row looks free.
    const bool anyTarget = !targetStore_.targets().empty();
    const bool roomAfter = treatmentLog_.canInsertAfter(protocol, visit, sequence);
    const bool canInsert = roomAfter && anyTarget;
    const bool canAddProtocol = roomAfter && !targetStore_.protocols().empty();
    const bool canInsertAbove =
        treatmentLog_.canInsertBefore(protocol, visit, sequence) && anyTarget;
    const bool canSkip = treatmentLog_.canRemoveAt(protocol, visit, sequence);
    const bool fired = beam::gui::TreatmentLog::delivered(*row);
    const QString blocked =
        fired ? QStringLiteral("Sonication %1 has already been delivered.").arg(sequence)
              : QStringLiteral("A sonication below %1 has already been delivered, so the order "
                               "cannot change.")
                    .arg(sequence);

    // Only what can actually be done to this row. Above and below rather than
    // before and after a number: the menu opened on a row, and that row is
    // what the operator is pointing at.
    QAction* const insertAbove =
        canInsertAbove ? menu.addAction(QStringLiteral("Add Row Before")) : nullptr;
    QAction* const insert =
        canInsert ? menu.addAction(QStringLiteral("Add Row After")) : nullptr;
    QAction* const wholeProtocol =
        canAddProtocol ? menu.addAction(QStringLiteral("Add Protocol to Plan")) : nullptr;
    // The lowest row of a block is the one the log is strictest about: it
    // refuses while anything below it has been delivered, which covers every
    // row in the block.
    const bool canSkipBlock =
        wholeBlock && treatmentLog_.canRemoveAt(protocol, visit, block.front());
    QAction* const skip =
        wholeBlock
            ? (canSkipBlock
                   ? menu.addAction(QStringLiteral("Delete %1 Rows").arg(block.size()))
                   : nullptr)
            : (canSkip ? menu.addAction(QStringLiteral("Delete Row")) : nullptr);
    // The whole visit, when none of it has run. Below a separator: it takes
    // more than the rows the menu was opened on.
    QAction* dropVisit = nullptr;
    if (treatmentLog_.canRemoveVisit(protocol, visit)) {
        if (!menu.isEmpty()) menu.addSeparator();
        dropVisit = menu.addAction(QStringLiteral("Delete Visit"));
    }
    if (menu.isEmpty()) {
        // An empty menu on right-click reads as a broken control, so the one
        // entry says why there is nothing to offer.
        menu.addAction(blocked)->setEnabled(false);
    }

    QAction* chosen = menu.exec(protocolView_->viewport()->mapToGlobal(inViewport));
    if (chosen == nullptr) return;
    if (chosen == wholeProtocol) {
        // Reloads and saves on its own.
        addProtocolToVisit(sequence);
        return;
    }
    if (chosen == dropVisit) {
        deleteVisit(visit);
        return;
    }

    if (chosen == skip) {
        const std::vector<int> doomed = wholeBlock ? block : std::vector<int>{sequence};
        // Last one first: removing a row closes the gap and renumbers the ones
        // below it, so working upwards leaves every sequence still to come
        // where it was found.
        int gone = 0;
        for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
            if (treatmentLog_.removeAt(protocol, visit, *it)) ++gone;
        if (gone == 0) {
            showMessage(QStringLiteral("Row %1 cannot be deleted: already delivered.")
                            .arg(sequence),
                        true);
            return;
        }
        markVisitEdited(currentProtocolName(), visit);
        showMessage(gone == 1 ? QStringLiteral("Row %1 deleted.").arg(doomed.front())
                              : QStringLiteral("%1 rows deleted.").arg(gone),
                    false);
    } else if (chosen == insert || chosen == insertAbove) {
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
        const bool above = chosen == insertAbove;
        const bool placed =
            above ? treatmentLog_.insertBefore(protocol, visit, sequence, std::move(added))
                  : treatmentLog_.insertAfter(protocol, visit, sequence, std::move(added));
        if (!placed) {
            showMessage(QStringLiteral("Nothing can be inserted here: a sonication at or below %1 "
                                       "has already been delivered.")
                            .arg(sequence),
                        true);
            return;
        }
        markVisitEdited(currentProtocolName(), visit);
        showMessage(QStringLiteral("%1 added %2 sonication %3 for this visit. The protocol is "
                                   "unchanged.")
                        .arg(picked)
                        .arg(above ? QStringLiteral("before") : QStringLiteral("after"))
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

// setShamAudio.m ends with `sound(shamData, fs)` -- the masking track is meant
// to be heard, and a sham the participant cannot hear is not blinded. The port
// assembled the buffer and dropped it on the floor.
//
// PlaySound with SND_MEMORY rather than Qt Multimedia, which is not in this
// build's Qt. The buffer has to outlive the call because SND_ASYNC plays from
// it, so it is held on the window.
QByteArray WorkflowWindow::wavFromSamples(const std::vector<double>& samples, int sampleRate) {
    QByteArray wav;
    if (samples.empty() || sampleRate <= 0) return wav;

    // MATLAB's sound() clips to [-1, 1]; anything louder is scaled down rather
    // than wrapping round, which would turn a loud mask into noise.
    double peak = 0.0;
    for (const double sample : samples) peak = std::max(peak, std::abs(sample));
    const double scale = peak > 1.0 ? 1.0 / peak : 1.0;

    const quint32 frames = static_cast<quint32>(samples.size());
    const quint32 dataBytes = frames * 2;  // 16-bit mono
    QDataStream out(&wav, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(sampleRate)
        << quint32(sampleRate * 2) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (const double sample : samples) {
        const double clamped = std::clamp(sample * scale, -1.0, 1.0);
        out << static_cast<qint16>(std::lround(clamped * 32767.0));
    }
    return wav;
}

// PLATFORM SEAM. The playback call below is the only Windows-specific line in
// this application, and it is here because this build's Qt has no
// multimedia module. Everything above it -- assembling the mask, reading the
// burst, writing the WAV -- is portable. Adding Qt Multimedia would replace
// these few lines with a QAudioSink and nothing else would move.
void WorkflowWindow::playShamAudio(const std::vector<double>& samples, int sampleRate) {
    // Whatever is playing is reading the buffer this is about to replace, so it
    // has to be stopped before the old bytes go away.
    stopShamAudio();
    shamAudioBuffer_ = wavFromSamples(samples, sampleRate);
    if (shamAudioBuffer_.isEmpty()) return;
#ifdef _WIN32
    PlaySoundW(reinterpret_cast<LPCWSTR>(shamAudioBuffer_.constData()), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    // Said out loud rather than failing quietly: an unheard mask is an
    // unblinded sham, which is worse than a sham that refuses to pretend.
    showMessage(QStringLiteral("Masking audio cannot be played on this platform, so this "
                               "sham is not blinded."),
                true);
#endif
}

void WorkflowWindow::stopShamAudio() {
#ifdef _WIN32
    PlaySoundW(nullptr, nullptr, SND_PURGE);
#endif
    shamAudioBuffer_.clear();
}

// What is fired is the treatment table's row for the current sonication
// number, never the Target List selection. Clicking a target to look at it
// fills the grid with that target, and firing from there would send the wrong
// one and log it against this sonication. So the schedule is put back first.
// To change what runs here, insert or skip a row: that edits the visit's
// schedule, and the log says so.
bool WorkflowWindow::selectScheduledTarget() {
    if (!protocolModel_ || !targetListWidget_) return true;
    const int position = currentSonication_;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& rows = protocolModel_->rows();
    if (position < 1 || position > static_cast<int>(rows.size())) return true;
    const QString scheduled = rows[static_cast<std::size_t>(position) - 1].target;
    if (scheduled.isEmpty()) return true;
    if (targetListName(targetListWidget_->currentItem()) == scheduled) return true;
    for (int i = 0; i < targetListWidget_->count(); ++i) {
        if (targetListName(targetListWidget_->item(i)) != scheduled) continue;
        // Selecting it refills the grid from this target, which is what gets sent.
        targetListWidget_->setCurrentRow(i);
        setExecutionStatus(QStringLiteral("Sonication %1 runs \"%2\", as scheduled.")
                               .arg(position)
                               .arg(scheduled));
        return true;
    }
    setExecutionStatus(QStringLiteral("Sonication %1 schedules \"%2\", which the Target List does "
                                      "not define. Nothing was sent.")
                           .arg(position)
                           .arg(scheduled),
                       true);
    return false;
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
    // The treatment table decides what runs, not whatever is selected.
    if (!selectScheduledTarget()) return;
    const int shown = std::clamp(stimParamModel_->currentShownRow(), 1,
                                 static_cast<int>(stimParamModel_->rows().size()));
    const beam::gui_qt::StimParamRow& r = stimParamModel_->rows()[static_cast<std::size_t>(shown - 1)];
    const beam::safety::SonicationSafetyParams params =
        beam::gui::stimParamsFromTableRow(r.pd, r.pi, r.bd, r.bi, r.amplitude, r.startTime, r.endTime);

    if (sham) {
        const beam::gui::ShamSonicationOutcome outcome =
            beam::gui::prepareShamSonication(params, shamBurst().samples, shamBurst().sampleRateHz);
        // A sham sends nothing, so there is no transmission or duty cycle.
        playShamAudio(outcome.maskingAudio, static_cast<int>(shamBurst().sampleRateHz));
        recordSonicationStart(r, true, std::nullopt, std::nullopt);
        startSonicationCountdown(static_cast<int>(outcome.durationSeconds), /*sham=*/true);
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
    startSonicationCountdown(static_cast<int>(outcome.durationSeconds), /*sham=*/false);
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

void WorkflowWindow::startSonicationCountdown(int durationSeconds, bool sham) {
    runningSham_ = sham;
    if (runningFlashTimer_ == nullptr) {
        runningFlashTimer_ = new QTimer(this);
        runningFlashTimer_->setInterval(450);
        connect(runningFlashTimer_, &QTimer::timeout, this, [this] {
            runningFlashOn_ = !runningFlashOn_;
            paintRunningButton();
        });
    }
    runningFlashOn_ = true;
    runningFlashTimer_->start();
    // The bar takes the colour of the tick this row will end up with, so the
    // two say the same thing about the same sonication.
    treatmentExecutionProgressBar_->setStyleSheet(progressBarStyle(
        sham ? QColor(0xe0, 0xa3, 0x2e) : QColor(0x5e, 0xc4, 0x6a)));
    sonicationCountdownSeconds_ = std::max(durationSeconds, 0);
    sonicationDeadlineMs_ =
        QDateTime::currentMSecsSinceEpoch() + 1000LL * sonicationCountdownSeconds_;
    treatmentExecutionProgressBar_->setValue(0);
    setSonicateEnabled(false);
    sonicationCountdownLabel_->setText(
        QString::fromStdString(beam::gui::countdownDisplayText(sonicationCountdownSeconds_)));
    sonicationCountdownTimer_->start();
    // After the timer is running, so the button reads "Sonicating..." rather
    // than merely going grey.
    setSonicateEnabled(false);
}

// Port of the Abort Sonication action: the source sends the same
// "Correction" command the correction measurement uses, which stops output.
void WorkflowWindow::abortSonication() {
    sonicationCountdownTimer_->stop();
    if (runningFlashTimer_ != nullptr) runningFlashTimer_->stop();
    sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
    closeOpenLogRow(/*aborted=*/true);
    stopShamAudio();
    setSonicateEnabled(true);
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
