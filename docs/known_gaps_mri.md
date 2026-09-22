# Known gaps and deliberate exclusions — MRI phase

Same convention as the other `docs/known_gaps*.md`. Beam's port of
`BeamV0/GUIMatlab/BEAM/MRI/` (11 own files + an 11-file vendored
`dicom2nifti/` toolbox = 22). Fifth phase.

## Reused from Diadem

BeamV0's MRI `.m` files (`get_ras_xform_fromHdr.m`,
`getRASAxisVectorsFromNifti.m`, `getSliceImage.m`) are **byte-identical**
to DiademV0's. Diadem already has a from-scratch, MATLAB-parity-verified
port of this math plus a NIfTI-1 reader and a DCMTK-based DICOM series
reader. Rather than re-derive it, `libs/mri/` and `libs/infra_dicom/` are
**source copies of Diadem's `libs/imaging/` and `libs/infra_dicom/`**,
renamespaced `diadem::imaging` -> `beam::mri` and `diadem::infra::dicom` ->
`beam::infra::dicom`. One-time source copy, **not** a build dependency on
`../Diadem`.

`dcmtk` was added to `Beam/vcpkg.json` — the cross-platform way to get a
DICOM reader (vcpkg builds it on Linux/macOS/Windows alike).

Copied into `libs/mri/`: `nifti_header`, `ras_transform`
(`getRasXformFromHeader`, `getRasAxisVectors`, `getVoxelRasXform`),
`nifti_file` (NIfTI-1 reader), `nifti_orientation` (`applyVoxelRasXform3D`),
`mri_loader` (`loadNiftiMriRas`), `dicom_coords`, `dicom_series_geometry`.
Copied into `libs/infra_dicom/`: `dicom_tags`, `dicom_slice`,
`dicom_series` (`assembleDicomSeriesRas`), `load_mri_ras` (`loadMriRas` --
the real `loadMRIRAS.m` dispatcher). Diadem's `dicom_probe` (a toolchain
smoke test) was not copied. Tests: Diadem's `imaging_tests.cpp` (NIfTI +
DICOM-coord parts) and `infra_dicom_tests.cpp`, renamespaced.

## Ported this phase

- **`get_ras_xform_fromHdr`** -> `getRasXformFromHeader`
- **`getRASAxisVectorsFromNifti`** -> `getRasAxisVectors`
- **`getSliceImage`** -> `getSliceImage` (Beam's own; `imrotate(·,90)` ->
  `rot90`)
- **`loadMRIRAS`** -> `beam::infra::dicom::loadMriRas`. Both branches now
  work: a DICOM folder goes through `assembleDicomSeriesRas` (the
  from-scratch replacement for the vendored `dicm2nii`), a `.nii*` file or
  fallback goes through `loadNiftiMriRas`.
- **`getDicomPixelSpatialReference`** / **`loadDicomDirRAS`** -- their
  capability (DICOM directory -> RAS-oriented volume + physical-position
  axes) is covered by `assembleDicomSeriesRas` + `dicom_coords`. The
  literal MATLAB functions aren't translated line-for-line; the coordinate
  math from them is (`getDicomPixelSpatialReferenceMath`,
  `resolveDicomOrientationDefaults`, `getDicomVolumeCoords`).
- Plus `nifti_utils/get_voxel_RAS_xform.m` and `.../vol_apply_xform.m`
  (3D only) -- dependencies of the NIfTI branch.

`assembleDicomSeriesRas` covers the common case only: one consistent
orientation, uniform slice spacing, 16-bit uncompressed pixel data.
Multi-orientation / interleaved / gantry-tilted / compressed series throw a
clear named error. Replicating the full vendored `dicm2nii.m` (~3100 lines
of vendor-specific quirks: mosaics, multi-frame, diffusion tags) is
explicitly out of scope.

## Ported later

