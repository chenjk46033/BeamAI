#include "gui_qt/mri_slice_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSlider>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

namespace beam::gui_qt {

namespace {
// Left margin reserved for the Y-axis ruler line + tick labels + rotated
// axis title; bottom-spacer height reserved for the X-axis ruler line +
// tick labels + axis title (GUI/drawMrImages.m's xdata/ydata axes -- see
// MriSliceView::xRangeMm/yRangeMm for which physical axis maps to which
// edge, per plane, and xAxisLabel/yAxisLabel for the xlabel/ylabel text
// drawMrImages.m sets: `xlabel(app.axSag,'Y (mm)')`/`ylabel(...,'Z (mm)')`
// for sagital, `X (mm)`/`Z (mm)` for coronal, `X (mm)`/`Y (mm)` for axial.
constexpr int kAxisMarginLeftPx = 58;
// Target tick count for the "nice round number" step algorithm below --
// an approximation of MATLAB's own auto-tick spacing, not a literal port
// (the source's tick placement is `axes`' own default behavior, nothing
// this project's own code chose).
constexpr int kTargetTickCount = 8;
// BeamV0's own uislider divides its travel into exactly 5 evenly spaced
// points (endpoints included) when its Limits change at runtime
// (initSliceSliders.m) -- NOT the "nice round number" placement the image
// axes above use. Confirmed against the real app with real subject data
// loaded: a [-112, 113] range shows "-112, -56, 0, 57, 113", not
// "-100, -50, 0, 50, 100" (what niceTicks() would give, and what this
// port originally drew -- user: "The values displayed with scroll bar are
// wrong... They should be -112, -56, 0, 57, 113").
constexpr int kSliderTickCount = 5;
// zoomFactor_'s own range, shared by the context menu's Zoom slider and
// wheelEvent's per-notch nudge below -- 1.0 = no crop, this view's own
// unadjusted default (see updateImage()'s zoomFactor_ < 1 branch).
constexpr double kMaxZoom = 4.0;
// "Open Viewer"'s own display scale, relative to how big the image is
// currently rendered in its source pane (1.0 = same size) -- user: "show
// them in the viewer window 2x of its image dimension. make it a ratio
// that we can experiment." An easy single knob to retune without
// touching the spawn logic itself.
constexpr double kViewerScale = 2.0;
// Image display bounds -- grows/shrinks with the window between these
// (see resizeEvent). Raised once already from an earlier 150/420 (user:
// "those display box for 3 images are too small"), then raised again by
// 50% per the user's explicit follow-up ask ("still too small. should be
// 50% more") -- large enough to read real anatomy on a normal monitor
// without the row dominating the whole window.
// Lowered back from 330 -- combined with the max below, that pushed the
// whole window's *minimum* size past what an ordinary laptop screen can
// give it (Qt: "minimum size: 1920x1419"), which Windows then can't
// honor, forcing the window smaller than Qt thinks its own minimum is
// and producing exactly the garbled/inconsistent layouts reported. A
// smaller floor leaves real headroom on an ordinary screen regardless of
// whatever the width-driven `side` calculation lands on.
constexpr int kImageMinSizePx = 200;
constexpr int kImageMaxSizePx = 900;
// The slider's own width is kept in exact lockstep with the image's
// width every resize (see resizeEvent) -- not left to fill the whole
// column the way QSlider's default Expanding size policy would
// otherwise stretch it (user, earlier: "sliding bar ... too wide"; user,
// later: "make the size the same width of scroll bar").

// QLabel's own sizeHint() defaults to its *current* pixmap's size --
// fine for a label that never resizes, but fatal for one being resized
// by its own layout every frame: the pixmap only ever gets redrawn to
// whatever size the label most recently had, so sizeHint() forever
// echoes the past instead of stating a real preference, and the layout
// (seeing no reason to grow it) never does. Confirmed directly: forcing
// the MRI row's own layout stretch to 10x its sibling's changed nothing
// at all -- proof the image size was never actually being negotiated,
// just echoed. A constant "I'd like to be as big as possible" sizeHint
// breaks that: Qt's normal box-layout algorithm (the same one that
// already correctly balances this row against the tab group below it)
// can then negotiate a real size against its siblings' real demands,
// with nothing left for any one number to get stuck on.
class ImageLabel : public QLabel {
public:
    using QLabel::QLabel;
    QSize sizeHint() const override { return QSize(kImageMaxSizePx, kImageMaxSizePx); }
};

// Picks a "nice" (1/2/5 x 10^n) step so that (hi-lo)/step lands near
// targetCount -- the same family of algorithm most plotting libraries
// (and MATLAB's own default axis ticking) use, so the result looks like
// a normal ruler instead of arbitrary fractional numbers.
double niceTickStep(double lo, double hi, int targetCount) {
    const double range = hi - lo;
    if (!(range > 0.0) || targetCount <= 0) return 1.0;
    const double roughStep = range / targetCount;
    const double magnitude = std::pow(10.0, std::floor(std::log10(roughStep)));
    const double residual = roughStep / magnitude;
    double niceResidual;
    if (residual < 1.5) {
        niceResidual = 1.0;
    } else if (residual < 3.0) {
        niceResidual = 2.0;
    } else if (residual < 7.0) {
        niceResidual = 5.0;
    } else {
        niceResidual = 10.0;
    }
    return niceResidual * magnitude;
}

// Every tick value in [lo, hi] on a niceTickStep grid, ascending.
std::vector<double> niceTicks(double lo, double hi, int targetCount) {
    std::vector<double> ticks;
    if (!(hi > lo)) return ticks;
    const double step = niceTickStep(lo, hi, targetCount);
    const double first = std::ceil(lo / step) * step;
    for (double v = first; v <= hi + step * 1e-6; v += step) {
        ticks.push_back(v);
    }
    return ticks;
}

// `count` values evenly spaced across [lo, hi], endpoints included --
// BeamV0's own uislider divides its travel this way (not niceTicks()'s
// "round number" placement) whenever its Limits change at runtime; see
// kSliderTickCount's comment.
std::vector<double> evenlySpacedTicks(double lo, double hi, int count) {
    std::vector<double> ticks;
    if (count <= 1 || !(hi > lo)) {
        ticks.push_back(lo);
        return ticks;
    }
    const double step = (hi - lo) / (count - 1);
    for (int i = 0; i < count; ++i) {
        ticks.push_back(lo + step * i);
    }
    return ticks;
}

}  // namespace

QImage renderMriSliceImage(const Eigen::MatrixXd& slice, std::optional<std::pair<double, double>> window) {
    const int rows = static_cast<int>(slice.rows());
    const int cols = static_cast<int>(slice.cols());
    if (rows == 0 || cols == 0) {
        return QImage();
    }

    const double lo = window ? window->first : slice.minCoeff();
    const double hi = window ? window->second : slice.maxCoeff();
    const double range = (hi > lo) ? (hi - lo) : 1.0;

    QImage img(cols, rows, QImage::Format_Grayscale8);
    for (int r = 0; r < rows; ++r) {
        uchar* line = img.scanLine(r);
        for (int c = 0; c < cols; ++c) {
            const double v = std::clamp((slice(r, c) - lo) / range, 0.0, 1.0);
            line[c] = static_cast<uchar>(v * 255.0);
        }
    }
    return img;
}

QImage renderMriSliceImageWithOverlay(const Eigen::MatrixXd& slice, const Eigen::MatrixXd& arrayMaskSlice,
                                       const Eigen::MatrixXd& fiducialMaskSlice,
                                       const Eigen::MatrixXd& focusMaskSlice,
                                       std::optional<std::pair<double, double>> window) {
    const int rows = static_cast<int>(slice.rows());
    const int cols = static_cast<int>(slice.cols());
    if (rows == 0 || cols == 0) {
        return QImage();
    }

    const bool hasArrayMask = arrayMaskSlice.rows() == rows && arrayMaskSlice.cols() == cols;
    const bool hasFiducialMask = fiducialMaskSlice.rows() == rows && fiducialMaskSlice.cols() == cols;
    const bool hasFocusMask = focusMaskSlice.rows() == rows && focusMaskSlice.cols() == cols;
    if (!hasArrayMask && !hasFiducialMask && !hasFocusMask) {
        return renderMriSliceImage(slice, window);
    }

    const double lo = window ? window->first : slice.minCoeff();
    const double hi = window ? window->second : slice.maxCoeff();
    const double range = (hi > lo) ? (hi - lo) : 1.0;

    QImage img(cols, rows, QImage::Format_RGB32);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const double v = std::clamp((slice(r, c) - lo) / range, 0.0, 1.0);
            const int gray = static_cast<int>(v * 255.0);
            if (hasFiducialMask && fiducialMaskSlice(r, c) != 0.0) {
                img.setPixel(c, r, qRgb(255, 60, 60));
            } else if (hasArrayMask && arrayMaskSlice(r, c) != 0.0) {
                // Fixed bright yellow, not a gray-preserving tint -- the
                // array sits outside the head, on the MRI's black
                // background (gray==0) far more often than on bright
                // tissue, and qRgb(gray, gray, 0) is pure black (invisible)
                // exactly there. Same fixed-color approach as the fiducial
                // marker overlay above, which doesn't have this problem.
                // Real bug: the user enabled Show Transducers and, at the
                // default slice position, saw nothing at all.
                img.setPixel(c, r, qRgb(255, 255, 0));
            } else if (hasFocusMask && focusMaskSlice(r, c) > 0.0) {
                // Blend proportionally to the [0,1] anti-aliased ellipsoid
                // coverage value, rather than a hard cutoff like the other
                // two overlays -- see rasterizeFocusEllipsoidOntoMriGrid.
                const double coverage = std::clamp(focusMaskSlice(r, c), 0.0, 1.0);
                const int blendedR = static_cast<int>(gray * (1.0 - coverage) + 255.0 * coverage);
                const int blendedB = static_cast<int>(gray * (1.0 - coverage) + 255.0 * coverage);
                const int blendedG = static_cast<int>(gray * (1.0 - coverage));
                img.setPixel(c, r, qRgb(blendedR, blendedG, blendedB));  // magenta glow
            } else {
                img.setPixel(c, r, qRgb(gray, gray, gray));
            }
        }
    }
    return img;
}

