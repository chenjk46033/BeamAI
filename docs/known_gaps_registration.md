# Known gaps and deliberate exclusions — Registration phase

Same convention as the other `docs/known_gaps*.md`. Beam's port of
`BeamV0/GUIMatlab/BEAM/Registration/` (27 `.m` files across the top level +
`ImageBasedModel/`, `MNIRegistration/`, `MRINeuroNav/`). Sixth phase.

Registration splits cleanly: a small core of point-set / fiducial-marker
geometry is pure and portable; the rest is GUI `app` glue, external
registration tools (SPM12), or a whole image-based transducer-detection
subsystem.

## Reused from Diadem

`affineRegistration.m`, `applyAffineMatrixToFrameData.m`,
`getTransducerFiducialMarkersPositionFromFrame.m`, and
`getArrayFiducialMarkerNames.m` are **byte-identical** (or trivial) between
BeamV0 and DiademV0. Those C++ ports are copied from Diadem's
`libs/registration`, renamespaced `diadem::registration` ->
`beam::registration`. `affineRegistration` gained one addition: BeamV0's
`getAffineMatrixFromRegistration.m` calls it with a `weights` vector
(`[0.5,1,0.5,0.5,1,0.5]`), which DiademV0's does not — so the weighted
centering path is ported here.

`setArrayFiducialMarkers.m` and
`MRINeuroNav/.../getTranslationMatrixFromTransducerFiducials.m` are
**BeamV0-specific** — a different 6-marker set and geometry than DiademV0's
8 corner-element markers — and were ported from BeamV0's own source.

## Ported this phase (`libs/registration/`)

**`affine_registration.{hpp,cpp}`**
- `affineRegistration` — Horn's quaternion method, 3D branch, with the
  weighted path. The 2D branch and the `Bfit`/`ErrorStats` outputs are not
  ported (unreachable / unused, same as Diadem).
- `getAffineMatrixFromRegistration` — wraps it with BeamV0's fixed weights
  and packages R/t into a 4x4 (with the `meters2mmFlag` /1000 branch).

**`fiducial_markers.{hpp,cpp}`**
- `getArrayFiducialMarkerNames` — the 8 "EL*" names. NOTE: BeamV0's
  `setArrayFiducialMarkers.m` no longer uses these (it builds its own 6
  "LeftY1Z3"/... names inline); kept for file parity, effectively dead.
- `setArrayFiducialMarkers` — BeamV0's 6-marker construction: fixed offset
  from each array half's rect centre, then rotated about that half's Y1Z1
  marker by the array's x/y/z basis (elements 1/81, 24/1, 31/40). Returns
  `std::vector<FiducialMarker>` rather than mutating `arrayData`.
- `applyAffineMatrixToFiducialMarkers` — the per-list transform from
  `applyAffineMatrixToFrameData.m` (which in MATLAB bundles two point
  lists against a full `frame` struct; ported as the single reusable
  transform).
- `getTransducerFiducialMarkersPositionFromFrame` — slider-delta ->
  physical displacement, rotated into the registration frame.
- `getFiducialPositionFromName` — name lookup (last match wins; throws if
  absent).
- `getTranslationMatrixFromTransducerFiducials` — the X/Y/Z basis from the
  6 fiducials' pairwise unit-vector medians. The MATLAB `app` argument is
  unused in the source body and dropped.

**`ImageBasedModel/` -- `image_model.{hpp,cpp}`.** The pure-algorithm parts
of the photo-based transducer-detection pipeline:
- `getImgOrientationAngle` -> `orientationAngleFromExif(tag)` -- just the
  EXIF-Orientation -> angle mapping (1/3/6/8 -> 0/180/90/-90); the
  `imfinfo` read is the caller's.
- `Lib/perimwalkImage2` -> `perimwalkImage2` -- the 8-connectivity DFS
  perimeter ordering + jump-split + best-segment scoring, replicated
  offset-for-offset and LIFO-stack-faithful so the walk order matches.
- `Lib/perimdistanceImage` -> `perimdistanceImage`.
- `getDiscreteFromImage` -> `getDiscreteFromImage` -- the scalp-marker
  sampling at 0/10/30/50/70/90/100 % of the Nz->Iz perimeter distance.
  Takes a binary contour matrix; `find`/crop/`vecnorm`/`cumsum` ported
  directly. The unused `NzCnt` intermediate is dropped.
- `organizeTransducerSlots` -> `organizeTransducerSlots` -- the
  sort-by-Z / sort-by-Y slot ordering and pairing with the array's
  fiducial-marker names + x coordinates. `app.sys.imageRegStruct...` and
  `app.sys.arrayData.fiducialMarkers` become plain arguments.
