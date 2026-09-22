#include "gui_qt/startup_screen_window.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QAbstractScrollArea>
#include <QAction>
#include <QCheckBox>
#include <QStackedWidget>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QHBoxLayout>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>

#include "gui/countdown_presenter.hpp"
#include "gui/initial_placement_presenter.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "gui_qt/mri_slice_view.hpp"
#include "gui_qt/pulse_waveform_view.hpp"
#include "gui_qt/sonication_tab_chart.hpp"
#include "gui_qt/stim_param_table_model.hpp"
#include "gui_qt/total_sonication_view.hpp"
#include "gui_qt/treatment_protocol_table_model.hpp"
#include "gui_qt/zoomable_image_view.hpp"
#include "stimulation/events.hpp"
#include "ui_startup_screen.h"

namespace beam::gui_qt {

namespace {

const QString kLastMriDirSettingsKey = QStringLiteral("lastMriDir");

// Port of setExampleTargetImages.m's per-flag sag/cor/axial file choice
// (now embedded Qt resources instead of imread() from disk). Preserves
// its ACC quirk verbatim: axialIm actually loads ACCWhiteMatter.bmp and
// corIm loads ACCAxial.bmp, not the axial-suffixed file for either.
struct ExampleTargetImages {
    QString sag, cor, axial;
};
std::optional<ExampleTargetImages> exampleTargetImagePaths(const QString& exampleFlag) {
    if (exampleFlag == QStringLiteral("ACC")) {
        return ExampleTargetImages{":/example_targets/ACCSag.bmp", ":/example_targets/ACCAxial.bmp",
                                   ":/example_targets/ACCWhiteMatter.bmp"};
    }
    if (exampleFlag == QStringLiteral("SCC1")) {
        return ExampleTargetImages{":/example_targets/SCC1Sag.bmp", ":/example_targets/SCC1Cor.bmp",
                                   ":/example_targets/SCC1Axial.bmp"};
    }
    if (exampleFlag == QStringLiteral("aMCC1")) {
        return ExampleTargetImages{":/example_targets/aMCC1Sag.bmp", ":/example_targets/aMCC1Cor.bmp",
                                   ":/example_targets/aMCC1Axial.bmp"};
    }
    return std::nullopt;
}

// The source's [0,1,0] green (registration complete) vs. getOffColor.m's
// [0.85,0.33,0.10] (an orange-red, not gray -- MATLAB's default axes
// "off"/uilamp color), as CSS. Keeps the .ui's own circular lamp shape.
void setLampOn(QLabel* lamp, bool on) {
    lamp->setStyleSheet(on ? QStringLiteral("background-color: rgb(0,255,0); border-radius: 10px;")
                           : QStringLiteral("background-color: rgb(217,84,26); border-radius: 10px;"));
}

// Same app-local .ini (not the Windows registry) BeamMainWindow's own
// loadMriFile/loadMriFolder already use.
QSettings appSettings() {
    return QSettings(QCoreApplication::applicationDirPath() + QStringLiteral("/beam.ini"), QSettings::IniFormat);
}

// Swaps a Designer placeholder widget for a real one at the same
// geometry/parent. Qt Designer's own widget-promotion mechanism only
// supports classes with a bare QWidget*-parent constructor, which
// MriSliceView's required `plane` argument doesn't fit -- this is the
// manual equivalent, used for every custom widget this window needs.
// Deletes `placeholder`; the caller must not dereference it again.
//
// Deliberately does not copy `placeholder->isVisible()`: this runs
// before the top-level window is ever shown, so isVisible() always
// reads false here regardless of the .ui's own `visible` property --
// explicitly propagating that would permanently hide the replacement
// (an explicit setVisible(false) sticks even after the window is later
// shown). None of the widgets this replaces are ever meant to start
// hidden, so the replacement is simply left at its own default (visible).
template <typename T, typename... Args>
T* replaceWidget(QWidget* placeholder, Args&&... args) {
    QWidget* parent = placeholder->parentWidget();
    const QRect geometry = placeholder->geometry();
    delete placeholder;
    auto* replacement = new T(std::forward<Args>(args)...);
    replacement->setParent(parent);
    replacement->setGeometry(geometry);
    return replacement;
}

// Small hand-drawn glyph icons for the target list's row buttons --
// avoids relying on platform standard icons, which don't cover
// create/rename/move-to-target.
QIcon buildGlyphIcon(void (*draw)(QPainter&)) {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(80, 80, 80), 1.4));
    draw(painter);
    return QIcon(pixmap);
}

QIcon createTargetIcon() {
    return buildGlyphIcon([](QPainter& p) {
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
    });
}

QIcon renameTargetIcon() {
    return buildGlyphIcon([](QPainter& p) {
        p.drawLine(QPointF(3.5, 12.5), QPointF(10, 6));
        p.setBrush(QColor(80, 80, 80));
        QPolygonF tip;
        tip << QPointF(10, 6) << QPointF(12, 4) << QPointF(13, 5) << QPointF(11, 7);
        p.drawPolygon(tip);
        p.drawLine(QPointF(3.5, 12.5), QPointF(5, 12));
    });
}

QIcon deleteTargetIcon() {
    return buildGlyphIcon([](QPainter& p) {
        p.drawLine(QPointF(4, 5), QPointF(12, 5));
        p.drawRect(QRectF(5, 5, 6, 8));
        p.drawLine(QPointF(6.5, 3), QPointF(9.5, 3));
        p.drawLine(QPointF(7, 7), QPointF(7, 11));
        p.drawLine(QPointF(9, 7), QPointF(9, 11));
    });
}

QIcon saveTargetIcon() {
    return buildGlyphIcon([](QPainter& p) {
        p.drawLine(QPointF(3, 8), QPointF(6.5, 12));
        p.drawLine(QPointF(6.5, 12), QPointF(13, 4));
    });
}

QIcon cancelTargetIcon() {
    return buildGlyphIcon([](QPainter& p) {
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
    });
}

}  // namespace

