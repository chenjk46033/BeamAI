# Deferred & excluded

**Important caveat, added 2026-09-14:** everything below tracks whether
each BeamV0 `.m` file's real *logic* was translated into C++ -- not
whether `beam_app` is a feature-complete application. GUI's 100% here
does NOT mean the interactive app matches BeamV0; a widget-by-widget
audit (re-run 2026-09-18 against DesignerStartupWindow, the active
window) found ~59% of BeamV0's real controls exist in `beam_app` at
all -- Register and Correction tabs are entirely unbuilt there. See
[GUI widget inventory](gui_widget_inventory.md) for that
separate, much less complete picture, and `README.md`'s Conversion
status section for the same caveat.

As of 2026-09-12, every phase -- including GUI -- was at 100% Converted,
0 files left in Deferred anywhere in the project (136/0/90 overall). A
2026-09-14 correction moved `windowUpDownCallback.m` (MRI phase) from
Excluded back to Deferred -- real, portable brightness logic
misclassified on first read, not yet ported pending a rendering-pipeline
change (see `docs/known_gaps_mri.md`'s Deferred section) -- so the
current overall count is 137 Converted / 1 Deferred / 88 Excluded. GUI
was the last holdout (58%, 26 files deferred) until a file-by-file audit
closed it out the same day: unlike the other phases, GUI is a ground-up
Qt rebuild rather than a line-for-line translation, so its remaining
files were mostly pure widget/app-object-model glue with nothing left to
extract, rather than undiscovered math. 3 small real ports (auto-naming
string logic, a help-text lookup, a slider default) came out of the
audit; several more files turned out to already be covered by existing
code (just never individually credited -- the audit also caught 6 files
that had literally zero mentions anywhere in the ledger doc before now,
a pre-existing gap, not new work going missing); the rest are honestly
Excluded (per `docs/conversion-status.md`'s own definition: never going
to be genuinely revisited), not Deferred. See
`docs/known_gaps_gui.md`'s "Fourth pass" section for the full
file-by-file breakdown.

Per-phase reasons (what's converted / deferred / excluded and why):
`docs/known_gaps.md`, `docs/known_gaps_util.md`,
`docs/known_gaps_correction.md`, `docs/known_gaps_stimulation.md`,
`docs/known_gaps_mri.md`, `docs/known_gaps_registration.md`,
`docs/known_gaps_serialcom_sham.md`, `docs/known_gaps_gui.md`.

Notable: MRI's 14 excluded includes the 11-file vendored `dicom2nifti/`
toolbox (replaced by `libs/mri` + `libs/infra_dicom`). Stimulation's 9
excluded are 8 files of dead Verasonics VSX code (Beam drives its
hardware over USB/serial — see `docs/known_gaps_stimulation.md`) plus
`getSteeringAmplitudeCorrection.m` (dead for a different reason: no
callers anywhere, and a hardcoded data path for the wrong project).

GUI is being **rebuilt in Qt**, not translated line-for-line — `libs/gui`
(presenters, no Qt) + `libs/gui_qt` (`QWidget`/`QChart` views) + `beam_app`
(the shell: Sonicate / Register / Correction tabs). GUI's 24 converted
count below is stale (many slices have landed since); it also originally
listed RegistrationTab's ACPC-transform / fiducial-normal coordinate math
as part of that live tally, but a 2026-09-12 reachability check found
that math's source files (`getACPCTransform.m`,
`applyReferenceCoordinateTransform.m`, `getArrayFiducialNormals.m`) have
zero real callers anywhere in BeamV0 -- reclassified to Excluded (the
tested C++ ports stay in the tree; see `docs/known_gaps_gui.md` for the
full trace). The rest of the original list still holds: the
`GUI/Safety/` cores, the CorrectionTab presenters, the Case Report Form
logic,
SonicationTab's protocol-naming / response-scoring / color-map / pulse-plot
logic, and its target-carry-forward ranking algorithm
(`getTopTargetsFromTreatmentProtocolTable.m`). Past that point GUI work
shifts from math extraction to actually building widgets: the Sonicate tab
now has two real, editable grids — `stimParamTable`
(`createStimParamTable.m`/`setStimParamTableNames.m`), wired to the ported
sort/current-row presenters, and `treatmentProtocolTable`
(`createTreatmentProtocolParamTable.m`/`setTreatmentProtocolTableNames.m`),
which finally gives the target carry-forward ranking algorithm a real
caller (an ACC-region selector + "Compute Best Targets" button), plus
"Add Sonication" / "Remove Marked" row management on the first grid. Rows
in the treatment-protocol grid now color live by response (green/yellow/
white) as you edit Pain/Mood. The Register tab has its placeholder
replaced with a real fiducial-marker grid, populated end to end from a
synthetic array through the ported registration geometry, plus a
slider-navigated sagittal/coronal/axial MRI slice viewer (this project's
first image rendering, built on the already-ported `getSliceImage.m` —
not a port of `drawMrImages.m` itself, see `docs/known_gaps_gui.md`).
`beam_app --mri <path.nii>` loads that viewer from a real NIfTI file
through the real `loadNiftiMriRas` reader (verified against a real
192×256×256 scan; not bundled with the repo), which also turns on an
array-footprint / fiducial-marker overlay (`drawTransducersOnMRI.m`'s
rasterization math, ported, plus new plumbing for the fiducial markers --
see `docs/known_gaps_gui.md`) tinting the same three slice views yellow/red.
The Sonicate tab now has a real "Sonicate" button too: it runs
`generalSonicateMaster.m`'s decision logic
(`beam::gui::prepareSonication` -- counted under Stimulation above, see
`docs/known_gaps_stimulation.md`) and, if it passes, sends the resulting
command over the first available serial port, with no simulated-hardware
fallback (reports "no serial port available" rather than faking success).
It also gained a File menu (Save/Load Session -- `libs/gui/session_io`'s
own text format, not BeamV0's `.mat`; see `docs/known_gaps_gui.md`). The
MRI viewer's overlay grew a third color: a magenta glow for
`drawFocusOnMRI.m`'s "focus" marker (`setFiducialTemplate.m`'s ellipsoid,
ported for this single-point, unrotated call site only -- see
`docs/known_gaps_gui.md`), blended by its anti-aliased `[0,1]` coverage
value rather than a hard on/off cutoff. That "focus" is the source's own
simplification, not a real acoustic target: just the mean of every array
element's center, since no per-sonication targeting exists in this port
yet. The Sonicate button also now starts a real countdown ("Time
remaining: M:SS", ticking every 2 seconds -- `startStandaloneCountdown.m`/
`updateFigureTimer.m`'s tick/format logic, preserving a genuine MATLAB
floor/mod quirk in its final tick or two). The MRI overlay's array
footprint is now centered on the loaded volume on a real `--mri` load
too, reusing a real computation found hiding inside the glue-looking
`initTransducers.m` (a fixed, uncommented mm offset from the source,
reproduced as-is -- see `docs/known_gaps_gui.md`). The Sonicate tab's
Treatment Protocol sub-tab now has a real protocol/visit selector too --
a multi-visit treatment-session store (`libs/gui/treatment_session_store`)
built out as new feature work (not a translation -- the demo never had
this data model before), covering `initTreatmentProtocolTables.m`/
`addTreatmentProtocolSession.m`/`setTreatmentProtocolTableDisplay.m`/
`setTreatmentProtocolTableData.m`'s write-back half; see
`docs/known_gaps_gui.md`. GUI's 26 deferred are the rest of the UI layer
(remaining tables, plotting, timers) -- down from 39 after two
2026-09-12 reachability-check passes: the first moved 7 genuinely-dead
`RegistrationTab/Targeting/`+`AutoReg/` files (the ACPC cluster,
including `getACPCFromSys.m` itself -- it never had a real "Find ACPC"
button anywhere in the app) to Excluded; the user then pushed back
("surely there's more to find") on a second pass, which widened the
reachability-check method itself (it had been missing menu/dropdown/
selection-group callbacks, not just buttons) and found 6 more dead files
elsewhere in the bucket (`allTargetEvents.m`, `updateMNITargetLabel.m`,
`updateTransducerPositionEditFields.m`, `sagCrossROICallback.m`,
`currentSonicationTimeMarker.m`, `progressBarButtonPushed.m`). A third
pass (user: "so you are saying there is no more files to be ported?")
extended the same check to *already-Converted* files from later "Real
UI" slices, not just the Deferred bucket -- found the same mistake there
too: `createSonicationUpdateTable.m`/`removeSonicationUpdateTable.m`
(the source files behind this port's real "Add Sonication"/"Remove
Marked" buttons -- the buttons work fine, the source files just never
had a real caller in BeamV0, ever) and `getMaxSteeringRange.m` also move
Converted -> Excluded. GUI now 36/26/26 (58%).

A final 2026-09-12 pass closed out all 26 remaining GUI Deferred files --
see `docs/known_gaps_gui.md`'s "Fourth pass" section for the full
file-by-file resolution. GUI now 43/0/45 (100%).

Separately, in the **Registration** category (not GUI --
`registerArrayToFiducials.m` lives in the plain `Registration/` folder):
the "Register To MRI Fiducials" button is now real too. It fits the
array's nominal fiducial positions onto the fiducial table's current
values via the already-ported `getAffineMatrixFromRegistration` +
`applyAffineToArrayData`, then re-rasterizes the array/focus overlays --
see `docs/known_gaps_registration.md`. The "Register Arrays to Current Position" button (the manual's "Outside
the MRI" registration, using the "Array Lock Position Subject Left/
Right" sliders) is real too now -- it re-fits from the fiducial table
the same way, then adds a slider-derived world-frame translation on top
(`registerCurrentTransducerPosition`, scoped to the source's
`MRIFiducialsButton` branch only). Registration's 3 remaining deferred
are GUI-`app`-coupled glue around all this (saving fiducial ROIs, a
registration-check init) plus the image-based registration orchestrator
(`getImageData.m` -- confirmed real but not portable: it shells out to
an external Python computer-vision pipeline via MATLAB's `py.*` interop,
not translatable MATLAB math). `loadDicomDir.m` was reclassified to
Excluded -- no callers anywhere (its near-namesake `loadDicomDirRAS.m`
doesn't actually call it, a coincidental filename match), and its
capability is covered anyway by `assembleDicomSeriesRas` (MRI phase). A
2026-09-12 reachability sweep (the same one that found more dead files
in GUI, prompted by the user asking "are we complete?") found 3 more
dead Registration files too: `setSysarrayDataFiducialMarkers.m`/
`getFiducialROIGroupIndicies.m` (both live once, later disabled/deleted,
per git history) and `getFrameFiducialsCenter.m` (never had a caller,
ever). Registration now 16/3/8 (84%).

A 2026-09-12 pass closed out Registration's remaining 3 Deferred:
`setRegistrationCheck.m` (the Register tab's 3 status lamps + Sonicate
button enable, ported as `computeRegistrationCheckLampState`, disclosed
quirk preserved -- see `docs/known_gaps_registration.md`) and
`saveFiducialROIs.m` (its capability turned out to already be exactly
what `libs/gui/session_io`'s fiducial save does -- ledger credit only, no
new code) both moved to Converted; `getImageData.m` (the Python-CV
interop orchestrator) moved to Excluded instead of staying Deferred --
it was never going to be genuinely revisited, so Excluded is the honest
bucket per `docs/conversion-status.md`'s own definition. Registration now
18/0/9 (100%).

And in **MRI**: `updateSysWithMRI.m`'s last deferred content -- voxel
resolution (`res = abs(diff(dim(1:2)))`, per axis) and the display
intensity window (`[0, max(aImg(:))]`) -- was closed out 2026-09-12 as
`beam::mri::computeVoxelResolution`/`computeDisplayWindow`; the former now
also backs `rasterizeArrayOntoMriGrid`'s own spacing calculation instead
of duplicating it inline. MRI now 8/0/14 (100%).

A 2026-09-14 correction: `loadDefaultSys.m` had been misclassified
Excluded ("hardcoded path" pattern-matched the other dead stubs in that
bucket, without registering that the path points to real, load-bearing
product data -- BeamV0's own `DefaultSubjectV0/*.mat`, loaded
unconditionally on every startup, before anything else runs). Caught
when the user compared `beam_app`'s synthetic-blob default MRI display
against a real BeamV0 session and found it "far from that, not even
close." Reclassified to Converted: `apps/beam_app` now tries a NIfTI
export of that same default subject (`DefaultSubjectV0/defaultSubjectMNIV1.nii`,
kept external to Beam's own repo -- matching BeamV0's own architecture
of not bundling this data either) before falling back to synthetic,
exactly the same graceful-degradation policy `--mri` already had. First
attempt resolved that path relative to the process's current working
directory only, so it silently fell back to synthetic whenever CWD
wasn't the repo root (e.g. running the exe from its own
`build-msvc\apps\Release\` output folder) -- caught immediately by the
user doing exactly that. Fixed by resolving relative to the running
exe's own location (`QCoreApplication::applicationDirPath()`) instead,
tried before the CWD-relative path. Verified from both a repo-root shell
and `cd`'d into the exe's own folder (the user's exact repro): a
zero-argument `beam_app` launch now shows real anatomy in all 3 slice
views immediately in both cases, matching BeamV0. MRI now 9/0/13 (100%).
Only the MRI piece of `loadDefaultSys.m`'s much larger
default `sys` state (38 fields total) is replicated -- see
`docs/gui_widget_inventory.md`.

A second, GUI-category bug (also caught by the user, still on the same
report -- the data loaded correctly but the images still weren't
visible) turned out to be a real layout mistake, not just missing data:
`libs/gui_qt`'s 3 `MriSliceView`s were nested inside the Register tab's
own layout, but `BeamV0.mlapp` parents its equivalent MRI axes directly
to the main figure (`app.axSag = uiaxes(app.UIFigure)`, not to any tab)
-- so the real app shows them on every tab, immediately, and this port's
placement meant the default (Sonicate) tab showed no MRI at all. Fixed
by moving the 3 views into a persistent row above `BeamMainWindow`'s tab
group; a follow-on sizing bug (the now-persistent row had no maximum
size and pushed the tab bar off-screen entirely) fixed with
`imageLabel_->setFixedSize(220, 220)`. See `docs/known_gaps_gui.md`'s
"Real UI: MRI slice viewer" slice for the full trace.

And in **Correction** (also not GUI -- `getRecieveWaveformFromSerial.m`
lives in the plain `Correction/` folder): the "Run Correction" button is
real too now. It sends `"Correction"` over the first available serial
port, accumulates the reply, and runs it through a from-scratch
Butterworth-bandpass filter design (`butterBandpass` + `filterIir` --
`butter`/`filter` are MATLAB builtins, so implementing them was new
infrastructure, not a `.m` port) cross-checked against a real MATLAB
`butter`/`filter` run -- see `docs/known_gaps_correction.md`. Correction's
last deferred file, `initializeCorrectionValues.m`, was closed out
2026-09-12: its one real computation (the startup-default values, all 1s,
before any correction has run) is now `beam::gui::correctionInitialState()`.
Correction now 17/0/5 (100%).

And in **Stimulation**: the Sonicate tab's pulse-waveform chart now has a
companion "Sonication timeline" chart -- a step plot of burst-on/pause
across the whole treatment duration
(`setBurstEventsFromStimParams.m`'s computational core ->
`computeSonicationEventTimeline`, standing in for
`updateSonicationPlots.m`'s previously-unported "burst/total-sonication
plots") -- see `docs/known_gaps_stimulation.md`. The Sonicate tab also
gained a "Sham" button next to "Sonicate"
(`GeneralSonication/unfocusedSonicate.m` -> `prepareShamSonication`):
computes the same masking-audio track (`setShamAudio`, already ported)
that a real sonication plays too, and starts the same countdown, but
sends nothing over serial. Stimulation's last deferred file,
`setStimParamsFromApp.m`, was closed out 2026-09-12: its per-row copy
(previously an untested inline lambda in `apps/beam_app/main.cpp`) is now
a real, tested port, `beam::gui::stimParamsFromTableRow`. Stimulation now
13/0/9 (100%). `getSteeringAmplitudeCorrection.m` was reclassified to
Excluded -- it has
no callers anywhere (checked the `.mlapp`'s embedded code directly) and
its hardcoded data path is for the wrong project (`Diadem\Stimulation\...`),
a leftover from BeamV0 being forked from Diadem.

**Duplicate-basename audit (2026-09-12):** BeamV0 has four `.m` basenames
that appear in two folders each. Two were already known and mattered --
`getElementPositionsFromArrayStruct.m` (a real shared `Arrays/` copy vs. a
dead local duplicate under `Correction/localizeArrays/`, correctly
Excluded) and `show_transducer.m` (`Util/` vs. `GUI/RegistrationTab/`,
both pure plotting, no functional ambiguity). Checked the other two this
pass: `convertDelaysToCycles.m` (`Stimulation/` vs. `Util/`) and
`normalVectorFrom3Points.m` (`Arrays/` vs. `Util/`) are both byte-identical
between their two copies -- whichever one the ported C++ / docs cite,
it's the same source either way. No ledger changes from this pass.