- `apply_affine_3d` -> `beam::mri::applyAffine3D` (`libs/mri/affine_volume`)
  -- resample a 3D volume through a world-mm affine onto an output grid
  (same voxel size) that fits the transformed bounding box. MATLAB's
  `imref3d`/`affine3d`/`imwarp` become an explicit inverse-map + trilinear
  sampler (imwarp `'linear'` + `FillValues` 0). The source's half-voxel
  bounding-box pad + `ceil` (which makes even an identity transform return
  a one-voxel-larger, half-voxel-shifted grid) is reproduced as-is.

## Ported in the GUI phase (`libs/registration/array_transform`)

**`registerArrayToFiducials.m`** — the "Register To MRI Fiducials" button
(operator manual, Device to Subject Registration). Fits the array's own
nominal fiducial positions onto 6 target positions (the fiducial table's
current mm values -- the operator's dragged-donut-marker positions in the
real app) via the already-ported `getAffineMatrixFromRegistration`, then
applies the result via the already-ported `applyAffineToArrayData` (which
also recomputes the fiducial markers via `setArrayFiducialMarkers`, so one
call gives back both). `getAffineMatrixFromRegistration`'s fixed BeamV0
weights require exactly 6 points on each side -- the source relies on this
implicitly (`app.FiducialROIs`/`arrayData.fiducialMarkers` always have 6
entries); this checks explicitly and throws `std::invalid_argument`
instead of failing inside `affineRegistration` with a less obvious
dimension-mismatch error. Dropped: the commented-out slider edit-field
updates, and the trailing `drawTransducersOnMRI(app, arrayData)` call --
re-rasterizing the array/focus overlays after registering is
`apps/beam_app`'s job (already-ported `rasterizeArrayOntoMriGrid`/
`rasterizeFocusEllipsoidOntoMriGrid`, wired the same way the initial
overlay was in the GUI phase -- see `docs/known_gaps_gui.md`).

Wired to a real button (`BeamMainWindow::setRegisterHandler`). Always
re-fits from the fixed nominal array (matching the source's
`app.sys.originArrayData`) -- clicking Register again re-fits from
scratch using whatever the fiducial table shows *then*, it doesn't
compound onto the array's last registered position. Verified via
`apps/beam_app --check` (`register movedArray=1`: nudges the fiducial
table by a known 12mm offset, registers, confirms the array's own
fiducial markers moved by exactly that) and a clean interactive run with
a real `--mri` file loaded (overlay counts visibly changed after
registering).