DesignerStartupWindow::DesignerStartupWindow(QWidget* parent)
    : QMainWindow(parent),
      ui_(new Ui::BeamStartupScreen),
      countdownTimer_(new QTimer(this)),
      pulseWaveformView_(nullptr),
      burstWaveformView_(nullptr),
      totalSonicationView_(nullptr) {
    // gui_qt is a static lib, so nothing forces the linker to keep the
    // example_targets.qrc resource-registration object unless something
    // calls into it explicitly -- without this, QPixmap(":/example_targets/...")
    // silently returns a null pixmap in the final beam_app executable.
    Q_INIT_RESOURCE(example_targets);
    // This .ui's own design is an explicit light theme (centralwidget's
    // white background, set directly in the .ui) -- but this app's
    // overall palette otherwise follows the OS theme, which on a dark-
    // themed system gives QPalette::WindowText a light color. MriSliceView
    // paints its title/tick-label/axis-caption text manually using
    // exactly that palette color (see its own paintEvent), so all of it
    // rendered light-on-white -- invisible (user: "the texts of the
    // image views, the scale of the axis values ... all are missing").
    // Forced here, before setupUi/child construction, so every child
    // widget inherits a palette matching the .ui's own chosen theme
    // rather than whatever the OS happens to be set to.
    setPalette(style()->standardPalette());

    ui_->setupUi(this);

    // setPalette() above doesn't reach centralwidget's descendants: any
    // Qt Style Sheet property set on a widget (centralwidget's own
    // `background-color`, set directly in the .ui) breaks normal
    // QPalette inheritance from ancestors for that widget's whole
    // subtree -- unstyled properties fall back to the app-wide default
    // (the OS-driven dark one) rather than whatever an ancestor's
    // setPalette() set, which is what left MriSliceView's manually-
    // painted text (it reads palette().color(QPalette::WindowText))
    // white-on-white.
    //
    // Patching that by adding an explicit `color:` to centralwidget's
    // own style sheet does fix the color, but a style sheet on ANY
    // widget puts its *entire* descendant subtree into Qt's "styled"
    // painting mode -- including MriSliceView's own internal spacer
    // widgets (titleSpacer_/bottomAxisSpacer_/sliderValueSpacer_), which
    // are otherwise plain, non-painting
    // QWidgets that let MriSliceView's own paintEvent output show
    // through. Styled mode makes them paint an opaque background of
    // their own, which then covers that exact text -- title/tick-
    // numbers/axis-caption all render at the *correct*, now-black
    // position, just hidden behind an opaque sibling on top. (Ruler tick
    // marks stayed visible only because they're drawn *outside* any
    // spacer's rect, in the plain margin next to the image -- same class
    // of "a child widget paints over the parent's own paintEvent output"
    // issue already documented on MriSliceView's ruler-line comment.)
    //
    // Fix: don't use a style sheet for this at all. A plain QPalette
    // achieves the desired background/text look without ever putting
    // descendants into styled mode.
    //
    // Dark theme, not the .ui's own literal white (confirmed against a
    // live BeamV0.mlapp screenshot to genuinely be pure white -- this is
    // a deliberate departure from that, not a mistaken copy: user, after
    // seeing white confirmed correct, "still don't like the white
    // background. lets go ahead and change it to dark background.").
    // Base/Button are a lighter shade than Window, not the same dark
    // tone -- the same lesson the earlier light-theme pass already hit
    // (see the light-gray-background experiment this file's git history
    // still has): every data-entry widget's own content area
    // (QLineEdit/QComboBox/QListWidget/QTableWidget paint themselves via
    // Base/Text, buttons via Button/ButtonText, neither via
    // Window/WindowText) needs *some* contrast against the panel
    // background around it, or it disappears into one indistinct field
    // again -- just darker this time instead of lighter.
    QPalette centralPalette = ui_->centralwidget->palette();
    centralPalette.setColor(QPalette::Window, QColor(45, 45, 48));
    centralPalette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    centralPalette.setColor(QPalette::Base, QColor(60, 63, 65));
    centralPalette.setColor(QPalette::Text, QColor(230, 230, 230));
    centralPalette.setColor(QPalette::Button, QColor(70, 70, 74));
    centralPalette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    ui_->centralwidget->setStyleSheet(QString());
    ui_->centralwidget->setPalette(centralPalette);
    ui_->centralwidget->setAutoFillBackground(true);

    // SonicateTab carries its own pure-white .ui style sheet property --
    // same fix, same reason, and it otherwise leaves a glaring white
    // patch under an otherwise dark window. A tab page doesn't reliably
    // inherit its palette back through QTabWidget the way a plain child
    // widget inherits from its parent (confirmed directly during the
    // earlier light-theme pass), so it needs its own explicit copy of
    // the same palette rather than relying on that inheritance.
    ui_->SonicateTab->setStyleSheet(QString());
    ui_->SonicateTab->setPalette(centralPalette);
    ui_->SonicateTab->setAutoFillBackground(true);

    // TargetListListBox/stimParamTable/treatmentProtocolTable's real
    // BeamV0 look is a plain white grid (typical MATLAB uitable/listbox
    // styling), not this window's own dark theme -- same
    // QPalette-not-stylesheet approach as centralPalette above.
    // treatmentProtocolTable's own model already paints each cell's
    // *background* white via Qt::BackgroundRole (TreatmentProtocolTableModel::
    // data(), computeTreatmentRowColor for a 0 response), but a role only
    // covers what it's set for -- it never touched *text* color, which
    // was still inheriting centralPalette's near-white (230,230,230)
    // QPalette::Text from its SonicateTab ancestor. White text on a
    // white cell is invisible, not merely hard to read (user: "no matter
    // which protocol I chose the content in the displayed table is
    // empty" -- the rows were there, just unreadable).
    QPalette whiteContentPalette = ui_->TargetListListBox->palette();
    whiteContentPalette.setColor(QPalette::Base, Qt::white);
    whiteContentPalette.setColor(QPalette::Text, Qt::black);
    ui_->TargetListListBox->setPalette(whiteContentPalette);
    ui_->stimParamTable->setPalette(whiteContentPalette);
    ui_->treatmentProtocolTable->setPalette(whiteContentPalette);

    // "Sonications" panel title: same grey-text/whiter-background look as
    // the Pulse Details tab strip below. QPalette, not setStyleSheet --
    // SonicationsPanel contains TargetListListBox, and a stylesheet on
    // the parent would push it (and its own item-row widgets) into
    // styled mode, same trap the tab-bar fix above avoided.
    QPalette sonicationsPanelPalette = ui_->SonicationsPanel->palette();
    sonicationsPanelPalette.setColor(QPalette::WindowText, QColor(0x44, 0x44, 0x44));
    sonicationsPanelPalette.setColor(QPalette::Window, Qt::white);
    ui_->SonicationsPanel->setPalette(sonicationsPanelPalette);
    ui_->SonicationsPanel->setAutoFillBackground(true);
    // A plain QGroupBox doesn't actually fill from QPalette::Window in
    // this style -- needs a real stylesheet for the background, which
    // means its whole subtree (TargetListListBox included) now enters
    // Qt's "styled" mode. Give TargetListListBox's own frame/label an
    // explicit stylesheet too so that doesn't silently repeat the same
    // text-disappears bug row highlighting hit earlier -- its own row
    // widgets already protect themselves the same way.
    ui_->SonicationsPanel->setStyleSheet(QStringLiteral(
        "QGroupBox { background-color: white; border: 1px solid #c0c0c0; }"
        "QGroupBox::title { color: #444444; }"));
    ui_->TargetListListBoxLabel->setStyleSheet(QStringLiteral("color: #444444; background: transparent;"));
    ui_->TargetListListBox->setStyleSheet(QStringLiteral("background-color: white; color: black;"));

    // Treatment Protocol/Pulse Details/Targeting Examples: MATLAB App
    // Designer's own flat light-grey tab strip (grey text, whiter
    // background), not this window's dark OS theme -- same reasoning as
    // whiteContentPalette above. Styling tabBar() alone, not TabGroup3
    // itself -- QTabBar and the tab pages are sibling children of
    // QTabWidget (the pages live in an internal QStackedWidget), so this
    // stays out of the "styled subtree" trap that broke
    // TreatmentProtocolTab's own plain-QPalette labels the first time
    // (see MriSliceView's own comment on the same Qt gotcha).
    ui_->TabGroup3->tabBar()->setStyleSheet(QStringLiteral(
        "QTabBar::tab { background: #e8e8e8; color: #444444; padding: 4px 14px; "
        "border: 1px solid #c0c0c0; border-bottom: none; }"
        "QTabBar::tab:selected { background: white; color: #1a1a1a; }"
        "QTabBar::tab:!selected { margin-top: 2px; }"));

    // Sonicate/Register/Correction now switch via menu, not tabs.
    ui_->TabGroup->tabBar()->hide();
    connect(ui_->ShowSonicateTabAction, &QAction::triggered, this, [this]() { ui_->TabGroup->setCurrentIndex(0); });
    connect(ui_->ShowRegisterTabAction, &QAction::triggered, this, [this]() { ui_->TabGroup->setCurrentIndex(1); });
    connect(ui_->ShowCorrectionTabAction, &QAction::triggered, this, [this]() { ui_->TabGroup->setCurrentIndex(2); });

    QFont menuFont = ui_->menubar->font();
    menuFont.setPointSize(menuFont.pointSize() + 3);
    ui_->menubar->setFont(menuFont);
    ui_->FileMenu->setFont(menuFont);
    ui_->FileMenu->setTitle(QStringLiteral("  %1  ").arg(ui_->FileMenu->title()));
    for (QAction* action : {ui_->ShowSonicateTabAction, ui_->ShowRegisterTabAction, ui_->ShowCorrectionTabAction}) {
        action->setText(QStringLiteral("  %1  ").arg(action->text()));
    }

    // Replace the 3 MRI-axes QLabel placeholders with the real widgets.
    sagitalView_ = replaceWidget<MriSliceView>(ui_->axSag, std::string("sagital"));
    ui_->axSag = nullptr;
    coronalView_ = replaceWidget<MriSliceView>(ui_->axCor, std::string("coronal"));
    ui_->axCor = nullptr;
    axialView_ = replaceWidget<MriSliceView>(ui_->axAxial, std::string("axial"));
    ui_->axAxial = nullptr;

    exampleSagView_ = replaceWidget<ZoomableImageView>(ui_->UIAxesExampleSag);
    ui_->UIAxesExampleSag = nullptr;
    exampleCorView_ = replaceWidget<ZoomableImageView>(ui_->UIAxesExampleCor);
    ui_->UIAxesExampleCor = nullptr;
    exampleAxialView_ = replaceWidget<ZoomableImageView>(ui_->UIAxesExampleAxial);
    ui_->UIAxesExampleAxial = nullptr;

    // MriSliceView is a self-contained composite (title + image + its own
    // mm-labeled slider + left/right step buttons + axis caption + a
    // hover value popup) -- BeamMainWindow uses one to fully replace the
    // separate axSag/SagitalAxismmLabel/sagSlider/sagLeft/sagRight
    // cluster BeamV0.mlapp itself keeps as distinct widgets (this .ui's
    // own literal conversion of that). Giving it only axSag's own
    // 400x370 plot-sized box left no room for that internal chrome --
    // its title/tick labels/slider/buttons rendered clipped or entirely
    // off-widget. Reclaim the old slider-row/mm-label rows' vertical
    // space (down to y=417, right where the checkbox row starts) and
    // hide those now-redundant separate widgets -- MriSliceView's own
    // internal slider/buttons/labels are the real, working equivalent.
    // Bottom edge tracks wherever TabGroup actually sits in the .ui,
    // not a hardcoded constant, so moving TabGroup there doesn't need a
    // matching C++ edit.
    const int kMriBottomY = ui_->TabGroup->y() - 8;
    for (MriSliceView* view : {sagitalView_, coronalView_, axialView_}) {
        const QRect geo = view->geometry();
        view->setGeometry(geo.x(), geo.y(), geo.width(), kMriBottomY - geo.y());
    }
    for (QWidget* redundant :
         {static_cast<QWidget*>(ui_->SagitalAxismmLabel), static_cast<QWidget*>(ui_->sagSlider),
          static_cast<QWidget*>(ui_->sagLeft), static_cast<QWidget*>(ui_->sagRight),
          static_cast<QWidget*>(ui_->CoronalAxismmLabel), static_cast<QWidget*>(ui_->corSlider),
          static_cast<QWidget*>(ui_->corLeft), static_cast<QWidget*>(ui_->corRight),
          static_cast<QWidget*>(ui_->AxialAxismmLabel), static_cast<QWidget*>(ui_->axialSlider),
          static_cast<QWidget*>(ui_->axialLeft), static_cast<QWidget*>(ui_->axialRight)}) {
        redundant->setVisible(false);
    }

    // app.WindowUp/WindowDown -- the brightness up/down step buttons --
    // removed per the user's explicit ask ("remove the up arrow button
    // and down arrow button (for brightness control)"). BrightnessLabel
    // goes with them: a standalone "Brightness" caption with no controls
    // next to it would be a dangling label, not a real widget. Brightness
    // is still fully controllable -- MriSliceView's own right-click
    // context menu already has a live Brightness slider (see its own
    // contextMenuEvent) -- this only removes the separate, redundant
    // step-button pair the .ui also carried.
    ui_->WindowUp->setVisible(false);
    ui_->WindowDown->setVisible(false);
    ui_->BrightnessLabel->setVisible(false);

    // app.ShowTransducersCheckBox/ShowTargetCheckBox/ShowFieldCheckBox's
    // own default Value (all true) -- same defaults BeamMainWindow sets.
    ui_->ShowTransducersCheckBox->setChecked(true);
    ui_->ShowTargetCheckBox->setChecked(true);
    ui_->ShowFieldCheckBox->setChecked(true);
    connect(ui_->ShowTransducersCheckBox, &QCheckBox::toggled, this, [this](bool) { applyOverlayVisibility(); });
    connect(ui_->ShowFieldCheckBox, &QCheckBox::toggled, this, [this](bool) { applyOverlayVisibility(); });
    connect(ui_->ShowTargetCheckBox, &QCheckBox::toggled, this, [this](bool) { refreshTargetCrosshairs(); });

    // Real charts in a scroll area, replacing the 3 QLabel placeholders --
    // see refreshCharts(). QtCharts needs real minimum height to draw
    // axes/series at all; a scroll area gives each chart that.
    delete ui_->axTotalSonicationPlot;
    delete ui_->axPulseWaveformPlot;
    delete ui_->axBurstWaveformPlot;
    ui_->axTotalSonicationPlot = nullptr;
    ui_->axPulseWaveformPlot = nullptr;
    ui_->axBurstWaveformPlot = nullptr;
    auto* pulseDetailsOuterLayout = new QVBoxLayout(ui_->PulseDetailsTab);
    pulseDetailsOuterLayout->setContentsMargins(0, 0, 0, 0);
    auto* pulseDetailsScroll = new QScrollArea();
    pulseDetailsScroll->setWidgetResizable(true);
    pulseDetailsScroll->setFrameShape(QFrame::NoFrame);
    pulseDetailsOuterLayout->addWidget(pulseDetailsScroll);
    auto* pulseDetailsContent = new QWidget();
    pulseDetailsScroll->setWidget(pulseDetailsContent);
    auto* pulseDetailsLayout = new QVBoxLayout(pulseDetailsContent);
    pulseDetailsLayout->setContentsMargins(4, 4, 4, 4);
    pulseDetailsLayout->setSpacing(4);
    // Short minimums -- just enough for TotalSonicationView's own fixed
    // title/axis-label margins (66px) plus a usable plot strip -- so all
    // 3 charts fit in TabGroup3's real default height without forcing a
    // scroll (user: "narrow [the Y scale] so the other two tables can
    // show up in screen"). Expanding + stretch so they actually grow to
    // fill whatever extra height PulseDetailsTab has when the window is
    // enlarged, instead of staying pinned at this minimum (user: "these
    // tables ... need to be able to resize when the window is resized").
    totalSonicationView_ = new TotalSonicationView();
    totalSonicationView_->setMinimumHeight(110);
    totalSonicationView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    pulseDetailsLayout->addWidget(totalSonicationView_, /*stretch=*/1);
    auto* bottomRow = new QHBoxLayout();
    pulseWaveformView_ = new PulseWaveformView();
    pulseWaveformView_->setMinimumHeight(110);
    pulseWaveformView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    bottomRow->addWidget(pulseWaveformView_);
    burstWaveformView_ = new PulseWaveformView();
    burstWaveformView_->setMinimumHeight(110);
    burstWaveformView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    bottomRow->addWidget(burstWaveformView_);
    pulseDetailsLayout->addLayout(bottomRow, /*stretch=*/1);

    // Sonicate tab's control cluster -- see each setter's own header
    // comment for what it wires. countdownTimer_ mirrors BeamMainWindow's
    // own (startStandaloneCountdown.m's hardcoded 2s tick period).
    countdownTimer_->setInterval(2000);
    connect(countdownTimer_, &QTimer::timeout, this, [this]() {
        ++countdownTickCount_;
        const int timeLeft = beam::gui::countdownTimeLeftSeconds(countdownDurationSeconds_, countdownTickCount_);
        if (beam::gui::isCountdownDone(timeLeft)) {
            countdownTimer_->stop();
            ui_->SonicationCountdownLabel->setText(QStringLiteral("No sonication running."));
            return;
        }
        ui_->SonicationCountdownLabel->setText(QString::fromStdString(beam::gui::countdownDisplayText(timeLeft)));
    });

    // Sonications panel's Target List, pre-populated with
    // getTopTargetsFromTreatmentProtocolTable.m's hardcoded ranking
    // names. Create/rename/delete/move-to-target are per-row buttons
    // (installTargetRowButtons), not separate external buttons.
    ui_->TargetListListBox->clear();
    // Real per-target X/Y/Z (mm), not a shared default -- BeamV0's own
    // app.sys.protocolTables(i).stimParamTableData row 1 for each
    // target, straight from DefaultSubjectV0\defaultSubjectMNIV1.mat
    // (user: "SCC1 x y z ... does not match that of BeamV0"). SCC6's
    // outlying X and the identical aMCC4-6 triple are reproduced as-is
    // -- that's genuinely what the real default-subject data ships,
    // not a copy/paste bug introduced here.
    static constexpr struct {
        const char* name;
        double x, y, z;
    } kTargetDefaults[] = {
        {"SCC1", -7.106738, 80.000000, 15.902263},
        {"SCC2", -7.106700, 89.203495, 35.020857},
        {"SCC3", -7.106700, 90.651188, 22.814661},
        {"SCC4", -7.106700, 90.683276, 27.861885},
        {"SCC5", -7.106700, 87.122101, 30.916612},
        {"SCC6", 26.117660, 66.808217, 23.456026},
        {"aMCC1", -7.106700, 87.081096, 36.000000},
        {"aMCC2", -7.106700, 87.054532, 17.761170},
        {"aMCC3", -7.106700, 82.055398, 15.795461},
        {"aMCC4", 0.265999, 33.995003, -6.500000},
        {"aMCC5", 0.265999, 33.995003, -6.500000},
        {"aMCC6", 0.265999, 33.995003, -6.500000},
    };
    for (const auto& target : kTargetDefaults) {
        auto* item = new QListWidgetItem(QString::fromLatin1(target.name));
        ui_->TargetListListBox->addItem(item);
        installTargetRowButtons(item);
        // See targetRowsStore_'s own comment -- one default row per
        // target, index-parallel to the list item just added above.
        StimParamRow row;
        row.x = target.x;
        row.y = target.y;
        row.z = target.z;
        targetRowsStore_.push_back({row});
    }
    connect(ui_->TargetListListBox, &QListWidget::currentRowChanged, this, [this](int row) {
        loadTargetRow(row);
        updateTargetListHighlight();
    });
    ui_->TargetListListBox->setCurrentRow(0);
    updateTargetListHighlight();
    // Real BeamV0 quirk, confirmed against the source:
    // MoveToTargetButtonPushed.m always jumps to arrayCenterMm_, not the
    // selected target's own position -- this single button preserves
    // that, same as every per-row button used to.
    connect(ui_->MoveToSelectedTargetButton, &QPushButton::clicked, this, [this]() {
        sagitalView_->setSliderValueMm(arrayCenterMm_.x());
        coronalView_->setSliderValueMm(arrayCenterMm_.y());
        axialView_->setSliderValueMm(arrayCenterMm_.z());
    });

    connect(ui_->TargetsListBox, &QListWidget::currentTextChanged, this, [this](const QString& name) {
        ui_->TargetingTextArea->setPlainText(
            QString::fromStdString(beam::gui::exampleTargetHelpText(name.toStdString())));
        if (const std::optional<ExampleTargetImages> images = exampleTargetImagePaths(name)) {
            exampleSagView_->setPixmap(QPixmap(images->sag));
            exampleCorView_->setPixmap(QPixmap(images->cor));
            exampleAxialView_->setPixmap(QPixmap(images->axial));
        }
    });
    ui_->TargetsListBox->setCurrentRow(0);

    // Register tab: the .mlapp's own Limits [1 4] for all 4 lock-position
    // sliders, defaulted per initial_placement_presenter's own port of
    // the source's starting frame.
    for (QSlider* s : {ui_->LeftHorizontalPositionYSlider, ui_->RightHorizontalPositionYSlider}) {
        s->setRange(1, 4);
        s->setValue(static_cast<int>(beam::gui::defaultArrayFramePosition().horizontal));
    }
    for (QSlider* s : {ui_->LeftVerticalPositionZSlider, ui_->RightVerticalPositionZSlider}) {
        s->setRange(1, 4);
        s->setValue(static_cast<int>(beam::gui::defaultArrayFramePosition().vertical));
    }

    // The .ui's own File -> Load MRI is a single QAction (matching
    // LoadMRIMenuSelected.m's real single menu item, which pops a
    // listdlg() at click time -- unlike BeamMainWindow's own 2-item
    // File/Folder submenu, a port-only substitute for that runtime
    // dialog). This binding keeps the .ui's real single-action structure
    // as designed and wires it straight to a file picker.
    connect(ui_->LoadMRIMenu, &QAction::triggered, this, &DesignerStartupWindow::loadMriFile);

    // Deferred, not captured synchronously here -- centralwidget hasn't
    // been through QMainWindow's own initial layout pass yet at
    // constructor time (its size() is still some Qt placeholder
    // default, not the real ~1260x900ish it ends up with once actually
    // shown), which made the very first real resizeEvent() compute a
    // wildly wrong scale factor from that placeholder baseline. Firing
    // on the next event-loop turn (after main.cpp's own show() call)
    // lets that initial layout pass happen first.
    QTimer::singleShot(0, this, [this]() {
        baselineCentralSize_ = ui_->centralwidget->size();
        captureBaselineGeometry(ui_->centralwidget);
    });
}

