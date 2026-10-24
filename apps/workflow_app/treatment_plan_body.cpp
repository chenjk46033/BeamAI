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

#include "device_link_panel.hpp"
#include "workflow_window.hpp"

#include "treatment_library_dialog.hpp"
#include "example_image_view.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QFileInfo>
#include <vector>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QScreen>
#include <QWheelEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QUrl>
#include <QDialogButtonBox>
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
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpacerItem>
#include <QDoubleSpinBox>
#include <QScopeGuard>
#include <QSpinBox>
#include <algorithm>
#include <set>
#include <utility>
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
#include "safety/checks.hpp"
#include "safety/limits.hpp"
#include "gui/top_targets.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "serialcom/command.hpp"
#include "serialcom/link_monitor.hpp"
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
// Selection is transient pointer feedback, so it carries no hue: the table's
// hues all mean something (yellow and green a response, cyan the next row).
const QColor kRowSelectedFill(212, 221, 226);
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
    painter.end();
    QIcon icon(canvas);
    icon.addPixmap(canvas, QIcon::Disabled);
    return icon;
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
    "QPushButton:disabled { background: #9e2222; color: #f0cccc; border-color: #6e1313; }";

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
        using Model = beam::gui_qt::TreatmentProtocolTableModel;
        const QVariant fill = index.data(Model::kResponseFillRole);
        const bool coloured = fill.canConvert<QColor>() && fill.value<QColor>().isValid();
        // A selected row would otherwise paint its highlight over both.
        if (!coloured && !index.data(Model::kCurrentRowRole).toBool()) return;
        option->state &= ~QStyle::State_Selected;
        const QVariant background = index.data(Qt::BackgroundRole);
        if (background.canConvert<QColor>())
            option->backgroundBrush = QBrush(background.value<QColor>());
    }
};

class StimParamDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const QVariant background = index.data(Qt::BackgroundRole);
        if (!background.canConvert<QColor>()) return;
        option->state &= ~QStyle::State_Selected;
        option->backgroundBrush = QBrush(background.value<QColor>());
        const QVariant foreground = index.data(Qt::ForegroundRole);
        if (foreground.canConvert<QColor>()) {
            option->palette.setColor(QPalette::Text, foreground.value<QColor>());
            option->palette.setColor(QPalette::HighlightedText, foreground.value<QColor>());
        }
    }

public:
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        auto* spin = qobject_cast<QDoubleSpinBox*>(
            QStyledItemDelegate::createEditor(parent, option, index));
        if (spin == nullptr)
            return QStyledItemDelegate::createEditor(parent, option, index);
        using Model = beam::gui_qt::StimParamTableModel;
        switch (index.column()) {
            case Model::kX:
            case Model::kY:
            case Model::kZ:
                // Millimetres from the array centre, and they go both ways.
                spin->setDecimals(3);
                spin->setRange(-500.0, 500.0);
                spin->setSingleStep(1.0);
                break;
            case Model::kAmplitude:
                // Deliberately wider than the 3 MPa BeamV0 names as its
                // maximum: an amplitude over the limit is for the safety
                // check to report, not for the editor to silently refuse.
                spin->setDecimals(3);
                spin->setRange(0.0, 20.0);
                spin->setSingleStep(0.1);
                break;
            case Model::kStartTime:
            case Model::kEndTime:
                spin->setDecimals(3);
                spin->setRange(0.0, 3600.0);
                spin->setSingleStep(1.0);
                break;
            case Model::kBurstDuration:
            case Model::kBurstInterval:
                spin->setDecimals(4);
                spin->setRange(0.0, 3600.0);
                spin->setSingleStep(0.01);
                break;
            case Model::kPulseDuration:
            case Model::kPulseInterval:
                // 0.005 and 0.01 are the shipped defaults; five decimals
                // leaves room below them.
                spin->setDecimals(5);
                spin->setRange(0.0, 3600.0);
                spin->setSingleStep(0.001);
                break;
            default:
                spin->setDecimals(4);
                spin->setRange(-1.0e6, 1.0e6);
                break;
        }
        // Opens with the value selected, so typing replaces it rather than
        // landing in the middle of the digits already there.
        spin->selectAll();
        return spin;
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
    // Before the store: which patient's log is read depends on this.
    loadPatientRegistry();
    buildCaseDemographics();
    buildPatientChooser();
    refreshPatientChooser();
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
    stimParamView_->setObjectName(QStringLiteral("stimParamView"));
    // configureModelTable fits the log table's own editor; these columns are
    // seconds and millimetres, not responses.
    stimParamView_->setItemDelegate(new StimParamDelegate(stimParamView_));
    stimParamView_->setModel(stimParamModel_);
    configureModelTable(stimParamView_);
    stimParamView_->setItemDelegate(new StimParamDelegate(stimParamView_));
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
    // Hovering a parameter says what it shipped as, so an edited one can be
    // recognised without opening a file. Answered in eventFilter: the model
    // holds the operator's value and knows nothing about the installed one.
    stimParamView_->viewport()->installEventFilter(this);
    // The grid is exactly as tall as the rows it holds -- normally one -- so
    // the sub-tabs below start right under it instead of after a band of
    // empty white. updateStimGridHeight keeps that true as rows come and go.
    connect(stimParamModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateStimGridHeight();
        // Switching target refills the grid wholesale: these are different
        // numbers, and they get the same checks as typed ones.
        updateSonicationSafety();
    });
    connect(stimParamModel_, &QAbstractItemModel::rowsInserted, this, [this] { updateStimGridHeight(); });
    connect(stimParamModel_, &QAbstractItemModel::rowsRemoved, this, [this] { updateStimGridHeight(); });
    rightColumn->addWidget(stimParamView_);
    // Under the grid, because that is where the numbers that fail it are
    // typed. BeamV0 puts the same text in SystemStatusTextArea, a panel on
    // the far side of its window from the table it is about.
    sonicationSafetyLabel_ = new QLabel(bodyContent);
    sonicationSafetyLabel_->setWordWrap(true);
    sonicationSafetyLabel_->setTextFormat(Qt::RichText);
    // Worth copying: the numbers in it are what an operator quotes when they
    // ask someone about a refused sonication.
    sonicationSafetyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                   Qt::TextSelectableByKeyboard);
    sonicationSafetyLabel_->setCursor(Qt::IBeamCursor);
    rightColumn->addWidget(sonicationSafetyLabel_);
    // Separate from the banner above it: that one is about the sonication
    // being fired now, this one about the rest of the visit.
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
    // BeamV0 ranks after every delivery and offers no way out. The box is
    // the way out: ticked is BeamV0, unticked leaves the plan alone.
    autoRankCheckBox_ = new QCheckBox(QStringLiteral("Rank targets automatically"), protocolTab);
    autoRankCheckBox_->setObjectName(QStringLiteral("autoRankCheckBox"));
    autoRankCheckBox_->setChecked(true);
    autoRankCheckBox_->setStyleSheet(QStringLiteral("QCheckBox { color: #dcebef; }"));
    autoRankCheckBox_->setToolTip(
        QStringLiteral("After each sonication, point the treatment rows still to come at "
                       "whichever targets scored best in the screening rows above them. "
                       "Delivered rows are never changed. Untick to edit the plan yourself."));
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
    protocolSelectorRow->addSpacing(18);
    protocolSelectorRow->addWidget(autoRankCheckBox_);
    protocolSelectorRow->addStretch(1);
    protocolSelectorRow->addWidget(manageLibraryButton_);
    protocolSelectorRow->addWidget(manageTargetsButton_);
    // Also here: a visit is saved from where the operator is working, and once
    // treatment has started that is this page.
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
        const int widths[] = {112, 28, 78, 68, 72, 300, 100, 106, 86, 86, 56, 200};
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
    // Nothing in the plan says which rows screen and which treat -- it is read
    // off the durations. This says out loud what was read.
    planBlocksLabel_ = new QLabel(protocolTab);
    // Not wrapped: a wrapped label's height depends on its width, which this
    // layout settles only on a resize -- until then it is drawn half cut.
    planBlocksLabel_->setWordWrap(false);
    planBlocksLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    planBlocksLabel_->setStyleSheet(QStringLiteral("QLabel { color: #9fb0ba; padding: 2px 2px; }"));
    protocolLayout->addWidget(planBlocksLabel_, 0);
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
    // Reference anatomy is useless at thumbnail size, and this tab is read
    // rather than operated: while it is the one on show the tab area takes
    // the height it needs, and the MRI views above get it back on the way
    // out.
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
                // Colour only: the safety check marks the cells it blamed,
                // and that must not read as the operator having typed
                // something -- it would save the file and re-enter the check
                // that just ran.
                const bool presentationOnly =
                    !roles.isEmpty() &&
                    std::all_of(roles.begin(), roles.end(), [](int role) {
                        return role == Qt::BackgroundRole || role == Qt::ForegroundRole;
                    });
                if (presentationOnly) return;
                saveCurrentTargetSonications();
                saveTreatmentTargetStore();
                updateSonicationPlots();
                // The value just typed is the one most likely to be out of
                // range.
                updateSonicationSafety();
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
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                saveTreatmentTargetStore();
                // The plan row hands its Duration to the grid when the
                // sonication becomes current. Editing that Duration while it
                // already is current has to do the same, or the number the
                // operator just set sits in one table while the other -- the
                // one that fires -- keeps the old one.
                using Plan = beam::gui_qt::TreatmentProtocolTableModel;
                const bool touchedDuration = topLeft.column() <= Plan::kDuration &&
                                             bottomRight.column() >= Plan::kDuration;
                const int row = currentSonication_ - 1;
                if (touchedDuration && row >= topLeft.row() && row <= bottomRight.row())
                    applyScheduledDurationToGrid();
                // Queued: this rewrites the rows it was handed, and resetting
                // a model from inside its own signal is how a view ends up
                // reading an index that no longer exists.
                QMetaObject::invokeMethod(
                    this, [this] { precheckPendingSonications(); }, Qt::QueuedConnection);
            });
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
                // BeamV0 waits for the next Sonicate; by then the answer is the same.
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

QString WorkflowWindow::patientsPath() const {
    return QDir(treatmentDataDirectory()).filePath(QStringLiteral("patients.csv"));
}

// Where one person's record lives: their log, and where each of their visits
// was left. The protocol/target library is the install's and stays above this.
//
// Empty when nobody has been named on a device that has treated somebody
// before. That is the point: there is then no record to read or write, so
// whatever is on screen cannot be filed against a patient nobody has chosen.
// A device that has never named anybody keeps its record where a pre-patient
// install put it, which is what makes an upgrade a no-op until the first
// patient is identified.
QString WorkflowWindow::patientDataDirectory() const {
    if (currentPatientId_ <= 0)
        return patients_.empty() ? treatmentDataDirectory() : QString();
    const QString directory = QDir(treatmentDataDirectory())
                                  .filePath(QStringLiteral("patients/%1").arg(currentPatientId_));
    QDir().mkpath(directory);
    return directory;
}

// Who the patient is, as against which participant number they carry. Added
// to the Case page's form rather than a page of their own: it is one of the
// three things the operator fills in before treating somebody, and splitting
// it off would mean two places to look for the same answer.
void WorkflowWindow::buildCaseDemographics() {
    const auto add = [this](const QString& label, const QString& placeholder) {
        auto* edit = new QLineEdit(ui_->caseGroup);
        edit->setPlaceholderText(placeholder);
        styleInputField(edit);
        // Below Site ID, above the Accept button, which is the last row.
        ui_->caseForm->insertRow(ui_->caseForm->rowCount() - 1, label, edit);
        return edit;
    };
    lastNameEdit_ = add(QStringLiteral("Last name"), QStringLiteral("Optional"));
    firstNameEdit_ = add(QStringLiteral("First name"), QStringLiteral("Optional"));
    // The format in the label, not only the placeholder: a placeholder is
    // gone the moment anything is typed, which is exactly when it is needed.
    dateOfBirthEdit_ =
        add(QStringLiteral("Date of birth (YYYY-MM-DD)"), QStringLiteral("Optional"));
    // Typed plainly. An input mask returned "--" for an empty field, which
    // the validation rejected, discarding everything else typed.

    // The header carries the name once there is one -- it is what the
    // operator recognises, where the Participant ID is what a CRF carries.
    const auto follow = [this] {
        const beam::gui::Patient typed = caseDemographics();
        const QString shown = QString::fromStdString(beam::gui::patientDisplayName(typed));
        ui_->participantHeader->setText(
            shown.isEmpty() ? QStringLiteral("Participant: —")
                            : QStringLiteral("Participant: %1").arg(shown));
    };
    connect(lastNameEdit_, &QLineEdit::textChanged, this, follow);
    connect(firstNameEdit_, &QLineEdit::textChanged, this, follow);

    // Two steps, because they are two decisions. Creating a record and
    // leaving the page used to be one press, which gave the operator no
    // moment to check that the person they just entered is the person now in
    // the list -- and made the Case page appear to jump on its own.
    createPatientButton_ = new QPushButton(QStringLiteral("Create patient"), ui_->caseGroup);
    createPatientButton_->setStyleSheet(
        QStringLiteral("QPushButton { padding: 9px 16px; background: #2b4a58; color: #dcebef; "
                       "border: 1px solid #39758e; border-radius: 5px; font-weight: 600; } "
                       "QPushButton:hover { background: #35596a; }"));
    createPatientButton_->setToolTip(
        QStringLiteral("Opens this patient's record and adds them to the list below, without "
                       "leaving this page."));
    connect(createPatientButton_, &QPushButton::clicked, this, [this] {
        if (!caseFieldsUsable()) return;
        selectPatient(caseDemographics(), /*advancing=*/false);
    });

    // The Accept button moves out of the form's own column and alongside it.
    const int acceptRow = ui_->caseForm->rowCount() - 1;
    ui_->caseForm->takeRow(acceptRow);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(createPatientButton_, 1);
    buttons->addWidget(ui_->acceptCaseButton, 2);
    ui_->caseForm->addRow(buttons);

    // A session names its patient, so it opens a record like the two above.
    auto* loadSession = new QPushButton(QStringLiteral("Load session file…"), ui_->caseGroup);
    // The Accept button's blue: both open a record and leave this page.
    loadSession->setStyleSheet(
        QStringLiteral("QPushButton { padding: 11px 18px; background: #176b87; color: white; "
                       "border: 0; border-radius: 5px; font-weight: 600; outline: none; } "
                       "QPushButton:hover { background: #2083a3; } "
                       "QPushButton:focus { outline: none; border: 0; }"));
    loadSession->setMinimumHeight(38);
    loadSession->setToolTip(
        QStringLiteral("Opens a saved visit: its patient, their history, the MRI, the fiducials "
                       "and the registration. The fields above are not used."));
    connect(loadSession, &QPushButton::clicked, this, [this] { chooseTreatmentSession(); });
    ui_->caseForm->addRow(loadSession);
}

// The two required fields, and a date of birth that is blank or a date.
// Shared by both buttons so they cannot disagree about what is acceptable.
bool WorkflowWindow::caseFieldsUsable() {
    if (ui_->participantEdit->text().trimmed().isEmpty() ||
        ui_->siteEdit->text().trimmed().isEmpty()) {
        showMessage(QStringLiteral("Participant ID and Site ID are required."), true);
        return false;
    }
    std::string born;
    if (dateOfBirthEdit_ != nullptr && !readDateOfBirth(dateOfBirthEdit_->text(), &born)) {
        showMessage(QStringLiteral("\"%1\" is not a date. Use YYYY-MM-DD, or leave the date of "
                                   "birth blank.")
                        .arg(dateOfBirthEdit_->text().trimmed()),
                    true);
        dateOfBirthEdit_->setFocus(Qt::OtherFocusReason);
        dateOfBirthEdit_->selectAll();
        return false;
    }
    // Show what was stored rather than what was typed: "1999-1-1" becomes
    // "1999-01-01", so the operator can see the date was understood.
    if (dateOfBirthEdit_ != nullptr && !born.empty())
        dateOfBirthEdit_->setText(QString::fromStdString(born));
    return true;
}

void WorkflowWindow::showCaseDemographics(const beam::gui::Patient& patient) {
    if (lastNameEdit_ == nullptr) return;
    ui_->participantEdit->setText(QString::fromStdString(patient.externalId));
    ui_->siteEdit->setText(QString::fromStdString(patient.site));
    lastNameEdit_->setText(QString::fromStdString(patient.lastName));
    firstNameEdit_->setText(QString::fromStdString(patient.firstName));
    dateOfBirthEdit_->setText(QString::fromStdString(patient.dateOfBirth));
}

// Blank, or YYYY-MM-DD. Anything else is a typo and is reported rather than
// quietly dropped -- a date of birth nobody notices failing to save is worse
// than one that refuses.
bool WorkflowWindow::readDateOfBirth(const QString& text, std::string* out) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        out->clear();
        return true;
    }
    // Leading zeros are not information, and slashes are accepted too. What
    // is STORED is always yyyy-MM-dd, so the column sorts.
    for (const QString& format : {QStringLiteral("yyyy-M-d"), QStringLiteral("yyyy/M/d"),
                                  QStringLiteral("yyyyMMdd")}) {
        const QDate parsed = QDate::fromString(trimmed, format);
        if (!parsed.isValid()) continue;
        *out = parsed.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
        return true;
    }
    return false;
}

beam::gui::Patient WorkflowWindow::caseDemographics() const {
    beam::gui::Patient typed;
    typed.externalId = ui_->participantEdit->text().trimmed().toStdString();
    typed.site = ui_->siteEdit->text().trimmed().toStdString();
    if (lastNameEdit_ == nullptr) return typed;
    typed.lastName = lastNameEdit_->text().trimmed().toStdString();
    typed.firstName = firstNameEdit_->text().trimmed().toStdString();
    readDateOfBirth(dateOfBirthEdit_->text(), &typed.dateOfBirth);
    return typed;
}