**`MRINeuroNav/TranslateArrayPosition/registerCurrentTransducerPostion.m`**
— scoped to its `MRIFiducialsButton` branch only (the manual's "Outside
the MRI" registration: "Register Arrays to Current Position", using the
"Array Lock Position Subject Left/Right" sliders) ->
`beam::registration::registerCurrentTransducerPosition`. Calls
`registerArrayToFiducials` (above), then converts the horizontal/
vertical physical slider reading into a world-frame translation via the
already-ported `getTranslationMatrixFromTransducerFiducials` basis
(reapplying the source's own Z-vector sign-flip-if-negative here, since
that ported function doesn't do it internally) and applies it on top.
The other two branches (`MRIFree`, hardcoded MNI landmarks;
`PhotoBased`, needs an `imageRegStruct` this port doesn't model) stay
deferred. The source's own Left/Right slider-mismatch check (a
`SystemStatusTextArea` warning) isn't reproduced -- there's no such
textarea binding here to write it to.

Wired to a real button + two sliders (`BeamMainWindow::setRegisterCurrentPositionHandler`).
Verified via `apps/beam_app --check` (`currentPositionMoved=1`: moves the
vertical slider by one unit and confirms the array center shifted by
exactly the 10mm `verticalDelta`) and a clean interactive run. Tests
`RegisterCurrentTransducerPosition.VerticalSliderShiftsByExactDeltaMagnitude`/
`HorizontalSliderShiftsByExactDeltaMagnitude` check the shift magnitude
exactly (it equals the raw mm delta regardless of the fiducial geometry,
since each basis column is a unit vector) rather than a fragile
geometry-dependent value.

**`setRegistrationCheck.m`** (2026-09-12) — the Register tab's 3 status
lamps (Right/Left/InsideMRI Registration) plus the Sonicate button's
enable state, both driven off the same two completion flags
(`app.sys.frame.MRIRegistrationComplete`/`CurrentRegistrationComplete`)
`checkSonicationSafety.m`'s report also reads (see
`docs/known_gaps_gui.md`'s safety-presenter note) -- but this is a
separate, independent piece of the source's own UI wiring, not a
duplicate of that report. Ported as
`beam::gui::computeRegistrationCheckLampState`
(`libs/gui/registration_check_presenter`). Disclosed source quirk,
preserved as-is: the source's two `if` blocks run unconditionally in
sequence, so the *second* block's write to the Right lamp always
overwrites the first's -- net effect, the Right lamp tracks
`currentRegistrationComplete` only, not `mriRegistrationComplete` despite
the first block's apparent intent. Wired to real state in
`apps/beam_app/main.cpp`: the "Register To MRI Fiducials"/"Register
Arrays to Current Position" buttons each set their flag on success and
recompute the lamps + Sonicate button's `QPushButton::setEnabled`
(`BeamMainWindow::setRegistrationCheckLampState`) -- so the Sonicate
button now starts disabled and only becomes clickable after both
registrations have succeeded at least once, matching the source's own
gating. Verified via `apps/beam_app --check`
(`registrationCheckSonicateEnabled=1` after both registration buttons
fire) and a headless Release build.

**2026-09-16 audit** — read the Register tab's own inline `.mlapp`
callbacks directly (`HorizontalPositionSliderValueChanged`,
`VerticalPositionSliderValueChanged`, `RegisterToMRIFiducialsButtonPushed`
in `matlab/document.xml`, not a standalone `.m` file, so easy to miss in
a file-count audit) against the current port and found two real,
narrow behavioral gaps beyond what was disclosed above:

- `RegisterToMRIFiducialsButtonPushed` sets `CurrentRegistrationComplete
  = 0` in addition to `MRIRegistrationComplete = 1` -- re-fitting to
  fiducials invalidates any prior "current position" registration, since
  that offset was relative to the now-superseded array pose. The port's
  `performRegistration` set only `mriRegistrationComplete`, leaving
  `currentRegistrationComplete` sticky-true forever once set once. Fixed
  in `apps/beam_app/main.cpp`.
- `HorizontalPositionSliderValueChanged`/`VerticalPositionSliderValueChanged`
  both set `CurrentRegistrationComplete = 0` and recompute the lamps on
  *every* slider move, live -- not just at the next button click. The 4
  position sliders had no `valueChanged` wiring at all. Added
  `BeamMainWindow::setPositionSlidersChangedHandler` (connects all 4)
  and wired it in `main.cpp` to invalidate + recompute immediately,
  matching the source.

Confirmed via the same `--check` sequence (fiducial-register then
current-position-register, in that order) and the full 234-test suite;
neither fix changes any registration math, only the completion-flag
bookkeeping around it.

Everything else checked in this pass -- the `MovetoButtonGroup`
radio-navigation/`MoveFiducialtoCurrentViewButtonPushed` substitution
(table-row-driven instead, already disclosed in
`libs/gui_qt/include/gui_qt/main_window.hpp`'s own comments),
`RegistrationTypeButtonGroup`'s tab-switch-only scope, the Left/Right
slider mismatch warning, and the 4-slider reset-to-1 on re-registering
to fiducials -- was already faithfully ported and wired.

**`saveFiducialROIs.m`** (2026-09-12) — copies each fiducial marker's
name/position from the live ROI widgets into `app.sys.frame.FiducialROIs`
for `File->Save Subject`. Its exact capability (a `{name, position}` pair
per marker) is already what `libs/gui/session_io`'s fiducial section
saves (`beam::registration::FiducialMarker{name, position}`, the same two
fields) -- built as "new infrastructure" in an earlier slice without
crediting this source file. Recognizing the match, reclassified Deferred
-> Converted; no new code needed.

## Deferred

None -- the last item here (`ImageBasedModel/getImageData.m`) was
reclassified to Excluded 2026-09-12, see below (it was never going to be
genuinely revisited, so Deferred was the wrong bucket for it).

  ~~`setSysarrayDataFiducialMarkers.m`, `getFiducialROIGroupIndicies.m`,
  `getFrameFiducialsCenter.m`~~ -- believed deferred here too at the
  time (same "app`-coupled glue" description as `setRegistrationCheck.m`/
  `saveFiducialROIs.m`, above, before those two were themselves ported),
  but a 2026-09-12 reachability check found all three dead:
  `setSysarrayDataFiducialMarkers.m`'s only reference is a commented-out
  line in `startUpFunction.m` (git history shows it was live once);
  `getFiducialROIGroupIndicies.m` had several real call sites once (git
  history), all since deleted; `getFrameFiducialsCenter.m` never had a
  caller, ever. Moved to Excluded, see below. No longer in this bucket.

## Excluded

- **`ImageBasedModel/getImageData.m`** (moved here from Deferred,
  2026-09-12) — the 379-line orchestrator: `imread` / `imfinfo` /
  `imrotate`, interactive point picking, and a pipeline over the pieces
  above. Read in full 2026-09-11 while auditing the Deferred bucket: it's
  real (a genuine caller, `PhotoBasedRegistrationButtonPushed` in
  `BeamV0.mlapp`), but not a portable-math candidate like some other "GUI
  glue" turned out to be -- it shells out to an entire Python
  computer-vision pipeline via MATLAB's `py.*` interop
  (`py.CensorImg.check_if_censored`/`restore_original_jpeg`, `pyenv`,
  `insert(py.sys.path,...)`) for face-de-identification/reconstruction,
  plus commented-out interactive `drawfreehand` ROI picking. Porting it
  would mean porting an external Python ML pipeline this repo doesn't
  have and never will -- by definition (`docs/conversion-status.md`)
  that's Excluded, not Deferred ("will be revisited"). With this move,
  every file in `ImageBasedModel/` is now Excluded -- the whole
  subsystem has no portable content, only image I/O, interactive
  picking, and this Python dependency.

- **`ImageBasedModel/addCondaToPath.m`** — prepends a hardcoded Anaconda
  path.
- **`loadDicomDir.m`** — reclassified here 2026-09-11 after turning up no
  callers *anywhere*: not the plain `.m` tree (its only near-namesake,
  `MRI/loadDicomDirRAS.m`, does NOT call it -- a coincidental filename
  substring, confirmed by reading `loadDicomDirRAS.m` in full; it calls
  `getDicomPixelSpatialReference` instead and reloads the DICOM files
  itself), not `BeamV0.mlapp`'s embedded code, not BeamV0's git history
  (one commit, the initial Diadem-fork copy). Its would-be capability
  (DICOM directory -> coordinate-registered volume) is covered anyway by
  `assembleDicomSeriesRas` (`libs/infra_dicom`, ported in the MRI phase --
  see `docs/known_gaps_mri.md`). Genuinely dead, not merely deferred; not
  ported.
- **`setSysarrayDataFiducialMarkers.m`** — reclassified here 2026-09-12:
  its only reference anywhere is a commented-out line in
  `GUI/startUpFunction.m`; git history shows a real, uncommented call
  once, later disabled.
- **`getFiducialROIGroupIndicies.m`** — reclassified here 2026-09-12:
  zero callers now; git history shows several real call sites (in a
  since-deleted caller), all removed.
- **`getFrameFiducialsCenter.m`** — reclassified here 2026-09-12: never
  had a caller anywhere, not even historically -- defined once, never
  wired to anything.
- **`MNIRegistration/registerMNI2Subject.m`**,
  **`spmRegistrationEstimateAndReslice.m`**,
  **`spmRegistrationEstimateAndReslice_job.m`** — drive SPM12 (external
  MATLAB toolbox) for MNI-template registration. `addpath('...spm12')` +
  `spm_jobman`. Not portable domain logic.

## Build status

`cmake --preset msvc && cmake --build --preset msvc`, **130/130** tests via
`.\build-msvc\tests\Debug\unit_tests.exe` (registration + its ImageBasedModel
slice: affine registration, fiducial markers, array transform, perimeter
walk, scalp markers, slot organise, 3D affine resample). Run the exe
directly — `ctest` is Application-Control-blocked on this machine.

## MATLAB parity

`matlab_verify/` diffs the ports against the real BeamV0 `.m` functions on
synthetic point sets / fiducials / a synthetic 90-element array (atol/rtol
1e-9): **89/89 matched** (`compare_parity.ps1 -Phase registration`,
2026-09-10). Covers `affineRegistration` (R/t/q, unweighted + weighted),
`getAffineMatrixFromRegistration` (both `meters2mmFlag` branches),
`getTranslationMatrixFromTransducerFiducials`, `getFiducialPositionFromName`,
the per-point transform of `applyAffineMatrixToFrameData`, and
`setArrayFiducialMarkers`.

Not in the parity set: the `ImageBasedModel/` perimeter-walk /
scalp-marker / slot-organise functions (covered by unit tests, replicated
offset-for-offset), `getImgOrientationAngle` (a 4-case EXIF switch, needs a
real image file), and `getTransducerFiducialMarkersPositionFromFrame`
(needs a full `frame` struct).
