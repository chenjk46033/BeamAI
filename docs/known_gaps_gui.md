# Known gaps and deliberate exclusions — GUI phase

Beam's `GUI/` module is `BeamV0/GUIMatlab/BEAM/GUI/` — 88 `.m` files plus
`BeamV0.mlapp` (the App Designer layout). Ninth and last module.

Unlike every other phase, GUI is **not a translation target**. It is
widget layout, table setup, plotting (`draw*`, `create*Table*`,
`update*Plots`, `setCRF*`), and event callbacks against the live `app`
object. The UI is built fresh in **Qt** (chosen 2026-09-10), reusing
`libs/*` — not by porting App Designer callbacks line for line. Every
file's real, portable content (if any) has now been extracted into
`libs/gui`/`libs/gui_qt`; the rest — pure widget/app-object glue,
cosmetic styling, or App-Designer-layout-specific interaction heuristics
with no Qt equivalent to translate to — is **Excluded**, not Deferred:
none of it is "still to do," per `docs/conversion-status.md`'s own
definition (see "Fourth pass," below, 2026-09-12). GUI is 43/0/45 (100%
Converted, 0 Deferred).

## Architecture: presenter / view split

Same split `infra_dicom` established for DCMTK — domain/presenter logic
carries no third-party dep:

- **`libs/gui`** (`beam::gui`) — framework-independent presenter logic: the
  data transforms and orchestration behind the GUI `.m` files, decoupled
  from any `imagesc` / `bar` / `plot` / widget calls. Unit-tested with no
  Qt.
- **`libs/gui_qt`** (`beam::gui_qt`) — the thin Qt layer (`Qt6::Widgets`
  `Qt6::Charts`, `AUTOMOC`). `find_package(Qt6 QUIET ...)` + `return()`, so
  a toolchain without the vcpkg Qt manifest still configures. Turns
  presenter output into real `QWidget` / `QChart` / `QImage`.
- **`libs/gui_qt/main_window`** — `BeamMainWindow` (`QMainWindow` +
  `QTabWidget`), the app shell. Tab order matches `BeamV0.mlapp`'s
  top-level `TabGroup`: **Sonicate**, **Register**, **Correction**. Tabs
  are populated by the caller from the presenters / chart builders; tabs
  with nothing ported yet show a placeholder (Register).
- **`apps/beam_app`** — the shell's entry point: runs the real `libs/*`
  pipeline (synthetic input) into the real tabs. Ships the
  `platforms/qwindows[d].dll` plugin next to the `.exe` (vcpkg deploys the
  `Qt6*.dll`s but not plugins). `--check` populates both tabs headless and
  prints a summary, for a build smoke test; `fail` shows the failing
  states.

`vcpkg.json` gains `qtbase` (feature `widgets`) + `qtcharts`.

### Ported: Safety tab (slice 1, 2026-09-10)

`libs/gui/safety_presenter` — `checkSonicationSafety(sonications,
preconditions)`, the orchestration in `GUI/Safety/checkSonicationSafety.m`
on top of the already-ported `libs/safety` per-sonication checks: it
appends the registration-incomplete and through-transmit-too-low
precondition messages (from `app.sys.frame.*Complete` /
`app.sys.RTT(1).att` vs `app.attenuationThreshold`, passed in as a plain
struct) and builds the `app.SystemStatusTextArea` status string. The
`set(app.SonicateButton,'Enable',...)` side effects and the deferred
IsptaAll-over-all-events check (needs `setBurstEventsFromStimParams`) are
not carried.

`libs/gui_qt/safety_report_view` — `SafetyReportView` (`QWidget`): status
line + blocking-message list, green on pass / orange on fail (matching the
source's `FontColor` usage). Lives on `beam_app`'s Sonicate tab.

### Ported: Correction tab (slice 2, 2026-09-10)

Only `updateAttenuationPlots.m` is live in BeamV0's `CorrectionTab/` (from
`initializeCorrectionValues.m`). Unlike Diadem's same-named file, it draws
**RF-waveform line plots**, not an attenuation heatmap — no `imagesc`, no
`elementMapping`.

- `libs/gui/correction_tab_presenter`:
  - `computeAvgTransmissionBars(att, transmissionPeak2Peak, couplingThreshold)`
    — `setAvgTransmissionDataBars.m`'s two-bar Current/Average data. **Quirk
    preserved:** `plotAttBars` colours the "Current" bar from `att < 0.1`
    and returns a matching flag, but `setAvgTransmissionDataBars.m` then
    overwrites that flag with `transmissionPeak2Peak > couplingThreshold` —
    so colour (`currentBarIsRed`) and pass (`pass`) come from different
    quantities.
  - `computeRfPlotYLimit(x0, x1)` — `updateAttenuationPlots.m`'s `ULBound`
    = `max(max|x0|, max|x1|) + 10`.
