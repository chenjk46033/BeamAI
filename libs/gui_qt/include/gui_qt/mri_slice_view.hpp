#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>
#include <Eigen/Core>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// The first image-rendering code in this project: a slice-navigation
// widget for the Register tab, built on the already-ported/tested
// beam::mri::getSliceImage (GUI/MRI/getSliceImage.m). Deliberately scoped
// to display -- it takes a beam::mri::Volume3D directly rather than also
// standing up beam::mri::loadNiftiMriRas file loading in this slice; the
// NIfTI reader's own correctness is already covered by libs/mri's tests.

class QContextMenuEvent;
class QEvent;
class QLabel;
class QPixmap;
class QPushButton;
class QSlider;
class QWheelEvent;

namespace beam::gui_qt {

// Renders one already-extracted slice matrix (getSliceImage.m's output) as
// an 8-bit grayscale QImage. `window`, if given, is an explicit (lo, hi)
// display range (windowUpDownCallback.m's adjustable brightness window);
// omitted (the default), it's min-max normalized instead -- the source has
// no display scaling of its own (imagesc auto-scales its colormap the same
// way) absent an explicit window, so that default is a disclosed choice,
// not a port of specific MATLAB behavior.
QImage renderMriSliceImage(const Eigen::MatrixXd& slice,
                           std::optional<std::pair<double, double>> window = std::nullopt);

// Same base rendering as renderMriSliceImage (including the same optional
// `window`), then tints pixels using
// arrayMaskSlice/fiducialMaskSlice/focusMaskSlice (getSliceImage.m applied
// to beam::gui::rasterizeArrayOntoMriGrid /
// rasterizeFiducialMarkersOntoMriGrid / rasterizeFocusEllipsoidOntoMriGrid's
// output, so they line up pixel-for-pixel with `slice`): red for fiducial
// markers, else yellow for the array footprint (both hard-threshold,
// fiducial wins), else magenta blended proportionally to the focus mask's
// [0,1] coverage value (its anti-aliased ellipsoid edge, rather than a
// hard cutoff). Simplified stand-in for drawMrImages.m's alpha-blended
// `imshow` overlays: fixed colors/opacity, no transparency-slider or
// checkbox gating (see docs/known_gaps_gui.md). Any mask may be an empty
// (default-constructed) matrix to skip that overlay.
QImage renderMriSliceImageWithOverlay(const Eigen::MatrixXd& slice, const Eigen::MatrixXd& arrayMaskSlice,
                                       const Eigen::MatrixXd& fiducialMaskSlice,
                                       const Eigen::MatrixXd& focusMaskSlice,
                                       std::optional<std::pair<double, double>> window = std::nullopt);

// One anatomical plane's slider + rendered slice. `plane` is one of
// getSliceImage.m's spellings ("sagital", "coronal", "axial") and fixes
// which of the volume's three axes the slider walks (nx/ny/nz
// respectively, matching the source's img(i,:,:)/img(:,j,:)/img(:,:,k)
// indexing).
class MriSliceView : public QWidget {
    Q_OBJECT

public:
    explicit MriSliceView(std::string plane, QWidget* parent = nullptr);

    // Copies the volume in and resets the slider to its middle slice.
    void setVolume(beam::mri::Volume3D volume);

    // Optional array-footprint / fiducial-marker / focus-ellipsoid overlay
    // volumes, same (nx,ny,nz) as the volume passed to setVolume (see
    // beam::gui::rasterizeArrayOntoMriGrid /
    // rasterizeFiducialMarkersOntoMriGrid / rasterizeFocusEllipsoidOntoMriGrid).
    // Pass default-constructed Volume3Ds (nx==0) to clear an overlay.
    void setOverlayVolumes(beam::mri::Volume3D arrayMask, beam::mri::Volume3D fiducialMask,
                           beam::mri::Volume3D focusMask);

    // Real physical-mm calibration for the tick labels drawn to the left
    // of and below the image (GUI/drawMrImages.m's `xdata`/`ydata`
    // arguments to `imshow`, per plane -- see the .cpp for exactly which
    // of dimLR/dimAP/dimIS maps to which edge). Pass std::nullopt (the
    // default) to draw no ticks -- the synthetic default volume has no
    // physical calibration to show.
    void setAxes(std::optional<beam::mri::RasAxisVectors> axes);

    // drawROIs.m's `app.targetROIs`: one green crosshair per currently-
    // shown sonication target (stimParamTable.Data.show(i)), in real mm.
    // A crosshair is only actually painted when THIS plane's current
    // slice's voxel index exactly matches that target's own index along
    // the axis this plane's slider walks (drawROIs.m's own
    // `coordinateToIndex(app.sagSlider.Value,'x',sys) == xi` gate,
    // per-plane) -- it disappears the instant the slider moves off that
    // exact slice, same as the source. No-op (draws nothing) until
    // setAxes() has real axes.
    void setTargetCrosshairs(std::vector<Eigen::Vector3d> targetsMm);