DesignerStartupWindow::~DesignerStartupWindow() { delete ui_; }

void DesignerStartupWindow::captureBaselineGeometry(QWidget* widget) {
    for (QObject* child : widget->children()) {
        auto* childWidget = qobject_cast<QWidget*>(child);
        if (childWidget == nullptr) continue;
        // A QTabWidget's pages aren't its own direct children -- Qt
        // parents them under its own internal QStackedWidget, which
        // (like QAbstractScrollArea's viewport below) auto-sizes its
        // current page to exactly fill itself on every layout pass.
        // Recording/overwriting the page's own geometry here fights
        // that same-class internal management, just one level deeper
        // than the QAbstractScrollArea case -- so skip storing/setting
        // the page itself, but keep recursing into it: its own
        // absolutely-positioned children (stimParamTable, TabGroup3,
        // etc.) still need to be captured and scaled.
        const bool isTabPage = qobject_cast<QStackedWidget*>(widget) != nullptr;
        if (!isTabPage) {
            baselineGeometry_[childWidget] = childWidget->geometry();
        }
        // Opaque -- see this method's own header comment. Any widget
        // with its own QLayout (e.g. PulseDetailsTab's chart hosts) is
        // the same class as QAbstractScrollArea here -- manually
        // setGeometry()-ing its layout-managed children on resize fights
        // that layout instead of the widget's own internal management.
        // QStackedWidget is explicitly exempted from that layout check --
        // it's every QTabWidget's own internal page container, backed by
        // a real QStackedLayout (so childWidget->layout() is non-null for
        // it too), but recursing into it is exactly how a *nested*
        // QTabWidget (TabGroup3, one tab page down from TabGroup) and
        // everything below it (stimParamTable, treatmentProtocolTable,
        // ...) gets reached at all -- without this exemption none of
        // them were ever captured/rescaled, only sitting pinned at their
        // .ui-authored size while TabGroup's own outer box resized
        // around them (user: "these tables and stimtable need to be able
        // to resize when the window is resized").
        if (qobject_cast<MriSliceView*>(childWidget) != nullptr) continue;
        if (qobject_cast<QAbstractScrollArea*>(childWidget) != nullptr) continue;
        if (qobject_cast<QStackedWidget*>(childWidget) == nullptr && childWidget->layout() != nullptr) continue;
        captureBaselineGeometry(childWidget);
    }
}

void DesignerStartupWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (baselineCentralSize_.isEmpty()) return;  // captureBaselineGeometry() hasn't run yet
    const double sx = static_cast<double>(ui_->centralwidget->width()) / baselineCentralSize_.width();
    const double sy = static_cast<double>(ui_->centralwidget->height()) / baselineCentralSize_.height();
    for (const auto& [widget, rect] : baselineGeometry_) {
        widget->setGeometry(static_cast<int>(std::lround(rect.x() * sx)), static_cast<int>(std::lround(rect.y() * sy)),
                            static_cast<int>(std::lround(rect.width() * sx)),
                            static_cast<int>(std::lround(rect.height() * sy)));
    }
}

void DesignerStartupWindow::loadMriFile() {
    QSettings settings = appSettings();
    const QString startDir = settings.value(kLastMriDirSettingsKey).toString();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load MRI"), startDir,
                                                        QStringLiteral("NIfTI (*.nii *.nii.gz)"));
    if (path.isEmpty()) return;
    settings.setValue(kLastMriDirSettingsKey, QFileInfo(path).absolutePath());
    if (onLoadMri_) onLoadMri_(path);
}

void DesignerStartupWindow::setLoadMriHandler(std::function<void(QString)> onLoadMri) {
    onLoadMri_ = std::move(onLoadMri);
}

