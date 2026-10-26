#pragma once

// The Treatment plan stage's body: BeamV0.mlapp's SonicateTab, re-homed into this application's

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

// BeamV0's own target names, the ranking getTopTargetsFromTreatmentProtocolTable.m works from,
const char* const kTargetNames[] = {"SCC1",  "SCC2",  "SCC3",  "SCC4",  "SCC5",  "SCC6",
                                    "aMCC1", "aMCC2", "aMCC3", "aMCC4", "aMCC5", "aMCC6"};
// The workflow page paints a dark work surface and sets a light Text colour for it, which item
void applyItemViewPalette(QWidget* view) {
    QPalette palette = view->palette();
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::AlternateBase, QColor(0xf0, 0xf4, 0xf6));
    palette.setColor(QPalette::Text, QColor(0x17, 0x31, 0x3f));
    palette.setColor(QPalette::WindowText, QColor(0x17, 0x31, 0x3f));
    palette.setColor(QPalette::Highlight, QColor(0x28, 0x88, 0xa2));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    // A closed QComboBox paints its current text with the Button roles, not the Base/Text ones
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

// A pane heading, painted exactly as kHeaderStyle paints a table column heading: same ink, same
const char* const kPaneHeadingStyle =
    "QLabel { color: #17313f; background: #dcebef; padding: 4px; font-weight: 600; "
    "border-bottom: 1px solid #b7cbd1; }";

// The height kHeaderStyle's sections are pinned to, so a heading beside a table lines up with
constexpr int kHeadingHeight = 22;

// Where a Target List row keeps its name: see addTargetListRow for why it cannot be the item's
constexpr int kTargetNameRole = Qt::UserRole + 1;
// A stable key for the target's own sonications, so renaming a target or inserting one above it
constexpr int kTargetIdRole = Qt::UserRole + 2;
// Set on the Target List row the Current Sonication Number points at.
constexpr int kCurrentTargetRole = Qt::UserRole + 3;

// The marker table's own selection fill, reused so a clicked row looks the same on both pages.
const QColor kRowSelectedFill(212, 221, 226);
const QColor kCurrentRowFill(0x1f, 0x4e, 0x79);

// The countdown bar takes the colour of the tick the row will end up with: green while
QString progressBarStyle(const QColor& chunk) {
    return QStringLiteral("QProgressBar { background: #16303a; border: 1px solid #2b6172; "
                          "border-radius: 4px; } "
                          "QProgressBar::chunk { background: %1; border-radius: 3px; }")
        .arg(chunk.name());
}

// The in-row Sonicate button, and what it becomes while a sonication is actually being sent:
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

// The sham: play, hollow.
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

// Yellow, not amber: it has to be told apart from the blue beside it and the red beyond it at a
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

// Red, and red is on this row for nothing else: it is the one control that interrupts a
const char* const kStopStyle =
    "QPushButton { background: #d32f2f; color: #ffffff; border: 1px solid #8e1b1b; "
    "border-radius: 4px; padding: 0 4px; } "
    "QPushButton:hover { background: #e14343; } "
    "QPushButton:pressed { background: #a81f1f; } "
    "QPushButton:disabled { background: #9e2222; color: #f0cccc; border-color: #6e1313; }";

// A running button pulses between these two, in the colour of the button it already is -- blue
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

// The per-row Target List icons.
const char* const kRowIconStyle =
    "QToolButton { background: #f7fafb; color: #17313f; border: 1px solid #9eacb4; border-radius: 3px; "
    "min-width: 20px; max-width: 20px; min-height: 18px; max-height: 18px; padding: 0; "
    "font-family: 'Segoe UI Symbol'; font-size: 11px; font-weight: 700; } "
    "QToolButton:hover { background: #e3f2f6; border-color: #2888a2; } "
    "QToolButton:pressed { background: #cce7ee; }";

// The selection highlight paints over whatever background the model asked for, so selecting the
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
                // Deliberately wider than the 3 MPa BeamV0 names as its maximum: an amplitude
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
                // 0.005 and 0.01 are the shipped defaults; five decimals leaves room below
                spin->setDecimals(5);
                spin->setRange(0.0, 3600.0);
                spin->setSingleStep(0.001);
                break;
            default:
                spin->setDecimals(4);
                spin->setRange(-1.0e6, 1.0e6);
                break;
        }
        // Opens with the value selected, so typing replaces it rather than landing in the
        spin->selectAll();
        return spin;
    }
};

// The Target List row the current sonication fires at, in the same dark blue as that row in the
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

// Response Pain and Response Mood run -2..2 (getResponseFromTreatmentProtocolData.m rounds and
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
    // Stretch rather than beam_app's fixed 72px: the same 12 columns have a whole window's
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // A QTableView's own minimumSizeHint grows once a populated model is attached, and that
    view->setMinimumHeight(120);
    view->setItemDelegate(new CurrentRowDelegate(view));
    QPalette palette = view->palette();
    palette.setColor(QPalette::Highlight, kRowSelectedFill);
    palette.setColor(QPalette::HighlightedText, QColor(0x17, 0x31, 0x3f));
    view->setPalette(palette);
}

}  // namespace

namespace {

// BeamV0 loads its masking track from sonicationSound.mat, which is not redistributable and not
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

    // Walk the chunks rather than assuming data starts at 36: a file written by another tool
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

// Read once: the file does not change while the application runs, and a sham should not stall
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
}  // namespace beam::app