**`setNiftiFromSys.m`** — its portable core is now `writeNiftiVolume`
(`libs/mri/nifti_file`): the inverse of `readNiftiVolumeScaled` (348-byte
header + pad + voxels in the datatype the header names). New code, not from
Diadem (whose imaging lib is read-only). The `sys.niftiinfo` / `niftiV`
bookkeeping and the `nifti_utils` dependency are not carried over — the
caller supplies the header + volume.

## New infrastructure (no `.m` counterpart, GUI phase, 2026-09-11)

**`reorientedVolumeToVolume3D`** (`libs/mri/mri_loader`) — reshapes
`loadNiftiMriRas`'s flat, column-major `ReorientedVolume` output into the
`Volume3D` (one matrix per k-slice) that `getSliceImage` expects. Pure
plumbing between two already-ported representations, not a translation of
any single `.m` file — doesn't change this phase's conversion count. Real
gtest coverage (`ReorientedVolumeToVolume3D.ReshapesColumnMajorFlatVoxelsPerKSlice`).

This is what lets `apps/beam_app --mri <path.nii>` (GUI phase, see
`docs/known_gaps_gui.md`) load a **real** NIfTI file through the actual
`loadNiftiMriRas` reader into the Register tab's MRI viewer, verified
against a real 192x256x256 anatomical scan found on the local machine
(loads in ~3.4s). That file is **not** bundled with the repo — real MRI
data, unclear redistribution rights, and 25 MB besides — `--mri` takes a
local path at runtime.