void DesignerStartupWindow::setRegisterHandler(std::function<void()> onRegister) {
    connect(ui_->RegisterToMRIFiducialsButton, &QPushButton::clicked, this, [this, onRegister]() {
        onRegister();
        // RegisterToMRIFiducialsButtonPushed.m: resets all 4 position
        // sliders back to 1 (no shift) on every successful re-registration.
        ui_->RightHorizontalPositionYSlider->setValue(1);
        ui_->RightVerticalPositionZSlider->setValue(1);
        ui_->LeftHorizontalPositionYSlider->setValue(1);
        ui_->LeftVerticalPositionZSlider->setValue(1);
    });
}

void DesignerStartupWindow::setRegisterCurrentPositionHandler(
    std::function<void(double, double)> onRegisterCurrentPosition) {
    connect(ui_->RegisterArraystoCurrentPositionButton, &QPushButton::clicked, this, [this, onRegisterCurrentPosition]() {
        onRegisterCurrentPosition(ui_->RightHorizontalPositionYSlider->value(),
                                  ui_->RightVerticalPositionZSlider->value());
    });
}

void DesignerStartupWindow::setPositionSlidersChangedHandler(std::function<void()> onPositionSlidersChanged) {
    for (QSlider* slider : {ui_->LeftHorizontalPositionYSlider, ui_->LeftVerticalPositionZSlider,
                            ui_->RightHorizontalPositionYSlider, ui_->RightVerticalPositionZSlider}) {
        connect(slider, &QSlider::valueChanged, this, [onPositionSlidersChanged](int) { onPositionSlidersChanged(); });
    }
}

void DesignerStartupWindow::setRegistrationCheckLampState(const beam::gui::RegistrationCheckLampState& state) {
    setLampOn(ui_->RightRegistrationLamp, state.rightLampOn);
    setLampOn(ui_->LeftRegistrationLamp, state.leftLampOn);
    setLampOn(ui_->InsideMRIRegistrationLamp, state.insideMriLampOn);
    ui_->SonicateButton->setEnabled(state.sonicateButtonEnabled);
}

void DesignerStartupWindow::setSafetyReport(const beam::gui::SonicationSafetyReport& report) {
    // Same status wording as BeamMainWindow's SafetyReportView, but
    // this is the only line actually shown -- the checkbox/CRF row's
    // single-line box has no room for the full message list too (see
    // the .ui's own comment on SystemStatusTextArea).
    const QString statusLine = report.pass ? QStringLiteral("Sonication Online")
                                           : QStringLiteral("Not ready — safety checks failed");
    ui_->SystemStatusTextArea->setText(statusLine);
    ui_->SystemStatusTextArea->setStyleSheet(
        report.pass ? QStringLiteral("background: transparent; border: none; color: rgb(26, 138, 26);")
                    : QStringLiteral("background: transparent; border: none; color: rgb(255, 0, 0);"));

    QString tooltipHtml = QStringLiteral("<html>") + statusLine.toHtmlEscaped();
    for (const std::string& message : report.messages) {
        tooltipHtml += QStringLiteral("<br>") + QString::fromStdString(message).toHtmlEscaped();
    }
    tooltipHtml += QStringLiteral("</html>");
    ui_->SystemStatusTextArea->setToolTip(tooltipHtml);
}