// Who this device has treated, on the page where the operator says which of
// them is in front of them. Typing the exact Participant ID was the only way
// in, and a near miss quietly opened a second record for the same person.
void WorkflowWindow::buildPatientChooser() {
    patientChooserGroup_ = new QGroupBox(QStringLiteral("Patients on this device"),
                                         ui_->casePage);
    auto* layout = new QVBoxLayout(patientChooserGroup_);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    // A device that has treated two hundred people cannot be read down. The
    // filter is the only way to find somebody without scrolling, and the
    // count says whether what is on screen is all of them.
    auto* search = new QHBoxLayout;
    search->setSpacing(8);
    patientFilterEdit_ = new QLineEdit(patientChooserGroup_);
    patientFilterEdit_->setPlaceholderText(
        QStringLiteral("Find by Participant ID, name or site"));
    patientFilterEdit_->setClearButtonEnabled(true);
    styleInputField(patientFilterEdit_);
    connect(patientFilterEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPatientChooser(); });
    search->addWidget(patientFilterEdit_, 1);
    patientCountLabel_ = new QLabel(patientChooserGroup_);
    patientCountLabel_->setStyleSheet(QStringLiteral("color: #7e8c93;"));
    search->addWidget(patientCountLabel_, 0);
    layout->addLayout(search);

    patientChooserHint_ = new QLabel(patientChooserGroup_);
    patientChooserHint_->setWordWrap(true);
    patientChooserHint_->setStyleSheet(QStringLiteral("color: #7e8c93;"));
    // Fixed, both of them: a QVBoxLayout hands spare height to whatever will
    // take it, and with the table at a fixed height that was the hint label,
    // which floated in the middle of an empty box.
    patientChooserHint_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    patientChooserGroup_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(patientChooserHint_);

    patientTable_ = new QTableWidget(0, 7, patientChooserGroup_);
    patientTable_->setHorizontalHeaderLabels({QStringLiteral("Participant ID"),
                                              QStringLiteral("Name"),
                                              QStringLiteral("Date of birth"),
                                              QStringLiteral("Site"),
                                              QStringLiteral("Visits"),
                                              QStringLiteral("Created"),
                                              QStringLiteral("Last treated")});
    patientTable_->verticalHeader()->setVisible(false);
    patientTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    patientTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Click a header to sort; the list opens in registry order, which is the
    // order patients were first seen.
    patientTable_->setSortingEnabled(true);
    patientTable_->horizontalHeader()->setSortIndicatorShown(true);
    // A deliberate opening order rather than whatever Qt last had.
    patientTable_->sortByColumn(0, Qt::AscendingOrder);
    patientTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    patientTable_->setAlternatingRowColors(true);
    patientTable_->setShowGrid(false);
    // Its own colours, not the registration table's: that one is styled for a
    // widget with a fixed height on a pane of its own, and borrowing it left
    // every second row drawn in the dark palette with dark text.
    patientTable_->setStyleSheet(QStringLiteral(
        "QTableWidget { color: #17313f; background: #ffffff; border: 1px solid #39758e; } "
        "QTableWidget::item { color: #17313f; background: #ffffff; padding: 3px 8px; } "
        "QTableWidget::item:alternate { color: #17313f; background: #eef4f7; } "
        "QTableWidget::item:selected, QTableWidget::item:selected:!active "
        "{ background: #58c7e5; color: #083d4d; }"));
    patientTable_->horizontalHeader()->setStyleSheet(QStringLiteral(
        "QHeaderView::section { padding: 3px 8px; margin: 0px; color: #17313f; "
        "background: #dce6ea; border: 0; border-bottom: 1px solid #39758e; }"));
    // Every column as wide as it needs to be, grouped at the left, with the
    // last one absorbing the slack. Stretching Participant ID instead put a
    // hand's width between the name and everything said about it.
    for (int column = 0; column < patientTable_->columnCount(); ++column)
        patientTable_->horizontalHeader()->setSectionResizeMode(column,
                                                                QHeaderView::ResizeToContents);
    patientTable_->horizontalHeader()->setStretchLastSection(true);
    // Left, not Qt's centred default: the last column is the stretched one,
    // so a centred label floats a hand's width from the data beneath it.
    patientTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(patientTable_);

    // Selecting fills the fields rather than accepting outright: the operator
    // still presses Accept, and still sees what they are about to open.
    // By the id carried on the row, never by the row number: the list can be
    // filtered and sorted, so position stopped meaning registry order.
    const auto take = [this](int row) {
        beam::gui::Patient chosen;
        if (!patients_.find(patientIdAtRow(row), &chosen)) return;
        showCaseDemographics(chosen);
    };
    connect(patientTable_, &QTableWidget::currentCellChanged, this,
            [take](int row, int, int, int) { take(row); });
    connect(patientTable_, &QTableWidget::cellDoubleClicked, this,
            [this, take](int row, int) {
                take(row);
                ui_->acceptCaseButton->click();
            });

    patientTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(patientTable_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& where) {
                // Viewport coordinates, not the view's: the header is above
                // the point this reports from.
                const QModelIndex at = patientTable_->indexAt(where);
                if (!at.isValid()) return;
                showPatientMenu(at.row(), patientTable_->viewport()->mapToGlobal(where));
            });

    // Under the identity form, above the spacer that holds the page open. No
    // stretch: the list is as tall as it needs to be and the spacer below it
    // takes the rest, rather than the table filling the screen with blank rows.
    ui_->casePageLayout->insertWidget(1, patientChooserGroup_, 0);
}

// The registry id carried on a row, or 0. Rows are filtered and sorted, so
// their position says nothing about which patient they are; looking one up by
// row number would open whoever happened to be in that seat.
int WorkflowWindow::patientIdAtRow(int row) const {
    if (patientTable_ == nullptr || row < 0 || row >= patientTable_->rowCount()) return 0;
    const QTableWidgetItem* item = patientTable_->item(row, 0);
    return item == nullptr ? 0 : item->data(Qt::UserRole).toInt();
}

void WorkflowWindow::refreshPatientChooser() {
    if (patientTable_ == nullptr) return;
    const std::vector<beam::gui::Patient>& people = patients_.patients();
    patientChooserGroup_->setVisible(!people.empty());
    patientChooserHint_->setText(
        people.empty()
            ? QString()
            : QStringLiteral("Pick the patient in front of you, or type a new Participant ID "
                             "above to start a record. Each patient's visits are kept "
                             "separately."));

    // Whoever the filter admits. Matched against the Participant ID, the
    // name and the site, because those are the three things an operator has
    // in front of them when they are looking somebody up.
    const QString needle =
        patientFilterEdit_ == nullptr ? QString() : patientFilterEdit_->text().trimmed();
    std::vector<const beam::gui::Patient*> shown;
    for (const beam::gui::Patient& patient : people) {
        if (!needle.isEmpty()) {
            const QString haystack =
                QStringLiteral("%1 %2 %3 %4")
                    .arg(QString::fromStdString(patient.externalId),
                         QString::fromStdString(patient.lastName),
                         QString::fromStdString(patient.firstName),
                         QString::fromStdString(patient.site));
            if (!haystack.contains(needle, Qt::CaseInsensitive)) continue;
        }
        shown.push_back(&patient);
    }

    const QSignalBlocker blocker(patientTable_);
    // Sorting is off while the rows are written: with it on, each setItem can
    // move the row being filled out from under the next one.
    patientTable_->setSortingEnabled(false);
    patientTable_->setRowCount(static_cast<int>(shown.size()));
    for (std::size_t index = 0; index < shown.size(); ++index) {
        const beam::gui::Patient& patient = *shown[index];
        // Their own log, read only to count what is in it. A log that will
        // not parse costs this row its two computed columns, not the page.
        int visits = 0;
        QString lastTreated;
        try {
            beam::gui::TreatmentLog log;
            log.load(QDir(QDir(treatmentDataDirectory())
                              .filePath(QStringLiteral("patients/%1").arg(patient.id)))
                         .filePath(QStringLiteral("treatment_log.csv"))
                         .toStdString());
            std::set<std::pair<std::string, int>> seen;
            for (const beam::gui::TreatmentLogRow& row : log.rows()) {
                seen.insert({row.protocol, row.visit});
                if (row.startedAt.empty()) continue;
                const QString stamp = QString::fromStdString(row.startedAt);
                if (stamp > lastTreated) lastTreated = stamp;
            }
            visits = static_cast<int>(seen.size());
        } catch (const std::exception&) {
            lastTreated = QStringLiteral("unreadable");
        }

        const auto cell = [](const QString& text, bool rightAlign = false) {
            auto* item = new QTableWidgetItem(text);
            if (rightAlign) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        // Both stamps to the minute: the date alone cannot tell two visits on
        // one day apart, and the seconds are noise in a column.
        const auto toMinute = [](const QString& stamp) {
            if (stamp.isEmpty()) return QStringLiteral("—");
            QString shown = stamp.left(16);
            shown.replace(QLatin1Char('T'), QLatin1Char(' '));
            return shown;
        };
        const int row = static_cast<int>(index);
        auto* identity = cell(QString::fromStdString(patient.externalId));
        // What the row IS, as against where it sits.
        identity->setData(Qt::UserRole, patient.id);
        patientTable_->setItem(row, 0, identity);
        patientTable_->setItem(
            row, 1,
            cell(patient.lastName.empty() && patient.firstName.empty()
                     ? QStringLiteral("—")
                     : QString::fromStdString(beam::gui::patientDisplayName(patient))));
        // Masked, not shown: the list is a lookup aid on a screen other
        // people can see, and a full date of birth identifies somebody. The
        // record itself keeps it, and the editor is where it is read.
        patientTable_->setItem(row, 2,
                               cell(patient.dateOfBirth.empty()
                                        ? QStringLiteral("—")
                                        : QString::fromUtf8("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                                                            "\xe2\x80\xa2-\xe2\x80\xa2\xe2\x80\xa2"
                                                            "-\xe2\x80\xa2\xe2\x80\xa2")));
        patientTable_->setItem(row, 3, cell(QString::fromStdString(patient.site)));
        auto* visitCell = cell(QString::number(visits), true);
        // Sorted as a number, not as text, or 10 would come before 2.
        visitCell->setData(Qt::EditRole, visits);
        patientTable_->setItem(row, 4, visitCell);
        patientTable_->setItem(row, 5,
                               cell(toMinute(QString::fromStdString(patient.createdAt))));
        patientTable_->setItem(row, 6,
                               cell(lastTreated == QStringLiteral("unreadable")
                                        ? lastTreated
                                        : toMinute(lastTreated)));
        // The one being treated now, so the page says which record is open.
        if (patient.id == currentPatientId_) {
            for (int column = 0; column < patientTable_->columnCount(); ++column) {
                QTableWidgetItem* item = patientTable_->item(row, column);
                QFont bold = item->font();
                bold.setBold(true);
                item->setFont(bold);
            }
        }
    }
    patientTable_->setSortingEnabled(true);
    if (patientCountLabel_ != nullptr) {
        patientCountLabel_->setText(
            shown.size() == people.size()
                ? QStringLiteral("%1 patient(s)").arg(people.size())
                : QStringLiteral("%1 of %2 patient(s)").arg(shown.size()).arg(people.size()));
    }
    // As tall as its rows, capped: a device with many patients scrolls rather
    // than pushing the Accept button off the page.
    const int rowHeight = patientTable_->verticalHeader()->defaultSectionSize();
    const int wanted = patientTable_->horizontalHeader()->height() +
                       static_cast<int>(shown.size()) * rowHeight +
                       2 * patientTable_->frameWidth();
    patientTable_->setFixedHeight(std::clamp(wanted, rowHeight * 2, rowHeight * 12));
}

// How many sonications this patient has actually had. The gate on removing
// them: a patient who has been sonicated is a record, and no later decision
// erases that. Planned-but-unfired rows do not count -- nothing happened to
// them -- but they are worth naming before they go.
int WorkflowWindow::patientDeliveryCount(int patientId, int* plannedRows) const {
    int delivered = 0;
    if (plannedRows != nullptr) *plannedRows = 0;
    try {
        beam::gui::TreatmentLog log;
        log.load(QDir(QDir(treatmentDataDirectory())
                          .filePath(QStringLiteral("patients/%1").arg(patientId)))
                     .filePath(QStringLiteral("treatment_log.csv"))
                     .toStdString());
        for (const beam::gui::TreatmentLogRow& row : log.rows()) {
            if (beam::gui::TreatmentLog::delivered(row)) ++delivered;
            else if (plannedRows != nullptr) ++*plannedRows;
        }
    } catch (const std::exception&) {
        // A log that will not parse is not a log that can be proved empty.
        // Counting it as delivered refuses the removal, which is the safe way
        // round for a file that may hold a treatment nobody can read.
        return 1;
    }
    return delivered;
}

void WorkflowWindow::showPatientMenu(int row, const QPoint& globalPosition) {
    beam::gui::Patient patient;
    if (!patients_.find(patientIdAtRow(row), &patient)) return;
    const QString name = QString::fromStdString(patient.externalId);

    QMenu menu(this);
    QAction* edit = menu.addAction(QStringLiteral("Edit \"%1\" details…").arg(name));
    QAction* remove = menu.addAction(QStringLiteral("Delete \"%1\"").arg(name));

    int planned = 0;
    const int delivered = patientDeliveryCount(patient.id, &planned);
    const bool open = patient.id == currentPatientId_;
    remove->setEnabled(delivered == 0 && !open);
    if (open) {
        remove->setToolTip(QStringLiteral("This is the patient being treated. Open somebody "
                                          "else before deleting them."));
    } else if (delivered > 0) {
        remove->setToolTip(QStringLiteral("%1 has had %2 sonication(s). What a patient was "
                                          "given is a record and is not deleted here.")
                               .arg(name)
                               .arg(delivered));
    }
    // A disabled item says nothing on its own; the reason has to be reachable.
    menu.setToolTipsVisible(true);

    QAction* chosen = menu.exec(globalPosition);
    if (chosen == edit) editPatient(patient.id);
    else if (chosen == remove) deletePatient(patient.id, planned);
}

// Everything the operator can change about a patient, in one dialog. Two
// chained input boxes could not show what was already on file while the
// second was open, and could not reach demographics at all.
void WorkflowWindow::editPatient(int patientId) {
    beam::gui::Patient patient;
    if (!patients_.find(patientId, &patient)) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Patient details"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    const auto field = [&](const QString& label, const std::string& value,
                           const QString& placeholder) {
        auto* edit = new QLineEdit(QString::fromStdString(value), &dialog);
        edit->setPlaceholderText(placeholder);
        styleInputField(edit);
        form->addRow(label, edit);
        return edit;
    };
    QLineEdit* identifier =
        field(QStringLiteral("Participant ID *"), patient.externalId, QStringLiteral("Required"));
    QLineEdit* site = field(QStringLiteral("Site ID"), patient.site, QString());
    QLineEdit* lastName =
        field(QStringLiteral("Last name"), patient.lastName, QStringLiteral("Optional"));
    QLineEdit* firstName =
        field(QStringLiteral("First name"), patient.firstName, QStringLiteral("Optional"));
    QLineEdit* born = field(QStringLiteral("Date of birth (YYYY-MM-DD)"), patient.dateOfBirth,
                            QStringLiteral("Optional"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Looped, so a field that will not do sends the operator back to the
    // dialog with everything they typed still in it. Returning instead threw
    // the whole edit away over one bad date, which is how a name typed into
    // this dialog could vanish without the operator being sure it had.
    beam::gui::Patient edited;
    while (true) {
        if (dialog.exec() != QDialog::Accepted) return;

        edited = beam::gui::Patient{};
        edited.externalId = identifier->text().trimmed().toStdString();
        edited.site = site->text().trimmed().toStdString();
        edited.lastName = lastName->text().trimmed().toStdString();
        edited.firstName = firstName->text().trimmed().toStdString();
        if (!readDateOfBirth(born->text(), &edited.dateOfBirth)) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Date of birth"),
                QStringLiteral("\"%1\" is not a date. Use YYYY-MM-DD, or leave it blank.")
                    .arg(born->text().trimmed()));
            born->setFocus(Qt::OtherFocusReason);
            born->selectAll();
            continue;
        }
        // Normalised, so the dialog shows what the record will hold.
        born->setText(QString::fromStdString(edited.dateOfBirth));
        if (!patients_.update(patientId, edited)) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Participant ID"),
                QStringLiteral("\"%1\" could not be used: it is blank, or it already belongs "
                               "to another patient on this device.")
                    .arg(identifier->text().trimmed()));
            identifier->setFocus(Qt::OtherFocusReason);
            identifier->selectAll();
            continue;
        }
        break;
    }
    savePatientRegistry();
    // The open patient's log carries their identifier, and the Case page
    // carries all five fields; both are now out of date.
    if (patientId == currentPatientId_) {
        treatmentLog_.setPatient(edited.externalId, edited.site,
                                 edited.lastName.empty() && edited.firstName.empty()
                                     ? std::string()
                                     : beam::gui::patientDisplayName(edited));
        beam::gui::Patient shown = edited;
        shown.id = patientId;
        showCaseDemographics(shown);
        saveTreatmentTargetStore(/*announce=*/false);
    }
    refreshPatientChooser();
    showMessage(QStringLiteral("Patient %1 saved as \"%2\" (%3).")
                    .arg(patientId)
                    .arg(QString::fromStdString(edited.externalId),
                         QString::fromStdString(beam::gui::patientDisplayName(edited))),
                false);
}