MriSliceView::MriSliceView(std::string plane, QWidget* parent)
    : QWidget(parent),
      plane_(std::move(plane)),
      titleSpacer_(new QWidget()),
      imageLabel_(new ImageLabel()),
      bottomAxisSpacer_(new QWidget()),
      leftButton_(new QPushButton(QStringLiteral("<"))),
      slider_(new QSlider(Qt::Horizontal)),
      rightButton_(new QPushButton(QStringLiteral(">"))),
      resetSliderButton_(new QPushButton(QStringLiteral("R"))),
      sliderValueSpacer_(new QWidget()),
      coordinateLabel_(new QLabel(
          this, Qt::Window | Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)) {
    coordinateLabel_->setStyleSheet(
        QStringLiteral("QLabel { border: none; background-color: rgb(45,45,48); color: rgb(230,230,230); padding: 2px; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kAxisMarginLeftPx, 0, 0, 0);
    layout->setSpacing(0);
    const QFontMetrics fm(font());
    titleSpacer_->setFixedHeight(fm.height() + 6);
    layout->addWidget(titleSpacer_);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(kImageMinSizePx, kImageMinSizePx);
    imageLabel_->setMaximumSize(kImageMaxSizePx, kImageMaxSizePx);
    imageLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(imageLabel_);
    // Exactly what the X-axis tick marks (4px) + one line of tick-number
    // text below the image (numberBaselineY = axisBottom + 4 + ascent + 2
    // in paintEvent, descending fm.descent() further) actually need --
    // see bottomAxisSpacerBasePx_'s own comment.
    bottomAxisSpacerBasePx_ = 4 + fm.ascent() + 2 + fm.descent();
    bottomAxisSpacer_->setFixedHeight(bottomAxisSpacerBasePx_);
    layout->addWidget(bottomAxisSpacer_);
    auto* sliderRow = new QHBoxLayout();
    leftButton_->setFixedWidth(28);
    rightButton_->setFixedWidth(28);
    slider_->setMaximumWidth(kImageMaxSizePx - 2 * 28 - 12);
    sliderRow->addStretch();
    sliderRow->addWidget(leftButton_);
    sliderRow->addWidget(slider_);
    sliderRow->addWidget(rightButton_);
    resetSliderButton_->setFixedWidth(24);
    resetSliderButton_->setToolTip(QStringLiteral("Reset"));
    sliderRow->addWidget(resetSliderButton_);
    sliderRow->addStretch();
    layout->addLayout(sliderRow);
    sliderValueSpacer_->setFixedHeight(fm.height() + 2);
    layout->addWidget(sliderValueSpacer_);

    titleText_ = QString::fromStdString(plane_);
    if (!titleText_.isEmpty()) titleText_[0] = titleText_[0].toUpper();  // "sagital" -> "Sagital", etc.
    QObject::connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        updateImage();
        // Keep the value pop-up live while actually dragging the handle --
        // isSliderDown(), not underMouse(): the </>/Reset buttons also
        // call setValue() and sit right next to the slider, so a mere
        // hover-proximity check could fire this for a click on one of
        // them too, anchoring the tooltip wherever the cursor actually
        // is (e.g. off at the Reset button) instead of near the slider.
        if (slider_->isSliderDown()) {
            QToolTip::showText(QCursor::pos() + QPoint(16, -10), currentSliderValueText(), slider_);
        }
        emit sliceIndexChanged(value);
    });
    slider_->installEventFilter(this);
    imageLabel_->installEventFilter(this);  // temporary drag-to-pan -- see eventFilter()
    imageLabel_->setMouseTracking(true);
    QObject::connect(leftButton_, &QPushButton::clicked, this,
                      [this]() { slider_->setValue(slider_->value() - 1); });
    QObject::connect(rightButton_, &QPushButton::clicked, this,
                      [this]() { slider_->setValue(slider_->value() + 1); });
    QObject::connect(resetSliderButton_, &QPushButton::clicked, this,
                      [this]() { slider_->setValue(initialSliderValue_); });
}