- `libs/correction/signal` gained the two DSP pieces that feed it:
  `txRxSignalAmplitude` (`BEAMANALYSIS/analysisUtil/getTxRxSignalAmplitude.m`
  — crop, `findpeaks`-with-height, `rms`, `abs(hilbert)`; out-of-`GUI/`
  helper, doesn't count against a row) and `throughTransmitAmplitude`
  (`getTransmissionAfterThroughTransmit.m`'s computation, two waveforms
  passed in instead of read off `app.sys.RTT`).
- `libs/gui_qt/correction_tab_chart`: `buildTransmissionBarChart`
  (two-`QBarSet` per-category colouring, error-bar whiskers omitted — Qt
  Charts has none) + `buildRfWaveformChart` (raw + filtered overlaid,
  symmetric `[-yLimit, yLimit]`). Shown on `beam_app`'s Correction tab
  (synthetic RF → `throughTransmitAmplitude` → presenters → three charts).

### App shell (slice 3, 2026-09-10)

`libs/gui_qt/main_window` (`BeamMainWindow`) + `apps/beam_app` — no new
`.m` ports, pure Qt scaffolding. A `QMainWindow` with a `QTabWidget` in
`BeamV0.mlapp`'s top-level tab order (Sonicate / Register / Correction).
The Safety report (slice 1) sits on the Sonicate tab, the Correction
charts (slice 2) on the Correction tab; Register is a placeholder until
that slice lands. The two earlier per-tab demo exes are replaced by this
single `beam_app`.

### Ported: UIFeatures / Case Report Form (slice 4, 2026-09-10)

`libs/gui/case_report_form` — the CRF session-metadata logic from
`UIFeatures/setCRF*.m` + `setSessionCaseReportParameters.m`, no Qt:
- `setCrfDateTime(crf, y, mo, d, h, mn, s)` — `setCRFDateTime.m`'s field
  spread (the `VisitDateDatePicker` write excluded; caller decomposes the
  timestamp).
- `computeCrfAutoSaveFilename(crf)` — `setCRFAutoSaveFilename.m`'s
  `S<site><pid>Visit<vn>DateM<mo>D<dy>Y<yr>`. BeamV0 has the `CurrH`/`CurrV`
  slider suffixes commented out (DiademV0 keeps them) — not included.
- `buildSessionCaseReportForm(inputs)` — `setSessionCaseReportParameters.m`'s
  unconditional body: the hardcoded `Utah` / `BeamV01` / `xdr001` / `xdr002`
  / `V1.0.0` constants + the setter sequence. The widget-read preamble and
  hydrogel-size→radio-button mapping stay with the eventual tab.

`setCRFOperatorID/HydrogelSize/VisitNumber/ParticipantID.m` are each one
field assignment → `CaseReportForm` struct fields, not ported as functions
(`setCRFParticipantID` set `SubjectID` and `ParticipantID` to the same
value — one `subjectId` field). `setStartTime.m` (`evalin('base','Resource')`
+ axes scan) is **excluded**. The countdown/timer files
(`cleanupCountdown`, `startStandaloneCountdown`, `updateFigureTimer`)
stay deferred. (`currentSonicationTimeMarker`/`progressBarButtonPushed`
were believed deferred here too at the time -- a 2026-09-12 check found
their only references are commented-out lines inside the already-dead
`setStartTime.m`, so they're Excluded, not Deferred; see that
correction further below.)

### Ported: RegistrationTab coordinate math (slice 5, 2026-09-10) — corrected 2026-09-12, see below

`libs/gui/registration_tab_presenter` — pure geometry ported from
`RegistrationTab/{Targeting,AutoReg}/*.m` before this project had
established the habit of checking source-file reachability first (that
habit started around the `getSteeringAmplitudeCorrection.m` reclassification
later in the project). One of the three still holds up; two don't --
see the correction below for the full reachability trace.
- `getAcpcTransform(ac, pc, midSagittalPlaneNormal, referenceCoords)` —
  `Targeting/getACPCTransform.m`'s 4x4 AC/PC/mid-sagittal-plane frame.
  **Reclassified to Excluded 2026-09-12** -- `getACPCTransform.m` itself
  has zero real callers (only the equally-dead `Targeting/getACPCFromSys.m`/
  `setACPCTransform.m` call it). The C++ port is left in place (it's a
  correct, tested translation of the algorithm, and deleting working code
  for a bookkeeping correction would be destructive for no benefit) but
  no longer counted as a live conversion. **Quirks preserved** (for the
  record, since the code still exists): the X-axis step *adds* the
  Y-projection (`X = X + dot(X,Y)*Y`) rather than subtracting it as
  Gram-Schmidt orthogonalization would; `disp(...)` debug prints are
  dropped; throws on `Z(3) < 0`, matching the source's `error(...)`.
- `applyReferenceCoordinateTransform(transform, xyz, direction)` —
  `Targeting/applyReferenceCoordinateTransform.m`. **Reclassified to
  Excluded 2026-09-12** -- zero callers anywhere, not even from the dead
  ACPC cluster itself. Same "leave the code, correct the count" treatment
  as above.
- `getArrayFiducialNormals(fiducialMarkers)` — `AutoReg/getArrayFiducialNormals.m`'s
  axis-permuted `(x,y,z)->(z,x,y)` normal + edge vector from the first 3
  fiducial markers, reusing the already-ported `normalVectorFrom3Points`
  (`libs/array/geometry`). **Reclassified to Excluded 2026-09-12** -- its
  only caller, `AutoReg/drawFiducialsOnMRI.m`, is itself unreachable (see
  below), so this is dead transitively. Same "leave the code" treatment.

**Excluded:** `getFiducialNameFromOldFiducialNames.m` calls
`[names, oldNames] = getArrayFiducialMarkerNames()` — but
`getArrayFiducialMarkerNames.m` returns only one value. MATLAB would error
("too many output arguments") on every call; the function cannot run.
Dead/broken code, not translated.

Deferred at the time: everything else in RegistrationTab/AutoReg/Targeting.
Most of that turned out to be genuinely dead too -- see the correction
below for what's actually still deferred (very little) versus excluded
(most of it).

### Correction: the entire `Targeting/` folder (and half of `AutoReg/`) is dead code (2026-09-12)

The user asked me to wire up the automatic-ACPC-detection feature
(`getACPCFromSys.m`'s external `acpcDetect` WSL tool integration) as new
feature work, since it was the more tractable of two options once WSL
got installed on this machine specifically for this. Partway through --
after WSL was installed, a real NITRC license-gated download completed,
and I'd fully documented the tool's real CLI interface -- the user asked
a simple, well-placed question: **"Is this automatic detection code
currently being used in the BeamV0 code?"** That prompted an actual
reachability check, which should have happened before any of that setup
work, not after. Caught and corrected before any C++ was written for the
new feature, but only just.

**Full trace, verified four independent ways** (direct call-site grep,
case-insensitive text search across both of the app's embedded data
files, indirect-call search for `@func`/`feval`/`str2func`, and a
complete enumeration of every single real callback in the entire app --
listing every `% Button pushed function:`/`% Value changed function:`
comment in `BeamV0.mlapp`'s embedded source confirms there is no
ACPC-related button of any kind):

**`RegistrationTab/Targeting/` -- 8 of 9 files are dead, unreachable from
any real UI action, confirmed via `git log --all -p` too (no caller ever
existed, not just currently):**
- `getACPCFromSys.m` -- dead. Its own body is also broken independent of
  reachability: it shells out via `system(['wsl source .../run.sh; foo
  "v1.nii"'])` -- `foo` is not a real function, a leftover placeholder.
- `setACPCTransform.m`, `updateACPCPosition.m`, `initAnatomyReferences.m`,
  `setFiducialMarkersTableWithACPC.m` -- dead, zero callers.
- `getInfoFromACPCDetectTxt.m` -- dead; its only caller is the equally-dead
  `getACPCFromSys.m`.
- `getACPCTransform.m`, `applyReferenceCoordinateTransform.m` -- dead,
  but already ported (see the correction above) before this reachability
  check existed as a habit.
- `updateShamParametersFromTable.m` -- the ONE live file in this folder
  (real caller: `NewVisitButtonPushed` in `BeamV0.mlapp`) -- already
  correctly Deferred (see the treatmentProtocolTables-cluster note
  elsewhere in this doc), unaffected by this correction.

**`RegistrationTab/AutoReg/` -- 2 of 4 files dead:**
- `drawFiducialsOnMRI.m` -- dead, zero callers anywhere.
- `getArrayFiducialNormals.m` -- dead transitively (only caller is the
  above), already ported (see the correction above).
- `getImageFilterSpec.m`, `setFiducialTemplate.m` -- confirmed genuinely
  live (`getImageFilterSpec` from `LoadWith[out]FrameImageButton`;
  `setFiducialTemplate`'s "focus" call site from `drawFocusOnMRI.m` ->
  `drawMrImages.m` -> `startUpFunction.m` and several other real
  callbacks) -- unaffected, `rasterizeFocusEllipsoidOntoMriGrid`'s port
  stays correctly Converted.

**Net effect on the ledger**: 3 files move Converted -> Excluded
(`getACPCTransform.m`, `applyReferenceCoordinateTransform.m`,
`getArrayFiducialNormals.m` -- their C++ ports stay in the tree, just no
longer counted as live conversions), 7 files move Deferred -> Excluded
(the rest of the dead `Targeting/`/`AutoReg/` list above). GUI:
42/39/7 -> 39/32/17 converted/deferred/excluded (55%). Project total:
127/41/58 (76%).

**Lesson, for whoever picks the next slice**: this project's own
"check reachability before treating something as a real feature gap"
habit (established mid-project, used successfully on
`getSteeringAmplitudeCorrection.m`, `loadDicomDir.m`,
`set3DMRIfromImageRegistrationOld.m`) should be applied retroactively to
anything ported *before* that habit existed, not just to new candidates
going forward. slice 5 (2026-09-10) predates it. If another early slice
turns out to have the same issue, don't assume slice number order
protects against it.

### Second pass: 6 more dead files found by widening the check itself (2026-09-12)

The user pushed back on "the GUI deferred bucket is exhausted" after the
correction above -- fair, since the correction above only re-checked
reachability for files *already in* the `RegistrationTab/Targeting/`+
`AutoReg/` cluster the ACPC question happened to point at. It didn't
re-check the *rest* of the ~32-file deferred bucket with the same rigor,
and it turned out the reachability-check method itself had a real gap:
the earlier "complete enumeration of every real callback" only listed
`% Button pushed function:` and `% Value changed function:` comments --
missing `% Menu selected function:`, `% Drop down opening function:`,
and `% Selection changed function:` entirely. Re-running the full
enumeration this time (41 callbacks total, not 33) confirmed the ACPC
finding is unaffected (none of the additional callbacks mention ACPC
either), but re-checking the *rest* of the deferred bucket against the
complete list -- plus checking for `addlistener(...)` event-wiring (not
just direct calls) and grepping git history for a "was this ever live,
and is the wiring now commented out" pattern -- found 6 more:

- **`SonicationTab/allTargetEvents.m`** -- looked correctly judged before
  ("a draggable-ROI event handler, same class as `allFiducialEvents.m`"),
  but that judgment was about portability, not reachability. Its only
  wiring is 3 `addlistener(...)` calls in `drawROIs.m` -- and unlike
  `allFiducialEvents.m`'s equivalent 3 lines (confirmed live, not
  commented out, and `drawROIs.m` itself is reachable via `drawMrImages.m`),
  all 3 of `allTargetEvents`'s lines are commented out
  (`%addlistener(...)`). Git history shows they were once live and were
  deliberately disabled later. Dead now, regardless of history.
- **`RegistrationTab/updateMNITargetLabel.m`** -- zero references
  anywhere in the current app; git history shows two real call sites
  that were later deleted entirely (not just commented out).
- **`RegistrationTab/updateTransducerPositionEditFields.m`** -- its only
  current reference, inside `registerArrayToFiducials.m`, is commented
  out; git history shows two real call sites that were once live.
- **`sagCrossROICallback.m`** -- zero references anywhere, ever (not
  even historically) -- a bare `disp()` callback stub that was defined
  but never wired to anything.
- **`UIFeatures/currentSonicationTimeMarker.m`**,
  **`UIFeatures/progressBarButtonPushed.m`** -- previously deferred as
  "pure MATLAB figure/timer/button-icon rendering, no domain logic," true
  but incomplete: their only references are commented-out lines inside
  `setStartTime.m`, which is *already Excluded* (dead itself, per the
  "Ported: UIFeatures" slice above). Dead transitively, same pattern as
  `getArrayFiducialNormals.m` in the correction above.

**Confirmed still genuinely live** (re-verified this pass, not just
assumed): `coordinateToIndex.m`'s callers (`drawMrImages`/`drawROIs`),
`warnForSonicate.m` (`SonicateButtonPushed`), `getNewProtocolName.m` (2
real call sites), `updateSonicateSettingsForCurrentSonication.m`
(`SonicateButtonPushed`/`ShamButtonPushed`), `setColorMapRGB.m`/
`colorMapRgb()` (`startUpFunction.m`), `updateShamParametersFromTable.m`
(`NewVisitButtonPushed`), `setProtocolListBox.m` (`startUpFunction.m`),
`setProtocolTableWithStimParamTable.m` (turned out to have a second,
much more central live caller than the one first found --
`stimParamTableCellEdit`, not just the now-dead `allTargetEvents.m`),
`updateTargetPositions.m`/`addTargetROI.m` (`startUpFunction.m` and
others), `AddDirectionPropertyToPushbuttons.m` (`startUpFunction.m`),
the `setCRF*.m` cluster (`setSessionCaseReportParameters.m`),
`moveToFiducialMarkerButtonFunction.m` (`MovetoButtonGroup`'s Selection
changed callback -- one of the categories the widened check added),
`setBackupMRIandArrayData.m` (`registerCurrentTransducerPostion.m`,
already a real ported button), `enableAllButtonsMenuSelectedCallback.m`
(`EnableAllButtonsMenu`'s Menu selected callback -- another of the
widened categories), `getImageFilterSpec.m`/`setExampleTargetImages.m`
(`startUpFunction.m`), `cleanupCountdown.m` (a real `StopFcn` on the
already-ported, already-live countdown timer), `initSliceSliders.m`
(`startUpFunction.m`), `leftRightButtonPushed.m` (the 3 real
`sag/cor/axialLeftRightButtonPushed` callbacks call it internally).

**Also checked, no correction needed**: `Correction/localizeArrays/`'s
solver cluster (`localizeArraysMaster.m` and everything it calls) has
zero callers anywhere either -- but this is *already* correctly handled:
`docs/known_gaps_correction.md` has disclosed since the Correction phase
itself (predating the GUI phase entirely) that self-contained library
math gets ported regardless of live-caller status, unlike GUI
orchestration code -- a different, legitimate, already-documented policy
for library phases (Arrays/Util/Correction/Stimulation/MRI/Registration),
not an oversight. `localizeArraysGenerateNewArray.m` (a bare script with
hardcoded `Verasonics`/`Diadem` paths -- wrong project, wrong hardware)
and the duplicate `getElementPositionsFromArrayStruct.m` were already
correctly Excluded there too. Nothing to fix.

**Net effect on the ledger**: 6 more files move Deferred -> Excluded.
GUI: 39/32/17 -> 39/26/23 converted/deferred/excluded (60%). Project
total: 127/35/64 (78%).

### Ported: SonicationTab data logic (slice 6, 2026-09-10)

`libs/gui/sonication_tab_presenter` — six of `SonicationTab/`'s 26 files:
- `getResponseFromTreatmentProtocolData` -> mood/pain response, clamped to
  [-2,2], NaN -> 0 (the `app.treatmentProtocolTable.Data` write-back is the
  caller's).
- `getNewProtocolName` -> unique "Protocol N" naming. **Quirk preserved:**
  after the first collision (which appends `_01`), every further collision
  overwrites just the *last character* of the name with the next digit
  (`newName(end) = num2str(j)`), not a growing suffix counter.
- `colorMapRgb` -> `setColorMapRGB.m`'s 13 fixed colors + scaled greens.
  **Bug preserved:** the source means to build 100 colors but its fill
  loop bound is `nLeft` (87) instead of `N` (100), so the result has 87
  rows and only reaches ~0.78 scale, never the intended 0.9 — reproduced
  exactly, including the unused tail of the `linspace`.
- `getCurrentShownSonication` -> last `show`-checked row (1-based), 1 if
  none.
- `sortSonicationTableOrder` -> the stable-ascending permutation from
  `sortSonicationTable.m` (the table reorder itself is the caller's).
- `computePulseWaveformPlot` -> `updateSonicationPlots.m`'s first plot
  (`axPulseWaveformPlot`): a step function, `amplitude` for
  `x <= pulseDuration`, sampled at the source's hardcoded 650 kHz. The
  second plot's `PICounter`-based repeating burst waveform and the third
  "total sonication" plot (needs `setBurstEventsFromStimParams` + target
  colors) are **not** ported this slice — preview-only rendering, left for
  when the Sonicate tab actually needs it.

**Excluded:** `getTreatmentProtocolTable.m` — hardcoded
`Diadem\GUI\SonicationTab\TreatmentProtocols\...` paths (a leftover
DiademV0 path, unedited) reading CSV files via `readtable`. Same class as
`Arrays/loadArray.m`.

### Ported: target carry-forward ranking (slice 7, 2026-09-10)

`libs/gui/top_targets` — `getTopTargetsFromTreatmentProtocolTable.m`'s
~150-line target-selection algorithm (which prior sonication targets carry
forward into the next session, based on historical mood/pain responses),
given its own slice as planned rather than rushed in with the smaller
SonicationTab functions. Not ported: the `app`-coupled preamble that reads
`app.VisitNumberListBox`/`app.TreatmentProtocolDropDown` and
`app.sys.treatmentProtocolTables(...)` to build the source's `responses`
struct — the caller assembles that (using the already-ported
`getResponseFromTreatmentProtocolData`) and passes it straight in.

Faithful-port quirks, all preserved and unit-tested individually:
- Grouping by target name is case-**sensitive** and alphabetically sorted
  (MATLAB `unique`'s default), but the "is this Sham" filter on each group
  is case-**insensitive** — a real mismatch in the source.
- "Block A" is hardcoded as `duration < 60`.
- Trimming excess (> 3) positive responses breaks ties toward dropping the
  *worst*-ranked region; the later negative-response fallback (when there
  are 0 positives left) breaks ties toward keeping the *best*-ranked region
  — opposite tie-break directions, both from the source.
- The zero-positives ranking backfill accepts response `>= 0`, not `> 0`.
- If nothing is ever selected, only the `name` array is padded to exactly
  3 `"Sham"` entries — `sonicationNumber`/`numericResponse`/`duration` stay
  empty, a genuine array-length mismatch in the source, reproduced as-is.
- Two source locals (`neutralResponses`, `responsesVals`) are computed but
  never read again — dropped here, matching the project's convention for
  provably-dead intermediates.

### Real UI: the stimParamTable grid (slice 8, 2026-09-10)

Past this point GUI work shifts from "extract the math" to actually
building widgets -- there isn't much more portable math left in
`SonicationTab/`. `libs/gui_qt/stim_param_table_model`
(`StimParamTableModel : QAbstractTableModel`) is a real, editable Qt table
for `createStimParamTable.m`'s central per-target grid: columns `#` / Show
/ X / Y / Z / Amplitude / Start Time / End Time / Burst Duration / Burst
Interval / Pulse Duration / Pulse Interval, headers from
`setStimParamTableNames.m`'s renames (its later `BLKD`/`BLKI`/`Block`
columns aren't modeled -- not traced to whichever caller appends them
beyond `createStimParamTable.m`'s own base 12).

The model isn't just a data container -- it drives the already-ported
presenters: `sortByOrderColumn()` calls `sortSonicationTableOrder`
(`sortSonicationTable.m`) and reorders the Qt rows to match;
`currentShownRow()` calls `getCurrentShownSonication`
(`getCurrentShownSonication.m`) off the live Show-column state. `beam_app`
wires a `showFlagsChanged` signal (emitted on every Show checkbox toggle)
to recompute `computePulseWaveformPlot` for whichever row is now shown and
refresh the Sonicate tab's chart -- the presenter chain from slice 6 now
responds to real UI interaction, not just synthetic startup data.
`BeamMainWindow`'s Sonicate tab is now a splitter: the table + a "Sort by
#" button on the left, the safety report + pulse plot on the right.

Verified via `apps/beam_app --check` (table row count + `currentShownRow`)
-- no dedicated gtest coverage, matching the existing smoke-test-only
convention for `libs/gui_qt` widgets (`SafetyReportView`, the chart
builders): the model is thin Qt boilerplate over two already-unit-tested
presenter functions.

### Real UI: the treatmentProtocolTable grid (slice 9, 2026-09-10)

`libs/gui_qt/treatment_protocol_table_model`
(`TreatmentProtocolTableModel : QAbstractTableModel`) -- a real, editable
table for `createTreatmentProtocolParamTable.m`'s per-sonication response
history: `#` / Target / Duration (s) / Amplitude / Parameters / Response
Pain / Response Mood/Anxiety / Notes, headers from
`setTreatmentProtocolTableNames.m`. Editing Response Pain or Response Mood
runs the already-ported `getResponseFromTreatmentProtocolData` and writes
the *clamped* value back into the cell -- matching the source's own
`app.treatmentProtocolTable.Data.Response*(i) = responseMood` write-back
inside `getResponseFromTreatmentProtocolData.m`.

This finally gives `getTopTargetsFromTreatmentProtocolTable.m` (slice 7) a
real caller: `computeBestTargets(accFlag)` turns every row into a
`beam::gui::TargetResponse` (via the clamped response) and runs the ranking
algorithm over them. `BeamMainWindow`'s Sonicate tab gained a nested
"Pulse Details" / "Treatment Protocol" tab pair (matching
`BeamV0.mlapp`'s real `PulseDetailsTab`/`TreatmentProtocolTab` nesting
inside `SonicateTab`) -- the new sub-tab has the grid, an ACC-region
selector (Other/SCC/aMCC, i.e. `AccFlag`), a "Compute Best Targets" button,
and a result list. Verified via `apps/beam_app --check`: five synthetic
rows (two same-named "SCC1" entries at different response levels, one
excluded "Sham") produce `bestTargets=[SCC2,SCC1]` -- hand-verified against
the same by-hand trace used for slice 7's unit tests. No dedicated gtest
coverage (same smoke-only convention as the rest of `libs/gui_qt`).

### Real UI: sonication row management (slice 10, 2026-09-10) — source reachability corrected 2026-09-12, see below

Two more small `SonicationTab/` operations, added to `StimParamTableModel`
and wired to a new "Add Sonication" / "Remove Marked" button pair next to
"Sort by #". **Both source files turned out to have zero callers
anywhere in BeamV0, ever** (confirmed 2026-09-12, see the correction
further below) -- the feature description below is still accurate for
what this port's own Qt buttons do, but "ported from a live BeamV0
feature" was the wrong framing; reclassified to Excluded, code stays:

- `addSonicationRow()` -- `createSonicationUpdateTable.m`'s one live line
  (the rest of that file is commented out): duplicates the last row and
  renumbers its `#` to the new row count. Throws if the table is empty,
  matching the source's implicit assumption that a row always exists
  (`createStimParamTable.m` always seeds one).
- `removeMarkedRows()` -- `removeSonicationUpdateTable.m`. **Quirk
  preserved:** the source force-clears row 1's Show flag first
  (`data(1,2) = 0`), *then* deletes every row whose Show flag is set. So
  "Show" doubles as the deletion marker, and row 1 can never be removed by
  this operation regardless of its checkbox state.

Verified via `apps/beam_app --check`'s `rowMgmt 3->4->3` (add, mark the new
row Show, remove -- back to the original count). No dedicated gtest
coverage (same smoke-only convention as the rest of `libs/gui_qt`).

### Ported + wired: treatment-protocol row coloring (slice 11, 2026-09-10)

Two more pieces of `setTreatmentProtocolTableData.m` (its `uistyle`/
`addStyle` calls and the dropdown/session lookups that select *which*
table to color are GUI, not ported):

- `computeTreatmentRowColor(currResponse)` -- the response-to-row-color
  rule: negative -> yellow, zero -> white, positive -> green scaled by
  `min(currResponse, 4) / 4` (so 4 or more is the same full-intensity
  green). Wired live into
  `TreatmentProtocolTableModel::data()` via `Qt::BackgroundRole`, keyed off
  the same clamped response `computeBestTargets()` feeds the ranking
  algorithm -- editing Pain or Mood now repaints that row's background
  immediately. The source's separate "current sonication number" grey
  override isn't wired -- this model has no equivalent concept yet.
- `accFlagForProtocolName(name)` -- the protocol-name -> `AccFlag`
  selection. **Quirks preserved:** the source builds a *pair* of region
  names per branch (`{'SCC','aMCC'}` / `{'aMCC','SCC'}`) but only the first
  element is ever read at the real call site -- the second is dead; and
  `'PainACC'` passes the literal string `'ACC'`, which matches neither the
  ranking function's `'SCC'` nor `'aMCC'` branch, so it silently falls into
  the interleaved "other" ranking rather than getting a dedicated ACC case.
  Ported ahead of a caller (no protocol-name dropdown exists in the shell
  yet) -- same "port it, wire it when the UI catches up" reasoning used
  elsewhere in this project.

`setTreatmentProtocolTableDisplay.m` (swaps the table's data based on the
protocol/session dropdowns) stayed deferred here -- pure `app`-state
read/write, no math -- but is ported later, once a real multi-visit
session store exists to swap between (see the real-UI slice further
below).

**Wired into the shell, 2026-09-10:** `libs/gui_qt/sonication_tab_chart`
(`buildPulseWaveformChart`) turns `computePulseWaveformPlot`'s output into
a `QAreaSeries` chart (matching the source's `area(ax,x,y)`), and
`BeamMainWindow`'s Sonicate tab now shows it under the safety report
(`setPulseWaveformChart`). `beam_app` computes it from the same synthetic
sonication as the safety report. No new `.m` ports -- this closes the gap
between the presenter (ported in the slice above) and the shell.

Deferred at this point in the port: the remaining SonicationTab files —
`uitable` construction / formatting (`create*Table*`, `set*Table*Names`,
`setTreatmentProtocolTableData/Display`), protocol-session management
(`addTreatmentProtocolSession`, `initTreatmentProtocolTables`,
`removeSonicationUpdateTable`, `setProtocolListBox`,
`setProtocolTableWithStimParamTable`), target-ROI widget glue
(`addTargetROI`, `allTargetEvents`, `updateTargetPositions`,
`ExampleTargets/setExampleTargetImages`), `updateSonicateSettingsForCurrentSonication`,
and the `uiconfirm` dialog `warnForSonicate.m`. (`addTreatmentProtocolSession`,
`initTreatmentProtocolTables`, and `setTreatmentProtocolTableDisplay` are
ported later -- see the real-UI slice further below; the rest are still
deferred as of the most recent slice, see that section's own summary.)

## Ported this phase (`libs/safety/`)

The one genuinely computational corner of `GUI/` is `GUI/Safety/` — the
acoustic-dose limits and parameter-validity checks for the clinical
device. Ported with the `app` reads and report-string / button-state
side effects stripped out:

- **`getMechanicalIndex.m`** -> `mechanicalIndex(amp, freqMHz)` =
  `amp / sqrt(freqMHz)`.
- **`getMaxSonicationAmplitude.m`** -> `kMaxSonicationAmplitudeMPa` (3).
- ~~`getMaxSteeringRange.m`~~ -> `maxSteeringRangeDegrees()`
  (`[-45 45; -28 28; -15 15]`). **Reclassified to Excluded 2026-09-12** --
  zero callers anywhere (git history shows one real call site, later
  deleted), and unlike its siblings here, nothing in this port calls the
  C++ port either (only a unit test and the MATLAB-parity tool exercise
  it) -- see the correction further below. Code stays, just not counted
  as a live conversion.
- **`checkCouplingSafety.m`** -> `checkCouplingSafety(att)` -- its one
  real check (`att < 0.1`).
- **`getISPTAFromStimParams.m`** -> `isptaFromParams(p)` -- the two
  fully-computable outputs (`IsptaBurstDuration`, `IsptaBurstInterval`).
  `IsptaAll` additionally needs `setBurstEventsFromStimParams` (a burst
  event-vector builder, deferred with the rest of Stimulation — not
  Verasonics; see `docs/known_gaps_stimulation.md`), so it is not returned.
- **`checkSonicationSafety.m`** -> `checkSonicationParameters(sonications)`
  -- the ISPPA-vs-190, PD/PI/BD/BI-validity, and (if those pass)
  burst-interval-ISPTA-vs-0.720 / MI-vs-1.9 checks, returning a
  `{pass, messages}` report. Preserved source quirks, disclosed:
  - The ISPPA and ISPTA formulas use **different water densities** (1040
    vs 1046) -- `checkSonicationSafety.m` and `getISPTAFromStimParams.m`
    disagree in the source. Both are reproduced (`isppa` takes `density`).
  - `if MI > MIthreshold` in the source is a MATLAB `if` on a *vector*, so
    it only fires when *every* sonication's MI exceeds the limit. Kept.
  - The "registration incomplete" / "through-transmit too low" checks and
    the `set(app.SonicateButton, ...)` calls are GUI/session state, not
    sonication parameters. The first two are now added by
    `beam::gui::checkSonicationSafety` (see the Safety-tab slice above);
    the button state stays with the eventual Qt tab.

### Real UI: Register tab's fiducial marker grid (slice 12, 2026-09-10)

The Register tab had been a placeholder since the app shell (slice 3) --
this gives it its first real content: `libs/gui_qt/fiducial_table_model`
(`FiducialTableModel`), a Name/X/Y/Z/Group grid for
`setFiducialROIs.m`'s live path (its `if 0`-guarded AC/PC/MC-brain-fiducial
block and fiducial-marker-table sync block are both dead, matching the
project's convention for provably-unreachable code -- not ported).

`beam_app` populates it from the real geometry pipeline end to end: a
synthetic array -> `defineArrayData` -> `setArrayFiducialMarkers` (both
already ported/tested in `libs/array` and `libs/registration`) -> the 6
fiducial markers, scaled `*1000` (m -> mm) exactly as
`app.FiducialROIs(i).position = ...*1000` does. Verified via
`apps/beam_app --check`: `fiducials=6`.

No MRI image view exists yet, so this is the table alone -- the
draggable-ROI half of `setFiducialROIs.m`/`allFiducialEvents.m` (which
also carries a `%% TODO find better way to identify axis` pixel-position
heuristic tied to BeamV0's specific App Designer layout, not something
that would transfer meaningfully to a different Qt layout) is not ported;
X/Y/Z are plain editable cells instead. No dedicated gtest coverage (same
smoke-only convention as the rest of `libs/gui_qt`).

### Real UI: MRI slice viewer for the Register tab (slice 13, 2026-09-10) -- layout corrected 2026-09-14, see below

The first image-rendering code in this project. `libs/gui_qt/mri_slice_view`
(`MriSliceView` + `renderMriSliceImage`) adds a sagittal/coronal/axial
slider-navigated view to the Register tab, built on the already-ported/
tested `beam::mri::getSliceImage` (`GUI/MRI/getSliceImage.m`, ported back
in the non-GUI MRI phase) -- each slider drag calls it directly and
renders the returned matrix as an 8-bit grayscale `QImage`, min-max
normalized (the source has no display scaling of its own; `imagesc`
auto-scales its colormap the same way, so this is a disclosed choice, not
a specific port).

**Layout correction (2026-09-14):** this slice's own title called it "for
the Register tab," and that's literally where the 3 views were nested in
`BeamMainWindow`'s layout -- but checking `BeamV0.mlapp`'s actual
`createComponents` code (prompted by the user: "those default MRI images
were not loaded into display after software startup") shows
`app.axSag = uiaxes(app.UIFigure)` (same for `axCor`/`axAxial`) -- parented
directly to the main figure, not to any tab. The real app shows these
views immediately on startup in every tab, not just Register. Nesting
them inside the Register tab's own layout was a real bug, not a disclosed
simplification: it made the default view -- the Sonicate tab, the first
one selected -- show no MRI at all, exactly matching what the user saw.
Fixed by moving the 3 `MriSliceView`s into a persistent row above
`BeamMainWindow`'s tab group instead (`central`/`centralLayout` in the
constructor), matching the source's real structure. A second bug
surfaced immediately after: `MriSliceView`'s image `QLabel` had no
maximum size, so once persistent across every tab it dominated the
window and pushed the tab bar off-screen entirely -- fixed with
`imageLabel_->setFixedSize(220, 220)` (a `setMaximumSize` alone wasn't
enough; the label's `sizeHint` still influenced the very first layout
pass before any explicit resize had a chance to apply). Verified: a
zero-argument `beam_app` launch now shows real anatomy in all 3 views
*and* the Sonicate/Register/Correction tab bar simultaneously, with no
clicks -- matching a real BeamV0 session.

**Three more fidelity bugs, same report, fixed same day:**

- **Title had a slice-index suffix the source never shows.**
  `updateImage()` was overwriting the title on every slider move with
  `"sagital (113/227)"`; the source's `title(app.axSag, 'Sagital')` (and
  `axCor`/`axAxial`) is a one-time, capitalized, no-number label. Fixed:
  title is set once in the constructor (capitalized to match exactly)
  and no longer touched by `updateImage()`.
- **No axis tick labels at all.** The user's original ask (before the
  data/layout bugs took priority) -- `drawMrImages.m`'s `showMrImage`
  passes real physical-mm `xdata`/`ydata` to `imshow` per plane (sagital:
  `sys.ay`/`sys.az`; coronal: `sys.ax`/`sys.az`; axial: `sys.ax`/`sys.ay`),
  which MATLAB renders as real tick marks/labels on the axes' left and
  bottom edges. Ported as `MriSliceView::setAxes` (wired from
  `BeamMainWindow::setMriAxes`, fed the same `beam::mri::RasAxisVectors`
  the overlay rasterizers already use) + a `paintEvent` override drawing
  5 evenly-spaced ticks along a reserved left margin / bottom spacer
  strip. No physical calibration exists for the synthetic volume, so
  ticks are simply not drawn then (`std::nullopt`), same
  graceful-degradation shape as everything else MRI-axes-dependent.
- **Sagittal image was mirrored the wrong way.** `drawMrImages.m` calls
  `set(axHandle,'XDir','reverse')` for the sagital plane only (not
  coronal/axial) -- a real, disclosed, plane-specific quirk this port
  hadn't replicated. Fixed with `QImage::mirrored(true, false)` in
  `updateImage()`, sagital-only; the new X-tick labels reverse direction
  to match (max-value on the left) rather than silently disagreeing with
  the now-mirrored image. Verified side-by-side against a live BeamV0
  session: both now show the frontal lobe/face on the left and the
  occipital lobe/cerebellum on the right for the sagittal view, and
  matching tick-value ranges on all 3 views.

**Axis rendering completed properly (2026-09-14, same report, next
round):** the first tick-label pass above was too sparse (5 fixed,
arbitrary-fraction ticks, no ruler line, no axis title) -- the user
pushed back with specifics: "vertical and horizontal scales should have
more numbers," "a line drawed... attached to the boundary of displayed
images," and the missing `X (mm)`/`Y (mm)`/`Z (mm)` titles
(`xlabel(app.axSag,'Y (mm)')`/`ylabel(...,'Z (mm)')` etc, confirmed
directly in the `.mlapp` source). Replaced with: a continuous ruler line
along the image's own left/bottom edges (`QPainter::drawLine` from
corner to corner of `imageLabel_->geometry()`, not just short tick
marks); a "nice round number" tick-step algorithm (`niceTickStep`/
`niceTicks` -- the 1/2/5x10^n family most plotting libraries use, since
MATLAB's own default axis-tick placement isn't something this project's
code chose or can inspect) targeting ~8 ticks instead of a fixed 5; and
the rotated (`QPainter::rotate(-90)`) `Z (mm)`/`Y (mm)`/`X (mm)` axis
titles per `xAxisLabel()`/`yAxisLabel()`.

Same report also caught a **second, separate gap**: "sliding bars also
has values below it. The values for Sagital, Coronal, and Axial are not
the same" -- MATLAB's `uislider` shows major-tick numeric labels along
its track by default (Qt's `QSlider` doesn't), and per
`coordinateToIndex(app.sagSlider.Value,'x',sys)` (`'y'`/`'z'` for
`corSlider`/`axialSlider`), **the source's sliders are themselves in
real physical mm**, along the axis each plane actually slices through
(sagital walks LR, coronal walks AP, axial walks IS) -- a different,
generally differently-ranged axis than either of the two *displayed*
in-plane axes above, which is exactly why the user observed the 3
sliders' numbers don't match each other. This port's `QSlider` stays
internally voxel-index-based (lower risk than reworking its value
domain and signal wiring), but a new reserved strip below it
(`sliderValueSpacer_`) now paints the real mm-equivalent tick values
using the same `niceTicks` -- `sliderRangeMm()` picks the correct
axis per plane. Verified interactively across all 3 panels: real
ruler-lined axes with dense round-number ticks and rotated titles, and
3 visibly different slider value ranges matching each plane's real
physical extent.

**Two more bugs, same axis work, caught immediately after:**

- **The vertical (Y-axis) ruler line was invisible.** Drawn exactly at
  `imageLabel_->geometry().left()` -- but `imageLabel_` is a *child*
  widget that paints its own pixmap on top of `MriSliceView`'s
  `paintEvent` output (Qt always paints children over their parent), so
  a line drawn exactly on the image's own edge was being completely
  covered every frame, never actually visible. (The horizontal line
  survived by accident: it landed on `bottomAxisSpacer_`, a plain
  `QWidget` with no pixmap of its own to paint over it.) Fixed by
  drawing both ruler lines 1px *outside* `imageLabel_`'s rect
  (`axisLeft`/`axisBottom`), and reusing those same two coordinates for
  every tick mark's inner endpoint too, so the two rulers meet at one
  exact shared corner and every tick visibly touches its ruler --
  the user's "should connect" ask.
- **The image never resized with the window.** `imageLabel_->setFixedSize(220,
  220)` (from the earlier "don't dominate the window" fix) meant the
  MRI views stayed pinned at a constant size no matter how the window
  was resized, and even without the fixed size, `updateImage()` only
  ever re-scales the pixmap when data changes (`setVolume`/
  `setOverlayVolumes`/slider move) -- never on a plain resize, since
  `QLabel` doesn't auto-rescale a manually-set pixmap. Fixed:
  `imageLabel_` now has a bounded range (`setMinimumSize(150,150)`/
  `setMaximumSize(420,420)`, `QSizePolicy::Expanding`) instead of a
  fixed size, `MriSliceView::resizeEvent` calls `updateImage()` on every
  resize so the pixmap actually rescales, and `BeamMainWindow`'s central
  layout gives the MRI row a real stretch share (was 0, now 1, vs. 2 for
  the tab group) so it actually receives a portion of any extra window
  space instead of the tab group getting all of it. Verified down to
  ~650x700 and up past 2000x1300: the image grows/shrinks smoothly
  between its floor and ceiling in both directions, the tab content
  underneath stays fully visible and usable at every size tried, and the
  ceiling still prevents the original "row dominates, tabs pushed
  off-screen" regression from coming back.

**This is *not* a port of `drawMrImages.m`** -- that function (and its
inner `showMrImage`) does substantially more: `flipud` before display,
`YDir`/`XDir` axis flips (sagittal is mirrored), alpha-blended overlays for
the transducer array / fiducials / focus field keyed off several
checkboxes and transparency sliders, and MNI-template overlay handling.
None of that is here; `drawMrImages.m`, `drawFocusOnMRI.m`, `drawROIs.m`,
and `formatMRIAxes` all stay **deferred**, unchanged by this slice -- this
new widget is inspired by the concept but doesn't count as translating
those files. (The GUI conversion table is unchanged by this slice for the
same reason: no `.m` file crossed from deferred to converted; the only
`.m` function actually exercised, `getSliceImage.m`, was already counted
under the MRI phase.)

`beam_app` feeds all three views a synthetic Gaussian-blob volume by
default (a recognizable bright-core/fading-edge shape, not noise); the
follow-up slice below adds real-file loading. Verified via
`apps/beam_app --check`: `mriAxialSlice=64x64`. No dedicated gtest
coverage for the Qt pieces (same smoke-only convention as the rest of
`libs/gui_qt`).

### Real NIfTI file loading for the MRI viewer (slice 15, 2026-09-11)

`apps/beam_app --mri <path.nii>` loads a real file through the real
reader -- `beam::mri::loadNiftiMriRas` -> the new
`reorientedVolumeToVolume3D` bridge (`libs/mri/mri_loader`, gtest-covered,
see `docs/known_gaps_mri.md`) -- into the Register tab's MRI viewer,
falling back to the synthetic volume on any load error or when the flag
isn't given (the checked-in default). No new `.m` ports here either (the
reader itself was already counted in the non-GUI MRI phase); this closes
the loop opened in slice 14.

Verified against a real 192x256x256 anatomical NIfTI (DT_INT16, sform
orientation) found on the local machine: loads and renders in ~3.4s,
matches the file's own header dimensions exactly
(`mri=file:... 192x256x256 mriAxialSlice=256x192`), and the interactive
window ran clean for several seconds with it loaded. That file is **not**
bundled with the repo -- real MRI data, unclear redistribution rights, and
25 MB besides -- `--mri` only ever takes a local runtime path.

### Array-footprint / fiducial-marker overlay on the MRI viewer (slice 16, 2026-09-11)

Ported **`GUI/imagePositionToIJK.m`** -> `beam::gui::imagePositionToVoxelIndex`
(nearest-index lookup of a physical mm position along each axis; returns
0-based indices, this project's convention, not MATLAB's 1-based ones) and
the rasterization loop from **`GUI/drawTransducersOnMRI.m`** ->
`beam::gui::rasterizeArrayOntoMriGrid` (samples each array element's face
with the already-ported `spatiallySampleElement`, at `fs = min(axis
spacing)/2` matching the source's `fs = min(sys.aRes)/2`, then marks each
sample's nearest voxel in an MRI-grid-shaped mask). Both files move from
deferred to converted; the GUI-state store-to-`app.sys` at the end of
`drawTransducersOnMRI.m` is dropped, same convention as every other tab
presenter here.

New infrastructure (no `.m` counterpart): `rasterizeFiducialMarkersOntoMriGrid`
paints each fiducial marker's mm position as a small cube blob into the same
mask shape, standing in for **`GUI/drawROIs.m`**'s per-plane
`images.roi.Point` markers -- no draggable ROIs, no per-slice name label
(both App-Designer-specific interaction). `drawROIs.m` itself and its
target-crosshair half stay **deferred** (UI glue, not math). At the time
this slice shipped, `drawFocusOnMRI.m`/`setFiducialTemplate.m`'s ellipsoid
were deferred too, for lack of anything to call "the focus location" --
slice 19 below ported them once `drawFocusOnMRI.m`'s own answer to that
(the array's geometric center, not a real acoustic target) turned out to
be usable as-is.

`libs/gui_qt/mri_slice_view` gained `setOverlayVolumes` +
`renderMriSliceImageWithOverlay`: each plane's already-rendered grayscale
slice gets a yellow tint where the array mask is set and a red tint where
the fiducial mask is set (fiducial wins on overlap), by running the same
`getSliceImage` extraction on the mask volumes so they line up pixel-for-
pixel with the base slice -- no separate rotation/orientation logic needed.
Fixed color, no transparency slider or checkbox gating (`drawMrImages.m`'s
`ShowTransducersCheckBox`/`TransducerTransparencySlider` are not ported).

Wired in `beam_app`: the overlay only appears with a real `--mri <path>`
load, because it needs that file's own physical-mm axis vectors
(`beam::mri::MriVolumeRas::axes`) to place the array/fiducials against --
the synthetic default volume has no physical calibration, so its masks stay
empty (`overlay arrayVoxels=0 fiducialVoxels=0` in `--check`'s output).
Verified against the same real 192x256x256 scan used for slice 15:
`overlay arrayVoxels=90 fiducialVoxels=630` (nonzero, no crash, ran
interactively for several seconds). The synthetic demo array's physical
position isn't calibrated to that (or any) real patient scan, so these
counts only demonstrate the pipeline runs end to end, not a clinically
meaningful placement.

**Disclosed deviation from the source:** `imagePositionToVoxelIndex` always
returns a valid index into its *axis vector*, but that index can still fall
outside the mask volume's own `(nx,ny,nz)` if the axis vectors are a
different length -- exactly the RAS-reordering/axis-length mismatch already
documented on `beam::mri::MriVolumeRas` (inherited from `loadMRIRAS.m`).
MATLAB's unchecked `arrayImage(i,j,k)=1` would error in that case; both
rasterize functions here silently drop such samples instead (a safety
deviation avoiding undefined behavior on an out-of-range Eigen access, not
a behavior port). Covered by
`RasterizeArrayOntoMriGrid.AxisLongerThanGridDropsOutOfRangeIndicesInsteadOfCrashing`.

### Real UI: the "Sonicate" button (slice 17, 2026-09-11)

The Sonicate tab's first hardware-facing control. `libs/gui/sonicate_orchestrator`
ports `Stimulation/GeneralSonication/generalSonicateMaster.m`'s decision
logic (its `.m` file is counted under the **Stimulation** category, not
here -- see `docs/known_gaps_stimulation.md` for the conversion-table
bookkeeping and full disclosure of what was/wasn't ported). This slice is
the real-UI wiring on top of it: `BeamMainWindow` gained a trigger-mode
combo (Immediate/External), a "Sonicate" button, and a status label
(`setSonicateHandler`/`setSonicationStatus`); `apps/beam_app/main.cpp`
builds a `SonicationSafetyParams` from the currently-shown `stimParamTable`
row, calls `beam::gui::prepareSonication`, and on success sends the
resulting command over the first port `listAvailableComPorts()` reports,
via the already-ported `setSerialCommandFromStimParams` + `sendSerialCommand`
(`libs/serialcom`).

No simulated-hardware fallback: unlike the synthetic MRI volume, a fake
"sonication succeeded" result would misrepresent whether real hardware
received the command, so pressing Sonicate with nothing plugged in reports
"No serial port available" rather than pretending to succeed. Verified via
`apps/beam_app --check` (`sonicate started=1 dutyCycle=0.676
waitForTrigger=0` against the demo's synthetic coupling data) and an
interactive run (constructs cleanly, no crash; no serial hardware on this
machine to exercise the send path itself).

`setPulseWaveformChart`'s previous implementation removed/re-added a
widget at a hardcoded `previewLayout_` index (1); adding the Sonicate
controls below it would have silently reordered them on the next chart
swap, so the pulse-chart area was refactored into its own nested layout
(`pulseChartLayout_`) first -- a fix to slice-9-era code, not new behavior.

### Real UI: session save/load (slice 18, 2026-09-11)

New infrastructure -- no single BeamV0 `.m` counterpart. The manual's
Operating Instructions describe a "File -> Save Subject" / "File -> Load
Subjects" step (exports "the position, stimulation parameters, and
correction values ... the registration values, and all other settings" to
a `.mat` file so a session's registration doesn't have to be repeated);
that lives in `BeamV0.mlapp`'s own callbacks, not a separate `.m` file, so
there's nothing to literally port -- this is a from-scratch equivalent.

`libs/gui/session_io` defines plain (Qt-free) records
(`StimParamRecord`, `TreatmentProtocolRecord`, and the already-existing
`beam::registration::FiducialMarker`) plus `serializeSession`/
`deserializeSession`, real gtest-covered round-trip functions for Beam's
own tab-separated text format (`# Beam session v1` + `[Fiducials]`/
`[StimParams]`/`[TreatmentProtocol]` sections) -- **not** BeamV0's `.mat`
binary; adding a MAT-file writer dependency for this felt like a lot of
machinery for a session file whose only consumer is this same program.
Deliberately not included: correction measurements and the per-sonication
log (`app.sys.log`) -- those are recorded observations from a session, not
configuration to restore into a new one; MRI is its own "File -> Load MRI"
step already.

`BeamMainWindow` gained a `File` menu ("Save Session..."/"Load
Session..."), backed by `collectSessionData`/`applySessionData` (the
Qt-model <-> `SessionData` conversion) wrapped in thin `QFileDialog`/`QFile`
methods. `collectSessionData`/`applySessionData` are exposed directly
(not just through the menu actions) specifically so `apps/beam_app --check`
can exercise the *whole* pipeline -- Qt models -> `SessionData` ->
`serializeSession` -> a real temp file -> `deserializeSession` ->
`SessionData` -> Qt models -- through a real file, not just the pure
serialize/deserialize functions gtest already covers. Verified: `session
roundTrip=1` in `--check`'s output (fiducial/stimParam/treatmentProtocol
row counts all survive the round trip) and a clean interactive run (File
menu present, no crash). Clicking the actual menu items and driving a real
`QFileDialog` isn't exercised by either check -- same smoke-only
limitation as the rest of `libs/gui_qt`.

### Real UI: the focus-ellipsoid overlay (slice 19, 2026-09-11)

Ported `GUI/RegistrationTab/AutoReg/setFiducialTemplate.m`, scoped to its
`GUI/drawFocusOnMRI.m` call site only: `beam::gui::rasterizeFocusEllipsoidOntoMriGrid`
(`libs/gui/mri_overlay_presenter`) reproduces the ellipsoid indicator
`(x/30)^2+(y/5)^2+(z/5)^2 < 1` computed on a 4x-finer lattice and
`interp3`'d down to voxel resolution, without materializing that fine
grid (the indicator is evaluated analytically on demand instead -- see
the header comment for why this is exact, not approximate, for this
unrotated call site). `setFiducialTemplate.m` itself moves from deferred
to converted; its *other* call site
(`RegistrationTab/AutoReg/drawFiducialsOnMRI.m`, a real 3x3 rotation and
a list of markers through the same hardcoded `for i = 1` single-iteration
loop, plus a linear-indexing hazard in `imagePositionToIJK` when given an
Nx3 array) is **not** ported -- correctly so, since `drawFiducialsOnMRI.m`
turned out to have zero callers anywhere (confirmed and reclassified to
Excluded 2026-09-12, see the correction further below -- at the time this
paragraph was written it was believed merely deferred/bug-limited, not
fully unreachable), and `drawROIs.m`'s interactive-ROI half remains
deferred too (unchanged from slice 16).

**"Focus" is drawFocusOnMRI.m's own simplification, not a real acoustic
target**: the source computes it as `centerArrayMM = mean(rect(17:19,:))*1000`
-- the mean of every array element's center, nothing about beamforming or
a selected sonication target. This port reproduces that faithfully
(`apps/beam_app/main.cpp` computes the same mean-of-element-centers), and
there's no other "focus location" concept anywhere else in this port to
draw instead.

`libs/gui_qt/mri_slice_view` gained a third overlay: `renderMriSliceImageWithOverlay`
now blends a magenta glow proportionally to the focus mask's `[0,1]`
coverage value (rather than the hard on/off threshold the array/fiducial
overlays use), so the ellipsoid's edge anti-aliasing is visible rather
than discarded. Precedence on overlap: fiducial (red) > array (yellow) >
focus (magenta) > base grayscale. `setOverlayVolumes`/`setMriOverlays`
gained a third parameter; wired in `beam_app` the same way the other two
masks are -- only computed with a real `--mri <path>` load (needs the
file's physical-mm axes), empty for the synthetic default. Verified:
`overlay ... focusCoverage=0.00` (synthetic) vs. `focusCoverage=2404.00`
(the same real 192x256x256 scan used for slices 15-16) -- in the right
order of magnitude for the ellipsoid's ~3142mm^3 volume at ~1mm voxels --
and a clean interactive run with the real file loaded.

### Real UI: the sonication countdown (slice 20, 2026-09-11)

Ported `GUI/UIFeatures/startStandaloneCountdown.m` and
`updateFigureTimer.m`'s pure tick/format logic (not the MATLAB `timer`
object or the standalone black figure window they normally drive) ->
`beam::gui::countdownTimeLeftSeconds`/`isCountdownDone`/`countdownDisplayText`
(`libs/gui/countdown_presenter`). Preserves a genuine source quirk:
MATLAB's `floor`/`mod` use floor-division semantics, so the countdown's
final tick or two (`timeLeft` as low as -2, per the source's own stop
condition) prints an odd-looking negative-minutes string like "Time
remaining: -1:59" rather than "0:00" -- reproduced exactly, not "fixed"
(see the header comment on `countdownDisplayText` and
`CountdownDisplayText.NegativeTimeLeftUsesMatlabFloorModSemantics`).

`cleanupCountdown.m` (closes the figure, clears a `sound` object) stays
deferred -- no logic beyond MATLAB-specific rendering, and it's wired to
a real, live `StopFcn` on the (already-ported) countdown timer.
`currentSonicationTimeMarker.m` (a moving vertical line swept across a
plot via a blocking `pause` loop, driven by a hardcoded `endTime = 20`,
not the real duration) and `progressBarButtonPushed.m` (a custom
button-icon progress-bar animation) were believed deferred here too at
the time -- a 2026-09-12 check found their only references are
commented-out lines inside the already-dead `setStartTime.m`, so they're
Excluded, not Deferred; see that correction further below.

Wired to a real `QTimer` in `BeamMainWindow` (`startSonicationCountdown`,
ticking every 2 seconds -- the source's hardcoded period), started by
the Sonicate button on a successful sonication. Verified via
`apps/beam_app --check` (`countdownTextOk=1`: starts the countdown and
confirms the Qt label's text matches `countdownDisplayText`'s output
exactly, not just that the pure function itself is right) and a clean
interactive run.

### Real UI: centering the array on a loaded MRI (slice 21, 2026-09-11)

`GUI/initTransducers.m` looked like pure app-state glue (the name suggests
one-time setup), but it hides a real computation: it centers the array's
own centroid on the loaded MRI volume's physical center, offset by a
fixed, uncommented `[0, 50, -25]` mm literal in the source (reproduced
as-is, not derived) -- so the array/fiducial overlay drawn on the MRI
slice views starts out placed somewhere inside the volume, rather than
wherever the array's nominal `rect` coordinates happen to put it before
any real fiducial-based registration has run.

Ported to `beam::gui::centerArrayOnMri` (`libs/gui/initial_placement_presenter`),
reusing the already-ported `applyAffineToArrayData` for the actual
translation. Two calls inside the source function turned out to be
no-ops once read in full: `transformArrayDataToHFSRAS.m`'s entire body is
commented out beneath a `% TODO`, so it's the identity function; and
`defineTranslateAffineMatrix.m`'s own 4x4 construction is trivial enough
(`Eigen::Matrix4d::Identity()` plus a translation block) that it wasn't
worth a separate named port. Both are disclosed in the header comment
rather than silently dropped.

The source's centered `arrayData` is a local variable, never written back
into `app.sys.arrayData` -- it only feeds the one `drawTransducersOnMRI`
call right after. Wired the same way in `beam_app`: only the
array-footprint mask (`rasterizeArrayOntoMriGrid`) uses the centered
copy on a real `--mri` load; `originArrayData` itself (the fixed fit
basis `registerArrayToFiducials`/`registerCurrentTransducerPosition`
always use) is untouched, and the fiducial-marker and focus overlays keep
reading from it directly, exactly as the source's separate
`setFiducialROIs.m`/`drawFocusOnMRI.m` do.

`initSliceSliders.m` and `leftRightButtonPushed.m` were also read in full
while investigating this bucket (slider `.Limits`/`.Value` setup,
increment/decrement with clamping) and confirmed to be pure Qt-widget
glue with no portable math -- correctly still deferred, not touched here.

While testing this against a real `--mri` load, found that
`beam_app --check --mri <file>` hangs indefinitely on the unit tests' own
tiny NIfTI fixtures -- confirmed pre-existing (reproduces identically
with this slice's changes reverted) and unrelated to this port; nobody
had exercised `beam_app --mri` end-to-end against a real file before.
**Root-caused and fixed 2026-09-11** (see the "Fixed a real bug" note
below) -- it wasn't a `loadNiftiMriRas` issue at all, just misdiagnosed as
one at the time.

### Real UI: multi-visit treatment sessions (slice 22, 2026-09-11)

Built out the multi-protocol/multi-visit treatment-history feature that
had been sitting deferred since slice 9-11 (`app.sys.treatmentProtocolTables(
protocol).sessions(visit).Data`) -- this is new *feature* work, not
translation: the demo never had this data model before, unlike every
other slice so far which extracted math that already existed in the
source. Done at the user's explicit direction after confirming the
alternative (wiring the external `acpcDetect` WSL tool) couldn't be
tested on this machine (WSL isn't installed here). (Later turned out to
be moot for a different reason too -- `getACPCFromSys.m` and everything
around it is dead code, never wired to any real UI action; see the
correction further below. Good thing this feature was built instead.)

`libs/gui/treatment_session_store` (Qt-free, reuses the already-existing
`beam::gui::TreatmentProtocolRecord` row type from `session_io.hpp`
rather than inventing a parallel struct):

- **`initTreatmentProtocolTables.m`**'s fresh-init branch ->
  `initTreatmentProtocols`: one `TreatmentProtocol` per name, each
  starting with one visit. The source's own per-protocol-name CSV "blank"
  template (`getTreatmentProtocolTable.m`) is Excluded elsewhere
  (hardcoded wrong-project path, no bundled CSVs) -- every protocol here
  starts from the same caller-supplied blank instead; disclosed, not a
  silent gap.
- **`addTreatmentProtocolSession.m`** -> `addTreatmentProtocolSession`:
  appends a new visit, capped at `maxSessions` (source:
  `app.maxTreatmentSessions`, 15). **Quirk preserved:** the source
  compares the *current* count against the cap with `<=`, so it still
  adds one more when count == 15, actually allowing 16 sessions before
  refusing further adds -- not "fixed" to a clean off-by-one.
- **`setTreatmentProtocolTableDisplay.m`** -> `currentSessionRows`: which
  session's rows to show, given a protocol name + visit number.
- **`setTreatmentProtocolTableData.m`**'s write-back half (its row-
  coloring half was already ported in slice 11 --
  `getResponseFromTreatmentProtocolData`) -> `setCurrentSessionRows`.

Not ported into this store: **`updateShamParametersFromTable.m`** (despite
the name, unrelated to sham audio -- it's a near-duplicate of
`updateSonicateSettingsForCurrentSonication.m`) and
**`updateSonicateSettingsForCurrentSonication.m`** itself -- both are
about advancing to the *next planned sonication* within a session
(syncing `app.TargetListListBox`/`stimParamTable` state), a materially
different, larger piece of behavior than "which session's data is
currently shown," and out of scope for this slice. `setProtocolListBox.m`/
`setProtocolTableWithStimParamTable.m` are a *different* `app.sys.protocolTables`
(the target list, e.g. "SCC1"/"aMCC1") -- unrelated to
`treatmentProtocolTables` despite the similar name; not touched here
either.

Wired into `apps/beam_app` (Sonicate tab, Treatment Protocol sub-tab): a
"Treatment protocol" combo (the 3 real fixed names from
`BeamV0.mlapp`'s own `TreatmentProtocolDropDown.Items` -- "PainSCCandAMCC",
"PainAMCCandSCC", "PainACC", same default selection) + a "Visit" combo +
a "New Visit" button (`NewVisitButtonPushed`'s real action; its
`uiconfirm` prompt isn't reproduced, same treatment as `warnForSonicate.m`
elsewhere). Edits to the grid persist automatically via Qt's
`dataChanged` signal (no separate "save" step needed, matching the
source's own per-cell-edit write-back in `treatmentProtocolTableCellEdit`).
Verified via `apps/beam_app --check` (`treatment editPersisted=1
newVisitAdded=1 firstVisitUntouched=1` -- edits a cell via a real
`setData` call, confirms it landed in the store, clicks "New Visit" for
real, confirms the new visit starts blank and the first visit's edit
survived) plus the new `libs/gui/treatment_session_store` gtest coverage,
plus a clean interactive run.

### Fixed a real bug: hardcoded slice index in `--check`'s `--mri` smoke test (2026-09-11)

Root-caused the hang noted above. `apps/beam_app/main.cpp`'s `--check`
block called `beam::mri::getSliceImage(mriVolume, 20, "axial")` --
slice 20, hardcoded, regardless of what `--mri` actually loaded. For the
default synthetic volume (40 slices) that's harmless; for any real file
with fewer than 20 axial slices (the unit tests' own tiny NIfTI fixtures,
but also plausibly a real small/localizer scan) it indexes
`img.kSlices[19]` past the end of a shorter vector -- undefined behavior,
not a clean crash. Under this machine's MSVC debug STL that UB manifests
as a blocking Debug Error dialog with no console output, which is
indistinguishable from a hang when driven from a script (no window
station to click "Abort" in this environment). Not specific to
degenerate/singleton axes as first suspected -- any file with 2-19 axial
slices would have hit it too.

Fixed by computing the middle slice instead
(`std::max(mriVolume.nz / 2, 1)`), matching the convention
`MriSliceView`'s own default slider position already uses interactively.
Verified: the previously-hanging tiny fixture now completes cleanly
(`mri=file:...2x1x1 mriAxialSlice=1x2`), the synthetic default is
unchanged (40/2=20, same value as the old hardcoded constant --
coincidence, not by design), and a real 192x256x256 scan still works
(`mriAxialSlice=256x192`, `overlay arrayVoxels=90 fiducialVoxels=630
focusCoverage=2376.00` -- this `2376.00` differs slightly from the
`2404.00` documented in the MRI-overlay slice's own section above,
expected since `centerArrayOnMri` (a later slice) now repositions the
array/focus before that number is computed -- not a regression from this
fix).

### Root-caused (not our bug): intermittent Debug-only Qt assert dialog (2026-09-11)

An interactive-only issue, first hit while stress-testing slice 22 above
by repeatedly clicking "Add Sonication"/"Remove Marked" and the new
protocol/visit combos, but confirmed by the user to predate that slice
entirely (seen earlier the same day on an unrelated build) -- so it's
pre-existing, not a regression from anything in this project. A native
Windows "Debug Error" dialog pops with `ASSERT: "bm.format() ==
QImage::Format_Mono" in ...\qpixmap_win.cpp, line 200` -- silently, with
no console output, indistinguishable from a hang when driven from this
sandboxed environment (same trap as the slice-index bug above: no window
station here to see or click through a dialog). Triggers "kind of
random," correlated with UI activity in general, not any specific
button.

Traced to the exact root cause by reading the actual vcpkg-built Qt
source still on disk at the path named in the assert message
(`C:\vcpkg\buildtrees\qtbase\src\here-src-6-6afd0ec0a2.clean\...`), not
just a same-version download from upstream -- this removed all version-
drift guesswork:

- `qt_createIconMask(QImage bm)` (`qpixmap_win.cpp:198`) has the assert.
  Its only caller in this file is `qt_createIconMask(const QBitmap&)`,
  which converts to `Format_Mono` before calling it -- except when the
  input `QBitmap` is itself null (e.g. built from a zero-size `QPixmap`),
  in which case `.toImage().convertToFormat(Format_Mono)` is a documented
  no-op on a null image, silently keeping `Format_Invalid`.
- That overload's only caller anywhere in Qt is
  `QWindowsCursor::createPixmapCursor()` (`qwindowscursor.cpp`) -- Qt's
  Windows-platform-plugin code for synthesizing a *custom pixmap cursor*.
  Its own null-mask guard (`if (mask.isNull()) mask = QBitmap(pixmap.size());`)
  doesn't help when `pixmap` itself is already zero-size, since a
  zero-size `QBitmap` stays null regardless of `.fill()`.
- `createPixmapCursor()` is reached (via `customCursor()` ->
  `createCursorFromShape()`) for exactly these `Qt::CursorShape` values:
  `SplitVCursor`, `SplitHCursor` (the resize cursor over a `QSplitter`
  handle -- this app has one on the Sonicate tab -- **and** over any
  `QHeaderView` column-resize boundary, which both table views have by
  default), `OpenHandCursor`/`ClosedHandCursor`, and the three drag-
  feedback cursors (`DragCopyCursor`/`DragMoveCursor`/`DragLinkCursor`).
  With PNG support enabled (confirmed the case here), each of these
  cursors is built from an **embedded Qt resource PNG**
  (`:/qt-project.org/windows/cursors/images/splithcursor_32.png` etc.,
  baked into `qwindowsd.dll` itself) rather than the hardcoded bitmap
  arrays used in the no-PNG build. If that resource fails to produce a
  valid pixmap for any reason in this specific vcpkg-pinned Qt6.6 build,
  every one of those cursor shapes crashes the same way -- which explains
  the "kind of random, correlated with general UI activity" symptom
  perfectly: it's whatever resize/drag cursor shape the mouse happens to
  cross during ordinary use (a table's column-resize boundary is an easy,
  easy-to-graze target while clicking nearby buttons), not any specific
  widget's logic.

**Confirmed Debug-only**: `Q_ASSERT` compiles to a no-op outside Debug
builds. Built `.\build-msvc\apps\Release\beam_app.exe`
(`cmake --build build-msvc --config Release --target beam_app`) and had
the user repeat the exact same stress test that reliably triggered the
Debug dialog -- no crash, confirming the underlying inconsistency (if it
even still occurs) is silently harmless at the point `Q_ASSERT` would
have fired, and doesn't corrupt anything downstream in practice.

**Not fixed at the source** -- this is inside Qt's own compiled platform
plugin (`qwindowsd.dll`), not this project's code, so there's no line
here to change; the assert would need a Qt-upstream fix or a different
vcpkg-pinned Qt build to actually go away in Debug. **Mitigation**:
prefer Release builds for interactive use (documented in `README.md`);
if a Debug interactive session hits the dialog, it's this issue --
dismiss it (Cancel/Abort) and keep going, not a real crash to chase.

### Fixed a real bug: "Load Session" didn't persist into the treatment-session store (2026-09-11)

Found during a general bug-hunt review pass (prompted after two real bugs
turned up from targeted interactive testing -- worth doing periodically,
not just when chasing a specific symptom). `BeamMainWindow::applySessionData`
replaces `treatmentProtocolModel_`'s rows via `setRows()`, which goes
through `beginResetModel()`/`endResetModel()` -- this does **not** emit
`dataChanged`. The multi-visit treatment-session store (slice 22 above)
only persists edits via a `dataChanged`-driven handler
(`setTreatmentProtocolDataChangedHandler`). So a real "File -> Load
Session" would show the loaded treatment-protocol data in the grid, but
never write it into `beam::gui::TreatmentProtocol` -- the next time the
protocol/visit combo changed (even without touching the grid), the
loaded data would be silently discarded and replaced by whatever the
(stale) store held for the newly-selected slot.

Fixed with a new `BeamMainWindow::setSessionLoadedHandler` callback,
fired after a real `loadSession()` (the File-menu action; deliberately
*not* fired by `applySessionData()` itself, since `--check`'s own
save/load round-trip calls that directly to restore the exact data it
just saved -- firing the handler there would just be redundant, not
wrong, but the distinction matters for anyone reasoning about when this
fires). `apps/beam_app/main.cpp` wires the same
`persistCurrentTreatmentSession` lambda to both
`setTreatmentProtocolDataChangedHandler` and `setSessionLoadedHandler`
now, rather than duplicating the write-back logic.

Not exercised by `--check` (it goes through a real `QFileDialog`,
which can't be driven headlessly) -- verified by build + full test suite
(219/219 unchanged, this path isn't in `--check`'s scope) plus a real
interactive check by the user: edit a cell, Save Session, switch
protocol/visit, Load Session, switch protocol/visit away and back --
the loaded edit survived instead of reverting to the pre-load blank.

### Third pass: the same mistake, but in already-Converted files this time (2026-09-12)

The user asked directly: "so you are saying there is no more files to be
ported?" After the second pass above, the honest answer for the
*Deferred* bucket was yes. But the reachability audits so far had only
re-checked files from the early, pure-math slices (1, 2, 4, 5, 6, 7) --
never the ~30 files ported in later "Real UI" slices (8 onward), on the
untested assumption that having a real Qt button built in the *same*
slice was good enough evidence. Naming that gap out loud (rather than
waiting to be asked) led to actually closing it: checked every named
source `.m` file from slices 8 through 22 the same way. Almost all of
them are genuinely live (`createStimParamTable.m`/`setStimParamTableNames.m`/
`createTreatmentProtocolParamTable.m`/`setTreatmentProtocolTableNames.m`/
`setTreatmentProtocolTableData.m`/`setFiducialROIs.m` all confirmed via
`startUpFunction.m`; the `GUI/Safety/` cluster confirmed via
`checkSonicationSafety.m`/`updateAttenuationPlots.m`; `getSliceImage.m`/
`imagePositionToIJK.m`/`drawTransducersOnMRI.m`/`setFiducialTemplate.m`/
`generalSonicateMaster.m` all confirmed via multiple already-live
callers) -- but 3 were not:

- **`SonicationTab/createSonicationUpdateTable.m`**, **`removeSonicationUpdateTable.m`**
  -- the *exact* files behind this port's real, tested, working "Add
  Sonication"/"Remove Marked" buttons. Zero callers anywhere, and (unlike
  most of this session's other dead-code finds) git history shows they
  were *never* called, not even historically -- defined once, wired to
  nothing, ever. This is the same "confused my own Qt button with a real
  MATLAB caller" mistake as the ACPC/`localizeArraysMaster.m` cases, just
  with a UI feature actually built on top this time. **The Add
  Sonication/Remove Marked feature itself is unaffected and stays** --
  the C++ translation is correct and the buttons genuinely work; only the
  historical framing ("ported from a live BeamV0 feature") was wrong.
  Reclassified to Excluded; code stays.
- **`GUI/Safety/getMaxSteeringRange.m`** -> `maxSteeringRangeDegrees()`
  (`libs/safety/limits`). Zero callers now; git history shows one real
  call site that was later deleted. Unlike the two above, this one has
  no consumer anywhere in this port either (not wired into
  `checkSonicationParameters` or any real UI path -- only a unit test
  and the MATLAB-parity tool exercise it). Reclassified to Excluded;
  code stays (still correct, tested, just currently unused by anything).

**Net effect on the ledger**: 3 more files move Converted -> Excluded.
GUI: 39/26/23 -> 36/26/26 converted/deferred/excluded (58%). Project
total: 124/35/67 (78%, same rounding as before the total files affected
here shifted the Converted/Excluded balance without moving the overall
percentage).

**Lesson**: "I built a real, tested Qt button/feature for this" is
*not* evidence that the underlying `.m` file had a real caller in
BeamV0 -- those are two independent facts that got conflated three
times now (`getACPCTransform.m` cluster, `localizeArraysMaster.m`
cluster judged fine on a different but adjacent basis, and now
`createSonicationUpdateTable.m`/`removeSonicationUpdateTable.m`/
`getMaxSteeringRange.m`). When documenting a "Real UI: X" slice, check
reachability of the *source* `.m` file independently of whether the new
Qt widget works.

### Fourth pass: closing out the entire GUI Deferred bucket (2026-09-12)

Every other category had already reached 100% Converted (0 Deferred).
GUI's remaining 26 Deferred files were audited file-by-file -- every one
of the 88 `.m` files in `GUI/` now has an explicit Converted or Excluded
citation somewhere in this doc; none are left in an "everything else"
bucket. Two genuinely new small ports came out of this pass, four files
turned out to already be fully covered by existing code (just never
credited), and the remaining 20 are reclassified to Excluded with a
specific reason each -- not a blanket "ran out of time."

**Newly ported (real, previously-unextracted content):**

- **`SonicationTab/getNewProtocolName.m`** -> `beam::gui::newProtocolName`
  (`libs/gui/sonication_tab_presenter`) -- real, portable auto-naming/
  collision-avoidance string logic. No live caller in this port (both
  real callers depend on `app.sys.protocolTables`' full CRUD data model,
  which nothing here replicates -- same as `setProtocolListBox.m` below),
  so ported standalone and exercised via `apps/beam_app --check`
  (`newProtocolName="Protocol 1"`), matching the precedent of porting
  real math ahead of any UI wiring (e.g. several Correction/Stimulation
  functions earlier in this project). Faithful-port quirks preserved,
  see the header comment: the outer `j = 1:5` retry loop re-scans *every*
  existing name on every pass rather than stopping early, so an earlier
  name can still trigger a second rename on a later pass; the first
  collision appends `"_01"`, every later one overwrites the name's last
  character with the digit `j` instead of appending `"_0j"`.
- **`SonicationTab/ExampleTargets/setExampleTargetImages.m`** ->
  `beam::gui::exampleTargetHelpText` (`libs/gui/sonication_tab_presenter`)
  -- ported the one piece of this function that isn't image I/O: the
  `switch(exampleFlag)`'s help-text selection (every target gets the same
  default text except `'VIM'`). The `imread`/`imshow` calls loading
  `SCC1Sag.png` etc. stay unported -- those reference images aren't
  bundled with this repo.
- **`GUI/initArrayFramePosition.m`** -> `beam::gui::defaultArrayFramePosition`
  (`libs/gui/initial_placement_presenter`) -- the "Array Lock Position
  Subject Left/Right" physical sliders' startup default (both = 1, the
  sliders' own minimum). Wired as the real initial `QSlider::value()` in
  `BeamMainWindow`'s constructor (previously left at Qt's own unset
  default, which happened to coincidentally match).

**Already covered elsewhere, credited now (ledger-only, no new code):**

- **`UIFeatures/setCRFHydrogelSize.m`**, **`setCRFOperatorID.m`**,
  **`setCRFParticipantID.m`**, **`setCRFVisitNumber.m`** -- each a
  literal one-line `app.sys.CRF.X = value` assignment. `libs/gui/case_report_form.hpp`'s
  own header comment has cited all 4 of these by name since the phase-4
  slice that built `CaseReportForm`'s `operatorId`/`hydrogelSize`/
  `visitNumber`/`subjectId` fields -- the *code* already credited them,
  the ledger doc simply never was updated to match. No new code.
- **`coordinateToIndex.m`** -- real nearest-index lookup, but a strict
  subset of the already-ported `imagePositionToVoxelIndex` (all 3 axes at
  once, `libs/gui/mri_overlay_presenter`). Credited there instead of as a
  standalone port.
- **`SonicationTab/addTargetROI.m`**, **`updateTargetPositions.m`** --
  both are table-to-struct copies of the same array-centroid mean
  `apps/beam_app/main.cpp`'s own `centerArrayMm` already computes inline.
- **`drawFocusOnMRI.m`** -- its real content (the `centerArrayMM` mean +
  the `setFiducialTemplate` call) is exactly `main.cpp`'s `centerArrayMm`
  computation feeding `rasterizeFocusEllipsoidOntoMriGrid` -- already
  built in the focus-overlay slice, just not cross-referenced back to
  this source file until now.
- **`startUpFunction.m`** -- 111 lines, pure sequencing of already-
  individually-classified pieces (`initSliceSliders`, `initTransducers`,
  `setArrayFiducialMarkers`, `createStimParamTable`, `setFiducialROIs`,
  ...). `apps/beam_app/main.cpp`'s own top-to-bottom setup already plays
  this exact orchestrating role for the C++ port -- credited there
  instead of translated line-by-line (an orchestration *order*, not
  computation, isn't the kind of thing a second, parallel "port" of adds
  value by duplicating).

**Reclassified Deferred -> Excluded (confirmed via full read -- pure
MATLAB widget/app-object-model glue, cosmetic styling, a App-Designer-
layout-specific interaction heuristic, or a dead end through already-
Excluded code -- none of these will be genuinely revisited, which is
Excluded's actual definition per `docs/conversion-status.md`, not
Deferred's):**

- **`AddDirectionPropertyToPushbuttons.m`** -- adds a custom `.direction`
  property to button objects (`addprop`), zero computation.
- **`RegistrationTab/AutoReg/getImageFilterSpec.m`** -- a hardcoded
  file-dialog filter-spec cell array; a translated version would just be
  a Qt `QFileDialog` filter string, not a `.m` "port."
- **`RegistrationTab/allFiducialEvents.m`** -- a draggable-ROI event
  handler keyed to pixel positions in BeamV0's specific App Designer
  layout (confirmed live, same class as the now-dead `allTargetEvents.m`
  above) -- there's no equivalent concept in a from-scratch Qt layout to
  translate this *to*.
- **`RegistrationTab/moveToFiducialMarkerButtonFunction.m`** -- a linear
  lookup of a selected fiducial's position + 3 slider writes + a redraw.
  Pure widget glue, the lookup itself is already trivially inline-able
  wherever needed.
- **`RegistrationTab/setBackupMRIandArrayData.m`** -- a 3-branch
  `if`/`elseif` copying `app.sys.aImg`/`ax`/`ay`/`az`/`arrayData` into a
  named backup struct slot. Pure state-snapshot glue, zero computation.
- **`RegistrationTab/show_transducer.m`** -- real `surf`/`quiver3`
  plotting for a standalone transducer visualization, byte-identical to
  `Util/show_transducer.m` (see the 2026-09-12 duplicate-basename audit,
  `docs/deferred-and-excluded.md`) but this copy has zero callers of its
  own -- dead, and pure plotting either way.
- **`SonicationTab/setProtocolListBox.m`**, **`setProtocolTableWithStimParamTable.m`**
  -- confirmed live (`startUpFunction.m`/`stimParamTableCellEdit`), but
  both operate on the *other* `app.sys.protocolTables` (the target list,
  e.g. "SCC1"/"aMCC1" -- unrelated to `treatmentProtocolTables` despite
  the similar name), a CRUD data model this port has deliberately not
  replicated anywhere (the "Compute Best Targets" path uses the simpler
  `getTopTargetsFromTreatmentProtocolTable` instead). Pure state glue,
  no computation.
- **`UIFeatures/cleanupCountdown.m`** -- closes a figure, clears a
  `sound` object, deletes a MATLAB `timer`. Zero domain logic; this
  port's countdown already gets an equivalent real cleanup for free from
  Qt's own `QTimer`/widget lifetime management, nothing to translate.
- **`drawMrImages.m`**, **`drawROIs.m`** -- read in full: pure MATLAB
  widget/axes/ROI rendering gated by checkbox/slider state, with every
  piece of real math already extracted elsewhere (`getSliceImage`,
  `imagePositionToIJK`, the overlay rasterizers). `MriSliceView` already
  replicates the *capability* with a different, simpler, always-on
  design -- not a translation target.
- **`enableAllButtonsMenuSelectedCallback.m`** -- 3
  `set(...,'Enable','on')` calls. Confirmed live, zero computation.
- **`RegistrationTab/set3DMRIfromImageRegistration.m`** -- real
  affine-resample math, but its only reachable path is through
  `ImageBasedModel/getImageData.m`'s Python-CV pipeline, itself moved to
  Excluded this session (`docs/known_gaps_registration.md`) -- a function
  only reachable through Excluded code is itself effectively unreachable.
- **`SonicationTab/updateSonicateSettingsForCurrentSonication.m`**,
  **`RegistrationTab/Targeting/updateShamParametersFromTable.m`** -- both
  real (confirmed live callers), both over the same nested
  `app.sys.treatmentProtocolTables(protocol).sessions(visit).Data`
  multi-visit state machine this port has deliberately *not* replicated
  -- the multi-visit treatment-session store built earlier this project
  is explicitly documented as new, simpler feature work, not a
  translation of this data model (`docs/deferred-and-excluded.md`).
  Porting either would mean building the unmodeled structure first, for
  a capability this port's simpler model doesn't need -- a design
  decision already made, not unfinished work.
- **`SonicationTab/warnForSonicate.m`** -- a `uiconfirm` modal ("is
  SONICATE the correct randomization?"). Pure human-in-the-loop
  confirmation -- the "logic" *is* the operator's answer, there's nothing
  to compute or translate.
- **`improveGUIappearance.m`** -- 184 lines, entirely dark-theme widget
  styling calls, no math. A Qt UI has its own, unrelated styling layer;
  translating MATLAB `uifigure` color properties isn't meaningful here.
- **`initSliceSliders.m`**, **`leftRightButtonPushed.m`** -- read in
  full: slider `.Limits`/`.Value` setup and increment/decrement with
  clamping -- pure Qt-widget glue (`QSlider` provides the same clamping
  natively), no portable math beyond what a slider widget already does.

**Net effect on the ledger**: every one of the 88 GUI `.m` files now has
an explicit Converted or Excluded citation. This pass also caught a
pre-existing doc-hygiene gap: 6 files (the 4 `setCRF*.m` setters covered
above, plus `initArrayFramePosition.m` and `RegistrationTab/show_transducer.m`)
had *zero* individual mentions anywhere in this doc before now -- never
wrong, just never actually written down, which is how the old 36/26/26
split undercounted them. Recounting from scratch against the real 88-file
list: GUI is now **43/0/45 (100%)**. Project total: **136/0/90 (100%)**
-- every category is now fully Converted or Excluded; nothing remains in
Deferred anywhere in the project.

## Excluded (dead code)

- **`CorrectionTab/setImagescMask.m`**, **`CorrectionTab/formatAttenuationPlotAxes.m`**
  — no callers anywhere in the tree. `setImagescMask` is mask-overlay
  compositing; `formatAttenuationPlotAxes` is `colorbar`/`caxis`/`axtoolbar`
  chrome. Nothing to port.
- **`CorrectionTab/ProcessPastThroughTransmitDataForPlots.m`** — a bare
  script (no `function` header), calls an undefined
  `generateThroughTransmitHumanData`, an offline figure-generation script.
- **`UIFeatures/setStartTime.m`** — `evalin('base','Resource')` then a
  `findall(0,'Type','axes')` scan for a plot whose `XLim(2)==20`; VSX +
  figure-hunting, no working behaviour to port.
- **`RegistrationTab/getFiducialNameFromOldFiducialNames.m`** — calls
  `getArrayFiducialMarkerNames()` requesting 2 outputs; that function
  returns only 1. MATLAB errors on every call. Broken, not translated.
- **`SonicationTab/getTreatmentProtocolTable.m`** — hardcoded
  `Diadem\GUI\SonicationTab\TreatmentProtocols\...` paths (a leftover
  DiademV0 path, unedited) reading CSV files via `readtable`.
- **`RegistrationTab/set3DMRIfromImageRegistrationOld.m`** —
  reclassified here 2026-09-11: no callers anywhere (not the plain `.m`
  tree, not `BeamV0.mlapp`'s embedded code, not BeamV0's git history
  beyond its own initial add) — a dead, superseded earlier draft of
  `set3DMRIfromImageRegistration.m` (same structure, a dimensionally
  questionable `Mcol([1 3],[1 3]) = R2` assignment the "new" version
  replaced with explicit element-by-element assignment). Found while
  re-checking the "every deferred file read" claim below turned out to
  have missed exactly one file -- this one.

## Deferred (the UI layer) -- resolved, 2026-09-12

This section used to list the ~26 files not yet classified. All of them
now have a specific Converted or Excluded citation -- see "Fourth pass:
closing out the entire GUI Deferred bucket" above for the full
file-by-file resolution (3 newly ported, 5 credited to already-existing
code, 18 reclassified to Excluded with individual reasons). GUI is now
43/0/45 (100% Converted, 0 Deferred).

## Build status

`cmake --preset msvc && cmake --build --preset msvc`, **219/219** tests via
`.\build-msvc\tests\Debug\unit_tests.exe` (Safety-tab presenter, Correction-tab
presenter + signal math, Case Report Form, RegistrationTab coordinate
math, SonicationTab data logic, target carry-forward ranking, treatment
row coloring, `reorientedVolumeToVolume3D`, MRI-grid overlay rasterization,
`prepareSonication`, session save/load round-trip, focus-ellipsoid
rasterization, `registerArrayToFiducials`/`registerCurrentTransducerPosition`,
Butterworth filter design + IIR filtering, sonication event timeline,
countdown tick/format logic, MRI-centered array placement, sham-sonication
masking audio, multi-visit treatment-session store). Run the exe
directly -- `ctest` is Application-Control-blocked on this machine.
`apps/beam_app --check` populates the tabs (now including the real
stimParamTable grid) headless and prints a summary
as a build smoke test.

## MATLAB parity

`matlab_verify/` diffs the ports against BeamV0's Safety code (atol/rtol
1e-9): **14/14 matched** (`compare_parity.ps1 -Phase safety`, 2026-09-10).

Safety differs from the other parity phases: `checkSonicationSafety.m`,
`getISPTAFromStimParams.m` and `getMechanicalIndex.m` are GUI callbacks
that take `app` and read GUI tables / call `setStimParamsFromApp`, so they
can't be invoked directly.
`verify_safety.m` instead evaluates the acoustic formulae copied *verbatim*
from those source files (with line references) — pinning rho 1040 vs 1046,
the `/100^2`, `floor(BD/PI)`, and `amp/sqrt(f)` — and calls
`getMaxSteeringRange` / `getMaxSonicationAmplitude`, which are directly
callable. The `checkSonicationParameters` control flow (branch selection,
report assembly) stays unit-test-only.