void WorkflowWindow::deletePatient(int patientId, int plannedRows) {
    beam::gui::Patient patient;
    if (!patients_.find(patientId, &patient)) return;
    const QString name = QString::fromStdString(patient.externalId);
    // Checked again here, not only when the menu was built: the menu may have
    // been open while something else was written.
    if (patientId == currentPatientId_ || patientDeliveryCount(patientId, nullptr) > 0) {
        showMessage(QStringLiteral("\"%1\" cannot be deleted.").arg(name), true);
        return;
    }

    QMessageBox ask(QMessageBox::Warning, QStringLiteral("Delete patient"),
                    QStringLiteral("Delete \"%1\" from this device?").arg(name),
                    QMessageBox::NoButton, this);
    ask.setInformativeText(
        plannedRows > 0
            ? QStringLiteral("They have had no sonications, but %1 planned row(s) will go with "
                             "them. This cannot be undone.")
                  .arg(plannedRows)
            : QStringLiteral("They have had no sonications. This cannot be undone."));
    QPushButton* confirm = ask.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    QPushButton* keep = ask.addButton(QStringLiteral("Keep"), QMessageBox::RejectRole);
    // Return on a delete dialog keeps them.
    ask.setDefaultButton(keep);
    ask.exec();
    if (ask.clickedButton() != confirm) return;

    // The directory goes with the row. Built from the id rather than taken
    // from anywhere, and removed only after it is confirmed to be the one
    // under this install's patients/ -- a recursive delete is worth being
    // sure about.
    const QDir runtime(treatmentDataDirectory());
    const QString directory = runtime.filePath(QStringLiteral("patients/%1").arg(patientId));
    const bool looksRight =
        QDir(directory).dirName() == QString::number(patientId) &&
        QFileInfo(QDir(directory).filePath(QStringLiteral(".."))).canonicalFilePath() ==
            QFileInfo(runtime.filePath(QStringLiteral("patients"))).canonicalFilePath();
    if (QFileInfo::exists(directory)) {
        // Refuse rather than carry on: dropping the registry row while the
        // directory stays would leave files nothing can account for.
        if (!looksRight || !QDir(directory).removeRecursively()) {
            showMessage(QStringLiteral("%1 could not be removed, so \"%2\" was left on the "
                                       "device rather than leaving their files behind.")
                            .arg(QDir::toNativeSeparators(directory), name),
                        true);
            return;
        }
    }
    patients_.remove(patientId);
    savePatientRegistry();
    refreshPatientChooser();
    showMessage(QStringLiteral("\"%1\" was deleted. Patient numbers are not reused, so nobody "
                               "else is given their place.")
                    .arg(name),
                false);
}

void WorkflowWindow::loadPatientRegistry() {
    try {
        patients_.load(patientsPath().toStdString());
    } catch (const std::exception& error) {
        // A list of patients that will not parse is not something to carry on
        // past quietly: every record below it is addressed by an id from it.
        showMessage(QStringLiteral("The patient list could not be read (%1). No patient record "
                                   "will be opened until it is fixed.")
                        .arg(error.what()),
                    true);
    }
}

void WorkflowWindow::savePatientRegistry() {
    try {
        patients_.save(patientsPath().toStdString());
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("The patient list could not be saved: %1").arg(error.what()),
                    true);
    }
}

// The Case page is where the operator says who is being treated, so accepting
// it is where that person's record is opened. BeamV0 had no such step: the
// subject was whoever the data directory happened to hold, and treating a
// second person meant copying the whole directory.
void WorkflowWindow::selectPatient(const beam::gui::Patient& details, bool advancing) {
    const std::string identifier = details.externalId;
    if (identifier.empty()) return;

    beam::gui::Patient known;
    const bool onFile = patients_.findByExternalId(identifier, &known);
    if (onFile && known.id == currentPatientId_) {
        // The same person, re-accepted. Nothing to do but say so -- their
        // details are theirs, and the form may hold anything.
        showCaseDemographics(known);
        return;
    }
    const bool switching = currentPatientId_ > 0;
    // A sonication that is going out belongs to the patient it was started
    // on. Changing who that is part-way through would file it against the
    // wrong person and leave its row open in a log nothing is writing to.
    if (switching && deliveryRunning()) {
        // The header follows the field as it is typed, so leaving the new
        // identifier in it would name a patient who is not the one being
        // sonicated.
        beam::gui::Patient open;
        if (patients_.find(currentPatientId_, &open)) showCaseDemographics(open);
        showMessage(QStringLiteral("A sonication is still going out. Stop it before changing "
                                   "which patient is being treated."),
                    true);
        return;
    }
    // Whoever is open is written out before anybody else is read in, so a
    // switch cannot carry one patient's rows into another's file.
    if (switching) savePatientData();

    beam::gui::Patient patient;
    try {
        patient = patients_.add(details);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("This patient could not be recorded: %1").arg(error.what()),
                    true);
        return;
    }
    currentPatientId_ = patient.id;
    savePatientRegistry();
    // The record may hold details the Case page was not told, which is what
    // picking an existing patient and pressing Accept should show.
    showCaseDemographics(patient);
    refreshPatientChooser();
    adoptLegacyPatientData(patient.id);

    // Before the plan is rebuilt, not after: the rebuild reads the array
    // position off the Target List, so clearing it afterwards would leave the
    // last patient's X/Y/Z in the row that was just built.
    if (switching) {
        // Everything from the MRI down belongs to the person it was measured
        // on. The stages are invalidated AND the data behind them dropped, or
        // the last patient's head stays on screen.
        workflow_.change(beam::gui::WorkflowStage::Imaging,
                         "A different patient is being treated; this patient's imaging, "
                         "registration and coupling have not been done.");
        clearPatientImagingAndRegistration();
        clearCouplingMeasurement();
    }
    loadPatientData();
    if (switching) refresh();

    // Says what just happened and what the page it lands on is for; otherwise
    // the jump to Imaging reads as unexplained.
    const QString who = QStringLiteral("%1 (%2)").arg(
        QString::fromStdString(beam::gui::patientDisplayName(patient)),
        QString::fromStdString(patient.externalId));
    const QString what =
        onFile ? QStringLiteral("Opened the existing record for %1 — patient %2.")
                     .arg(who)
                     .arg(patient.id)
               : QStringLiteral("Created a new record for %1 — patient %2 on this device.")
                     .arg(who)
                     .arg(patient.id);
    const QString next =
        advancing ? QStringLiteral(" Load their MRI to begin.")
                  : QStringLiteral(" They are in the list below. Accept the case when the "
                                   "record is right.");
    showMessage(what + next, false);
}

// A pre-patient install kept one subject's record in the runtime directory;
// the first patient named on it is that subject. A file that cannot be moved
// is left where it is and said so rather than lost to a tidy-up.
void WorkflowWindow::adoptLegacyPatientData(int patientId) {
    if (patientId <= 0) return;
    const QDir runtime(treatmentDataDirectory());
    const QString directory =
        runtime.filePath(QStringLiteral("patients/%1").arg(patientId));
    if (!QDir().mkpath(directory)) return;
    for (const QString& name : {QStringLiteral("treatment_log.csv"),
                                QStringLiteral("treatment_visit_state.csv")}) {
        const QString from = runtime.filePath(name);
        const QString to = QDir(directory).filePath(name);
        if (!QFileInfo::exists(from) || QFileInfo::exists(to)) continue;
        if (QFile::rename(from, to)) continue;
        if (QFile::copy(from, to) && QFile::remove(from)) continue;
        showMessage(QStringLiteral("%1 could not be moved under patient %2 and is no longer "
                                   "being read. Move it there by hand.")
                        .arg(QDir::toNativeSeparators(from))
                        .arg(patientId),
                    true);
    }
}

void WorkflowWindow::loadPatientData() {
    // The Treatment page keeps its own copy of the last message, and
    // showMessage only mirrors onto it while that page is showing -- so a
    // switch made from the Case page leaves the last patient's text there.
    if (sonicationCountdownLabel_ != nullptr) {
        sonicationCountdownLabel_->setText(QStringLiteral("No sonication running."));
        sonicationCountdownLabel_->setStyleSheet(
            QStringLiteral("QLabel { color: #dcebef; padding: 4px; }"));
    }
    treatmentLog_.clear();
    visitSonication_.clear();
    editedVisits_.clear();
    // So loadTreatmentProtocol treats this as a fresh protocol and opens on
    // this patient's latest visit rather than keeping the last one's place.
    lastLoadedProtocol_.clear();
    lastLoadedVisit_ = 0;
    currentSonication_ = 1;

    const QString directory = patientDataDirectory();
    if (!directory.isEmpty()) {
        try {
            treatmentLog_.load(
                QDir(directory).filePath(QStringLiteral("treatment_log.csv")).toStdString());
        } catch (const std::exception& error) {
            showMessage(QStringLiteral("This patient's treatment log could not be read: %1")
                            .arg(error.what()),
                        true);
        }
        loadVisitState();
    }
    // After the load, which clears it, and whatever the file said: the
    // registry is the authority on who this is. An old log whose SubjectID
    // column was never filled is stamped from here on its next save.
    beam::gui::Patient open;
    if (patients_.find(currentPatientId_, &open)) {
        treatmentLog_.setPatient(open.externalId, open.site,
                                 open.lastName.empty() && open.firstName.empty()
                                     ? std::string()
                                     : beam::gui::patientDisplayName(open));
    }
    if (treatmentProtocolCombo_ == nullptr) return;

    // Their Target List, which carries the parameters prescribed for them.
    // Read before the plan: loadTreatmentProtocol fills the grid from it.
    {
        const QString own = patientTargetsPath();
        const QString targetsPath = !own.isEmpty() && QFileInfo::exists(own)
                                        ? own
                                        : shippedDataFile(QStringLiteral("treatment_targets.csv"));
        if (!targetsPath.isEmpty()) {
            try {
                // loadTargets replaces the targets and leaves the protocols,
                // which are the site's library and belong to nobody.
                beam::gui::TreatmentTargetStore loaded = targetStore_;
                loaded.loadTargets(targetsPath.toStdString());
                targetStore_ = std::move(loaded);
            } catch (const std::exception& error) {
                showMessage(QStringLiteral("This patient's Target List could not be read (%1); "
                                           "the one on screen is the previous patient's.")
                                .arg(error.what()),
                            true);
            }
        }
    }
    // Disown the grid BEFORE emptying the list: emptying it changes the
    // selection, which saves the grid against the target it still thinks is
    // showing and writes the store back out.
    targetSonications_.clear();
    currentTargetId_ = -1;
    gridTargetId_ = -1;
    stimParamModel_->setRows({});
    targetListWidget_->clear();
    for (const beam::gui::TreatmentTarget& target : targetStore_.targets())
        addTargetListRow(QString::fromStdString(target.name));
    if (targetListWidget_->count() > 0) targetListWidget_->setCurrentRow(0);
    refreshVisitCombo();
    loadTreatmentProtocol(currentProtocolName());
}

void WorkflowWindow::savePatientData() {
    if (patientDataDirectory().isEmpty()) return;
    // The whole page, not just the log: the Target List and the protocol grid
    // are edited on the same screen, and a switch must not drop what was
    // typed into them any more than it may drop a delivery. Quietly, though:
    // the operator asked to open somebody, not to save somebody.
    saveTreatmentTargetStore(/*announce=*/false);
    saveVisitState();
}

// The install's own copy, read straight from data/ and never from the
// operator's runtime directory: loadTreatmentTargetStore prefers the runtime
// file, which is exactly the file this one exists to undo.
void WorkflowWindow::loadShippedTargetStore() {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList seeds = {
        QDir(executableDir).filePath(QStringLiteral("../../../data/treatment_targets.csv")),
        QDir(executableDir).filePath(QStringLiteral("../../data/treatment_targets.csv")),
        QDir(executableDir).filePath(QStringLiteral("data/treatment_targets.csv")),
        QDir::current().filePath(QStringLiteral("data/treatment_targets.csv"))};
    for (const QString& seed : seeds) {
        if (!QFileInfo::exists(seed)) continue;
        try {
            shippedTargets_.loadTargets(seed.toStdString());
            return;
        } catch (const std::exception&) {
            // An unreadable seed costs the tooltips and the restore action,
            // not the application.
        }
    }
}

QString WorkflowWindow::shippedTooltip(const QModelIndex& cell) const {
    if (!cell.isValid()) return QString();
    const std::optional<double> original = shippedValueFor(cell.column());
    if (!original.has_value()) return QString();
    const double now = cell.data(Qt::EditRole).toDouble();
    if (std::abs(now - *original) < 1e-12)
        return QStringLiteral("Shipped value %1, unchanged").arg(*original, 0, 'g', 6);
    return QStringLiteral("Shipped value %1 -- now %2.\nRight-click to restore the shipped "
                          "parameters.")
        .arg(*original, 0, 'g', 6)
        .arg(now, 0, 'g', 6);
}

std::optional<double> WorkflowWindow::shippedValueFor(int column) const {
    if (targetListWidget_ == nullptr || targetListWidget_->currentItem() == nullptr)
        return std::nullopt;
    const beam::gui::TreatmentTarget* shipped =
        shippedTargets_.findTarget(targetListName(targetListWidget_->currentItem()).toStdString());
    if (shipped == nullptr || shipped->points.empty()) return std::nullopt;
    const beam::gui::TreatmentTargetPoint& point = shipped->points.front();
    using Model = beam::gui_qt::StimParamTableModel;
    switch (column) {
        case Model::kAmplitude: return point.amplitude;
        case Model::kStartTime: return point.startTimeSeconds;
        case Model::kEndTime: return point.endTimeSeconds;
        case Model::kBurstDuration: return point.burstDurationSeconds;
        case Model::kBurstInterval: return point.burstIntervalSeconds;
        case Model::kPulseDuration: return point.pulseDurationSeconds;
        case Model::kPulseInterval: return point.pulseIntervalSeconds;
        // X/Y/Z are where this subject's target was placed, not something
        // the install ships a value for.
        default: return std::nullopt;
    }
}

// Parameters only. The placement stays: it belongs to this subject's
// registration, and the install has nothing to say about it.
void WorkflowWindow::restoreShippedParameters() {
    if (stimParamModel_ == nullptr || targetListWidget_ == nullptr) return;
    QListWidgetItem* item = targetListWidget_->currentItem();
    if (item == nullptr) return;
    const QString name = targetListName(item);
    const beam::gui::TreatmentTarget* shipped =
        shippedTargets_.findTarget(name.toStdString());
    if (shipped == nullptr || shipped->points.empty()) {
        showMessage(QStringLiteral("\"%1\" is not in the shipped Target List, so it has no "
                                   "original parameters to restore.")
                        .arg(name),
                    true);
        return;
    }
    const beam::gui::TreatmentTargetPoint& point = shipped->points.front();
    std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
    if (rows.empty()) return;
    QStringList changed;
    const auto put = [&changed](double& field, double shippedValue, const char* label) {
        if (std::abs(field - shippedValue) < 1e-12) return;
        changed << QStringLiteral("%1 %2 to %3")
                       .arg(QString::fromLatin1(label))
                       .arg(field, 0, 'g', 4)
                       .arg(shippedValue, 0, 'g', 4);
        field = shippedValue;
    };
    for (beam::gui_qt::StimParamRow& row : rows) {
        put(row.amplitude, point.amplitude, "Amplitude");
        put(row.startTime, point.startTimeSeconds, "Start Time");
        put(row.endTime, point.endTimeSeconds, "End Time");
        put(row.bd, point.burstDurationSeconds, "Burst Duration");
        put(row.bi, point.burstIntervalSeconds, "Burst Interval");
        put(row.pd, point.pulseDurationSeconds, "Pulse Duration");
        put(row.pi, point.pulseIntervalSeconds, "Pulse Interval");
    }
    if (changed.isEmpty()) {
        showMessage(QStringLiteral("\"%1\" already has its shipped parameters.").arg(name),
                    false);
        return;
    }
    stimParamModel_->setRows(std::move(rows));
    saveCurrentTargetSonications();
    saveTreatmentTargetStore();
    updateSonicationPlots();
    updateSonicationSafety();
    showMessage(QStringLiteral("%1 restored: %2.").arg(name, changed.join(QStringLiteral(", "))),
                false);
}

// Target parameters are prescribed per person, so the Target List lives with
// the patient. Protocols stay shared. Empty when no patient is open, and the
// device-wide copy is then the seed.
QString WorkflowWindow::patientTargetsPath() const {
    const QString patientDirectory = patientDataDirectory();
    if (patientDirectory.isEmpty()) return QString();
    return QDir(patientDirectory).filePath(QStringLiteral("treatment_targets.csv"));
}

// As shipped, never a runtime copy: a Target List is prescribed per patient,
// so there is no device-wide one to prefer.
QString WorkflowWindow::shippedDataFile(const QString& fileName) const {
    const QString executableDir = QCoreApplication::applicationDirPath();
    const QStringList seeds = {
        QDir(executableDir).filePath(QStringLiteral("../../../data/") + fileName),
        QDir(executableDir).filePath(QStringLiteral("../../data/") + fileName),
        QDir(executableDir).filePath(QStringLiteral("data/") + fileName),
        QDir::current().filePath(QStringLiteral("data/") + fileName)};
    for (const QString& seed : seeds)
        if (QFileInfo::exists(seed)) return seed;
    return QString();
}

QString WorkflowWindow::locateDataFile(const QString& fileName) const {
    const QString user = QDir(treatmentDataDirectory()).filePath(fileName);
    if (QFileInfo::exists(user)) return user;
    return shippedDataFile(fileName);
}

