# GUI widget inventory — BeamV0.mlapp vs `beam_app`

`docs/known_gaps_gui.md`'s 100%/"43 Converted" number measures one
thing: whether each `GUI/*.m` file's real *computational* logic has a
C++ counterpart in `libs/`. It does **not** measure whether `beam_app`
is an actual, feature-complete application matching BeamV0's real
interactive surface. Those are different questions, and conflating them
(reporting "100%" without this distinction) was misleading — this doc
is the honest accounting of the second question.

## Method

Every named component in `BeamV0.mlapp` was extracted directly from its
`matlab/document.xml` (`app.<Name> matlab.ui.*` declarations — 140
components total), then checked against **`DesignerStartupWindow`**
(`libs/gui_qt/include/gui_qt/startup_screen_window.hpp` +
`libs/gui_qt/src/startup_screen_window.cpp` + `designer/startup_screen.ui`)
— the active window as of 2026-09-18; `BeamMainWindow` was abandoned and
this doc no longer tracks it (see `feedback_confirm_active_window_before_gui_work`
in project memory). Pure layout/structural containers (`TabGroup`, `Tab`,
`ButtonGroup`, `Panel`, the figure itself) and plain caption `Label`s
paired with an already-counted control are excluded from the tally
below — they're not independently meaningful features. What's counted:
every `Button`, `EditField`/`NumericEditField`, `CheckBox`, `Slider`,
`DropDown`, `Switch`, `Lamp`, `ListBox`, `RadioButton`, `Table`,
`TextArea`, `UIAxes`, `Image`, and `Menu` — 91 functional controls.

## Result: 68/91 (~75%) present in some form, re-audited 2026-09-21

A control counts as "Present" only if it has real behavioral wiring
(a click/change handler calling into tested `libs/` logic), not merely
a correctly-positioned static widget with nothing connected — see the
2026-09-21 update below for the several Register-tab controls that are
now built in the `.ui` but stay in the Missing column on that basis.

| Control type | BeamV0 count | Present | Missing | Hidden |
|---|---:|---:|---|---|
| Button | 27 | 19 | LoadWithFrameImageButton, LoadWithoutFrameImageButton, MoveFiducialtoCurrentViewButton, PhotoBasedRegistrationButton, RunCorrectionButton, ZeroCorrectionButton | WindowUp, WindowDown |
| EditField/NumericEditField | 4 | 4 | — | — |
| CheckBox | 5 | 4 | ShowElementStatusCheckBox | — |
| Slider | 9 | 8 | TransducerTransparencySlider | — |
| DropDown | 2 | 2 | — | — |
| Switch | 1 | 1 | — | — |
| Lamp | 4 | 4 | — | — |
| ListBox | 3 | 3 | — | — |
| RadioButton | 12 | 3 | LeftY1Z1Button, LeftY1Z3Button, LeftY4Z1Button, MRIFiducialsButton, MRIFreeButton, PhotoBasedButton, RightY1Z1Button, RightY1Z3Button, RightY4Z1Button | — |
| Table | 2 | 2 | — | — |
| TextArea | 3 | 2 | CouplingStatusTextArea | — |
| UIAxes | 12 | 9 | axCompareAvgTransmissionBar, axFirstArray, axSecondArray | — |
| Image | 2 | 0 | ImageWithFrame, ImageWithoutFrame | — |
| Menu | 5 | 5 | — | — |
| **Total** | **91** | **66 + 2 hidden = 68** | **23** | **2** |