    // drawROIs.m's `app.FiducialROIs`: named fiducial markers (e.g. "LeftY1Z1"), red point + label, same
    // per-plane exact-slice gate as setTargetCrosshairs. Drawn on all 3 planes, unconditionally.
    void setFiducialLabels(std::vector<std::pair<QString, Eigen::Vector3d>> labelsMm);

    // Context menu's "Reset" -- back to this image's own initial default
    // (auto brightness, no zoom), same as it looked before any of the
    // above were ever used.
    void resetView();

    // The physical-mm value under the slider's current position (see
    // sliderRangeMm()'s header comment for which physical axis that is,
    // per plane) -- e.g. for "Move Fiducial to Current View", combining
    // all 3 planes' values into one X/Y/Z point.
    double currentSliderValueMm() const;

    // Inverse of currentSliderValueMm() -- moves the slider to the index
    // whose mm value is closest to `mm` (per sliderRangeMm()'s linear
    // mapping), clamped to the slider's range. For
    // moveToFiducialMarkerButtonFunction.m: clicking a fiducial's "Move
    // to" radio button jumps all 3 MRI slice sliders to that fiducial's
    // stored X/Y/Z. A no-op if setAxes() was never given real axes.
    void setSliderValueMm(double mm);

    // Same as setSliderValueMm, but also marks this position as the new
    // Reset target (initialSliderValue_) -- for centerSagittalSliceOnMm
    // specifically, which jumps the sagittal slider to the array center
    // once, right at real startup, so it shows the crosshair immediately
    // (see main.cpp). Plain setSliderValueMm (fiducial "Move to" clicks,
    // ordinary interactive moves) must NOT redefine what Reset means, or
    // Reset stops resetting anything.
    void setInitialSliderValueMm(double mm);