void WorkflowWindow::loadTreatmentTargetStore() {
    const auto locate = [this](const QString& fileName) { return locateDataFile(fileName); };

    loadShippedTargetStore();
    // This patient's Target List if they have one, else the device's, which
    // is what a new patient is seeded from. See patientTargetsPath.
    const QString own = patientTargetsPath();
    const QString targetsPath = !own.isEmpty() && QFileInfo::exists(own)
                                    ? own
                                    : locate(QStringLiteral("treatment_targets.csv"));
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
        // The log belongs to the patient, not to the install's seed -- and on
        // a device that has treated somebody before, to no patient at all
        // until the Case page names one.
        const QString patientDirectory = patientDataDirectory();
        if (!patientDirectory.isEmpty())
            treatmentLog_.load(QDir(patientDirectory)
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

void WorkflowWindow::saveTreatmentTargetStore(bool announce) {
    writeProtocolGridToStore();
    writeTargetPointsToStore();
    const QString directory = treatmentDataDirectory();
    try {
        // The Target List goes under the patient once there is one: its
        // parameters are prescribed for them. Before any patient is named it
        // goes where a pre-patient install put it, which is also what the
        // next new patient is seeded from.
        const QString own = patientTargetsPath();
        if (!own.isEmpty()) targetStore_.saveTargets(own.toStdString());
        // Protocols are the site's library and stay shared.
        targetStore_.saveProtocols(
            QDir(directory).filePath(QStringLiteral("treatment_protocols.csv")).toStdString());
        // The library above is the install's; the log below is this patient's.
        const QString patientDirectory = patientDataDirectory();
        if (patientDirectory.isEmpty()) {
            // Nobody is named, so there is nobody to file this against. Only
            // worth saying when something would actually be lost: this also
            // runs while the window is being built, before the Case page has
            // been near the operator, and a red banner there says nothing.
            if (!treatmentLog_.rows().empty()) {
                showMessage(QStringLiteral("Nothing was logged: the Case page has not said which "
                                           "patient is being treated."),
                            true);
            }
            return;
        }
        const QString logPath =
            QDir(patientDirectory).filePath(QStringLiteral("treatment_log.csv"));
        treatmentLog_.save(logPath.toStdString());
        // The log writes itself on every change, so there is no Save button to
        // press and no way to tell whether it happened. This is that telling.
        // The file first, then what happened to it: the operator is checking
        // which log this went into, not reading a headline.
        if (!announce) return;
        showMessage(QStringLiteral("%1 updated at %2")
                        .arg(QDir::toNativeSeparators(logPath))
                        .arg(QDateTime::currentDateTime().toString(
                            QStringLiteral("HH:mm:ss.zzz"))),
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
    // The page header carries the visit now that the case page does not ask
    // for one.
    ui_->visitHeader->setText(visitCreated ? QStringLiteral("Visit: %1").arg(visit)
                                           : QStringLiteral("Visit: —"));
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
    QMetaObject::invokeMethod(
        this, [this] { precheckPendingSonications(); }, Qt::QueuedConnection);
    selectScheduledSonication(keep);
    updateBestTargets();
    updatePlanBlocksLabel();
    // The checkbox belongs to the visit, so it follows the one just opened.
    if (autoRankCheckBox_ != nullptr) {
        const QSignalBlocker block(autoRankCheckBox_);
        autoRankCheckBox_->setChecked(automaticRankingOn());
    }
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
        // The current sonication first: one target can be screened for 30 s
        // and treated for 180 s in the same visit, and taking whichever row
        // merely names it first would run the screening duration for both.
        std::optional<double> scheduledDuration;
        const std::vector<beam::gui_qt::TreatmentProtocolRow>& planned = protocolModel_->rows();
        if (currentSonication_ >= 1 && currentSonication_ <= static_cast<int>(planned.size())) {
            const beam::gui_qt::TreatmentProtocolRow& current =
                planned[static_cast<std::size_t>(currentSonication_) - 1];
            if (current.target == name) scheduledDuration = current.duration;
        }
        if (!scheduledDuration.has_value()) {
            for (const beam::gui_qt::TreatmentProtocolRow& protocolRow : planned) {
                if (protocolRow.target == name) {
                    scheduledDuration = protocolRow.duration;
                    break;
                }
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

// BeamV0 checks only the sonication about to go out, so a plan can carry a
// row that cannot be delivered and say nothing until the visit reaches it --
// after the ones before it have already been given. This runs the same rules
// over every row still to run and marks the ones that would be refused.
//
// Advisory, deliberately. It blocks nothing: rows that are fine must not be
// held up by one further down, and a row can still change before it is
// reached -- ranking repoints targets, and a target's parameters can be
// edited. What actually stops a delivery is the check against the grid at
// fire time, which is unchanged.
void WorkflowWindow::precheckPendingSonications() {
    if (protocolModel_ == nullptr || prechecking_) return;
    prechecking_ = true;
    const auto done = qScopeGuard([this] { prechecking_ = false; });
    std::vector<beam::gui_qt::TreatmentProtocolRow> rows = protocolModel_->rows();
    QStringList blockedRows;
    bool changed = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        beam::gui_qt::TreatmentProtocolRow& row = rows[i];
        QStringList found;
        // Delivered rows are a record, not a plan; the current one already
        // has the banner above the grid saying this in full.
        const bool isCurrent = static_cast<int>(i) + 1 == currentSonication_;
        if (!row.delivered && !isCurrent) {
            // Composed the way the grid would be: the duration from this row,
            // everything else from the target it runs.
            const beam::gui::TreatmentLogRow planned =
                rowForTarget(row.target.toStdString(), row.duration);
            const beam::safety::SonicationSafetyParams params =
                beam::gui::stimParamsFromTableRow(planned.pulseDurationSeconds,
                                                  planned.pulseIntervalSeconds,
                                                  planned.burstDurationSeconds,
                                                  planned.burstIntervalSeconds, planned.amplitude,
                                                  planned.startTimeSeconds,
                                                  planned.endTimeSeconds);
            const beam::safety::SafetyReport report =
                beam::safety::checkSonicationParameters({params});
            for (const std::string& message : report.messages)
                found << QString::fromStdString(message);
            if (!found.isEmpty()) blockedRows << QString::number(i + 1);
        }
        if (row.blockedBy != found) {
            row.blockedBy = found;
            changed = true;
        }
    }
    if (changed) protocolModel_->setRows(std::move(rows));

    if (sonicationPrecheckLabel_ == nullptr) return;
    if (blockedRows.isEmpty()) {
        sonicationPrecheckLabel_->hide();
        return;
    }
    sonicationPrecheckLabel_->setText(
        QStringLiteral("%1 later sonication(s) would be blocked as the plan stands: row %2. "
                       "Hover the row for the rule.")
            .arg(blockedRows.size())
            .arg(blockedRows.join(QStringLiteral(", "))));
    sonicationPrecheckLabel_->show();
}

// Filed as a note on the row it was aimed at, stamped and worded like every
// other note, so a visit read back later shows the attempt as well as the
// deliveries. Nothing else changes: no row is created, no status is set, and
// the sonication is still refused.
void WorkflowWindow::recordRefusedSonication(bool sham) {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row =
        treatmentLog_.find(protocol, visit, currentSonication_);
    if (row == nullptr) return;
    const QString entry =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
        QStringLiteral("  %1 refused: %2")
            .arg(sham ? QStringLiteral("Sham") : QStringLiteral("Sonicate"),
                 sonicationSafetyMessages_.join(QStringLiteral("; ")));
    QString notes = QString::fromStdString(row->notes);
    if (!notes.isEmpty()) notes += QLatin1Char('\n');
    notes += entry;
    treatmentLog_.setNotes(protocol, visit, currentSonication_, notes.toStdString());
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
}

// Port of SonicateButtonPushed.m's last act:
//
//   if mod(i,4)==0
//       RunCorrectionButtonPushed(app, event);
//   end
//
// Coupling is not a property of the visit, it is a property of this moment:
// gel dries, the subject settles, the array shifts. Measuring once at the
// Correction stage and trusting that figure for twenty-four sonications is
// how a later one gets delivered against a transmission nobody has checked
// since the start. The source re-measures every fourth delivery; so does
// this.
//
// It runs the same path the Run button runs, so the plots, the numbers, the
// coupling verdict and the sonication safety report are all brought up to
// date together -- including Rule 320, which is what refuses the next
// sonication if the coupling has gone.
void WorkflowWindow::remeasureCouplingIfDue(int delivered) {
    if (delivered <= 0 || delivered % kCouplingRecheckEvery != 0) return;
    if (runCorrectionButton_ == nullptr) return;
    showMessage(QStringLiteral("Sonication %1 delivered. Re-measuring coupling, as every %2 "
                               "sonications.")
                    .arg(delivered)
                    .arg(kCouplingRecheckEvery),
                false);
    // Queued: this is reached from the countdown timer finishing a delivery,
    // and the measurement reads the serial port and rebuilds three charts.
    // Letting the delivery finish closing first keeps the two apart.
    QMetaObject::invokeMethod(
        this,
        [this, delivered] {
            autoCouplingRecheck_ = true;
            startCorrectionMeasurement([this, delivered] {
            // Cleared here, not on the way out: applyCorrectionResult reads
            // the flag, and it runs when the device answers.
            autoCouplingRecheck_ = false;
            // The Coupling page owns the verdict; mirror it the way the page
            // does, so the operator sees the same conclusion from here.
            couplingCheckPassed_ = correctionCheckPassed_;
            if (!correctionCheckPassed_) {
                showMessage(
                    QStringLiteral("Coupling has fallen below the threshold after sonication "
                                   "%1. Readjust coupling or add coupling gel over uncoupled "
                                   "elements, then run the measurement again on the Coupling "
                                   "page.")
                        .arg(delivered),
                    true);
            }
            // Rule 320 reads the new transmission.
            updateSonicationSafety();
            refresh();
            if (correctionCheckPassed_) {
                showMessage(
                    QStringLiteral("Coupling re-measured after sonication %1: transmission %2%. "
                                   "Treatment continues.")
                        .arg(delivered)
                        .arg(std::clamp(
                            static_cast<int>(std::lround(transmissionAmplitude_ * 100.0)), 0,
                            100)),
                    false);
            }
            });
        },
        Qt::QueuedConnection);
}

void WorkflowWindow::updateSonicationSafety() {
    if (sonicationSafetyLabel_ == nullptr || stimParamModel_ == nullptr) return;
    if (checkingSonicationSafety_) return;
    checkingSonicationSafety_ = true;
    const auto done = qScopeGuard([this] { checkingSonicationSafety_ = false; });
    std::vector<beam::safety::SonicationSafetyParams> sonications;
    for (const beam::gui_qt::StimParamRow& row : stimParamModel_->rows()) {
        sonications.push_back(beam::gui::stimParamsFromTableRow(row.pd, row.pi, row.bd, row.bi,
                                                                row.amplitude, row.startTime,
                                                                row.endTime));
    }
    const beam::safety::SafetyReport report =
        beam::safety::checkSonicationParameters(sonications);
    sonicationSafetyMessages_.clear();
    for (const std::string& message : report.messages)
        sonicationSafetyMessages_ << QString::fromStdString(message);
    sonicationSafetyPass_ = report.pass;

    // Which cells each broken rule is about. The grid marks them, because a
    // message naming a rule does not say which of eleven columns it is about,
    // and the cell the operator just edited is rarely the only one involved.
    // Which of them to change is a clinical decision; the application marks
    // what the rule reads and leaves the decision alone.
    using Model = beam::gui_qt::StimParamTableModel;
    using Kind = beam::safety::ViolationKind;
    std::set<std::pair<int, int>> unsafe;
    for (const beam::safety::Violation& violation : report.violations) {
        const int row = violation.sonicationIndex;
        const auto blame = [&unsafe, row](std::initializer_list<int> columns) {
            for (const int column : columns) unsafe.insert({row, column});
        };
        switch (violation.kind) {
            case Kind::kIsppa:
            case Kind::kMechanicalIndex:
                // Both are functions of amplitude alone.
                blame({Model::kAmplitude});
                break;
            case Kind::kPulseDurationOverInterval:
                blame({Model::kPulseDuration, Model::kPulseInterval});
                break;
            case Kind::kBurstDurationOverInterval:
                blame({Model::kBurstDuration, Model::kBurstInterval});
                break;
            case Kind::kPulseIntervalOverBurst:
                blame({Model::kPulseInterval, Model::kBurstDuration});
                break;
            case Kind::kBurstIntervalOverDuration:
                // "Duration" is not a column; Start and End are what make it.
                blame({Model::kBurstInterval, Model::kStartTime, Model::kEndTime});
                break;
            case Kind::kIsptaAll:
            case Kind::kIsptaBurstInterval:
                // Time-averaged intensity reads the amplitude and every term
                // of the duty cycle.
                blame({Model::kAmplitude, Model::kBurstDuration, Model::kBurstInterval,
                       Model::kPulseDuration, Model::kPulseInterval});
                break;
        }
    }

    // The source's own two preconditions, in its own words. They are about
    // the session rather than the numbers in the grid, which is why they are
    // added here and numbered in the 3xx band.
    if (workflow_.state(beam::gui::WorkflowStage::Registration).status !=
        beam::gui::WorkflowStatus::Complete) {
        sonicationSafetyMessages_ << QStringLiteral(
            "[Rule 310] Registration incomplete, please complete registration tab");
        sonicationSafetyPass_ = false;
    }
    if (transmissionAmplitude_ < kCouplingThreshold) {
        sonicationSafetyMessages_ << QStringLiteral(
            "[Rule 320] Through transmit too low, please complete correction tab");
        sonicationSafetyPass_ = false;
    }

    if (sonicationSafetyPass_) {
        // The source's own word for a clean report.
        sonicationSafetyLabel_->setText(
            QStringLiteral("<b style='color:#5ec46a'>&#9679; Sonication Online</b>"));
        sonicationSafetyLabel_->setStyleSheet(QStringLiteral(
            "QLabel { color: #dcebef; background: #12301c; border: 1px solid #2f6b3f; "
            "border-radius: 4px; padding: 5px 10px; }"));
    } else {
        QStringList shown;
        for (const QString& message : sonicationSafetyMessages_)
            shown << message.toHtmlEscaped();
        sonicationSafetyLabel_->setText(
            QStringLiteral("<b style='color:#ff554a'>&#9679; Sonication blocked</b> &nbsp; %1")
                .arg(shown.join(QStringLiteral("<br>"))));
        sonicationSafetyLabel_->setStyleSheet(QStringLiteral(
            "QLabel { color: #ffd9d6; background: #351a19; border: 1px solid #8e1b1b; "
            "border-radius: 4px; padding: 5px 10px; }"));
    }
    stimParamModel_->setUnsafeCells(unsafe);
    precheckPendingSonications();
    // Re-applies the gate above to whatever buttons are on the current row.
    setSonicateEnabled(sonicateEnabled_);
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

// BeamV0 re-ranks after every sonication, silently. This runs when the
// operator presses the button, says what it changed, and can be put back: a
// plan that rewrites itself between sonications is a plan nobody can hold in
// their head.
// A note is a running record of one sonication, not a caption: it is written
// to over a visit, by more than one person, and reading it back is the point.
// So it is kept as lines, edited in a box that holds lines, and the cell shows
// the most recent one with the rest a hover away.
void WorkflowWindow::editRowNotes(int sequence) {
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    const beam::gui::TreatmentLogRow* row = treatmentLog_.find(protocol, visit, sequence);
    if (row == nullptr) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Notes -- sonication %1 (%2)")
                              .arg(sequence)
                              .arg(QString::fromStdString(row->target)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* writtenLabel = new QLabel(QStringLiteral("Written so far:"), &dialog);
    layout->addWidget(writtenLabel);
    // What is already there is read: a record is added to, not rewritten, and
    // a box that let the caret wander through it is how a stamp ends up on a
    // line nobody typed.
    auto* written = new QPlainTextEdit(QString::fromStdString(row->notes), &dialog);
    written->setReadOnly(true);
    written->setMinimumSize(520, 150);
    layout->addWidget(written, 1);
    layout->addWidget(new QLabel(QStringLiteral("Add a note:"), &dialog));
    auto* entry = new QPlainTextEdit(&dialog);
    entry->setMinimumHeight(74);
    entry->setPlaceholderText(QStringLiteral("Stamped with the date and time when you press OK"));
    layout->addWidget(entry, 0);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    entry->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;

    const QString typed = entry->toPlainText().trimmed();
    if (typed.isEmpty()) return;  // the box was opened and thought better of
    // The stamp is put on by the application as the note is filed, so every
    // entry carries one and none of them can be typed around. The date is
    // part of it: a visit can run past midnight, and a log read next week has
    // to say which day a line is from.
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QStringList entryLines = typed.split(QLatin1Char('\n'));
    entryLines.first() = stamp + QStringLiteral("  ") + entryLines.first().trimmed();
    QString kept = QString::fromStdString(row->notes);
    if (!kept.isEmpty()) kept += QLatin1Char('\n');
    kept += entryLines.join(QStringLiteral("\n"));
    treatmentLog_.setNotes(protocol, visit, sequence, kept.toStdString());
    // Reload before saving: the save writes the table back into the log.
    loadTreatmentProtocol(protocolName);
    saveTreatmentTargetStore();
}

bool WorkflowWindow::automaticRankingOn() const {
    return !rankingOffVisits_.contains(visitKey(currentProtocolName(), currentVisitNumber()));
}

// BeamV0's three triggers: after a sonication, after a sham, and on a visit
// change. Silent -- nothing to rank is the normal state for most of a visit.
void WorkflowWindow::applyTargetRankingIfAutomatic() {
    if (applyingRanking_ || !automaticRankingOn()) return;
    applyTargetRanking(/*quiet=*/true);
}

void WorkflowWindow::applyTargetRanking(bool quiet) {
    if (applyingRanking_) return;
    // The scores it ranks on are read from the log, and a score just typed is
    // still only in the table. Flush first or the ranking is an edit behind.
    writeProtocolGridToStore();
    const QString protocolName = currentProtocolName();
    const std::string protocol = protocolName.toStdString();
    const int visit = currentVisitNumber();
    std::vector<beam::gui::PlanRow> plan;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rowsFor(protocol, visit)) {
        beam::gui::PlanRow entry;
        entry.sequence = row.sequence;
        entry.target = row.target;
        entry.duration = row.endTimeSeconds - row.startTimeSeconds;
        entry.response = beam::gui::TreatmentLog::responseTotal(row);
        entry.delivered = beam::gui::TreatmentLog::delivered(row);
        plan.push_back(std::move(entry));
    }
    bool anyTreatmentRow = false;
    for (const beam::gui::PlanRow& row : plan)
        if (!row.delivered && row.duration >= 60.0) anyTreatmentRow = true;
    if (!anyTreatmentRow) {
        if (!quiet)
            showMessage(QStringLiteral("Nothing to rank: this visit has no treatment sonication "
                                       "left to run. Ranking repoints the long sonications, not "
                                       "the screening ones."),
                        true);
        return;
    }
    const std::vector<beam::gui::RankedTarget> changes = beam::gui::rankTreatmentRows(plan);
    if (changes.empty()) {
        if (!quiet)
            showMessage(
                QStringLiteral("Target ranking: the plan already runs the best-scoring targets."),
                false);
        return;
    }

    applyingRanking_ = true;
    int moves = 0;
    for (const beam::gui::RankedTarget& change : changes) {
        // Not the one going out: it is already on its way to the hardware.
        if (change.sequence == openLogSequence_) continue;
        const beam::gui::TreatmentLogRow* was =
            treatmentLog_.find(protocol, visit, change.sequence);
        if (was == nullptr) continue;
        const beam::gui::TreatmentLogRow& before = *was;
        // The duration is the plan's and stays; everything else comes from
        // the target the row now runs.
        beam::gui::TreatmentLogRow row =
            rowForTarget(change.target, before.endTimeSeconds - before.startTimeSeconds);
        if (treatmentLog_.replacePlanned(protocol, visit, change.sequence, std::move(row)))
            ++moves;
    }
    const int moved = static_cast<int>(moves);
    if (moved > 0) {
        loadTreatmentProtocol(protocolName);
        saveTreatmentTargetStore();
    }
    applyingRanking_ = false;
    if (moved > 0 && !quiet)
        showMessage(QStringLiteral("Target ranking repointed %1 sonication(s) still to run.")
                        .arg(moved),
                    false);
}

void WorkflowWindow::updatePlanBlocksLabel() {
    if (planBlocksLabel_ == nullptr || protocolModel_ == nullptr) return;
    std::vector<beam::gui::PlanRow> plan;
    for (const beam::gui_qt::TreatmentProtocolRow& row : protocolModel_->rows()) {
        beam::gui::PlanRow entry;
        entry.sequence = row.number;
        entry.target = row.target.toStdString();
        entry.duration = row.duration;
        plan.push_back(std::move(entry));
    }
    QStringList pairs;
    QStringList orphans;
    for (const beam::gui::PlanBlock& block : beam::gui::planBlocks(plan)) {
        if (block.screening) continue;
        if (block.feedFirst == 0) {
            orphans << QStringLiteral("%1-%2").arg(block.first).arg(block.last);
            continue;
        }
        pairs << QStringLiteral("%1-%2 screen \u2192 %3-%4 treat")
                     .arg(block.feedFirst).arg(block.feedLast).arg(block.first).arg(block.last);
    }
    QString text;
    if (!pairs.isEmpty()) text = pairs.join(QStringLiteral("   \u00b7   "));
    if (!orphans.isEmpty()) {
        if (!text.isEmpty()) text += QStringLiteral("   \u00b7   ");
        text += QStringLiteral("rows %1 have no screening above them, so ranking leaves them alone")
                    .arg(orphans.join(QStringLiteral(", ")));
    }
    planBlocksLabel_->setText(text.isEmpty() ? QString()
                                             : QStringLiteral("Read from the durations:  %1").arg(text));
    planBlocksLabel_->setToolTip(planBlocksLabel_->text());
    planBlocksLabel_->setVisible(!text.isEmpty());
}

void WorkflowWindow::updateBestTargets() {
    if (!bestTargetsLabel_ || !protocolModel_) return;
    const beam::gui::AccFlag flag = accFlagCombo_->currentIndex() == 1   ? beam::gui::AccFlag::kScc
                                    : accFlagCombo_->currentIndex() == 2 ? beam::gui::AccFlag::kAmcc
                                                                          : beam::gui::AccFlag::kOther;
    const beam::gui::BestTargets best = protocolModel_->computeBestTargets(flag);
    QStringList names;
    for (const std::string& name : best.name) names << QString::fromStdString(name);
    bestTargetsLabel_->setText(names.isEmpty() ? QStringLiteral("--")
                                               : names.join(QStringLiteral(", ")));
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
    // Only while something is running: a bar left on the page with nothing
    // moving reads as a sonication that never finished.
    treatmentExecutionProgressBar_->setVisible(false);
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
    const auto makeDeliveryButton = [this, controlPanel](const QString& text, bool sham) {
        auto* button = new QPushButton(text, controlPanel);
        button->setIcon(sham ? makeShamIcon(18, kInkOnYellow) : makeSonicateIcon(18, kInkOnBlue));
        button->setIconSize(QSize(18, 18));
        button->setMinimumHeight(36);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QString::fromLatin1(sham ? kShamStyle : kSonicateStyle));
        connect(button, &QPushButton::clicked, this, [this, sham] { runSonication(sham); });
        (sham ? inRowShamButton_ : inRowSonicateButton_) = button;
        return button;
    };
    triggerModeCombo_ = new QComboBox(controlPanel);
    triggerModeCombo_->addItems({QStringLiteral("Immediate"), QStringLiteral("External")});
    triggerModeCombo_->setStyleSheet(QStringLiteral(
        "QComboBox { background: #ffffff; color: #17313f; padding: 4px 8px; "
        "border: 1px solid #7c9da8; border-radius: 4px; }"));
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

    // The device link lives on the Coupling page: it has to be up before
    // the correction measurement, which is a stage earlier than this one.
    controlColumn->addWidget(columnLabel(QStringLiteral("Trigger:")));
    controlColumn->addWidget(triggerModeCombo_);
    // The two deliveries, as words. Icons on a row of icons is how the wrong
    // one gets pressed, and the gap between them is deliberate: a slip lands
    // on the column, not on the other one.
    controlColumn->addSpacing(20);
    controlColumn->addWidget(makeDeliveryButton(QStringLiteral("Sonicate"), false));
    controlColumn->addSpacing(30);
    controlColumn->addWidget(makeDeliveryButton(QStringLiteral("Sham"), true));
    controlColumn->addSpacing(6);
    // Slack below, or the box layout spreads the controls apart.
    // No trailing stretch: the column is as tall as its controls now, and the
    // space below belongs to the page, not to an empty pane.

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
            // Off the page rather than sitting at 100%: the Status column now
            // says the sonication finished, and a bar left on screen reads as
            // one still in progress.
            treatmentExecutionProgressBar_->setValue(0);
            treatmentExecutionProgressBar_->setVisible(false);
            setSonicateEnabled(true);
            finishTreatmentButton_->setEnabled(true);
            if (deviceLink_ != nullptr) deviceLink_->setDelivering(false);
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

    connect(finishTreatmentButton_, &QPushButton::clicked, this, [this] { finishTreatmentStage(); });
}

// generalSonicateMaster.m's decision path: build stimParams from the shown
// row, run the two last-moment checks, and only then send. The transmission
// figure is the one the Correction stage actually measured.

QString WorkflowWindow::visitStatePath() const {
    const QString directory = patientDataDirectory();
    if (directory.isEmpty()) return QString();
    return QDir(directory).filePath(QStringLiteral("treatment_visit_state.csv"));
}

// Where each visit was left. The log holds what was delivered, which is not the
// same thing: the operator can point the visit at a sonication without firing
// it, and that choice has to survive a restart like everything else on the page.
void WorkflowWindow::saveVisitState() const {
    const QString path = visitStatePath();
    // Nobody named, so nothing to file it against.
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    QTextStream out(&file);
    out << "Protocol,Visit,CurrentSonication,Edited,AutoRank\n";
    // Every visit any of the three knows about: one created and left empty
    // has no position of its own, and writing only the positions lost it.
    QSet<QString> keys;
    for (auto entry = visitSonication_.constBegin(); entry != visitSonication_.constEnd();
         ++entry)
        keys.insert(entry.key());
    keys.unite(editedVisits_);
    keys.unite(rankingOffVisits_);
    QStringList ordered(keys.constBegin(), keys.constEnd());
    ordered.sort();  // a stable file, so a diff of it means something
    for (const QString& key : ordered) {
        const int split = key.lastIndexOf(QLatin1Char('|'));
        if (split <= 0) continue;
        QString protocol = key.left(split);
        protocol.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        out << '"' << protocol << "\"," << key.mid(split + 1) << ','
            << visitSonication_.value(key, 1) << ',' << (editedVisits_.contains(key) ? 1 : 0)
            << ',' << (rankingOffVisits_.contains(key) ? 0 : 1) << '\n';
    }
}

// The operator has changed this visit's rows, so the schedule no longer
// describes it and must not be seeded into it again.
void WorkflowWindow::markVisitEdited(const QString& protocol, int visit) {
    editedVisits_.insert(visitKey(protocol, visit));
    saveVisitState();
}

void WorkflowWindow::loadVisitState() {
    const QString path = visitStatePath();
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    in.readLine();  // header
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        // "Protocol",Visit,CurrentSonication[,Edited[,AutoRank]] -- the protocol
        // is quoted because it may hold a comma; everything after it is a
        // plain number. Edited is read when present, so a file
        // written before it still loads.
        if (!line.startsWith(QLatin1Char('"'))) continue;
        QString protocol;
        int at = 1;
        bool closed = false;
        while (at < line.size()) {
            if (line.at(at) == QLatin1Char('"')) {
                if (at + 1 < line.size() && line.at(at + 1) == QLatin1Char('"')) {
                    protocol += QLatin1Char('"');
                    at += 2;
                    continue;
                }
                closed = true;
                ++at;
                break;
            }
            protocol += line.at(at);
            ++at;
        }
        if (!closed || at >= line.size() || line.at(at) != QLatin1Char(',')) continue;
        const QStringList fields = line.mid(at + 1).split(QLatin1Char(','));
        if (fields.size() < 2) continue;
        bool visitOk = false;
        bool positionOk = false;
        const int visit = fields.at(0).trimmed().toInt(&visitOk);
        const int position = fields.at(1).trimmed().toInt(&positionOk);
        if (!visitOk || !positionOk || visit <= 0 || position <= 0) continue;
        const QString key = visitKey(protocol, visit);
        visitSonication_.insert(key, position);
        if (fields.value(2).trimmed() == QLatin1String("1")) editedVisits_.insert(key);
        // Absent means on, so a file written before this column keeps
        // BeamV0's always-on ranking.
        if (fields.value(3).trimmed() == QLatin1String("0")) rankingOffVisits_.insert(key);
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
        // over a cell to read a tooltip. Filed like any note the operator
        // writes -- its own line, stamped, after what is already there -- so a
        // line the application wrote and a line a person wrote read the same
        // way. Once per row.
        if (aborted) {
            const beam::gui::TreatmentLogRow* row =
                treatmentLog_.find(protocol, visit, openLogSequence_);
            const std::string warning = "Stopped by operator before it finished (499).";
            if (row != nullptr && row->notes.find(warning) == std::string::npos) {
                QString notes = QString::fromStdString(row->notes);
                if (!notes.isEmpty()) notes += QLatin1Char('\n');
                notes += QDateTime::currentDateTime().toString(
                             QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
                         QStringLiteral("  ") + QString::fromStdString(warning);
                treatmentLog_.setNotes(protocol, visit, openLogSequence_, notes.toStdString());
            }
        }
    }
    if (openLogSequence_ <= 0) return;
    treatmentLog_.close(protocol, visit, openLogSequence_,
                        QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString());
    // Read before the visit moves on, and only for one that ran to the end:
    // a stopped sonication is not a measurement interval.
    const int justDelivered = aborted ? 0 : openLogSequence_;
    openLogSequence_ = 0;
    // Reload before saving, not after. saveTreatmentTargetStore writes the
    // table's cells back into the log, so saving first pushed the table's empty
    // Notes over the warning set above; reloading first puts the warning into
    // the table, and the save then writes it back unchanged.
    loadTreatmentProtocol(currentProtocolName());
    saveTreatmentTargetStore();
    // BeamV0 re-ranks here, inside SonicateButtonPushed and ShamButtonPushed.
    applyTargetRankingIfAutomatic();
    advanceToNextScheduledSonication();
    remeasureCouplingIfDue(justDelivered);
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
// Sonicate and Sham live in the control panel, not on the row, so nothing
// removes them when a row is finished. This is what stops a delivered
// sonication being sent a second time.
bool WorkflowWindow::currentSonicationIsPending() const {
    if (protocolModel_ == nullptr) return false;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& rows = protocolModel_->rows();
    if (currentSonication_ < 1 || currentSonication_ > static_cast<int>(rows.size())) return false;
    // The one going out is pending until its row closes.
    if (openLogSequence_ == currentSonication_) return true;
    return !rows[static_cast<std::size_t>(currentSonication_) - 1].delivered;
}

void WorkflowWindow::setSonicateEnabled(bool enabled) {
    sonicateEnabled_ = enabled;
    // Before the early return: the top-bar ABORT is on every page and must
    // follow the delivery even when this row has no buttons on it.
    updateAbortButton();
    if (inRowSonicateButton_.isNull()) return;
    const bool running =
        sonicationCountdownTimer_ != nullptr && sonicationCountdownTimer_->isActive();
    // checkSonicationSafety.m ends by enabling or disabling SonicateButton on
    // exactly this flag. Nothing goes out while the parameters are unsafe.
    const bool live = !running && enabled && sonicationSafetyPass_ &&
                      currentSonicationIsPending() && !mcuConnectionInvalid_;
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
        // Red whether or not anything is running: the control that interrupts
        // a delivery is found by its colour, and a greyed one is found by
        // hunting.
        inRowStopButton_->setIcon(makeStopIcon(22, kInkOnRed));
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
    // Only the ones that sit on top of a table row.
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
        const bool goingOut = openLogSequence_ > 0 && oneBased == openLogSequence_;
        const bool wanted =
            row == oneBased - 1 &&
            (goingOut || !protocolModel_->rows()[static_cast<std::size_t>(row)].delivered);
        if (!wanted) {
            if (protocolView_->indexWidget(cell) != nullptr) {
                // The pair widget owns both buttons, so dropping it takes them.
                protocolView_->setIndexWidget(cell, nullptr);
            }
            continue;
        }
        if (protocolView_->indexWidget(cell) != nullptr) continue;
        // What belongs to the row is what says this row is the one running:
        // stop, and the lamp. The two deliveries are words in the Serial Port
        // column, where neither can be hit by a slip meant for the other.
        auto* pair = new QWidget(protocolView_);
        auto* row_layout = new QHBoxLayout(pair);
        row_layout->setContentsMargins(1, 0, 1, 0);
        row_layout->setSpacing(4);
        auto* halt = new QPushButton(pair);
        halt->setIconSize(QSize(22, 22));
        halt->setFixedSize(44, 26);
        halt->setCursor(Qt::PointingHandCursor);
        halt->setStyleSheet(QString::fromLatin1(kStopStyle));
        halt->setIcon(makeStopIcon(22, kInkOnRed));
        connect(halt, &QPushButton::clicked, this, [this] { abortSonication(); });
        auto* lamp = new QLabel(pair);
        lamp->setTextFormat(Qt::RichText);
        lamp->setText(lampText(QStringLiteral("#27343a"), QStringLiteral("#27343a")));
        lamp->setAlignment(Qt::AlignCenter);
        lamp->setFixedSize(56, 26);
        lamp->setStyleSheet(QString::fromLatin1(kLampOffStyle));
        // Nothing to click: it reports, so clicks belong to the row behind it.
        lamp->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        row_layout->addWidget(halt);
        row_layout->addWidget(lamp);
        row_layout->addStretch(1);
        // These sit on top of the row's first cell, so without this a
        // right-click on them is swallowed by the button and the row that is
        // about to run is the one row with no menu.
        for (QWidget* over : {static_cast<QWidget*>(pair), static_cast<QWidget*>(halt),
                              static_cast<QWidget*>(lamp)}) {
            over->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(over, &QWidget::customContextMenuRequested, this,
                    [this, over](const QPoint& at) {
                        showScheduleRowMenu(protocolView_->viewport()->mapFromGlobal(
                            over->mapToGlobal(at)));
                    });
        }
        protocolView_->setIndexWidget(cell, pair);
        protocolView_->setRowHeight(row, 30);
        inRowStopButton_ = halt;
        inRowOnLamp_ = lamp;
        setSonicateEnabled(sonicateEnabled_);  // paints them into whichever state applies
    }
}

// Port of what updateSonicateSettingsForCurrentSonication.m does to the grid:
//
//   stimParamTableData.startTime = 0;
//   stimParamTableData.endTime   = treatmentProtocolTable.Duration(currSonicationI);
//
// The plan row owns how long the sonication runs; the target owns everything
// else. Target ranking replaces the row's name and leaves its Duration alone,
// so a 180 s slot repointed at SCC1 runs for 180 s, not for the 30 s SCC1
// defines. From here the grid is what fires, so an edit made afterwards is
// the one that goes out -- until the current sonication changes again and
// this puts the schedule's own number back, as the source does.
void WorkflowWindow::applyScheduledDurationToGrid() {
    if (stimParamModel_ == nullptr || protocolModel_ == nullptr) return;
    const std::vector<beam::gui_qt::TreatmentProtocolRow>& planned = protocolModel_->rows();
    const int position = currentSonication_;
    if (position < 1 || position > static_cast<int>(planned.size())) return;
    const beam::gui_qt::TreatmentProtocolRow& planRow =
        planned[static_cast<std::size_t>(position) - 1];
    if (planRow.duration <= 0.0) return;
    // Only when the grid is showing the target this sonication runs.
    if (planRow.target != stimParamModel_->targetName()) return;

    std::vector<beam::gui_qt::StimParamRow> rows = stimParamModel_->rows();
    if (rows.empty()) return;
    bool changed = false;
    for (beam::gui_qt::StimParamRow& row : rows) {
        if (row.startTime != 0.0 || std::abs(row.endTime - planRow.duration) > 1e-12)
            changed = true;
        row.startTime = 0.0;
        row.endTime = planRow.duration;
    }
    if (!changed) return;
    stimParamModel_->setRows(std::move(rows));
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
    // Here rather than in loadTreatmentProtocol: that calls
    // switchToSelectedTarget before this function has said which sonication
    // is current, so the grid was filled from whichever row merely named the
    // target first -- a 180 s slot repointed at SCC1 by ranking showed SCC1's
    // own 30 s. And selecting the target below refills the grid only when the
    // selection actually changes, which it usually has not.
    applyScheduledDurationToGrid();
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

// The device echoes the parameters it parsed, in its own units. Comparing
// them against what we expected is the only check that both ends agree.
void WorkflowWindow::reportStimEcho(const std::string& reply, const beam::serialcom::SonicationTiming& timing,
                                    double dutyCycle) {
    if (deviceLink_ == nullptr) return;
    if (reply.empty()) {
        deviceLink_->appendHistory(DeviceLinkPanel::Log::Lost,
                                   QStringLiteral("sent != ack   no ack received."));
        return;
    }
    const std::string expected =
        beam::serialcom::normaliseEcho(beam::serialcom::expectedStimEcho(timing, dutyCycle));
    const std::string got = beam::serialcom::normaliseEcho(reply);
    const std::vector<std::string> differences =
        beam::serialcom::stimEchoDifferences(expected, got);
    if (differences.empty()) {
        deviceLink_->appendHistory(DeviceLinkPanel::Log::Health,
                                   QStringLiteral("sent == ack   %1")
                                       .arg(QString::fromStdString(got)));
        return;
    }
    QStringList named;
    for (const std::string& d : differences) named << QString::fromStdString(d);
    deviceLink_->appendHistory(DeviceLinkPanel::Log::Lost,
                               QStringLiteral("sent != ack   %1").arg(named.join(QStringLiteral(", "))));
    showMessage(QStringLiteral("The device echoed different sonication parameters than were "
                               "sent. Expected \"%1\", got \"%2\".")
                    .arg(QString::fromStdString(expected), QString::fromStdString(got)),
                true);
}

void WorkflowWindow::runSonication(bool sham) {

    // Nothing goes out unless the MCU answers first. Sham is exempt: it
    // delivers no ultrasound.
    if (!sham && (deviceLink_ == nullptr || !deviceLink_->askDevice())) {
        mcuConnectionInvalid_ = true;
        setSonicateEnabled(sonicateEnabled_);
        if (deviceLink_ != nullptr)
            deviceLink_->appendHistory(
                DeviceLinkPanel::Log::Lost,
                QStringLiteral("pre-sonication check failed; delivery refused"));
        setExecutionStatus(QStringLiteral("MCU Connection invalid. Please power off, unplug, "
                                          "and reconnect to MCU."),
                           true);
        return;
    }

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
    // Backstop: the buttons are already dead for a finished row, and a
    // second delivery would file a duplicate against the same sequence.
    if (!currentSonicationIsPending()) {
        setExecutionStatus(QStringLiteral("Sonication %1 has already been delivered. Nothing "
                                          "was sent.")
                               .arg(currentSonication_),
                           true);
        return;
    }
    // The button is already disabled while this is false; this is the
    // backstop for anything that reaches here another way.
    updateSonicationSafety();
    if (!sonicationSafetyPass_) {
        // A refusal is part of what happened to this visit. Without this the
        // record shows only deliveries, and an attempt that was stopped by a
        // rule leaves nothing at all behind -- the operator remembers it, the
        // log does not.
        recordRefusedSonication(sham);
        setExecutionStatus(QStringLiteral("Nothing was sent. %1")
                               .arg(sonicationSafetyMessages_.join(QStringLiteral("; "))),
                           true);
        return;
    }
    const int shown = std::clamp(stimParamModel_->currentShownRow(), 1,
                                 static_cast<int>(stimParamModel_->rows().size()));
    beam::gui_qt::StimParamRow r = stimParamModel_->rows()[static_cast<std::size_t>(shown - 1)];
    // No duration override here. The schedule's Duration reaches the grid
    // when the sonication becomes current -- switchToSelectedTarget applies
    // it, as updateSonicateSettingsForCurrentSonication.m does -- and
    // SonicateButtonPushed then builds the sonication from the grid. An edit
    // the operator makes after that is the one that goes out, which is what
    // makes those cells worth having.
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
    if (deviceLink_ != nullptr)
        deviceLink_->appendHistory(
            DeviceLinkPanel::Log::Sent,
            QStringLiteral("%1   expect %2")
                .arg(QString::fromStdString(command),
                     QString::fromStdString(beam::serialcom::expectedStimEcho(
                         timing, outcome.dutyCycle))));
    const bool sent = deviceLink_ != nullptr &&
                      deviceLink_->withLink([&](beam::serialcom::SerialLink& link) {
                          try {
                              const bool wrote = beam::serialcom::sendSerialCommand(link, command);
                              const std::string reply = link.readLine();
                              beam::serialcom::clearSerial(link);
                              reportStimEcho(reply, timing, outcome.dutyCycle);
                              status += wrote ? QStringLiteral(" | Sent, reply: \"%1\"")
                                                    .arg(QString::fromStdString(reply))
                                              : QStringLiteral(" | Link closed before send.");
                          } catch (const std::exception& e) {
                              status += QStringLiteral(" | Send failed: %1")
                                            .arg(QString::fromStdString(e.what()));
                          }
                      });
    if (!sent) {
        status += QStringLiteral(" | No serial connection -- command not sent.");
    } else if (deviceLink_ != nullptr) {
        // Checks keep running: the device answers throughout, and a miss
        // here is the only sign the MCU died mid-delivery.
        deviceLink_->setDelivering(true);
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
    treatmentExecutionProgressBar_->setVisible(true);
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
    treatmentExecutionProgressBar_->setValue(0);
    treatmentExecutionProgressBar_->setVisible(false);
    closeOpenLogRow(/*aborted=*/true);
    stopShamAudio();
    setSonicateEnabled(true);
    QString status = QStringLiteral("Sonication aborted by operator.");
    if (deviceLink_ != nullptr)
        deviceLink_->appendHistory(DeviceLinkPanel::Log::Sent,
                                   QStringLiteral("Correction (abort)"));
    const bool sent = deviceLink_ != nullptr &&
                      deviceLink_->withLink([&](beam::serialcom::SerialLink& link) {
                          try {
                              // Written, not waited on: the reply must not hold the
                              // window, and the next check drains whatever arrives.
                              beam::serialcom::sendSerialCommand(link, "Correction");
                              status += QStringLiteral(" Abort command sent.");
                          } catch (const std::exception& e) {
                              status += QStringLiteral(" Abort command failed: %1")
                                            .arg(QString::fromStdString(e.what()));
                          }
                      });
    if (!sent) status += QStringLiteral(" No serial connection -- no command sent.");
    if (deviceLink_ != nullptr) deviceLink_->setDelivering(false);
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
    // Said in a form that is still true after the jump it announces. "
    // Continuing to Report" read as something pending to an operator who was
    // already standing on the Report page looking at it.
    showMessage(QStringLiteral("Visit %1 finished. Its case report is below — export it as a "
                               "PDF or CSV to keep.")
                    .arg(currentVisitNumber()),
                false);
    refresh();
    // Report's own row, by a row the list shows. nextStage() can name a
    // hidden one, which moves the page without moving the highlight.
    goToNextStage();
}

void WorkflowWindow::openTreatmentLibrary(TreatmentLibraryDialog::Page page) {
    // The dialog edits a copy, and applying it replaces the store wholesale, so
    // anything still only in the grid has to reach the store first or it is lost.
    writeProtocolGridToStore();
    writeTargetPointsToStore();
    TreatmentLibraryDialog dialog(targetStore_, page, this);
    if (page == TreatmentLibraryDialog::Page::Targets) {
        // Which list this edits follows where it would be saved, so the dialog
        // cannot say one thing while saveTreatmentTargetStore does another.
        beam::gui::Patient open;
        const bool belongsToSomebody =
            !patientTargetsPath().isEmpty() && patients_.find(currentPatientId_, &open);
        dialog.setTargetScope(
            belongsToSomebody
                ? QStringLiteral("%1 (%2)")
                      .arg(QString::fromStdString(beam::gui::patientDisplayName(open)),
                           QString::fromStdString(open.externalId))
                : QString(),
            shippedDataFile(QStringLiteral("treatment_targets.csv")));
    }
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

    // Disowned before the list is emptied, for the reason loadPatientData
    // spells out: emptying it saves the grid against the target it still
    // thinks is showing, and that save would put the parameters just replaced
    // straight back.
    targetSonications_.clear();
    currentTargetId_ = -1;
    gridTargetId_ = -1;
    stimParamModel_->setRows({});
    targetListWidget_->clear();
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

// The Report stage: who was treated, what they were given this visit, and a
// file that carries both off the device. BeamV0 ports the CRF metadata
// (libs/gui/case_report_form) but has nothing that shows or exports it.
void WorkflowWindow::buildReportBody() {
    reportBody_ = new QWidget(ui_->placeholderPage);
    auto* outer = new QVBoxLayout(reportBody_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    auto* identityGroup = new QGroupBox(QStringLiteral("Case report"), reportBody_);
    auto* identityLayout = new QVBoxLayout(identityGroup);
    identityLayout->setContentsMargins(10, 6, 10, 8);
    identityLayout->setSpacing(6);

    auto* chooser = new QHBoxLayout;
    chooser->setSpacing(8);
    auto* visitLabel = new QLabel(QStringLiteral("Visit"), identityGroup);
    visitLabel->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; }"));
    chooser->addWidget(visitLabel);
    reportVisitCombo_ = new QComboBox(identityGroup);
    applyItemViewPalette(reportVisitCombo_);
    connect(reportVisitCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshReport(); });
    chooser->addWidget(reportVisitCombo_);
    auto* operatorLabel = new QLabel(QStringLiteral("Operator ID"), identityGroup);
    operatorLabel->setStyleSheet(QStringLiteral("QLabel { color: #f2f7f8; }"));
    chooser->addWidget(operatorLabel);
    reportOperatorEdit_ = new QLineEdit(identityGroup);
    reportOperatorEdit_->setPlaceholderText(QStringLiteral("Who ran this visit"));
    // Wide enough to type an initial-and-number into, not wide enough to
    // push the export buttons off the edge of a narrower screen.
    reportOperatorEdit_->setMaximumWidth(160);
    styleInputField(reportOperatorEdit_);
    connect(reportOperatorEdit_, &QLineEdit::textChanged, this, [this] { refreshReport(); });
    chooser->addWidget(reportOperatorEdit_);
    chooser->addStretch(1);
    exportReportButton_ = new QPushButton(QStringLiteral("Export CSV"), identityGroup);
    exportReportButton_->setStyleSheet(
        QStringLiteral("QPushButton { padding: 7px 14px; background: #176b87; color: white; "
                       "border: 0; border-radius: 5px; font-weight: 600; } "
                       "QPushButton:disabled { background: #3b474e; color: #7e8c93; }"));
    connect(exportReportButton_, &QPushButton::clicked, this, [this] { exportCaseReport(); });
    chooser->addWidget(exportReportButton_);
    exportReportPdfButton_ = new QPushButton(QStringLiteral("Export PDF"), identityGroup);
    exportReportPdfButton_->setStyleSheet(exportReportButton_->styleSheet());
    connect(exportReportPdfButton_, &QPushButton::clicked, this,
            [this] { exportCaseReportPdf(); });
    chooser->addWidget(exportReportPdfButton_);
    saveSessionButton_ = new QPushButton(QStringLiteral("Save session..."), identityGroup);
    saveSessionButton_->setStyleSheet(exportReportButton_->styleSheet());
    saveSessionButton_->setToolTip(
        QStringLiteral("Writes this patient's record, the MRI and the registration to one file, "
                       "so the next visit can resume without repeating them."));
    connect(saveSessionButton_, &QPushButton::clicked, this, [this] { saveTreatmentSessionFile(); });
    chooser->addWidget(saveSessionButton_);
    identityLayout->addLayout(chooser);

    reportIdentityLabel_ = new QLabel(identityGroup);
    reportIdentityLabel_->setWordWrap(true);
    reportIdentityLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reportIdentityLabel_->setStyleSheet(QStringLiteral(
        "QLabel { padding: 8px 10px; background: #eef2f4; color: #17313f; border-radius: 4px; }"));
    identityLayout->addWidget(reportIdentityLabel_);
    outer->addWidget(identityGroup);

    auto* deliveredGroup = new QGroupBox(QStringLiteral("What was delivered"), reportBody_);
    auto* deliveredLayout = new QVBoxLayout(deliveredGroup);
    deliveredLayout->setContentsMargins(10, 6, 10, 8);
    reportTable_ = new QTableWidget(0, 9, deliveredGroup);
    reportTable_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Target"), QStringLiteral("Started"),
         QStringLiteral("Ended"), QStringLiteral("Duration (s)"), QStringLiteral("Amplitude"),
         QStringLiteral("Sham"), QStringLiteral("Outcome"), QStringLiteral("Notes")});
    reportTable_->verticalHeader()->setVisible(false);
    reportTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportTable_->setAlternatingRowColors(true);
    reportTable_->setShowGrid(false);
    if (patientTable_ != nullptr) {
        reportTable_->setStyleSheet(patientTable_->styleSheet());
        reportTable_->horizontalHeader()->setStyleSheet(
            patientTable_->horizontalHeader()->styleSheet());
    }
    reportTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (int column = 0; column < reportTable_->columnCount() - 1; ++column)
        reportTable_->horizontalHeader()->setSectionResizeMode(column,
                                                               QHeaderView::ResizeToContents);
    reportTable_->horizontalHeader()->setStretchLastSection(true);
    deliveredLayout->addWidget(reportTable_);
    outer->addWidget(deliveredGroup, 1);

    auto* pastGroup = new QGroupBox(QStringLiteral("Reports already produced"), reportBody_);
    auto* pastLayout = new QVBoxLayout(pastGroup);
    pastLayout->setContentsMargins(10, 6, 10, 8);
    pastLayout->setSpacing(6);
    auto* pastHint = new QLabel(
        QStringLiteral("Kept with this patient's record. Double-click to open one."), pastGroup);
    pastHint->setStyleSheet(QStringLiteral("color: #7e8c93;"));
    pastLayout->addWidget(pastHint);
    pastReportsList_ = new QListWidget(pastGroup);
    pastReportsList_->setMaximumHeight(120);
    pastReportsList_->setStyleSheet(QStringLiteral(
        "QListWidget { background: #ffffff; color: #17313f; border: 1px solid #39758e; } "
        "QListWidget::item:selected { background: #58c7e5; color: #083d4d; }"));
    // Handed to whatever the machine opens PDFs with. BeamAI has no viewer of
    // its own and building one to look at a file it just wrote would be odd.
    connect(pastReportsList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item == nullptr) return;
                const QString path = item->data(Qt::UserRole).toString();
                if (path.isEmpty()) return;
                if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
                    showMessage(QStringLiteral("%1 could not be opened.")
                                    .arg(QDir::toNativeSeparators(path)),
                                true);
                }
            });
    pastLayout->addWidget(pastReportsList_);
    outer->addWidget(pastGroup);

    reportBody_->setVisible(false);
}

// The CRF metadata for the visit on show, built by the ported
// setSessionCaseReportParameters.m so the file this page writes and the
// filename BeamV0 would have chosen agree.
beam::gui::CaseReportForm WorkflowWindow::reportFormForVisit(int visit) const {
    beam::gui::SessionCrfInputs inputs;
    beam::gui::Patient open;
    if (patients_.find(currentPatientId_, &open)) inputs.participantId = open.externalId;
    inputs.visitNumber = visit;
    inputs.hydrogelSize =
        hydrogelSizeCombo_ != nullptr ? hydrogelSizeCombo_->currentText().toStdString() : "Small";
    // The visit is dated by its first delivery, not by today: a report run a
    // week later still describes the day the treatment happened.
    QDateTime when = QDateTime::currentDateTime();
    for (const beam::gui::TreatmentLogRow& row :
         treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit)) {
        if (row.startedAt.empty()) continue;
        const QDateTime stamp =
            QDateTime::fromString(QString::fromStdString(row.startedAt), Qt::ISODateWithMs);
        if (stamp.isValid()) {
            when = stamp;
            break;
        }
    }
    inputs.year = when.date().year();
    inputs.month = when.date().month();
    inputs.day = when.date().day();
    inputs.hour = when.time().hour();
    inputs.minute = when.time().minute();
    inputs.second = when.time().second();

    beam::gui::CaseReportForm crf = beam::gui::buildSessionCaseReportForm(inputs);
    // setSessionCaseReportParameters.m hardcodes a site; this device knows the
    // real one, and the operator is not something BeamV0 asks for at all.
    if (!open.site.empty()) crf.siteId = open.site;
    if (reportOperatorEdit_ != nullptr)
        crf.operatorId = reportOperatorEdit_->text().trimmed().toStdString();
    return crf;
}

void WorkflowWindow::refreshReport() {
    if (reportBody_ == nullptr || reportTable_ == nullptr) return;
    const std::string protocol = currentProtocolName().toStdString();

    // Every visit this patient has, newest first: a report is usually wanted
    // for the one just finished.
    std::set<int> visits;
    for (const beam::gui::TreatmentLogRow& row : treatmentLog_.rows())
        if (row.protocol == protocol) visits.insert(row.visit);
    const int wanted = reportVisitCombo_->currentData().isValid()
                           ? reportVisitCombo_->currentData().toInt()
                           : currentVisitNumber();
    {
        const QSignalBlocker blocker(reportVisitCombo_);
        reportVisitCombo_->clear();
        for (auto visit = visits.rbegin(); visit != visits.rend(); ++visit)
            reportVisitCombo_->addItem(QStringLiteral("Visit %1").arg(*visit), *visit);
        const int index = reportVisitCombo_->findData(wanted);
        if (index >= 0) reportVisitCombo_->setCurrentIndex(index);
    }
    const int visit = reportVisitCombo_->currentData().isValid()
                          ? reportVisitCombo_->currentData().toInt()
                          : 0;

    beam::gui::Patient open;
    if (!patients_.find(currentPatientId_, &open)) {
        reportIdentityLabel_->setText(
            QStringLiteral("No patient is open. Accept a case to produce a report."));
        reportTable_->setRowCount(0);
        exportReportButton_->setEnabled(false);
        exportReportPdfButton_->setEnabled(false);
        refreshPastReports();
        return;
    }

    const std::vector<beam::gui::TreatmentLogRow> rows = treatmentLog_.rowsFor(protocol, visit);
    int delivered = 0;
    int shams = 0;
    int stopped = 0;
    double seconds = 0.0;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        if (!beam::gui::TreatmentLog::delivered(row)) continue;
        ++delivered;
        if (row.sham) ++shams;
        if (beam::gui::TreatmentLog::stopped(row)) ++stopped;
        seconds += row.endTimeSeconds - row.startTimeSeconds;
    }

    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const QString born =
        open.dateOfBirth.empty() ? QString::fromUtf8("\xe2\x80\x94")
                                 : QString::fromStdString(open.dateOfBirth);
    reportIdentityLabel_->setText(
        QStringLiteral("<b>%1</b> &nbsp; Participant %2 &nbsp; Site %3 &nbsp; born %4"
                       "<br>Visit %5 of \"%6\" &nbsp; %7-%8-%9 &nbsp; hydrogel %10"
                       "<br>%11 sonication(s) delivered, %12 sham, %13 stopped early, "
                       "%14 s total output"
                       "<br>Export filename: <tt>%15.csv</tt>")
            .arg(QString::fromStdString(beam::gui::patientDisplayName(open)),
                 QString::fromStdString(open.externalId), QString::fromStdString(crf.siteId),
                 born)
            .arg(visit)
            .arg(currentProtocolName())
            .arg(crf.year, 4, 10, QLatin1Char('0'))
            .arg(crf.month, 2, 10, QLatin1Char('0'))
            .arg(crf.day, 2, 10, QLatin1Char('0'))
            .arg(QString::fromStdString(crf.hydrogelSize))
            .arg(delivered)
            .arg(shams)
            .arg(stopped)
            .arg(seconds, 0, 'f', 1)
            .arg(caseReportFileStem(open, crf, visit)));

    reportTable_->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const beam::gui::TreatmentLogRow& row = rows[index];
        const auto cell = [](const QString& text) { return new QTableWidgetItem(text); };
        const auto timeOfDay = [](const std::string& stamp) {
            const QDateTime when =
                QDateTime::fromString(QString::fromStdString(stamp), Qt::ISODateWithMs);
            return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss"))
                                  : QString::fromUtf8("\xe2\x80\x94");
        };
        const int at = static_cast<int>(index);
        reportTable_->setItem(at, 0, cell(QString::number(row.sequence)));
        reportTable_->setItem(at, 1, cell(QString::fromStdString(row.target)));
        reportTable_->setItem(at, 2, cell(timeOfDay(row.startedAt)));
        reportTable_->setItem(at, 3, cell(timeOfDay(row.endedAt)));
        reportTable_->setItem(
            at, 4, cell(QString::number(row.endTimeSeconds - row.startTimeSeconds, 'f', 1)));
        reportTable_->setItem(at, 5, cell(QString::number(row.amplitude)));
        reportTable_->setItem(at, 6, cell(row.sham ? QStringLiteral("yes") : QString()));
        reportTable_->setItem(
            at, 7,
            cell(!beam::gui::TreatmentLog::delivered(row) ? QStringLiteral("not delivered")
                 : beam::gui::TreatmentLog::stopped(row)  ? QStringLiteral("stopped early")
                                                          : QStringLiteral("completed")));
        QString notes = QString::fromStdString(row.notes);
        notes.replace(QLatin1Char('\n'), QString::fromUtf8(" \xc2\xb7 "));
        reportTable_->setItem(at, 8, cell(notes));
    }
    // A visit that delivered nothing has no case to report.
    exportReportButton_->setEnabled(delivered > 0);
    exportReportPdfButton_->setEnabled(delivered > 0);
    refreshPastReports();
}