bool MriSliceView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == slider_) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
            QToolTip::showText(QCursor::pos() + QPoint(16, -10), currentSliderValueText(), slider_);
        } else if (event->type() == QEvent::Leave) {
            QToolTip::hideText();
        }
    } else if (watched == imageLabel_) {
        // Temporary pan: drag to slide the image around; release snaps
        // straight back (panOffsetPx_ reset to {0,0}, no real/persisted
        // state changed) -- user: "no permanent real image shift". Raw
        // pixel delta, directly -- updateImage() composites the already
        // fully rendered pixmap at this offset unconditionally, so this
        // needs no zoom- or plane-specific handling here.
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                isPanning_ = true;
                panStartPos_ = mouseEvent->pos();
                coordinateLabel_->hide();
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (isPanning_) {
                panOffsetPx_ = mouseEvent->pos() - panStartPos_;
                updateImage();
            } else {
                const std::optional<Eigen::Vector3d> mm = worldMmAtImageLabelPos(mouseEvent->pos());
                if (mm.has_value()) {
                    coordinateLabel_->setText(QStringLiteral("(%1, %2, %3)")
                                                   .arg(mm->x(), 0, 'f', 1)
                                                   .arg(mm->y(), 0, 'f', 1)
                                                   .arg(mm->z(), 0, 'f', 1));
                    coordinateLabel_->adjustSize();
                    coordinateLabel_->move(QCursor::pos() + QPoint(16, -10));
                    coordinateLabel_->show();
                } else {
                    coordinateLabel_->hide();
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease && isPanning_) {
            isPanning_ = false;
            panOffsetPx_ = QPoint(0, 0);
            updateImage();
        } else if (event->type() == QEvent::Leave) {
            coordinateLabel_->hide();
        } else if (event->type() == QEvent::Wheel) {
            applyWheelZoom(static_cast<QWheelEvent*>(event)->angleDelta().y());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// The mm value under the slider's current position -- slider_ itself
// walks a 1-based voxel index (setVolume()), not real mm, so this maps
// that index fraction onto sliderRangeMm() the same way the tick labels
// below the slider do.
double MriSliceView::currentSliderValueMm() const {
    const std::optional<std::pair<double, double>> range = sliderRangeMm();
    if (!range.has_value() || slider_->maximum() <= slider_->minimum()) {
        return static_cast<double>(slider_->value());
    }
    const double t = static_cast<double>(slider_->value() - slider_->minimum()) /
                      (slider_->maximum() - slider_->minimum());
    return range->first + t * (range->second - range->first);
}

std::optional<Eigen::Vector3d> MriSliceView::worldMmAtImageLabelPos(QPoint posInImageLabel) const {
    const std::optional<std::pair<double, double>> xRange = xRangeMm();
    const std::optional<std::pair<double, double>> yRange = yRangeMm();
    if (!xRange.has_value() || !yRange.has_value()) return std::nullopt;
    const QPoint posInView = posInImageLabel + imageLabel_->pos();
    const QRect r = visibleImageRect_;
    if (!r.contains(posInView)) return std::nullopt;

    double tx = static_cast<double>(posInView.x() - r.left()) / r.width();
    if (plane_ == "sagital") tx = 1.0 - tx;
    const double xMmValue = xRange->first + tx * (xRange->second - xRange->first);
    const double ty = static_cast<double>(r.bottom() - posInView.y()) / r.height();
    const double yMmValue = yRange->first + ty * (yRange->second - yRange->first);

    const double sliderMm = currentSliderValueMm();
    if (plane_ == "sagital") return Eigen::Vector3d(sliderMm, xMmValue, yMmValue);
    if (plane_ == "coronal") return Eigen::Vector3d(xMmValue, sliderMm, yMmValue);
    return Eigen::Vector3d(xMmValue, yMmValue, sliderMm);
}

void MriSliceView::setSliderValueMm(double mm) {
    const std::optional<int> idx = nearestSliderIndexForMm(mm);
    if (idx.has_value()) slider_->setValue(*idx);
}

void MriSliceView::setInitialSliderValueMm(double mm) {
    const std::optional<int> idx = nearestSliderIndexForMm(mm);
    if (!idx.has_value()) return;
    initialSliderValue_ = *idx;
    slider_->setValue(*idx);
}

QString MriSliceView::currentSliderValueText() const {
    return QStringLiteral("%1 mm").arg(std::lround(currentSliderValueMm()));
}

Eigen::Index MriSliceView::axisExtent() const {
    if (plane_ == "sagital") {
        return volume_.nx;
    }
    if (plane_ == "coronal") {
        return volume_.ny;
    }
    return volume_.nz;  // "axial"
}

void MriSliceView::setVolume(beam::mri::Volume3D volume) {
    volume_ = std::move(volume);
    const Eigen::Index extent = std::max<Eigen::Index>(axisExtent(), 1);
    slider_->setMinimum(1);
    slider_->setMaximum(static_cast<int>(extent));
    initialSliderValue_ = static_cast<int>(std::max<Eigen::Index>(extent / 2, 1));
    slider_->setValue(initialSliderValue_);
    const Eigen::MatrixXd sample = beam::mri::getSliceImage(volume_, initialSliderValue_, plane_);
    if (sample.rows() > 0 && sample.cols() > 0) {
        sliceAspectWidthOverHeight_ = static_cast<double>(sample.cols()) / static_cast<double>(sample.rows());
    }
    applyImageAspectHeightCap();
    updateImage();
}

int MriSliceView::idealImageHeightPx() const {
    const int labelWidth = imageLabel_->width();
    if (labelWidth <= 0 || sliceAspectWidthOverHeight_ <= 0.0) return 0;
    const int idealHeight = static_cast<int>(std::lround(labelWidth / sliceAspectWidthOverHeight_));
    // A standalone viewer (openViewer()'s own window) isn't sharing a
    // row with 2 sibling panes -- no reason to cap it to the grid's own
    // [kImageMinSizePx, kImageMaxSizePx] box (user: "add controls to
    // allow the window to change size", i.e. let it grow/shrink freely
    // as the user drags the window).
    if (standaloneViewer_) return std::max(idealHeight, kImageMinSizePx);
    return std::clamp(idealHeight, kImageMinSizePx, kImageMaxSizePx);
}

void MriSliceView::setSliderRowBudgetPx(int budgetPx) {
    sliderRowBudgetPx_ = budgetPx;
    applyImageAspectHeightCap();
}

void MriSliceView::applyImageAspectHeightCap() {
    const int idealHeight = idealImageHeightPx();
    if (idealHeight <= 0) return;
    imageLabel_->setMaximumHeight(idealHeight);
    if (!standaloneViewer_) {
        // Extra blank space below this pane's own axis ticks so the
        // slider row still lands on the budget's row even though this
        // pane's real image is shorter than it -- see
        // setSliderRowBudgetPx()'s comment. A standalone viewer has no
        // sibling panes to align with, so it skips this entirely.
        const int fillerPx = std::max(0, sliderRowBudgetPx_ - idealHeight);
        bottomAxisSpacer_->setFixedHeight(bottomAxisSpacerBasePx_ + fillerPx);
    }
    if (layout() != nullptr) layout()->activate();
}

void MriSliceView::initAsStandaloneViewer(QSize initialSizePx) {
    standaloneViewer_ = true;
    zoomFactor_ = 1.0;
    if (initialSizePx.isValid()) {
        // Force this exact size for a correct initial layout pass --
        // openViewer() releases the cap right after sizing its window
        // around it, so later window resizes keep scaling the image
        // responsively (user: "add controls to allow the window to
        // change size") instead of staying frozen at this one starting
        // size. Releasing the cap *here* instead would leave nothing
        // for the window to size itself around but imageLabel_'s own
        // hardcoded ImageLabel::sizeHint() (900x900, unrelated to
        // initialSizePx) -- adjustSize() must still see the fixed size.
        imageLabel_->setFixedSize(initialSizePx);
    }
    // Without this, the rows below imageLabel_ (bottomAxisSpacer_, the
    // slider row) keep their pre-resize positions until some later
    // event forces a layout pass -- on first open that left the slider
    // row drawn inside the now-much-bigger image, only correcting
    // itself once e.g. the Zoom slider's own updateImage()+update()
    // incidentally followed a layout pass (user: "when sagital view is
    // opened...the scroll bar falls into inside the image area...when
    // I [zoom] out, the scroll bar would become outside of the image
    // area").
    if (layout() != nullptr) layout()->activate();
    updateImage();
}

void MriSliceView::releaseFixedSizeForResizing() {
    imageLabel_->setMinimumSize(kImageMinSizePx, kImageMinSizePx);
    imageLabel_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

void MriSliceView::openViewer() {
    auto* window = new QMainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(titleText_);
    auto* viewer = new MriSliceView(plane_);
    viewer->setVolume(volume_);
    viewer->setAxes(axes_);
    viewer->setOverlayVolumes(arrayMask_, fiducialMask_, focusMask_);
    viewer->setTargetCrosshairs(targetCrosshairsMm_);
    viewer->windowHi_ = windowHi_;         // same-class private access -- no public setter exists
    // setVolume() above reset initialSliderValue_ to the volume's plain
    // middle slice -- but this source pane's own Reset target may have
    // been re-pointed elsewhere (e.g. centerSagittalSliceOnMm's crosshair
    // slice), and the viewer's own Reset button should honor that same
    // target, not silently fall back to the middle (user: "when R is
    // clicked it does not hit the slice with cross-hair").
    viewer->initialSliderValue_ = initialSliderValue_;
    viewer->slider_->setValue(slider_->value());  // jump straight to the same slice, not the reset middle one
    // kViewerScale relative to *this pane's own current rendering*, not
    // the real slice's raw voxel size -- the shared 3-pane grid already
    // upscales past native to fill its own box (user measured this: at
    // native-based scaling, a "2x" viewer was only ~1.19x the pane's own
    // size on screen).
    viewer->initAsStandaloneViewer(imageLabel_->size() * kViewerScale);
    // One-way sync, viewer -> this source pane (user: "whatever actions
    // (resize, moving slicing, brightness changes) happened on the
    // separate window, it should happen on the pane view too"). `window`
    // owns `viewer` (WA_DeleteOnClose), so these connections are torn
    // down automatically when the viewer window closes.
    QObject::connect(viewer, &MriSliceView::sliceIndexChanged, this, [this](int index) { slider_->setValue(index); });
    QObject::connect(viewer, &MriSliceView::brightnessChanged, this, [this](std::optional<double> hi) {
        windowHi_ = hi;
        updateImage();
    });
    QObject::connect(viewer, &MriSliceView::zoomChanged, this, [this](double zoom) {
        zoomFactor_ = zoom;
        updateImage();
        update();
    });
    window->setCentralWidget(viewer);
    window->adjustSize();  // sizes the window around initAsStandaloneViewer()'s starting image size
    // Resizable from here on (user: "add controls to allow the window
    // to change size") -- MriSliceView's own resizeEvent already
    // recomputes imageLabel_'s height from its new width on every
    // resize, and idealImageHeightPx()/applyImageAspectHeightCap() skip
    // the shared-grid clamp for standaloneViewer_, so the image keeps
    // its aspect ratio and keeps growing/shrinking as the window is
    // dragged, instead of stopping at some fixed cap. Only released
    // *after* adjustSize() -- see initAsStandaloneViewer()'s comment.
    viewer->releaseFixedSizeForResizing();
    // Escape closes the viewer window (user: "An escape keystroke on any
    // separate viewer window should close that viewer window").
    auto* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), window);
    QObject::connect(closeShortcut, &QShortcut::activated, window, &QWidget::close);
    window->show();
}

void MriSliceView::setAxes(std::optional<beam::mri::RasAxisVectors> axes) {
    axes_ = std::move(axes);
    centerImageHorizontally();  // xRangeMm() may only now have a real value
    update();  // repaint the tick labels
}

namespace {
std::optional<std::pair<double, double>> vectorRangeMm(const Eigen::VectorXd& v) {
    if (v.size() == 0) return std::nullopt;
    return std::make_pair(v.minCoeff(), v.maxCoeff());
}

// Zoom center-crop, offset by rowOffset/colOffset (data-pixel units)
// during a pan drag; clamped to stay within the source matrix.
Eigen::MatrixXd cropCenter(const Eigen::MatrixXd& m, double zoomFactor, Eigen::Index rowOffset = 0,
                          Eigen::Index colOffset = 0) {
    if (zoomFactor <= 1.0 || m.rows() == 0 || m.cols() == 0) return m;
    const Eigen::Index newRows =
        std::clamp<Eigen::Index>(static_cast<Eigen::Index>(std::llround(m.rows() / zoomFactor)), 1, m.rows());
    const Eigen::Index newCols =
        std::clamp<Eigen::Index>(static_cast<Eigen::Index>(std::llround(m.cols() / zoomFactor)), 1, m.cols());
    const Eigen::Index rowStart =
        std::clamp<Eigen::Index>((m.rows() - newRows) / 2 + rowOffset, Eigen::Index{0}, m.rows() - newRows);
    const Eigen::Index colStart =
        std::clamp<Eigen::Index>((m.cols() - newCols) / 2 + colOffset, Eigen::Index{0}, m.cols() - newCols);
    return m.block(rowStart, colStart, newRows, newCols);
}
}  // namespace

// GUI/drawMrImages.m's showMrImage: xdata/ydata per plane --
// sagital: (sys.ay, sys.az); coronal: (sys.ax, sys.az); axial: (sys.ax, sys.ay).
std::optional<std::pair<double, double>> MriSliceView::xRangeMm() const {
    if (!axes_.has_value()) return std::nullopt;
    const std::optional<std::pair<double, double>> range =
        (plane_ == "sagital") ? vectorRangeMm(axes_->dimAP) : vectorRangeMm(axes_->dimLR);  // coronal, axial
    if (!range.has_value()) return std::nullopt;
    return applyZoomCrop(*range, panOffsetXMm_);
}

std::optional<std::pair<double, double>> MriSliceView::yRangeMm() const {
    if (!axes_.has_value()) return std::nullopt;
    const std::optional<std::pair<double, double>> range =
        (plane_ == "axial") ? vectorRangeMm(axes_->dimAP) : vectorRangeMm(axes_->dimIS);  // sagital, coronal
    if (!range.has_value()) return std::nullopt;
    return applyZoomCrop(*range, panOffsetYMm_);
}

std::pair<double, double> MriSliceView::applyZoomCrop(std::pair<double, double> range, double offsetMm) const {
    if (zoomFactor_ <= 1.0) return range;
    const double center = (range.first + range.second) / 2.0 + offsetMm;
    const double halfSpan = (range.second - range.first) / 2.0 / zoomFactor_;
    return {center - halfSpan, center + halfSpan};
}

QString MriSliceView::xAxisLabel() const {
    if (plane_ == "sagital") return QStringLiteral("Y (mm)");
    return QStringLiteral("X (mm)");  // coronal, axial
}

QString MriSliceView::yAxisLabel() const {
    if (plane_ == "axial") return QStringLiteral("Y (mm)");
    return QStringLiteral("Z (mm)");  // sagital, coronal
}

// drawMrImages.m: coordinateToIndex(app.sagSlider.Value,'x',sys) / 'y' for
// corSlider / 'z' for axialSlider -- the slider itself is in the same
// physical axis as axisExtent()'s voxel count (nx/dimLR for sagital,
// ny/dimAP for coronal, nz/dimIS for axial), not the two in-plane display
// axes above.
std::optional<std::pair<double, double>> MriSliceView::sliderRangeMm() const {
    if (!axes_.has_value()) return std::nullopt;
    if (plane_ == "sagital") return vectorRangeMm(axes_->dimLR);
    if (plane_ == "coronal") return vectorRangeMm(axes_->dimAP);
    return vectorRangeMm(axes_->dimIS);  // axial
}

const Eigen::VectorXd* MriSliceView::sliderAxisVector() const {
    if (!axes_.has_value()) return nullptr;
    if (plane_ == "sagital") return &axes_->dimLR;
    if (plane_ == "coronal") return &axes_->dimAP;
    return &axes_->dimIS;  // axial
}

std::optional<int> MriSliceView::nearestSliderIndexForMm(double mm) const {
    const Eigen::VectorXd* axis = sliderAxisVector();
    if (axis == nullptr || axis->size() == 0) return std::nullopt;
    Eigen::Index nearestIdx = 0;
    (axis->array() - mm).abs().minCoeff(&nearestIdx);
    return static_cast<int>(nearestIdx) + 1;  // 0-based array -> 1-based slider value
}

void MriSliceView::setTargetCrosshairs(std::vector<Eigen::Vector3d> targetsMm) {
    targetCrosshairsMm_ = std::move(targetsMm);
    updateImage();  // crosshairs are baked into the cached pixmap, not painted fresh each paintEvent
    update();
}

void MriSliceView::setFiducialLabels(std::vector<std::pair<QString, Eigen::Vector3d>> labelsMm) {
    fiducialLabels_ = std::move(labelsMm);
    updateImage();  // baked into the cached pixmap, same as setTargetCrosshairs
    update();
}

void MriSliceView::resetView() {
    windowHi_ = std::nullopt;
    zoomFactor_ = 1.0;
    updateImage();
    update();
    emit brightnessChanged(windowHi_);
    emit zoomChanged(zoomFactor_);
}

namespace {
// A labeled slider row for the context menu, as a QWidgetAction's
// default widget -- Qt menus keep a QWidgetAction's widget fully
// interactive while the menu stays open, so dragging it updates live
// without closing the menu (user: "It scroll and should change right
// away while scrolling").
QSlider* addSliderRow(QMenu& menu, const QString& label, int minValue, int maxValue, int initialValue) {
    auto* row = new QWidget(&menu);
    auto* layout = new QHBoxLayout(row);
    // Left-align this row's own label with the menu's plain-QAction
    // items (Open Viewer/Reset) -- a QWidgetAction's default widget
    // doesn't get the same left indent QMenu reserves for a regular
    // action's text, so without this it sits further left than they do
    // (user: "Their text should left-aligned").
    layout->setContentsMargins(17, 2, 9, 2);
    auto* labelWidget = new QLabel(label, row);
    // Same fixed width for every row's label -- "Brightness" and "Zoom"
    // are different lengths, so without this the Zoom slider starts
    // further left than the Brightness slider right below it (user:
    // "The scroll bars for brightness and zoom should be left
    // aligned"). Sized to the longer of the two known labels.
    labelWidget->setFixedWidth(QFontMetrics(labelWidget->font()).horizontalAdvance(QStringLiteral("Brightness")));
    layout->addWidget(labelWidget);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setMinimumWidth(120);
    slider->setRange(minValue, maxValue);
    slider->setValue(initialValue);
    layout->addWidget(slider);
    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(row);
    menu.addAction(action);
    return slider;
}
}  // namespace

void MriSliceView::contextMenuEvent(QContextMenuEvent* event) {
    // Image only -- right-clicking the title/axis/slider area of this
    // widget used to pop up the same brightness/zoom menu as clicking
    // the image itself (user: "On area outside of image, a right click
    // should not show that pop-up box"). mapFromGlobal, not event->pos()
    // directly -- when this event propagates up from imageLabel_ (a
    // child widget with no context menu handling of its own), pos() is
    // not reliably guaranteed to already be in *this* widget's own
    // coordinate space, but globalPos() always is screen-absolute.
    if (!visibleImageRect_.contains(mapFromGlobal(event->globalPos()))) {
        event->ignore();
        return;
    }
    QMenu menu(this);

    // Order: Open Viewer, Brightness, Zoom, Reset (user: "The image
    // context pop up should organized this way: Open Viewer, Brigtness,
    // Zoom, Reet").
    QAction* openViewerAction = menu.addAction(QStringLiteral("Open Viewer"));
    menu.addSeparator();

    // Brightness: windowUpDownCallback.m's own math (an adjustable
    // window(2), lo fixed at 0) extended a bit past the source's own
    // one-directional range so the slider has somewhere to go both
    // ways from a centered default (user: "the initial position for
    // brightness and zoom should be in the middle of scroll bar").
    // Slider 0..100, linear in hi: 50 (the middle) = hi at the volume's
    // own max (this view's own unadjusted default), 100 = brightest
    // (hi at 0), 0 = darker than default (hi at 2x the volume's max).
    const double maxHi = (volume_.nx > 0) ? beam::mri::computeDisplayWindow(volume_).hi : 0.0;
    const int initialBrightness = (maxHi > 0.0 && windowHi_.has_value())
                                       ? static_cast<int>(std::lround(100.0 - 50.0 * *windowHi_ / maxHi))
                                       : 50;
    QSlider* brightnessSlider = addSliderRow(menu, QStringLiteral("Brightness"), 0, 100, initialBrightness);
    QObject::connect(brightnessSlider, &QSlider::valueChanged, this, [this, maxHi](int value) {
        if (maxHi <= 0.0) return;
        windowHi_ = std::clamp(maxHi * (2.0 - static_cast<double>(value) / 50.0), 0.0, maxHi * 2.0);
        updateImage();
        emit brightnessChanged(windowHi_);
    });

    // Zoom: slider 0..100, log-scale in zoomFactor_ so 50 (the middle)
    // is exactly 1.0x (no crop, this view's own unadjusted default --
    // same centered-default ask as brightness above): 100 = kMaxZoom
    // (crop to the center and scale up), 0 = 1/kMaxZoom (scale the
    // whole, uncropped image down within the view -- see updateImage()'s
    // own zoomFactor_ < 1 branch).
    const int initialZoom =
        static_cast<int>(std::lround(50.0 + 50.0 * std::log(zoomFactor_) / std::log(kMaxZoom)));
    QSlider* zoomSlider = addSliderRow(menu, QStringLiteral("Zoom"), 0, 100, std::clamp(initialZoom, 0, 100));
    QObject::connect(zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        zoomFactor_ = std::pow(kMaxZoom, (static_cast<double>(value) - 50.0) / 50.0);
        updateImage();
        update();
        emit zoomChanged(zoomFactor_);
    });

    menu.addSeparator();
    QAction* resetAction = menu.addAction(QStringLiteral("Reset"));
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == resetAction) resetView();
    else if (chosen == openViewerAction) openViewer();
}