void DesignerStartupWindow::setMriVolume(const beam::mri::Volume3D& volume) {
    sagitalView_->setVolume(volume);
    coronalView_->setVolume(volume);
    axialView_->setVolume(volume);
    // Each pane's real image height differs (same width, different
    // aspect per plane), but no setSliderRowBudgetPx() call here means
    // each pane hugs its own image tightly instead of padding out to a
    // shared row across panes (user: "no need to maintain alignment of
    // the 3 scroll bars" -- previously kept aligned per an earlier ask,
    // "their titles are not aligned but the scroll bars are aligned";
    // this reverses that).
}

void DesignerStartupWindow::setMriAxes(std::optional<beam::mri::RasAxisVectors> axes) {
    sagitalView_->setAxes(axes);
    coronalView_->setAxes(axes);
    axialView_->setAxes(axes);
}

void DesignerStartupWindow::setMriOverlays(const beam::mri::Volume3D& arrayMask,
                                           const beam::mri::Volume3D& fiducialMask,
                                           const beam::mri::Volume3D& focusMask) {
    lastArrayMask_ = arrayMask;
    lastFiducialMask_ = fiducialMask;
    lastFocusMask_ = focusMask;
    applyOverlayVisibility();
}

// Same gating as BeamMainWindow::applyOverlayVisibility -- see its own
// comment for the real source quirk (ShowField's focus glow only renders
// when ShowTransducers is *also* checked).
void DesignerStartupWindow::applyOverlayVisibility() {
    const beam::mri::Volume3D empty;
    const beam::mri::Volume3D& arrayMask = ui_->ShowTransducersCheckBox->isChecked() ? lastArrayMask_ : empty;
    const beam::mri::Volume3D& fiducialMask = ui_->ShowTransducersCheckBox->isChecked() ? lastFiducialMask_ : empty;
    const bool focusVisible = ui_->ShowFieldCheckBox->isChecked() && ui_->ShowTransducersCheckBox->isChecked();
    const beam::mri::Volume3D& focusMask = focusVisible ? lastFocusMask_ : empty;
    sagitalView_->setOverlayVolumes(arrayMask, fiducialMask, focusMask);
    coronalView_->setOverlayVolumes(arrayMask, fiducialMask, focusMask);
    axialView_->setOverlayVolumes(arrayMask, fiducialMask, focusMask);
}

void DesignerStartupWindow::setArrayCenterMm(const Eigen::Vector3d& centerArrayMm) {
    arrayCenterMm_ = centerArrayMm;
    refreshTargetCrosshairs();
}

void DesignerStartupWindow::centerSagittalSliceOnMm(double xMm) { sagitalView_->setInitialSliderValueMm(xMm); }

void DesignerStartupWindow::setFiducialMarkers(std::vector<std::pair<QString, Eigen::Vector3d>> markersMm) {
    sagitalView_->setFiducialLabels(markersMm);
    coronalView_->setFiducialLabels(markersMm);
    axialView_->setFiducialLabels(std::move(markersMm));
}

void DesignerStartupWindow::refreshTargetCrosshairs() {
    // drawROIs.m overwrites every visible row's crosshair position to the
    // same centerArrayMM before drawing -- so this only needs to know
    // whether *any* row is currently shown, not each row's own X/Y/Z.
    bool anyTargetShown = false;
    if (stimParamModel_ != nullptr && ui_->ShowTargetCheckBox->isChecked()) {
        for (const StimParamRow& row : stimParamModel_->rows()) {
            if (row.show) {
                anyTargetShown = true;
                break;
            }
        }
    }
    const std::vector<Eigen::Vector3d> targets =
        anyTargetShown ? std::vector<Eigen::Vector3d>{arrayCenterMm_} : std::vector<Eigen::Vector3d>{};
    sagitalView_->setTargetCrosshairs(targets);
    coronalView_->setTargetCrosshairs(targets);
    axialView_->setTargetCrosshairs(targets);
}

void DesignerStartupWindow::refreshCharts() {
    if (stimParamModel_ == nullptr) return;
    const int rowi = stimParamModel_->currentShownRow();  // 1-based
    const auto& rows = stimParamModel_->rows();
    if (rowi < 1 || rowi > static_cast<int>(rows.size())) return;
    const StimParamRow& r = rows[static_cast<std::size_t>(rowi - 1)];

    pulseWaveformView_->setData(beam::gui::computePulseWaveformPlot(r.pd, r.pi, r.amplitude),
                                QStringLiteral("Pulse waveform"), QStringLiteral("Pulse Interval (s)"));
    burstWaveformView_->setData(beam::gui::computeBurstWaveformPlot(r.bd, r.bi, r.pd, r.pi, r.amplitude),
                                QStringLiteral("Burst waveform"), QStringLiteral("Burst Interval (s)"));

    beam::stimulation::SonicationSchedule s;
    s.startTime = r.startTime;
    s.endTime = r.endTime;
    s.bi = r.bi;
    s.bd = r.bd;
    s.pi = r.pi;
    s.pd = r.pd;
    std::vector<beam::stimulation::TimelineSegment> timeline;
    try {
        timeline = beam::stimulation::computeSonicationEventTimeline({s});
    } catch (const std::exception&) {
    }
    totalSonicationView_->setData(timeline, r.amplitude, r.endTime - r.startTime);
}

QString DesignerStartupWindow::siteId() const { return ui_->SiteIDEditField->text(); }
QString DesignerStartupWindow::visitNumber() const { return ui_->VisitNumberEditField->text(); }
QString DesignerStartupWindow::participantId() const { return ui_->ParticipantIDEditField->text(); }
void DesignerStartupWindow::setSiteId(const QString& value) { ui_->SiteIDEditField->setText(value); }
void DesignerStartupWindow::setParticipantId(const QString& value) { ui_->ParticipantIDEditField->setText(value); }
void DesignerStartupWindow::setVisitNumberField(const QString& value) { ui_->VisitNumberEditField->setText(value); }

void DesignerStartupWindow::setStimParamTableModel(StimParamTableModel* model) {
    stimParamModel_ = model;
    ui_->stimParamTable->setModel(model);
    const int columnWidths[] = {30, 26, 55, 55, 55, 75, 70, 65, 90, 85, 90, 85};
    for (int col = 0; col < StimParamTableModel::kColumnCount; ++col) {
        ui_->stimParamTable->setColumnWidth(col, columnWidths[col]);
    }
    ui_->stimParamTable->verticalHeader()->setDefaultSectionSize(22);
    refreshTargetCrosshairs();
    refreshCharts();
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                refreshTargetCrosshairs();
                refreshCharts();
            });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        refreshTargetCrosshairs();
        refreshCharts();
    });

    // The Target List's own initial selection (row 0, set in the
    // constructor) fired currentRowChanged before this model even
    // existed -- loadTargetRow() no-oped back then (guarded on
    // stimParamModel_ == nullptr) but still left currentTargetIndex_ at
    // that row. Reset it to -1 first so loadTargetRow()'s write-back
    // step (meant for switching *away* from an already-loaded target)
    // doesn't fire here and clobber targetRowsStore_[0]'s real defaults
    // with whatever placeholder rows `model` happened to start with.
    currentTargetIndex_ = -1;
    loadTargetRow(ui_->TargetListListBox->currentRow());
}