    // This plane's own ideal (letterbox-free) image height at its current
    // width -- 0 if not yet computable (no volume/width yet). A sibling
    // coordinator (e.g. DesignerStartupWindow) reads this from all 3
    // panes and feeds the max back via setSliderRowBudgetPx() so the
    // slider row lands on the same row across panes despite each pane's
    // real image height differing.
    int idealImageHeightPx() const;
    // The common height budget (title-to-slider-row) this pane should
    // reserve above its slider row -- 0 (the default) means "no
    // coordination", i.e. hug this pane's own image tightly. Shorter
    // panes get extra blank space below their axis ticks to make up the
    // difference; the tallest pane gets none.
    void setSliderRowBudgetPx(int budgetPx);

signals:
    // Fired whenever this view's own slice index / brightness / zoom
    // actually changes, regardless of what UI triggered it (slider drag,
    // </> buttons, Reset, the context menu's Brightness/Zoom sliders,
    // wheelEvent). openViewer() connects its spawned viewer's own copies
    // of these back to this source pane (user: "whatever actions
    // (resize, moving slicing, brightness changes) happened on the
    // separate window, it should happen on the pane view too") -- one
    // direction only, viewer -> source pane, not the other way.
    void sliceIndexChanged(int index);
    void brightnessChanged(std::optional<double> windowHi);
    void zoomChanged(double zoomFactor);

protected:
    void paintEvent(QPaintEvent* event) override;
    // Re-scales the pixmap (and, via the base class, triggers the ruler/
    // tick repaint in paintEvent, since that always reads imageLabel_'s
    // *current* geometry fresh) whenever this widget's size changes --
    // otherwise the image only ever rescales on the next data change
    // (setVolume/setOverlayVolumes/slider move), not on a plain window
    // resize.
    void resizeEvent(QResizeEvent* event) override;
    // Pops up the slider's current mm value near the cursor while hovering
    // or dragging it -- QSlider gives no visual feedback of its own value
    // (user: "I would like to display the current value of scroll bar...
    // when you point to it it displays as... a pop up"). Also handles
    // imageLabel_'s own mouse press/move/release for the temporary pan
    // drag below -- mouse events, unlike wheel/context-menu events,
    // don't propagate from a child widget to its parent on their own,
    // so this can't just be a mousePressEvent/mouseMoveEvent override
    // on MriSliceView itself the way contextMenuEvent is.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QString currentSliderValueText() const;
    std::optional<Eigen::Vector3d> worldMmAtImageLabelPos(QPoint posInImageLabel) const;
    void applyWheelZoom(int angleDeltaY);
    void updateImage();
    // Caps imageLabel_'s own height to this plane's real, unzoomed
    // aspect ratio at its current width -- eliminates the vertical
    // letterbox slack that was pushing the axis text/slider row well
    // below the actual image. Per-pane only, no cross-view alignment.
    void applyImageAspectHeightCap();
    // Marks this view as openViewer()'s own standalone window (not one
    // of the shared 3-pane grid's panes) and forces imageLabel_ to
    // exactly initialSizePx for a correct first layout pass --
    // idealImageHeightPx()/applyImageAspectHeightCap() skip the shared-
    // grid's [kImageMinSizePx, kImageMaxSizePx] clamp and cross-pane
    // budget for standaloneViewer_, so it can grow/shrink freely once
    // releaseFixedSizeForResizing() lifts this initial size cap.
    void initAsStandaloneViewer(QSize initialSizePx);
    // Lifts initAsStandaloneViewer()'s exact-size cap back to a normal
    // [kImageMinSizePx, unbounded] range, letting the window be resized
    // afterward (user: "add controls to allow the window to change
    // size") -- called once openViewer()'s window has already been
    // sized around the initial fixed size.
    void releaseFixedSizeForResizing();
    // Context menu's "Open Viewer" (user: "write a image viewer...When
    // clicked, open a new window, with the view as the Window name...
    // Show the view in 1:1 size" -- then "show them in the viewer window
    // 2x of its image dimension. make it a ratio that we can
    // experiment", see kViewerScale). Spawns a second, independent
    // MriSliceView of the same plane in its own top-level, resizable
    // window, showing the same slice/overlays/brightness this pane
    // currently has -- for a closer look at detail than the shared
    // 3-pane grid's own bounded box allows.
    void openViewer();
    // Shifts imageLabel_ horizontally (Qt's box layout otherwise just
    // left-aligns a fixed-size child) so the pixel where this plane's
    // x-axis equals 0 lands at the column's horizontal center, instead of
    // wherever it happens to fall in a left-anchored image (user: "images
    // ... should be centered around 0 value. But they are not").
    void centerImageHorizontally();
    Eigen::Index axisExtent() const;  // nx/ny/nz for this view's plane
    // The two physical-mm ranges (min, max) this plane's x/y edges span,
    // per drawMrImages.m's xdata/ydata -- empty if setAxes() was never
    // given real axes.
    std::optional<std::pair<double, double>> xRangeMm() const;
    std::optional<std::pair<double, double>> yRangeMm() const;
    // drawMrImages.m's xlabel/ylabel text, per plane (see the .cpp).
    QString xAxisLabel() const;
    QString yAxisLabel() const;
    // The physical-mm range of the axis this plane's *slider* walks --
    // sagital->dimLR, coronal->dimAP, axial->dimIS (drawMrImages.m:
    // `coordinateToIndex(app.sagSlider.Value,'x',sys)` etc -- the source's
    // sliders are in real mm, not a 1-based voxel index). Distinct from
    // xRangeMm/yRangeMm, which are the two *displayed* in-plane axes.
    std::optional<std::pair<double, double>> sliderRangeMm() const;
    // The full per-voxel physical-mm vector along the axis this plane's
    // slider walks (dimLR/dimAP/dimIS, matching sliderRangeMm() -- but the
    // full vector, not just its (min,max)), for the crosshair's nearest-
    // voxel-index equality test. nullptr if setAxes() was never given
    // real axes.
    const Eigen::VectorXd* sliderAxisVector() const;
    // The 1-based slider value (voxel index) whose own real physical-mm
    // position along sliderAxisVector() is nearest to `mm` -- the exact
    // same nearest-voxel search paintTargetCrosshairs() already does for
    // its "am I on the target's own slice" gate. setSliderValueMm()/
    // setInitialSliderValueMm() used to approximate this via a separate
    // linear interpolation over sliderRangeMm()'s (min,max) instead
    // (mmToSliderIndex, now removed) -- close enough for display, but not
    // guaranteed to land on the *exact* index paintTargetCrosshairs()
    // independently computes, so a plane whose voxel spacing isn't
    // perfectly even (axial's dimIS, in particular) could round to a
    // neighboring slice and silently fail that gate -- the crosshair just
    // doesn't appear (user, clicking Move To Target: "Axial view no
    // crosshair was drawn"). Sharing one exact computation for both
    // "where does the slider land" and "does the crosshair gate pass"
    // makes them consistent by construction, not by coincidence.
    // std::nullopt if setAxes() was never given real axes.
    std::optional<int> nearestSliderIndexForMm(double mm) const;
    // Bakes targetCrosshairsMm_ directly into `pixmap` (pixmap-local
    // coordinates) -- see the .cpp call site for why this can't be done
    // in paintEvent like the ruler/ticks. Called from updateImage(),
    // before imageLabel_->setPixmap().
    void paintTargetCrosshairs(QPixmap& pixmap) const;
    void paintFiducialLabels(QPixmap& pixmap) const;
    // Center-crop + pan offset, mm space; no-op at zoomFactor_ <= 1.
    std::pair<double, double> applyZoomCrop(std::pair<double, double> range, double offsetMm) const;