void MriSliceView::applyWheelZoom(int angleDeltaY) {
    constexpr double kStepPerNotch = 1.1;
    const double notches = angleDeltaY / 120.0;
    zoomFactor_ = std::clamp(zoomFactor_ * std::pow(kStepPerNotch, notches), 1.0 / kMaxZoom, kMaxZoom);
    updateImage();
    update();
    emit zoomChanged(zoomFactor_);
}

void MriSliceView::wheelEvent(QWheelEvent* event) {
    applyWheelZoom(event->angleDelta().y());
    event->accept();
}

void MriSliceView::setOverlayVolumes(beam::mri::Volume3D arrayMask, beam::mri::Volume3D fiducialMask,
                                     beam::mri::Volume3D focusMask) {
    arrayMask_ = std::move(arrayMask);
    fiducialMask_ = std::move(fiducialMask);
    focusMask_ = std::move(focusMask);
    updateImage();
}

void MriSliceView::updateImage() {
    if (axisExtent() <= 0) {
        return;
    }

    const int idx = slider_->value();
    const Eigen::MatrixXd fullSlice = beam::mri::getSliceImage(volume_, idx, plane_);

    // Pan drag offset, data-pixel units, only when zoomed in.
    Eigen::Index rowOffset = 0;
    Eigen::Index colOffset = 0;
    panOffsetXMm_ = 0.0;
    panOffsetYMm_ = 0.0;
    if (zoomFactor_ > 1.0 && fullSlice.rows() > 0 && fullSlice.cols() > 0 && imageLabel_->size().width() > 0 &&
        imageLabel_->size().height() > 0) {
        const Eigen::Index newRows = std::clamp<Eigen::Index>(
            static_cast<Eigen::Index>(std::llround(fullSlice.rows() / zoomFactor_)), 1, fullSlice.rows());
        const Eigen::Index newCols = std::clamp<Eigen::Index>(
            static_cast<Eigen::Index>(std::llround(fullSlice.cols() / zoomFactor_)), 1, fullSlice.cols());
        // Clamp the *drag* offset itself (not just the crop it produces) to whatever the crop can
        // actually represent. cropCenter() below clamps rowOffset/colOffset to keep the crop window
        // in bounds once you drag past the edge, but panOffsetXMm_/panOffsetYMm_ (which the tick
        // labels and crosshair placement read) were computed straight from the raw, unclamped drag
        // delta -- so once a drag exceeded the crop's range, the displayed image pinned at its edge
        // while the crosshair math kept sliding, drifting the crosshair off its real target. Clamping
        // here, before either the pixel or the mm offset is derived, keeps both consistent by
        // construction.
        const double maxPanPxX = (fullSlice.cols() - newCols) / 2.0 * imageLabel_->size().width() /
                                  static_cast<double>(newCols);
        const double maxPanPxY = (fullSlice.rows() - newRows) / 2.0 * imageLabel_->size().height() /
                                  static_cast<double>(newRows);
        const double clampedPanPxX = std::clamp(static_cast<double>(panOffsetPx_.x()), -maxPanPxX, maxPanPxX);
        const double clampedPanPxY = std::clamp(static_cast<double>(panOffsetPx_.y()), -maxPanPxY, maxPanPxY);
        colOffset = static_cast<Eigen::Index>(
            std::llround(-clampedPanPxX * static_cast<double>(newCols) / imageLabel_->size().width()));
        rowOffset = static_cast<Eigen::Index>(
            std::llround(-clampedPanPxY * static_cast<double>(newRows) / imageLabel_->size().height()));
        const std::optional<std::pair<double, double>> xFull =
            axes_.has_value() ? ((plane_ == "sagital") ? vectorRangeMm(axes_->dimAP) : vectorRangeMm(axes_->dimLR))
                              : std::nullopt;
        const std::optional<std::pair<double, double>> yFull =
            axes_.has_value() ? ((plane_ == "axial") ? vectorRangeMm(axes_->dimAP) : vectorRangeMm(axes_->dimIS))
                              : std::nullopt;
        if (xFull.has_value()) {
            panOffsetXMm_ = -clampedPanPxX * (xFull->second - xFull->first) /
                            (zoomFactor_ * imageLabel_->size().width());
        }
        if (yFull.has_value()) {
            panOffsetYMm_ = -clampedPanPxY * (yFull->second - yFull->first) /
                            (zoomFactor_ * imageLabel_->size().height());
        }
        // Sagittal's rendered image is horizontally mirrored after cropping (see updateImage() below);
        // flip the crop offset and its mm equivalent together so the visible pan direction matches the
        // drag, and xRangeMm() still describes the columns actually cropped.
        if (plane_ == "sagital") {
            colOffset = -colOffset;
            panOffsetXMm_ = -panOffsetXMm_;
        }
    }

    const Eigen::MatrixXd slice = cropCenter(fullSlice, zoomFactor_, rowOffset, colOffset);
    const Eigen::MatrixXd arraySlice =
        arrayMask_.nx > 0
            ? cropCenter(beam::mri::getSliceImage(arrayMask_, idx, plane_), zoomFactor_, rowOffset, colOffset)
            : Eigen::MatrixXd();
    const Eigen::MatrixXd fiducialSlice =
        fiducialMask_.nx > 0
            ? cropCenter(beam::mri::getSliceImage(fiducialMask_, idx, plane_), zoomFactor_, rowOffset, colOffset)
            : Eigen::MatrixXd();
    const Eigen::MatrixXd focusSlice =
        focusMask_.nx > 0
            ? cropCenter(beam::mri::getSliceImage(focusMask_, idx, plane_), zoomFactor_, rowOffset, colOffset)
            : Eigen::MatrixXd();
    const std::optional<std::pair<double, double>> window =
        windowHi_ ? std::make_optional(std::make_pair(0.0, *windowHi_)) : std::nullopt;
    QImage image = renderMriSliceImageWithOverlay(slice, arraySlice, fiducialSlice, focusSlice, window);
    // drawMrImages.m: set(axHandle,'XDir','reverse') for the sagital plane
    // only -- mirror to match (the other two planes keep MATLAB's default
    // left-to-right XDir).
    if (plane_ == "sagital") image = image.mirrored(/*horizontal=*/true, /*vertical=*/false);
    // zoomFactor_ > 1 already cropped the source data above (more real
    // pixels scaled up to fill the view); zoomFactor_ < 1 instead
    // shrinks the *target* size here, scaling the whole (uncropped)
    // image down within the view -- a real zoom-out, not just undoing
    // the crop (see the context menu's own Zoom slider comment).
    const QSize targetSize = (zoomFactor_ < 1.0) ? imageLabel_->size() * zoomFactor_ : imageLabel_->size();
    QPixmap pixmap = QPixmap::fromImage(image).scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Baked directly into the pixmap, not drawn in paintEvent like the
    // ruler/ticks -- imageLabel_ is a child widget Qt always paints over
    // this widget's own paintEvent output, so a crosshair drawn there
    // would be entirely hidden underneath the image (the same bug class
    // already fixed once for the ruler lines -- see paintEvent's own
    // comment on axisLeft/axisBottom).
    paintTargetCrosshairs(pixmap);
    paintFiducialLabels(pixmap);
    // The scaled pixmap generally doesn't exactly fill imageLabel_'s own
    // box (KeepAspectRatio can't do that unless the slice's aspect ratio
    // happens to match) -- QLabel's AlignCenter would otherwise letterbox
    // it inside the box. Record where the pixmap's *unpanned* position
    // lands, in this widget's own coordinates, for paintEvent to anchor
    // the ruler/ticks/slider to -- not imageLabel_'s full box, which
    // would leave them offset into the empty letterboxed margin, and not
    // wherever a temporary pan drag currently has the image sitting.
    const QRect labelRect = imageLabel_->geometry();
    const int homeX = (labelRect.width() - pixmap.width()) / 2;
    const int homeY = 0;  // top-aligned, not centered
    visibleImageRect_ = QRect(labelRect.topLeft() + QPoint(homeX, homeY), pixmap.size());
    // Keeps the slice slider's own row no wider than the rendered image at
    // zoomFactor_ == 1 (imageLabel_'s box letterboxed to the slice's own,
    // unzoomed aspect ratio) -- not visibleImageRect_, which shrinks along
    // with the pixmap itself when zoomed out, which would otherwise resize
    // the slider on every zoom change (user: "do not changing the size of
    // scroll bars when zooming mri images"). Fixed buttons total 80px, 3
    // gaps at the layout's own default 6px spacing.
    int unzoomedWidth = imageLabel_->size().width();
    if (sliceAspectWidthOverHeight_ > 0.0) {
        unzoomedWidth = std::min(
            unzoomedWidth, static_cast<int>(std::lround(imageLabel_->size().height() * sliceAspectWidthOverHeight_)));
    }
    slider_->setFixedWidth(std::max(20, unzoomedWidth - 80 - 3 * 6));
    // zoomFactor_ <= 1 has no cropped-away data, so still a visual nudge.
    const QPoint drawOffset = (zoomFactor_ > 1.0) ? QPoint(0, 0) : panOffsetPx_;
    // Alpha transparent -- imageLabel_ paints over this widget's own
    // ruler/tick paintEvent, so an opaque margin would hide them.
    QImage compositedImage(imageLabel_->size(), QImage::Format_ARGB32_Premultiplied);
    compositedImage.fill(Qt::transparent);
    QPainter compositor(&compositedImage);
    compositor.drawPixmap(homeX + drawOffset.x(), homeY + drawOffset.y(), pixmap);
    compositor.end();
    imageLabel_->setPixmap(QPixmap::fromImage(compositedImage));
    // Title stays "Sagital"/"Coronal"/"Axial" (set once in the
    // constructor, matching drawMrImages.m's title(...) calls exactly --
    // no slice index) -- previously overwritten here every slider move
    // with a "(idx/extent)" suffix the source never shows.
}