void DesignerStartupWindow::loadTargetRow(int index) {
    if (stimParamModel_ == nullptr) {
        currentTargetIndex_ = index;  // remember it for once the model arrives, see setStimParamTableModel
        return;
    }
    // Write the outgoing target's live edits back before switching --
    // stimParamModel_->rows() already reflects every edit/Add/Remove/
    // Sort made since it was loaded, so this is the one place that
    // needs to capture them, not a separate dataChanged hook.
    if (currentTargetIndex_ >= 0 && currentTargetIndex_ < static_cast<int>(targetRowsStore_.size())) {
        targetRowsStore_[static_cast<std::size_t>(currentTargetIndex_)] = stimParamModel_->rows();
    }
    currentTargetIndex_ = index;
    if (index >= 0 && index < static_cast<int>(targetRowsStore_.size())) {
        stimParamModel_->setRows(targetRowsStore_[static_cast<std::size_t>(index)]);
    }
}

void DesignerStartupWindow::installTargetRowButtons(QListWidgetItem* item) {
    auto* rowWidget = new QWidget(ui_->TargetListListBox);
    rowWidget->setStyleSheet(QStringLiteral("background-color: white;"));
    auto* layout = new QHBoxLayout(rowWidget);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    auto* nameLabel = new QLabel(item->text(), rowWidget);
    nameLabel->setFont(ui_->TargetListListBox->font());
    nameLabel->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
    layout->addWidget(nameLabel, /*stretch=*/1, Qt::AlignVCenter);

    // In-place rename: swaps nameLabel out for this QLineEdit (same slot,
    // same stretch), pre-filled with the current name, until Save/Cancel.
    auto* nameEdit = new QLineEdit(item->text(), rowWidget);
    nameEdit->setFont(ui_->TargetListListBox->font());
    nameEdit->hide();
    layout->addWidget(nameEdit, /*stretch=*/1, Qt::AlignVCenter);

    const auto addButton = [&](const QIcon& icon, const QString& tooltip, auto onClick) {
        auto* button = new QToolButton(rowWidget);
        button->setIcon(icon);
        button->setIconSize(QSize(15, 15));
        button->setToolTip(tooltip);
        button->setFixedSize(22, 22);
        button->setAutoRaise(true);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; background: transparent; }"
            "QToolButton:hover, QToolButton:pressed { border: none; background: transparent; }"
            "QToolTip { background-color: #ffffff; color: #1a1a1a; border: 1px solid #808080; padding: 4px 6px; }"));
        connect(button, &QToolButton::clicked, this, onClick);
        layout->addWidget(button, 0, Qt::AlignVCenter);
        return button;
    };

    // Create: always appends to the end of the list, regardless of
    // which row's button was clicked, and selects the new row.
    QToolButton* createButton = addButton(createTargetIcon(), QStringLiteral("Create new target"), [this]() {
        std::vector<std::string> existing;
        for (int i = 0; i < ui_->TargetListListBox->count(); ++i) {
            existing.push_back(ui_->TargetListListBox->item(i)->text().toStdString());
        }
        auto* newItem = new QListWidgetItem(QString::fromStdString(beam::gui::newProtocolName(existing)));
        ui_->TargetListListBox->addItem(newItem);
        targetRowsStore_.push_back({StimParamRow{}});
        installTargetRowButtons(newItem);
        ui_->TargetListListBox->setCurrentRow(ui_->TargetListListBox->count() - 1);
        ui_->TargetListListBox->scrollToItem(newItem);
    });

    QToolButton* deleteButton = addButton(deleteTargetIcon(), QStringLiteral("Delete this target"), [this, item]() {
        const int row = ui_->TargetListListBox->row(item);
        if (row < 0) return;
        // Same ordering reason as the old Remove Selected Protocol
        // handler: update state before takeItem(), which can
        // synchronously re-fire currentRowChanged.
        if (currentTargetIndex_ == row) currentTargetIndex_ = -1;
        targetRowsStore_.erase(targetRowsStore_.begin() + row);
        QWidget* widget = ui_->TargetListListBox->itemWidget(item);
        ui_->TargetListListBox->removeItemWidget(item);
        delete widget;
        delete ui_->TargetListListBox->takeItem(row);
        updateTargetListHighlight();
    });

    QToolButton* renameButton = addButton(renameTargetIcon(), QStringLiteral("Rename this target"), []() {});
    QToolButton* saveButton = addButton(saveTargetIcon(), QStringLiteral("Save name"), []() {});
    QToolButton* cancelButton = addButton(cancelTargetIcon(), QStringLiteral("Cancel"), []() {});
    saveButton->hide();
    cancelButton->hide();

    const auto exitEditMode = [nameLabel, nameEdit, createButton, deleteButton, renameButton, saveButton,
                               cancelButton]() {
        nameEdit->hide();
        nameLabel->show();
        createButton->show();
        deleteButton->show();
        renameButton->show();
        saveButton->hide();
        cancelButton->hide();
    };
    connect(renameButton, &QToolButton::clicked, this,
            [nameLabel, nameEdit, createButton, deleteButton, renameButton, saveButton, cancelButton, item]() {
                nameEdit->setText(item->text());
                nameLabel->hide();
                nameEdit->show();
                nameEdit->setFocus();
                nameEdit->selectAll();
                createButton->hide();
                deleteButton->hide();
                renameButton->hide();
                saveButton->show();
                cancelButton->show();
            });
    connect(saveButton, &QToolButton::clicked, this, [this, item, nameLabel, nameEdit, exitEditMode]() {
        const QString name = nameEdit->text();
        if (!name.isEmpty()) {
            item->setText(name);
            nameLabel->setText(name);
        }
        exitEditMode();
    });
    connect(cancelButton, &QToolButton::clicked, this, exitEditMode);
    connect(nameEdit, &QLineEdit::returnPressed, this, [saveButton]() { saveButton->click(); });

    // QListWidget doesn't reliably pick up a plain item-widget's own
    // sizeHint() for row height -- explicit, tall enough for the 22px
    // icons plus real top/bottom margin instead of clipping them.
    item->setSizeHint(QSize(rowWidget->sizeHint().width(), 38));
    ui_->TargetListListBox->setItemWidget(item, rowWidget);
}