    std::string plane_;
    beam::mri::Volume3D volume_;
    beam::mri::Volume3D arrayMask_;
    beam::mri::Volume3D fiducialMask_;
    beam::mri::Volume3D focusMask_;
    std::optional<beam::mri::RasAxisVectors> axes_;
    std::vector<Eigen::Vector3d> targetCrosshairsMm_;
    std::vector<std::pair<QString, Eigen::Vector3d>> fiducialLabels_;
    // windowUpDownCallback.m's app.sys.window(2) -- nullopt (the default)
    // means auto min/max per slice, matching this port's original
    // behavior; set once the context menu's Brightness slider is
    // dragged away from its own middle position.
    std::optional<double> windowHi_;
    // Real slice aspect (width/height), unzoomed -- set once in
    // setVolume(), read by applyImageAspectHeightCap(). Never touched
    // by zoomFactor_, so zooming can't resize imageLabel_'s own box.
    double sliceAspectWidthOverHeight_ = 1.0;
    // Set by setSliderRowBudgetPx(); see its own comment.
    int sliderRowBudgetPx_ = 0;
    // bottomAxisSpacer_'s own base height (before any setSliderRowBudgetPx()
    // filler on top) -- computed once in the constructor from this
    // instance's real font metrics, matching exactly what paintEvent's
    // X-axis tick marks + one line of tick-number text actually need, not
    // a separate guessed constant that could drift from the real drawing
    // math (user: "move the slider closer to the image", twice now).
    int bottomAxisSpacerBasePx_ = 0;
    // Set by initAsStandaloneViewer(); see its own comment.
    bool standaloneViewer_ = false;
    // New, port-only zoom, driven by the context menu's Zoom slider.
    // 1.0 = full image, no crop (the default, and the slider's own
    // middle position); > 1.0 = a tighter center crop scaled up to fill
    // the view; < 1.0 = the whole (uncropped) image scaled down within
    // the view (see updateImage()'s own zoomFactor_ < 1 branch).
    double zoomFactor_ = 1.0;
    // Temporary drag-to-pan, snaps back to {0,0} on release. Visual nudge
    // at zoomFactor_ <= 1; re-crops real data at zoomFactor_ > 1.
    QPoint panOffsetPx_{0, 0};
    bool isPanning_ = false;
    QPoint panStartPos_;
    // panOffsetPx_ converted to mm, kept in sync so ticks/crosshair match.
    double panOffsetXMm_ = 0.0;
    double panOffsetYMm_ = 0.0;
    // Title and axis-caption text are painted manually in paintEvent
    // (like the tick labels below), not shown via these QLabels' own text
    // -- the user wants both horizontally centered on the "0" position of
    // their respective horizontal axis (the image's xRangeMm() for the
    // title, the slider's own sliderRangeMm() for the caption), not on
    // the widget's full width, which a plain centered QLabel can't do
    // since the image sits at a fixed max size inside a wider column and
    // "0" isn't generally at the image's own visual center. These widgets
    // now exist only to reserve the correct vertical space in the layout.
    QWidget* titleSpacer_;
    QLabel* imageLabel_;
    // The actual visible pixmap's rect within imageLabel_ (in this
    // widget's own coordinates), not imageLabel_'s full box -- .scaled(...,
    // KeepAspectRatio) generally can't fill a square box exactly (a real
    // MRI slice usually isn't exactly square), leaving a letterboxed
    // margin QLabel centers the pixmap within. The ruler/ticks/slider
    // must anchor to this, not the full box, or they end up offset from
    // the real image edge -- the residual version of a bug the user kept
    // finding ("scroll bar fall into inside mri image"). Updated in
    // updateImage(), read in paintEvent().
    QRect visibleImageRect_;
    QWidget* bottomAxisSpacer_;
    QPushButton* leftButton_;
    QSlider* slider_;
    QPushButton* rightButton_;
    // "R" / tooltip "Reset" -- jumps the slider back to its initial
    // position (user: "The scroll bar needs to have a Rest button with
    // R as label, Reset as tooltip, to reset the scroll bar to its
    // initial position"). Distinct from the context menu's own Reset
    // (brightness/zoom only, see resetView()) -- this one is slider
    // position only.
    QPushButton* resetSliderButton_;
    // Captured once, in setVolume() -- the slider's own real starting
    // value (the middle slice) before any later caller repositions it
    // (centerSagittalSliceOnMm, a fiducial "Move to" click, etc.).
    int initialSliderValue_ = 1;
    QWidget* sliderValueSpacer_;
    QString titleText_;
    QLabel* coordinateLabel_;
};

}  // namespace beam::gui_qt