void MriSliceView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    applyImageAspectHeightCap();
    centerImageHorizontally();
    updateImage();
}

void MriSliceView::centerImageHorizontally() {
    const std::optional<std::pair<double, double>> xRange = xRangeMm();
    const int columnCenter = kAxisMarginLeftPx + (width() - kAxisMarginLeftPx) / 2;
    int desiredLeft = columnCenter - imageLabel_->width() / 2;  // plain center, no axes data yet
    if (xRange.has_value() && xRange->second > xRange->first) {
        double t = (0.0 - xRange->first) / (xRange->second - xRange->first);
        if (plane_ == "sagital") t = 1.0 - t;  // XDir='reverse', matches updateImage()'s mirror
        t = std::clamp(t, 0.0, 1.0);
        const int zeroPixelWithinImage = static_cast<int>(t * imageLabel_->width());
        desiredLeft = columnCenter - zeroPixelWithinImage;
    }
    desiredLeft = std::max(kAxisMarginLeftPx, desiredLeft);
    desiredLeft = std::min(desiredLeft, width() - imageLabel_->width());
    desiredLeft = std::max(kAxisMarginLeftPx, desiredLeft);
    imageLabel_->move(desiredLeft, imageLabel_->y());
}

void MriSliceView::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    const std::optional<std::pair<double, double>> xRange = xRangeMm();
    const std::optional<std::pair<double, double>> yRange = yRangeMm();
    const std::optional<std::pair<double, double>> sliderRange = sliderRangeMm();

    QPainter painter(this);
    // "Card" border around this whole pane -- title, image, axis
    // ticks/scale, and slider all included, not just the image itself
    // (user: "draw a border line rectangle around the player which
    // includes all of the gui associated with the image ... Make it
    // look like a card (of web gui)"). Drawn first, before anything
    // else, so it sits *behind* imageLabel_/slider_/the buttons (real
    // child widgets Qt always paints over this paintEvent's own output --
    // same ordering fact as the ruler lines below) and only shows
    // through the plain, non-painting spacer widgets around them.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(palette().color(QPalette::Window).lighter(115));
    painter.setPen(QPen(palette().color(QPalette::Window).lighter(180), 1.5));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1.5, -1.5), 8, 8);
    painter.restore();
    painter.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics fm(painter.font());
    // The real visible pixmap rect (see updateImage()), not
    // imageLabel_->geometry() -- using the full box here was the residual
    // cause of the slider/ticks landing inside the image's own letterboxed
    // margin whenever a slice's aspect ratio isn't exactly square.
    const QRect r = visibleImageRect_;

    // Title, geometrically centered on imageLabel_'s own stable box --
    // not on rawXRangeMm()'s "0" (an earlier approach): "0" isn't
    // generally at the image's own visual center, so that made the
    // title look off-center (user: "does not appear to be centered").
    // The box itself never changes with zoom, only with real
    // resize/layout, so this stays stable under zoom too.
    {
        const QRect titleRect = titleSpacer_->geometry();
        const QRect labelRect = imageLabel_->geometry();
        const int x = labelRect.center().x();
        const int textWidth = fm.horizontalAdvance(titleText_);
        painter.drawText(x - textWidth / 2, titleRect.top() + fm.ascent() + (titleRect.height() - fm.height()) / 2,
                          titleText_);
    }

    // The ruler lines are drawn 1px *outside* imageLabel_'s own rect, not
    // exactly on it: imageLabel_ is a child widget that paints its pixmap
    // on top of this paintEvent's output (Qt always paints children over
    // their parent), so a line drawn exactly at r.left()/r.bottom() was
    // being completely covered and never actually visible -- caught by
    // the user ("You did not draw vertical line"). axisLeft/axisBottom
    // are shared by both rulers' lines *and* their tick marks so the two
    // rulers meet at one exact corner point and every tick visibly
    // touches its ruler, per the user's "should connect" ask.
    const int axisLeft = r.left() - 1;
    const int axisBottom = r.bottom() + 1;

    // Orientation convention below (not a literal port -- getSliceImage.m's
    // rot90 already diverges from the source's exact flipud/YDir/XDir
    // pixel-for-pixel display, disclosed elsewhere): top of the image is
    // the Y-axis's max value, bottom is its min; left is the X-axis's min,
    // right is its max -- except sagital, whose image is itself mirrored
    // to match drawMrImages.m's real `XDir`='reverse' (see updateImage()),
    // so its X ticks run the other way (max on the left) to stay
    // consistent with what's actually drawn.

    // Y-axis: a ruler line down the image's left edge, tick marks +
    // number labels off of it, then the rotated axis title
    // (drawMrImages.m's ylabel -- "Z (mm)" or "Y (mm)", see yAxisLabel()).
    if (yRange.has_value()) {
        painter.drawLine(axisLeft, r.top(), axisLeft, axisBottom);
        int minLabelLeft = axisLeft;
        for (const double value : niceTicks(yRange->first, yRange->second, kTargetTickCount)) {
            const double t = (value - yRange->first) / (yRange->second - yRange->first);
            const int y = r.bottom() - static_cast<int>(t * r.height());
            painter.drawLine(axisLeft - 4, y, axisLeft, y);
            const QString label = QString::number(std::lround(value));
            const QRect textRect(0, y - fm.height() / 2, axisLeft - 6, fm.height());
            painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);
            minLabelLeft = std::min(minLabelLeft, axisLeft - 6 - fm.horizontalAdvance(label));
        }
        // Axis title literally overlapping the number column instead of
        // sitting in its own separate slot (user: "let it sit on top of
        // the line of scale values but display it as transparent" --
        // right-aligned/no-clipping placement wasn't wide enough to fit
        // both without collision or clipping off the widget edge; a
        // translucent overlay sidesteps that by letting both be legible
        // in the same footprint instead of needing room for both side by
        // side). Drawn after the numbers (on top, per "overlay"), same
        // pen color at reduced alpha.
        painter.save();
        QColor transparentPen = painter.pen().color();
        transparentPen.setAlpha(190);
        painter.setPen(transparentPen);
        painter.translate(axisLeft - 6, r.center().y());
        painter.rotate(-90);
        const QString label = yAxisLabel();
        const int textWidth = fm.horizontalAdvance(label);
        painter.drawText(-textWidth / 2, 0, label);
        painter.restore();
    }

    // X-axis: a ruler line along the image's bottom edge, tick marks +
    // number labels below it, then the axis title -- overlapping the
    // number row itself (translucent, see the Y-axis block above for
    // why) instead of a separate line below.
    if (xRange.has_value()) {
        painter.drawLine(axisLeft, axisBottom, r.right(), axisBottom);
        const bool reversed = (plane_ == "sagital");
        const int numberBaselineY = axisBottom + 4 + fm.ascent() + 2;
        for (const double value : niceTicks(xRange->first, xRange->second, kTargetTickCount)) {
            double t = (value - xRange->first) / (xRange->second - xRange->first);
            if (reversed) t = 1.0 - t;
            const int x = r.left() + static_cast<int>(t * r.width());
            painter.drawLine(x, axisBottom, x, axisBottom + 4);
            const QString label = QString::number(std::lround(value));
            const int textWidth = fm.horizontalAdvance(label);
            painter.drawText(x - textWidth / 2, numberBaselineY, label);
        }
        painter.save();
        QColor transparentPen = painter.pen().color();
        transparentPen.setAlpha(190);
        painter.setPen(transparentPen);
        const QString label = xAxisLabel();
        const int textWidth = fm.horizontalAdvance(label);
        painter.drawText(r.center().x() - textWidth / 2, numberBaselineY, label);
        painter.restore();
    }

    // Slider value row: MATLAB's uislider shows major-tick numbers along
    // its track by default; QSlider doesn't, so paint them into
    // sliderValueSpacer_ using the physical axis this plane's slider
    // actually walks (sliderRangeMm() -- a different range per plane,
    // generally not the same as either displayed axis above).
    if (sliderRange.has_value()) {
        const QRect sliderRect = slider_->geometry();
        const QRect valueRect = sliderValueSpacer_->geometry();
        // ceil/floor to match initSliceSliders.m's own Limits = [ceil(min),
        // floor(max)] -- the slider's real travel span, not the raw
        // unrounded data range.
        const double lo = std::ceil(sliderRange->first);
        const double hi = std::floor(sliderRange->second);
        for (const double value : evenlySpacedTicks(lo, hi, kSliderTickCount)) {
            const double t = (hi > lo) ? (value - lo) / (hi - lo) : 0.5;
            const int x = sliderRect.left() + static_cast<int>(t * sliderRect.width());
            const QString label = QString::number(std::lround(value));
            const int textWidth = fm.horizontalAdvance(label);
            painter.drawText(x - textWidth / 2, valueRect.top() + fm.ascent(), label);
        }
    }
}