void DesignerStartupWindow::updateTargetListHighlight() {
    const int current = ui_->TargetListListBox->currentRow();
    for (int i = 0; i < ui_->TargetListListBox->count(); ++i) {
        QWidget* row = ui_->TargetListListBox->itemWidget(ui_->TargetListListBox->item(i));
        if (row == nullptr) continue;
        const QString background = i == current            ? QStringLiteral("#cce5ff")
                                    : (i % 2 == 0)           ? QStringLiteral("#ffffff")
                                                              : QStringLiteral("#f2f4f7");
        row->setStyleSheet(QStringLiteral("background-color: %1;").arg(background));
    }
}

void DesignerStartupWindow::setSonicateHandler(std::function<void(bool)> onSonicate) {
    // TriggerSwitch (app.TriggerSwitch) is genuinely hidden at real
    // startup in BeamV0 (Visible='off' until some other action reveals
    // it, per the .ui's own header comment) -- reading its checked
    // state regardless of visibility is still a valid boolean, just not
    // interactively reachable yet; matches BeamMainWindow's own
    // simplification of always exposing this control rather than
    // reproducing the reveal condition.
    connect(ui_->SonicateButton, &QPushButton::clicked, this,
            [this, onSonicate]() { onSonicate(ui_->TriggerSwitch->isChecked()); });
}

void DesignerStartupWindow::setSonicationStatus(const QString& text) { ui_->SonicationStatusLabel->setText(text); }

void DesignerStartupWindow::setShamHandler(std::function<void()> onSham) {
    connect(ui_->ShamButton, &QPushButton::clicked, this, [onSham]() { onSham(); });
}

void DesignerStartupWindow::setSerialPorts(const std::vector<QString>& ports) {
    ui_->SerialPortDropDown->clear();
    for (const QString& port : ports) ui_->SerialPortDropDown->addItem(port);
}

void DesignerStartupWindow::setSerialConnectHandler(std::function<void(QString)> onSerialConnect) {
    connect(ui_->SerialConnectButton, &QPushButton::clicked, this,
            [this, onSerialConnect]() { onSerialConnect(ui_->SerialPortDropDown->currentText()); });
}

void DesignerStartupWindow::setSerialConnectedState(bool connected) {
    // connectSerial.m: red before attempting, green on success -- same
    // as BeamMainWindow's own setSerialConnectedState.
    ui_->ConnectedLamp->setStyleSheet(connected ? QStringLiteral("background-color: rgb(0,255,0); border-radius: 10px;")
                                                 : QStringLiteral("background-color: rgb(255,0,0); border-radius: 10px;"));
}

void DesignerStartupWindow::setGetParamsHandler(std::function<void()> onGetParams) {
    connect(ui_->GetParamsButton, &QPushButton::clicked, this, [onGetParams]() { onGetParams(); });
}

void DesignerStartupWindow::setAbortSonicationHandler(std::function<void()> onAbortSonication) {
    connect(ui_->AbortSonicationButton, &QPushButton::clicked, this,
            [onAbortSonication]() { onAbortSonication(); });
}

void DesignerStartupWindow::startSonicationCountdown(int durationSeconds) {
    countdownDurationSeconds_ = durationSeconds;
    countdownTickCount_ = 0;
    ui_->SonicationCountdownLabel->setText(QString::fromStdString(beam::gui::countdownDisplayText(durationSeconds)));
    countdownTimer_->start();
}

QString DesignerStartupWindow::sonicationCountdownText() const { return ui_->SonicationCountdownLabel->text(); }

void DesignerStartupWindow::setTreatmentProtocolTableModel(TreatmentProtocolTableModel* model) {
    ui_->treatmentProtocolTable->setModel(model);
    ui_->treatmentProtocolTable->verticalHeader()->setDefaultSectionSize(22);
    // The model's own kNumber column is real data (BeamV0's own "#"),
    // duplicating this QTableView's built-in row-number gutter -- keep
    // the model's column, hide the redundant gutter.
    ui_->treatmentProtocolTable->verticalHeader()->setVisible(false);
}

void DesignerStartupWindow::setTreatmentProtocolNames(const std::vector<QString>& names) {
    ui_->TreatmentProtocolDropDown->clear();
    for (const QString& name : names) ui_->TreatmentProtocolDropDown->addItem(name);
}

void DesignerStartupWindow::setVisitNumbers(const std::vector<int>& visitNumbers, int currentVisitNumber) {
    ui_->VisitNumberListBox->clear();
    int currentRow = 0;
    for (std::size_t i = 0; i < visitNumbers.size(); ++i) {
        auto* item = new QListWidgetItem(QStringLiteral("Visit %1").arg(visitNumbers[i]));
        item->setData(Qt::UserRole, visitNumbers[i]);
        ui_->VisitNumberListBox->addItem(item);
        if (visitNumbers[i] == currentVisitNumber) currentRow = static_cast<int>(i);
    }
    ui_->VisitNumberListBox->setCurrentRow(currentRow);
}

void DesignerStartupWindow::setTreatmentProtocolSelectorHandler(
    std::function<void(const QString&, int)> onSelectionChanged) {
    const auto fire = [this, onSelectionChanged]() {
        // currentItem() == nullptr, not count() == 0 -- QListWidget::clear()
        // fires currentRowChanged(-1) (no current item) at a point where
        // count() can still be mid-transition, and currentVisitNumber()'s
        // own fallback of 0 for "no selection" doesn't match any real
        // visit, so calling through with it throws std::out_of_range from
        // inside this Qt signal handler -- an uncaught exception mid-
        // signal-dispatch that surfaces as a silent, headless-looking
        // hang (an MSVC debug-abort dialog with no console output), not
        // a clean crash.
        if (ui_->TreatmentProtocolDropDown->count() == 0 || ui_->VisitNumberListBox->currentItem() == nullptr) return;
        onSelectionChanged(currentTreatmentProtocolName(), currentVisitNumber());
    };
    connect(ui_->TreatmentProtocolDropDown, &QComboBox::currentIndexChanged, this, fire);
    connect(ui_->VisitNumberListBox, &QListWidget::currentRowChanged, this, fire);
}

void DesignerStartupWindow::setNewVisitHandler(std::function<void()> onNewVisit) {
    connect(ui_->NewVisitButton, &QPushButton::clicked, this, [onNewVisit]() { onNewVisit(); });
}

QString DesignerStartupWindow::currentTreatmentProtocolName() const {
    return ui_->TreatmentProtocolDropDown->currentText();
}

int DesignerStartupWindow::currentVisitNumber() const {
    QListWidgetItem* item = ui_->VisitNumberListBox->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toInt() : 0;
}

void DesignerStartupWindow::setTreatmentProtocolDataChangedHandler(std::function<void()> onDataChanged) {
    connect(ui_->treatmentProtocolTable->model(), &QAbstractItemModel::dataChanged, this,
            [onDataChanged](const QModelIndex&, const QModelIndex&) { onDataChanged(); });
}

QString DesignerStartupWindow::hydrogelSize() const {
    if (ui_->MediumButton->isChecked()) return QStringLiteral("Medium");
    if (ui_->LargeButton->isChecked()) return QStringLiteral("Large");
    return QStringLiteral("Small");
}

int DesignerStartupWindow::currentSonicationNumber() const {
    return ui_->CurrentSonicationNumberEditField->text().toInt();
}

}  // namespace beam::gui_qt