**Register tab's core fiducial-registration workflow is now built and wired**
(2026-09-21) — the 4 lock-position sliders, both Register buttons, all 3
registration lamps, and the Sonicate-gate all call the same tested
`libs/registration`/`libs/gui` logic `BeamMainWindow` already used.
**Correction tab remains entirely unbuilt.** What's still missing from
Register: `RegistrationTypeButtonGroup`'s 3 radios (MRI Fiducials/Photo
Based/MRI Free source-switching), the `MovetoButtonGroup`'s 6 fiducial
position radios + `MoveFiducialtoCurrentViewButton`
(`moveToFiducialMarkerButtonFunction.m`, already-disclosed deferred),
and the whole Photo-Based sub-tab (`ImageWithFrame`/`ImageWithoutFrame`/
`PhotoBasedRegistrationButton`/its 2 load buttons) — correctly out of
scope, needs Python interop, same as elsewhere. All of these now exist
as real, correctly-positioned widgets in the `.ui` (not absent the way
the rest of Register tab was before 2026-09-21) but have no behavior
attached, so they stay in the Missing column per this doc's own
present-means-wired standard. Sonicate tab's pulse/burst/total-sonication
charts (`axPulseWaveformPlot`/`axBurstWaveformPlot`/`axTotalSonicationPlot`)
closed out 2026-09-18: `beam::gui::computeBurstWaveformPlot` (new,
tested port of `updateSonicationPlots.m`'s repeating-pulse burst logic)
+ the already-tested `buildPulseWaveformChart`/`buildSonicationTimelineChart`.
Targeting Examples closed out 2026-09-21: `TargetsListBox`/`TargetingTextArea`
wired to `beam::gui::exampleTargetHelpText`, the 3 `UIAxesExample*` now
real images (`beam::gui_qt::ZoomableImageView`, zoomable/pannable) loaded
from BeamV0's own bundled reference photos.

## What this means

About a quarter of BeamV0's real interactive controls still have no
counterpart in `beam_app`. Some of that gap is legitimate, already-
disclosed scope (Photo-Based/MRI-Free registration modes need Python
interop or hardcoded MNI landmarks; the individual fiducial position
radios were replaced by the 4 lock-position sliders' own registration
math, per `registerCurrentTransducerPosition`'s own header doc). What
remains missing beyond that is entirely Correction tab (built and
tested in the abandoned `BeamMainWindow`, still a blank tab here) plus
`RegistrationTypeButtonGroup`'s source-switching radios. (MRI-view
chrome -- axis labels, step buttons, visibility checkboxes -- and CRF
data entry, both called out as missing in earlier versions of this doc,
were closed out 2026-09-14; visible serial-connection state, Abort
Sonication, and Get Params were already wired by the time of this
2026-09-21 pass, just not yet reflected in this sentence.)

This is tracked here as a standing to-do list, separate from
`docs/known_gaps_gui.md`'s per-`.m`-file ledger. Update this table as
each item gets built.

**2026-09-14 update:** built a complete parent-assignment map for every
component in the `.mlapp` (`app.<Name> = ui*(app.<Parent>` across the
whole `createComponents` function, not just the ones a user happened to
notice) after discovering the MRI-axes-persistence bug was one instance
of a systemic pattern, not a one-off: **39 components are parented
directly to `app.UIFigure`** (visible on every tab, not tab-scoped) --
the 3 MRI axes (already fixed) plus `TriggerSwitch`, `SystemStatusTextArea`
(already built, just mis-placed inside Sonicate), the 3 MRI-navigation
sliders + step buttons + axis-mm captions (sliders already persistent
via `MriSliceView`; step buttons and axis-mm captions added later the
same day -- `<`/`>` `QPushButton`s flanking each slider, wired to
`leftRightButtonPushed.m`'s +/-1 clamped-at-limits step, plus a
"Sagital/Coronal/Axial Axis (mm)" caption label under each slider,
matching `SagitalAxismmLabel`/`CoronalAxismmLabel`/`AxialAxismmLabel`),
3 CRF fields (now added), `ShowTransducers`/`ShowTarget`/`ShowField`
checkboxes (now added), `ShowOverlayBrain`/`OverlayBrainVolumeButton`/`OverlayTransparencySlider`
(tied to the already-Excluded `overlayMRIOntoCurrentSubject.m` Python
pipeline -- confirmed via its real `ButtonPushedFcn`, so building these
3 would be cosmetic-only, deliberately skipped), `MNIPosXYZLabel`/
`TargetPosXYZLabel` (read-only position displays, unclear trigger
mechanism, not yet investigated), and `BrightnessLabel`/`WindowUp`/
`WindowDown` (real, portable logic in `windowUpDownCallback.m` --
misclassified Excluded, needs a rendering-pipeline change since this
port's views auto-normalize contrast per-slice rather than sharing one
volume-wide window range; scoped as its own follow-up, not rushed into
this pass).

**2026-09-16 update:** ported `drawROIs.m`'s target-ROI crosshair
(`MriSliceView::setTargetCrosshairs`/`paintTargetCrosshairs`, gated by
`showTargetCheckBox_` and `stimParamModel_`'s `show` column, position
sourced from `BeamMainWindow::setArrayCenterMm` -- same `centerArrayMM`
`drawFocusOnMRI.m` already uses, since the source overwrites every
visible row's own X/Y/Z to that one shared position before drawing).
Found via a direct comparison against the real running BeamV0 app (user:
"there are lines drawing on top of images n BeamV0 but not in Beam").
Baked into the pixmap itself, not drawn in `paintEvent`, for the same
reason the ruler lines needed that treatment (a child widget always
paints over its parent). Initially not visible at default startup -- the
synthetic placeholder array's raw (pre-`centerArrayOnMri`) center sat at
(~91, ~44, 0) mm, off of every default mid-slice -- same root cause as
the "Axial doesn't show transducer at startup" gap below, both traced to
the fabricated synthetic array geometry, not a rendering bug. Closed
alongside that gap the same day (see below): re-centering
`syntheticArrayRect`'s own formula on 0 puts the raw center within
~1-2mm of the default Sagittal slice.

## Related, separate gap: default *state*, not just widgets

`GUI/loadDefaultSys.m` loads a real default subject
(`DefaultSubjectV0/defaultSubjectMNIV1.mat`, external sibling folder,
~130 MB) unconditionally on every BeamV0 startup -- `app.sys` comes back
with 38 populated fields: real MRI, real array/fiducial positions, real
correction (`RTT`) values, real `stimParamTable`/`treatmentProtocolTables`,
CRF, registration state, anatomy, MNI. Found 2026-09-14 after the user
compared a fresh BeamV0 session against `beam_app`'s synthetic-blob
default and called it "far from that, not even close."

As of 2026-09-14, only the **MRI** piece of that default state is
replicated (`apps/beam_app` tries a NIfTI export of the same default
subject before falling back to synthetic -- see
`docs/known_gaps_mri.md`'s `loadDefaultSys.m` entry). The other ~37
fields' worth of real default state (array geometry, correction values,
stim params, CRF, ...) still comes from `beam_app`'s own independently-
fabricated synthetic demo data at each call site, not from this same
real dataset. Closing that gap fully would mean exporting the rest of
that default `sys` struct (via MATLAB, the same way the MRI piece was)
and wiring each into the corresponding already-built presenter/widget,
in place of the ad hoc synthetic values currently in
`apps/beam_app/main.cpp`.

**2026-09-16 confirmed symptom, then fixed** (user: "even though the
show transducer checkbox is enabled, the Axial view does not show
transducer in Beam, but it shows transducer in BeamV0") -- traced
end-to-end (`centerArrayOnMri`, `rasterizeArrayOntoMriGrid`,
`getSliceImage` all read correct and match their `.m` sources exactly)
to two compounding issues in `apps/beam_app/main.cpp`'s
`syntheticArrayRect(90)` placeholder, neither in the (already-correct)
overlay pipeline itself:

1. Every element sat at Z=0 -- a perfectly flat, zero-thickness shape,
   so it could only ever intersect the one exact Axial slice matching
   its mean Z, never any other slice including the default one. Fixed
   by giving it real Z spread, symmetric with the existing X/Y spread.
2. Verified numerically (a temporary per-slice voxel-hit count, not just
   the bounding-box range) that even with real 3D spread, each element
   only ever contributed a single voxel: `rasterizeArrayOntoMriGrid`'s
   own `spatiallySampleElement` (already-tested, untouched) densely
   samples each element's *own* corner quad, but the quad itself
   (`kHalf` = 0.3mm) was smaller than one voxel. Fixed by widening
   `kHalf` to a plausible real transducer-element size (6mm) so each
   element fills many voxels instead of one -- real coverage density,
   not just a wider bounding box.
3. The formula's own index offset (`i+1`/`i`, not centered) also put the
   raw, pre-registration mean position (used separately for
   `centerArrayMm`/the target crosshair above) at (~91, ~44, 0) mm,
   nowhere near any default slice either. Fixed by centering the index
   formula on 0 -- a real transducer's own blueprint origin would
   naturally sit at its center, not one corner.

None of these touch element count, order, or center-position *rows*
(only the values in them), so `setArrayFiducialMarkers`'s index-based
basis vectors (elements 1/81, 24/1, 31/40) see the same relative
geometry, just uniformly repositioned. Verified via a temporary
per-slice voxel-hit count (0-2 hits before -> 67/13/206 for
Sagittal/Coronal/Axial after) and confirmed visually (`PrintWindow`
capture, not a screen coordinate-mapped screenshot -- this session hit
real DPI-virtualization issues with the more usual screen-capture
approach on this machine).