// drawROIs.m's app.targetROIs: a green crosshair through the current
// sonication target's position, but only on the one slice that exactly
// contains it (per-plane nearest-voxel-index equality, same gate as the
// source's own `coordinateToIndex(...) == xi/yi/zi`). Baked directly into
// the pixmap (pixmap-local coordinates, (0,0) at its own top-left) rather
// than drawn in paintEvent -- see the call site's comment for why.
void MriSliceView::paintTargetCrosshairs(QPixmap& pixmap) const {
    const std::optional<std::pair<double, double>> xRange = xRangeMm();
    const std::optional<std::pair<double, double>> yRange = yRangeMm();
    const Eigen::VectorXd* sliderAxis = sliderAxisVector();
    if (sliderAxis == nullptr || sliderAxis->size() == 0 || !xRange.has_value() || !yRange.has_value()) {
        return;
    }
    const QRect r(0, 0, pixmap.width(), pixmap.height());
    QPainter painter(&pixmap);
    painter.setPen(QPen(QColor(0, 220, 0), 2));
    for (const Eigen::Vector3d& targetMm : targetCrosshairsMm_) {
        const double sliderAxisValueMm = (plane_ == "sagital")   ? targetMm.x()
                                          : (plane_ == "coronal") ? targetMm.y()
                                                                  : targetMm.z();
        // Same exact nearest-voxel search setSliderValueMm() uses to
        // land the slider in the first place -- see
        // nearestSliderIndexForMm()'s own comment for why sharing this
        // one computation matters.
        const std::optional<int> nearestIdx = nearestSliderIndexForMm(sliderAxisValueMm);
        if (!nearestIdx.has_value() || *nearestIdx != slider_->value()) continue;

        const double xMm = (plane_ == "sagital") ? targetMm.y() : targetMm.x();
        const double yMm = (plane_ == "axial") ? targetMm.y() : targetMm.z();
        double tx = (xMm - xRange->first) / (xRange->second - xRange->first);
        if (plane_ == "sagital") tx = 1.0 - tx;
        const double ty = (yMm - yRange->first) / (yRange->second - yRange->first);
        const int px = r.left() + static_cast<int>(tx * r.width());
        const int py = r.bottom() - static_cast<int>(ty * r.height());
        painter.drawLine(r.left(), py, r.right(), py);
        painter.drawLine(px, r.top(), px, r.bottom());
    }
}