**`loadDefaultSys.m`** (2026-09-14) — reclassified here from Excluded,
see below: loads a real ~130 MB default-subject dataset
(`DefaultSubjectV0/defaultSubjectMNIV1.mat`, a sibling folder *outside*
BeamV0's own repo, resolved via `what('BeamV0')` + `..`) unconditionally
on every startup, before `startUpFunction` runs -- this is why a fresh
BeamV0 launch shows real MRI images immediately, with no clicks. Missed
entirely on first pass: the "hardcoded path" pattern-matched the other
genuinely-dead Excluded stubs below, without registering that this path
points to real, load-bearing product data. This port can't read `.mat`
(no MAT-file reader), but replicates the same *architecture* -- a real
default subject kept external to the repo, not bundled -- rather than
committing a large binary into Beam's git history: `apps/beam_app/main.cpp`
tries `DefaultSubjectV0/defaultSubjectMNIV1.nii` (a `save_untouch_nii`
export of the same default subject's `app.sys.aImg`, done once from a
live MATLAB session since this port has no `.mat` reader) before falling
back to the synthetic volume, exactly the same graceful-degradation
policy `--mri` already had.

First attempt resolved that path relative to the process's *current
working directory* only -- worked when run from the repo root, silently
fell back to synthetic otherwise (e.g. double-clicking the exe, or
running it from its own `build-msvc/apps/Release/` output folder, both
of which set CWD to wherever the exe sits, not the repo root). Caught
immediately by the user running exactly that. Fixed by resolving instead
relative to `QCoreApplication::applicationDirPath()` (the running exe's
own location, stable regardless of launch method -- `beam_app.exe` sits
3 directories below the repo root, `DefaultSubjectV0` is a sibling of
the repo root, so 4 levels up), tried before the CWD-relative path as a
fallback. Verified end-to-end from both a repo-root shell *and* `cd`'d
directly into `build-msvc\apps\Release\` (the user's exact repro):
`apps/beam_app --check` with zero arguments reports
`mri=default-subject:...227x213x182` in both cases (matching
`size(app.sys.aImg)` from a live BeamV0 session exactly), and an
interactive run shows real anatomy in all 3 slice views with no flags
or clicks. Only the MRI piece of `loadDefaultSys.m`'s much larger
`sys` struct (38 fields -- also real default array/correction/stim-param/
CRF/registration state) is replicated; see `docs/gui_widget_inventory.md`
for the wider default-state gap this doesn't close.

**`updateSysWithMRI.m`** (2026-09-12) — mostly `app.sys.*` state glue
(storing the loaded image/headers/niftiinfo -- already this port's own
`mriVolume`/`ras.header` variables serve that role), but it has two real
computations: `res = abs(diff(dim(1:2)))` per axis (voxel spacing --
already used inline, duplicated, by `rasterizeArrayOntoMriGrid`'s `fs`
calculation) and `window = [0, max(aImg(:))]` (display intensity range).
Both now ported as named, tested functions:
`beam::mri::computeVoxelResolution` (`libs/mri/ras_transform`) and
`beam::mri::computeDisplayWindow` (`libs/mri/slice`) -- the former also
now shared by `rasterizeArrayOntoMriGrid` instead of duplicated inline.
Exposed via `apps/beam_app --check` (`mriResLR=`/`mriWindowHi=`, 0 for the
synthetic volume, which has no physical calibration).

## Deferred

**`windowUpDownCallback.m`** (reclassified here 2026-09-14, was
Excluded) — misclassified on first pass as "a GUI event handler,"
without actually reading it: it's real, portable brightness logic, not
glue. Computes a new display-window ceiling (`app.sys.window(2)`) from
the whole volume's own intensity range (`inc = round(range/8)`), with
bounds checks, then redraws -- the same "dismissed without reading"
mistake `loadDefaultSys.m` hit earlier this session. Not yet ported:
this port's `MriSliceView`s each auto-normalize contrast per-slice
(`renderMriSliceImage`'s own `slice.minCoeff()/maxCoeff()`), not from one
shared, adjustable, volume-wide window range like the source -- porting
`windowUpDownCallback.m`'s math is small, but wiring a *shared* window
through 3 independently-rendering views is a real rendering-pipeline
change, scoped as a dedicated follow-up rather than rushed alongside
other fixes. See `docs/gui_widget_inventory.md`.

## Excluded

**`overlayMRIOntoCurrentSubject.m`** — `uigetfile` + WSL + FreeSurfer BET +
FSL FLIRT + SPM12 via `system(...)`. External-tool pipeline.

**`dicom2nifti/` (11 files)** — the vendored `dicm2nii` / `nii_tool`
toolbox. Third-party; replaced by `libs/infra_dicom` + `libs/mri`'s NIfTI
reader, not translated.

## Build status

`cmake --preset msvc && cmake --build --preset msvc`, **80/80** tests via
`.\build-msvc\tests\Debug\unit_tests.exe` (30 new this phase: NIfTI + DICOM
readers and their coordinate math). Run the exe directly — `ctest` is
Application-Control-blocked on this machine. `tests/CMakeLists.txt` uses
`DISCOVERY_MODE PRE_TEST` so a blocked fresh binary doesn't fail the build.

DCMTK fixture tests write `.dcm` / `.nii` files into the working directory
via DCMTK's own writer API (independent construction path, not
hand-guessed bytes). The `W:`/`E:` lines DCMTK prints during the
garbage-file negative tests are expected — those tests assert the reader
throws.

## MATLAB parity

`matlab_verify/` diffs the coordinate-math ports against the real BeamV0
`.m` functions on synthetic headers + a synthetic volume (atol/rtol 1e-9):
**69/69 matched** (`compare_parity.ps1 -Phase mri`, 2026-09-10). Covers
`get_ras_xform_fromHdr` (qform, negative-qfac, and sform branches, full 3x4
xform), `getRASAxisVectorsFromNifti` (LR/AP/IS axis endpoints), and
`getSliceImage` for all three planes — the last confirms MATLAB's
`imrotate(·, 90)` is exactly `rot90` (the ported assumption).

The DICOM-assembly path (`assembleDicomSeriesRas`, `dicom_coords`) is not
in the parity set: it replaces vendored `dicm2nii` code and has no MATLAB
counterpart to diff against. Diadem's real-file NIfTI parity fixture
(`matlab_parity_nifti_mri`) was also not brought over.