void WorkflowWindow::exportCaseReport() {
    const int visit = reportVisitCombo_ != nullptr && reportVisitCombo_->currentData().isValid()
                          ? reportVisitCombo_->currentData().toInt()
                          : 0;
    beam::gui::Patient open;
    if (visit <= 0 || !patients_.find(currentPatientId_, &open)) return;

    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const QString reports = patientReportsDirectory();
    const QString suggested =
        QDir(reports.isEmpty() ? patientDataDirectory() : reports)
            .filePath(caseReportFileStem(open, crf, visit) + QStringLiteral(".csv"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export case report"),
                                                      suggested, QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        showMessage(QStringLiteral("%1 could not be written.").arg(QDir::toNativeSeparators(path)),
                    true);
        return;
    }
    QTextStream out(&file);
    const auto quoted = [](const QString& text) {
        QString escaped = text;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        escaped.replace(QLatin1Char('\n'), QString::fromUtf8(" \xc2\xb7 "));
        return QStringLiteral("\"%1\"").arg(escaped);
    };
    // The metadata first as name/value pairs, then the deliveries. One file
    // that answers both "whose visit was this" and "what happened in it".
    out << "Field,Value\n";
    const auto pair = [&](const char* name, const QString& value) {
        out << name << ',' << quoted(value) << '\n';
    };
    pair("SubjectID", QString::fromStdString(crf.subjectId));
    pair("SubjectName", QString::fromStdString(beam::gui::patientDisplayName(open)));
    pair("DateOfBirth", QString::fromStdString(open.dateOfBirth));
    pair("SiteID", QString::fromStdString(crf.siteId));
    pair("OperatorID", QString::fromStdString(crf.operatorId));
    pair("VisitNumber", QString::number(crf.visitNumber));
    pair("Protocol", currentProtocolName());
    pair("HydrogelSize", QString::fromStdString(crf.hydrogelSize));
    pair("SessionDate", QStringLiteral("%1-%2-%3")
                            .arg(crf.year, 4, 10, QLatin1Char('0'))
                            .arg(crf.month, 2, 10, QLatin1Char('0'))
                            .arg(crf.day, 2, 10, QLatin1Char('0')));
    pair("BEAMSerialNumber", QString::fromStdString(crf.snBeam));
    pair("Transducer1SerialNumber", QString::fromStdString(crf.snTransducer1));
    pair("Transducer2SerialNumber", QString::fromStdString(crf.snTransducer2));
    pair("SoftwareVersion", QString::fromStdString(crf.softwareVersion));
    pair("ExportedAt", QDateTime::currentDateTime().toString(Qt::ISODate));
    out << '\n';
    out << "Number,Target,Started,Ended,DurationSeconds,Amplitude,BD,BI,PD,PI,Sham,Outcome,"
           "ResponsePain,ResponseMood,Notes\n";
    for (const beam::gui::TreatmentLogRow& row :
         treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit)) {
        out << row.sequence << ',' << quoted(QString::fromStdString(row.target)) << ','
            << quoted(QString::fromStdString(row.startedAt)) << ','
            << quoted(QString::fromStdString(row.endedAt)) << ','
            << (row.endTimeSeconds - row.startTimeSeconds) << ',' << row.amplitude << ','
            << row.burstDurationSeconds << ',' << row.burstIntervalSeconds << ','
            << row.pulseDurationSeconds << ',' << row.pulseIntervalSeconds << ','
            << (row.sham ? 1 : 0) << ','
            << quoted(!beam::gui::TreatmentLog::delivered(row) ? QStringLiteral("not delivered")
                      : beam::gui::TreatmentLog::stopped(row)  ? QStringLiteral("stopped early")
                                                               : QStringLiteral("completed"))
            << ',';
        if (row.responsePain) out << *row.responsePain;
        out << ',';
        if (row.responseMood) out << *row.responseMood;
        out << ',' << quoted(QString::fromStdString(row.notes)) << '\n';
    }
    file.close();
    refreshPastReports();
    showMessage(QStringLiteral("Case report written to %1").arg(QDir::toNativeSeparators(path)),
                false);
}