// drawROIs.m's fiducial block: red point + name label, same per-plane exact-slice gate as paintTargetCrosshairs().
void MriSliceView::paintFiducialLabels(QPixmap& pixmap) const {
    if (fiducialLabels_.empty()) return;
    const std::optional<std::pair<double, double>> xRange = xRangeMm();
    const std::optional<std::pair<double, double>> yRange = yRangeMm();
    const Eigen::VectorXd* sliderAxis = sliderAxisVector();
    if (sliderAxis == nullptr || sliderAxis->size() == 0 || !xRange.has_value() || !yRange.has_value()) {
        return;
    }
    const QRect r(0, 0, pixmap.width(), pixmap.height());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics fm(painter.font());

    // Points drawn first so wide labels can't hide a neighboring point; labels placed after, nudged down on collision.
    struct VisibleMarker {
        QString name;
        int px;
        int py;
    };
    std::vector<VisibleMarker> visible;
    constexpr int kPointRadius = 4;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 30, 30));
    for (const auto& [name, markerMm] : fiducialLabels_) {
        const double sliderAxisValueMm = (plane_ == "sagital")   ? markerMm.x()
                                          : (plane_ == "coronal") ? markerMm.y()
                                                                  : markerMm.z();
        const std::optional<int> nearestIdx = nearestSliderIndexForMm(sliderAxisValueMm);
        if (!nearestIdx.has_value() || *nearestIdx != slider_->value()) continue;

        const double xMm = (plane_ == "sagital") ? markerMm.y() : markerMm.x();
        const double yMm = (plane_ == "axial") ? markerMm.y() : markerMm.z();
        double tx = (xMm - xRange->first) / (xRange->second - xRange->first);
        if (plane_ == "sagital") tx = 1.0 - tx;
        const double ty = (yMm - yRange->first) / (yRange->second - yRange->first);
        const int px = r.left() + static_cast<int>(tx * r.width());
        const int py = r.bottom() - static_cast<int>(ty * r.height());
        painter.drawEllipse(QPoint(px, py), kPointRadius, kPointRadius);
        visible.push_back({name, px, py});
    }

    std::vector<QRect> obstacles;
    for (const VisibleMarker& marker : visible) {
        obstacles.emplace_back(marker.px - kPointRadius, marker.py - kPointRadius, kPointRadius * 2, kPointRadius * 2);
    }
    for (const VisibleMarker& marker : visible) {
        const int textWidth = fm.horizontalAdvance(marker.name);
        QRect labelRect(marker.px + kPointRadius + 4, marker.py - fm.height() / 2, textWidth + 6, fm.height());
        bool moved = true;
        while (moved) {
            moved = false;
            for (const QRect& other : obstacles) {
                if (labelRect.intersects(other)) {
                    labelRect.moveTop(other.bottom() + 2);
                    moved = true;
                }
            }
        }
        obstacles.push_back(labelRect);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 30, 30, 190));  // roiAlpha 0.75 in the source
        painter.drawRoundedRect(labelRect, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(labelRect, Qt::AlignCenter, marker.name);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 30, 30));
    for (const VisibleMarker& marker : visible) {
        painter.drawEllipse(QPoint(marker.px, marker.py), kPointRadius, kPointRadius);
    }
}

}  // namespace beam::gui_qt