// BeamV0's own computeCrfAutoSaveFilename, sanitised for the filesystem.
// Hard to scan down a folder listing, but a filename is something a data
// management plan can specify. One place, so the format is one edit.
QString WorkflowWindow::caseReportFileStem(const beam::gui::Patient& patient,
                                           const beam::gui::CaseReportForm& crf,
                                           int visit) const {
    Q_UNUSED(patient);
    Q_UNUSED(visit);
    QString stem = QString::fromStdString(beam::gui::computeCrfAutoSaveFilename(crf));
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return stem.isEmpty() ? QStringLiteral("case-report") : stem;
}

// Where a patient's case reports are kept. Under the patient, like everything
// else of theirs, so moving their directory moves their reports with it.
QString WorkflowWindow::patientReportsDirectory() const {
    const QString patientDirectory = patientDataDirectory();
    if (patientDirectory.isEmpty()) return QString();
    const QString reports = QDir(patientDirectory).filePath(QStringLiteral("reports"));
    QDir().mkpath(reports);
    return reports;
}

// A CSV is what a monitor loads; this is what gets printed and filed. Built
// with the subset of HTML/CSS QTextDocument renders -- no nth-child, so the
// zebra striping is written per row.
QString WorkflowWindow::caseReportHtml(int visit) const {
    beam::gui::Patient open;
    if (!patients_.find(currentPatientId_, &open)) return QString();
    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const std::vector<beam::gui::TreatmentLogRow> rows =
        treatmentLog_.rowsFor(currentProtocolName().toStdString(), visit);

    int delivered = 0;
    int shams = 0;
    int stopped = 0;
    double seconds = 0.0;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        if (!beam::gui::TreatmentLog::delivered(row)) continue;
        ++delivered;
        if (row.sham) ++shams;
        if (beam::gui::TreatmentLog::stopped(row)) ++stopped;
        seconds += row.endTimeSeconds - row.startTimeSeconds;
    }

    // One ink, one accent, two greys. A case report is read, not admired, and
    // anything with more colour than this stops surviving a photocopier.
    const QString kInk = QStringLiteral("#1b2a33");
    const QString kMuted = QStringLiteral("#62757e");
    const QString kRule = QStringLiteral("#ccd8dd");
    const QString kAccent = QStringLiteral("#176b87");
    const QString kBand = QStringLiteral("#eef3f5");
    const QString kZebra = QStringLiteral("#f6f9fa");
    const QString kDash = QString::fromUtf8("\xe2\x80\x94");

    const auto esc = [](const QString& text) { return text.toHtmlEscaped(); };
    const auto orDash = [&](const QString& text) { return text.isEmpty() ? kDash : esc(text); };
    const auto str = [](const std::string& text) { return QString::fromStdString(text); };
    const auto timeOfDay = [&](const std::string& stamp) {
        const QDateTime when = QDateTime::fromString(str(stamp), Qt::ISODateWithMs);
        return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss")) : kDash;
    };
    const QString sessionDate = QStringLiteral("%1-%2-%3")
                                    .arg(crf.year, 4, 10, QLatin1Char('0'))
                                    .arg(crf.month, 2, 10, QLatin1Char('0'))
                                    .arg(crf.day, 2, 10, QLatin1Char('0'));

    // A full-width band of solid colour, which is how a rule is drawn here:
    // border on an empty block is not reliably rendered.
    const auto rule = [&](const QString& colour, int points) {
        return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                              "<tr><td style=\"background-color:%1; font-size:%2pt\">&nbsp;</td>"
                              "</tr></table>")
            .arg(colour)
            .arg(points);
    };
    const auto sectionTitle = [&](const QString& text) {
        return QStringLiteral("<p style=\"margin-top:16pt; margin-bottom:3pt; color:%1; "
                              "font-size:8pt; font-weight:bold\">%2</p>")
                   .arg(kAccent, esc(text)) +
               rule(kRule, 1);
    };
    // Key on the left in grey, value on the right in ink.
    const auto field = [&](const QString& name, const QString& value) {
        return QStringLiteral("<tr>"
                              "<td width=\"42%\" style=\"padding:3pt 6pt 3pt 0; color:%1\">%2</td>"
                              "<td style=\"padding:3pt 0; color:%3\">%4</td>"
                              "</tr>")
            .arg(kMuted, esc(name), kInk, orDash(value));
    };

    QString html = QStringLiteral(
                       "<html><body style=\"font-family:'Helvetica','Arial',sans-serif; "
                       "font-size:9pt; color:%1\">")
                       .arg(kInk);

    // Masthead: what this is on the left, who it is about on the right.
    html += QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                "<td valign=\"bottom\" style=\"color:%1; font-size:17pt; font-weight:bold\">"
                "CASE REPORT</td>"
                "<td valign=\"bottom\" align=\"right\" style=\"font-size:9pt; color:%2\">"
                "<b style=\"color:%3; font-size:11pt\">%4</b><br>%5 &nbsp;&middot;&nbsp; %6"
                "</td></tr></table>")
                .arg(kAccent, kMuted, kInk,
                     esc(str(beam::gui::patientDisplayName(open))),
                     esc(str(crf.subjectId)), esc(str(crf.siteId)));
    html += rule(kAccent, 3);

    // The two things a reader checks first, side by side.
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                           "<td width=\"48%\" valign=\"top\">");
    html += sectionTitle(QStringLiteral("PARTICIPANT"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("Participant ID"), str(crf.subjectId));
    html += field(QStringLiteral("Name"), str(beam::gui::patientDisplayName(open)));
    html += field(QStringLiteral("Date of birth"), str(open.dateOfBirth));
    html += field(QStringLiteral("Site ID"), str(crf.siteId));
    html += QStringLiteral("</table></td><td width=\"4%\"></td>"
                           "<td width=\"48%\" valign=\"top\">");
    html += sectionTitle(QStringLiteral("SESSION"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("Visit number"), QString::number(crf.visitNumber));
    html += field(QStringLiteral("Date"), sessionDate);
    html += field(QStringLiteral("Protocol"), currentProtocolName());
    html += field(QStringLiteral("Operator"), str(crf.operatorId));
    html += field(QStringLiteral("Hydrogel size"), str(crf.hydrogelSize));
    html += QStringLiteral("</table></td></tr></table>");

    // The four numbers somebody skims for, given room to be skimmed.
    const auto figure = [&](const QString& value, const QString& label, bool last) {
        return QStringLiteral("<td width=\"25%\" align=\"center\" "
                              "style=\"background-color:%1; padding:7pt 4pt; %2\">"
                              "<span style=\"font-size:15pt; font-weight:bold; color:%3\">%4"
                              "</span><br>"
                              "<span style=\"font-size:7.5pt; color:%5\">%6</span></td>")
            .arg(kBand,
                 last ? QString()
                      : QStringLiteral("border-right:2px solid #ffffff"),
                 kAccent, esc(value), kMuted, esc(label));
    };
    html += QStringLiteral("<p style=\"margin-top:14pt; margin-bottom:0\"></p>");
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>");
    html += figure(QString::number(delivered), QStringLiteral("sonications delivered"), false);
    html += figure(QString::number(shams), QStringLiteral("of which sham"), false);
    html += figure(QString::number(stopped), QStringLiteral("stopped early"), false);
    html += figure(QStringLiteral("%1 s").arg(seconds, 0, 'f', 1),
                   QStringLiteral("total output"), true);
    html += QStringLiteral("</tr></table>");

    html += sectionTitle(QStringLiteral("SONICATIONS"));
    // Qt honours neither per-cell padding nor cellpadding here, so the gap
    // between a right-aligned number and the left-aligned name beside it has
    // to come from the column width itself -- otherwise "1" and "SCC1" meet
    // and the row reads "1SCC1".
    const auto headCell = [&](const QString& text, const QString& align,
                              const QString& width = QString()) {
        return QStringLiteral("<td align=\"%1\"%2 style=\"background-color:%3; color:%4; "
                              "font-size:7.5pt; font-weight:bold; "
                              "border-bottom:1px solid %5\">%6</td>")
            .arg(align,
                 width.isEmpty() ? QString() : QStringLiteral(" width=\"%1\"").arg(width),
                 kBand, kMuted, kRule, esc(text));
    };
    // cellpadding, not per-cell padding: Qt's rich text honours the table
    // attribute and ignores the CSS. Without it the right-aligned sequence
    // number sits flush against the left-aligned target name -- "1SCC1".
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" "
                           "style=\"font-size:8pt\"><tr>");
    // The sequence number needs room on its right or it runs into the target
    // name: one is right-aligned, the next left-aligned, and they meet.
    html += headCell(QStringLiteral("#"), QStringLiteral("left"), QStringLiteral("5%"));
    html += headCell(QStringLiteral("Target"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Started"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Ended"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Dur s"), QStringLiteral("right"));
    html += headCell(QStringLiteral("Amp"), QStringLiteral("right"));
    html += headCell(QStringLiteral("BD"), QStringLiteral("right"));
    html += headCell(QStringLiteral("BI"), QStringLiteral("right"));
    html += headCell(QStringLiteral("PD"), QStringLiteral("right"));
    html += headCell(QStringLiteral("PI"), QStringLiteral("right"));
    html += headCell(QStringLiteral("Sham"), QStringLiteral("center"));
    html += headCell(QStringLiteral("Outcome"), QStringLiteral("left"));
    html += headCell(QStringLiteral("Pain"), QStringLiteral("right"));
    html += headCell(QStringLiteral("Mood"), QStringLiteral("right"));
    html += QStringLiteral("</tr>");

    int index = 0;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        // Striped in the generated markup: Qt's CSS has no nth-child.
        const QString background = (index++ % 2) == 1 ? kZebra : QStringLiteral("#ffffff");
        const bool ran = beam::gui::TreatmentLog::delivered(row);
        // A row that never fired is greyed, so the eye separates what was
        // given from what was only planned without having to read a column.
        const QString ink = ran ? kInk : kMuted;
        const auto cell = [&](const QString& text, const QString& align,
                             const QString& width = QString()) {
            return QStringLiteral("<td align=\"%1\"%2 style=\"background-color:%3; color:%4; "
                                  "border-bottom:1px solid %5\">%6</td>")
                .arg(align,
                     width.isEmpty() ? QString() : QStringLiteral(" width=\"%1\"").arg(width),
                     background, ink, kRule,
                     text.isEmpty() ? QString() : esc(text));
        };
        const QString outcome = !ran ? QStringLiteral("not delivered")
                                : beam::gui::TreatmentLog::stopped(row)
                                    ? QStringLiteral("stopped early")
                                    : QStringLiteral("completed");
        html += QStringLiteral("<tr>");
        html += cell(QString::number(row.sequence), QStringLiteral("left"), QStringLiteral("5%"));
        html += cell(str(row.target), QStringLiteral("left"));
        html += cell(timeOfDay(row.startedAt), QStringLiteral("left"));
        html += cell(timeOfDay(row.endedAt), QStringLiteral("left"));
        html += cell(QString::number(row.endTimeSeconds - row.startTimeSeconds, 'f', 1),
                     QStringLiteral("right"));
        html += cell(QString::number(row.amplitude), QStringLiteral("right"));
        html += cell(QString::number(row.burstDurationSeconds), QStringLiteral("right"));
        html += cell(QString::number(row.burstIntervalSeconds), QStringLiteral("right"));
        html += cell(QString::number(row.pulseDurationSeconds), QStringLiteral("right"));
        html += cell(QString::number(row.pulseIntervalSeconds), QStringLiteral("right"));
        html += cell(row.sham ? QStringLiteral("yes") : QString(), QStringLiteral("center"));
        html += cell(outcome, QStringLiteral("left"));
        html += cell(row.responsePain ? QString::number(*row.responsePain) : QString(),
                     QStringLiteral("right"));
        html += cell(row.responseMood ? QString::number(*row.responseMood) : QString(),
                     QStringLiteral("right"));
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    if (rows.empty()) {
        html += QStringLiteral("<p style=\"color:%1; font-size:8.5pt\">Nothing was scheduled "
                               "for this visit.</p>")
                    .arg(kMuted);
    }

    // Notes get their own section: they run to several lines and would make
    // the table above unreadable.
    QString notes;
    for (const beam::gui::TreatmentLogRow& row : rows) {
        if (row.notes.empty()) continue;
        notes += QStringLiteral("<tr>"
                                "<td width=\"14%\" valign=\"top\" style=\"padding:3pt 6pt 3pt 0; "
                                "color:%1\">Sonication %2</td>"
                                "<td style=\"padding:3pt 0\">%3</td></tr>")
                     .arg(kMuted)
                     .arg(row.sequence)
                     .arg(esc(str(row.notes)).replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }
    if (!notes.isEmpty()) {
        html += sectionTitle(QStringLiteral("NOTES"));
        html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" "
                               "style=\"font-size:8.5pt\">") +
                notes + QStringLiteral("</table>");
    }

    html += sectionTitle(QStringLiteral("DEVICE"));
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                           "<td width=\"48%\" valign=\"top\">"
                           "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("BEAM serial number"), str(crf.snBeam));
    html += field(QStringLiteral("Software version"), str(crf.softwareVersion));
    html += QStringLiteral("</table></td><td width=\"4%\"></td>"
                           "<td width=\"48%\" valign=\"top\">"
                           "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    html += field(QStringLiteral("Transducer 1"), str(crf.snTransducer1));
    html += field(QStringLiteral("Transducer 2"), str(crf.snTransducer2));
    html += QStringLiteral("</table></td></tr></table>");

    html += QStringLiteral("<p style=\"margin-top:18pt; margin-bottom:0\"></p>");
    html += rule(kRule, 1);
    html += QStringLiteral("<p style=\"margin-top:4pt; font-size:7.5pt; color:%1\">"
                           "Produced by BeamAI on %2 from this patient's treatment log. "
                           "A blank field is one BeamAI does not collect yet.</p>")
                .arg(kMuted, esc(QDateTime::currentDateTime().toString(Qt::ISODate)));
    html += QStringLiteral("</body></html>");
    return html;
}

void WorkflowWindow::exportCaseReportPdf() {
    const int visit = reportVisitCombo_ != nullptr && reportVisitCombo_->currentData().isValid()
                          ? reportVisitCombo_->currentData().toInt()
                          : 0;
    beam::gui::Patient open;
    if (visit <= 0 || !patients_.find(currentPatientId_, &open)) return;

    const beam::gui::CaseReportForm crf = reportFormForVisit(visit);
    const QString reports = patientReportsDirectory();
    const QString suggested =
        QDir(reports.isEmpty() ? patientDataDirectory() : reports)
            .filePath(caseReportFileStem(open, crf, visit) + QStringLiteral(".pdf"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export case report"),
                                                      suggested, QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
    writer.setTitle(QStringLiteral("Case report — %1 visit %2")
                        .arg(QString::fromStdString(open.externalId))
                        .arg(visit));
    QTextDocument document;
    // No setPageSize: those would be device units, and a document given a page
    // size is treated as already paginated, skipping the dpi scaling. That is
    // how 10pt text came out twelve times too small.
    document.setDefaultFont(QFont(QStringLiteral("Helvetica"), 10));
    document.setHtml(caseReportHtml(visit));
    document.print(&writer);

    if (!QFileInfo::exists(path)) {
        showMessage(QStringLiteral("%1 could not be written.").arg(QDir::toNativeSeparators(path)),
                    true);
        return;
    }
    refreshPastReports();
    showMessage(QStringLiteral("Case report written to %1").arg(QDir::toNativeSeparators(path)),
                false);
}

// What has already been produced for this patient. A report that cannot be
// found again is a report that gets made twice.
void WorkflowWindow::refreshPastReports() {
    if (pastReportsList_ == nullptr) return;
    pastReportsList_->clear();
    const QString reports = patientReportsDirectory();
    if (reports.isEmpty()) {
        pastReportsList_->addItem(QStringLiteral("No patient is open."));
        pastReportsList_->setEnabled(false);
        return;
    }
    pastReportsList_->setEnabled(true);
    QDir directory(reports);
    directory.setNameFilters({QStringLiteral("*.pdf"), QStringLiteral("*.csv")});
    // Newest first: the one just made is the one being looked for.
    const QFileInfoList files = directory.entryInfoList(QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        pastReportsList_->addItem(
            QStringLiteral("No reports yet for this patient. Exporting one puts it here."));
        return;
    }
    for (const QFileInfo& file : files) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2")
                .arg(file.fileName(),
                     file.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        item->setData(Qt::UserRole, file.absoluteFilePath());
        item->setToolTip(QDir::toNativeSeparators(file.absoluteFilePath()));
        pastReportsList_->addItem(item);
    }
}

// Reads a file the application itself wrote, so the session carries exactly
// what is on disk rather than a second rendering of it.
QString WorkflowWindow::patientFileText(const QString& name) const {
    const QString directory = patientDataDirectory();
    if (directory.isEmpty()) return QString();
    QFile file(QDir(directory).filePath(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(file.readAll());
}

void WorkflowWindow::saveTreatmentSessionFile() {
    beam::gui::Patient open;
    if (!patients_.find(currentPatientId_, &open)) {
        showMessage(QStringLiteral("Accept a case before saving a session."), true);
        return;
    }
    // Written out first, so the session and the device agree.
    savePatientData();

    beam::gui::TreatmentSessionFile session;
    session.participantId = open.externalId;
    session.siteId = open.site;
    session.mriPath = mriPath_.toStdString();
    session.appVersion = QCoreApplication::applicationVersion().toStdString();
    session.treatmentLogCsv = patientFileText(QStringLiteral("treatment_log.csv")).toStdString();
    session.visitStateCsv =
        patientFileText(QStringLiteral("treatment_visit_state.csv")).toStdString();
    session.targetsCsv = patientFileText(QStringLiteral("treatment_targets.csv")).toStdString();
    if (session.targetsCsv.empty()) {
        // So the session is self-contained wherever it is opened.
        QFile seed(shippedDataFile(QStringLiteral("treatment_targets.csv")));
        if (seed.open(QIODevice::ReadOnly | QIODevice::Text))
            session.targetsCsv = QString::fromUtf8(seed.readAll()).toStdString();
    }

    session.fiducials = fiducials_;
    session.fiducialLocated = fiducialLocated_;
    session.fiducialConfirmed = fiducialConfirmed_;
    session.fiducialConfidence = fiducialConfidence_;
    session.fiducialStability = fiducialStability_;
    if (leftHorizontalPositionSlider_ != nullptr) {
        session.lock.leftHorizontal = leftHorizontalPositionSlider_->value();
        session.lock.leftVertical = leftVerticalPositionSlider_->value();
        session.lock.rightHorizontal = rightHorizontalPositionSlider_->value();
        session.lock.rightVertical = rightVerticalPositionSlider_->value();
    }
    const auto complete = [this](beam::gui::WorkflowStage stage) {
        return workflow_.state(stage).status == beam::gui::WorkflowStatus::Complete;
    };
    session.imagingAccepted = complete(beam::gui::WorkflowStage::Imaging);
    session.registrationAccepted = complete(beam::gui::WorkflowStage::Registration);
    if (mriLoaded_) {
        session.mri.present = true;
        session.mri.volume = mriVolume_;
        session.mri.axes = mriAxes_;
    }

    // Beside the MRIs, where BeamV0 puts them; CRF auto-save name.
    const beam::gui::CaseReportForm crf = reportFormForVisit(currentVisitNumber());
    const QString suggested =
        QDir(imagingStartDirectory())
            .filePath(caseReportFileStem(open, crf, currentVisitNumber()) +
                      QStringLiteral(".beamsession"));
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Save session"), suggested,
                                     QStringLiteral("Beam session (*.beamsession)"));
    if (path.isEmpty()) return;
    try {
        beam::gui::writeTreatmentSession(path.toStdString(), session);
    } catch (const std::exception& error) {
        showMessage(QStringLiteral("The session could not be saved: %1").arg(error.what()), true);
        return;
    }
    showMessage(QStringLiteral("Session saved to %1%2")
                    .arg(QDir::toNativeSeparators(path),
                         session.mri.present ? QString()
                                             : QStringLiteral(" (no MRI was loaded to include)")),
                false);
}

void WorkflowWindow::chooseTreatmentSession() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load session"), imagingStartDirectory(),
        QStringLiteral("Beam session (*.beamsession);;All files (*)"));
    if (path.isEmpty()) return;
    rememberImagingDirectory(path);

    beam::gui::TreatmentSessionFile session;
    try {
        session = beam::gui::readTreatmentSession(path.toStdString());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("Load session"),
                              QString::fromUtf8(error.what()));
        showMessage(QStringLiteral("The session was not loaded; nothing was changed."), true);
        return;
    }
    loadTreatmentSessionFile(session, path);
}

void WorkflowWindow::loadTreatmentSessionFile(const beam::gui::TreatmentSessionFile& session,
                                              const QString& path) {
    if (deliveryRunning()) {
        showMessage(QStringLiteral("A sonication is still going out. Stop it before loading a "
                                   "session."),
                    true);
        return;
    }
    const QString participant = QString::fromStdString(session.participantId);

    // Runs the whole patient switch: outgoing written out, theirs cleared.
    beam::gui::Patient details;
    details.externalId = session.participantId;
    details.site = session.siteId;
    selectPatient(details, /*advancing=*/true);
    beam::gui::Patient opened;
    if (!patients_.find(currentPatientId_, &opened) ||
        opened.externalId != session.participantId) {
        showMessage(QStringLiteral("%1 could not be opened; the session was not loaded.")
                        .arg(participant),
                    true);
        return;
    }

    // The session IS the case record, so loading it accepts it.
    workflow_.complete(beam::gui::WorkflowStage::CaseSetup, nullptr);
    workflow_.complete(beam::gui::WorkflowStage::SystemCheck, nullptr);

    const QString directory = patientDataDirectory();
    // No prompt, as BeamV0; what is replaced is renamed aside, not destroyed.
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    int setAside = 0;
    const auto put = [&](const QString& name, const std::string& text) {
        if (text.empty()) return;
        const QString target = QDir(directory).filePath(name);
        if (QFileInfo::exists(target) &&
            QFile::rename(target, QStringLiteral("%1.replaced-%2").arg(target, stamp))) {
            ++setAside;
        }
        QFile file(target);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            file.write(QByteArray::fromStdString(text));
    };
    put(QStringLiteral("treatment_log.csv"), session.treatmentLogCsv);
    put(QStringLiteral("treatment_visit_state.csv"), session.visitStateCsv);
    put(QStringLiteral("treatment_targets.csv"), session.targetsCsv);
    loadPatientData();

    if (!session.mri.present) {
        showMessage(QStringLiteral("Session loaded for %1. It carries no MRI, so imaging and "
                                   "registration still have to be done.")
                        .arg(participant),
                    false);
        return;
    }

    // BeamV0 clears both registration flags on load; the head has moved.
    installMri(session.mri.volume, session.mri.axes,
               QString::fromStdString(session.mriPath));
    fiducials_ = session.fiducials;
    registrationSourceFiducials_ = fiducials_;
    fiducialLocated_ = session.fiducialLocated;
    // Confirming the restored marks is the first step of registering again.
    fiducialConfirmed_.fill(false);
    fiducialDetected_ = fiducialLocated_;
    fiducialConfidence_ = session.fiducialConfidence;
    fiducialStability_ = session.fiducialStability;
    sourceFiducialLocated_ = fiducialLocated_;
    sourceFiducialConfirmed_ = fiducialConfirmed_;
    for (QSlider* slider : {leftHorizontalPositionSlider_, leftVerticalPositionSlider_,
                            rightHorizontalPositionSlider_, rightVerticalPositionSlider_}) {
        if (slider == nullptr) continue;
        const QSignalBlocker blocker(slider);
        slider->setValue(slider == leftHorizontalPositionSlider_ ? session.lock.leftHorizontal
                         : slider == leftVerticalPositionSlider_ ? session.lock.leftVertical
                         : slider == rightHorizontalPositionSlider_
                             ? session.lock.rightHorizontal
                             : session.lock.rightVertical);
    }
    populateRegistrationTable();
    showMriPreviews();

    QString restored = QStringLiteral("Session loaded for %1.").arg(participant);
    if (session.imagingAccepted) workflow_.complete(beam::gui::WorkflowStage::Imaging, nullptr);
    // Nothing was invalidated: the load left this stage waiting to be done.
    workflow_.begin(beam::gui::WorkflowStage::Registration, nullptr);
    restored += fiducials_.size() == 6
                    ? QStringLiteral(" MRI and fiducials restored; register the array and accept.")
                    : QStringLiteral(" MRI restored; the fiducials still have to be placed.");
    // Never restored: gel dries and the subject moves.
    clearCouplingMeasurement();
    goToNextStage();
    refresh();
    // A load already on this page arrives at nothing, so jump explicitly.
    if (!fiducials_.empty() && selectedStage_ == beam::gui::WorkflowStage::Registration) {
        ui_->registrationTable->setCurrentCell(0, 0);
        registrationStartFiducialRow_ = 0;
        ui_->registrationReturnStartButton->setEnabled(true);
        // Deferred: panning needs the final view size.
        QTimer::singleShot(0, this, [this] { navigateToRegistrationFiducial(0); });
    }
    showMessage(restored + QStringLiteral(" Coupling must be measured again before sonicating."),
                false);
    Q_UNUSED(path);
}

}  // namespace beam::app
