# Known gaps and deliberate exclusions — Stimulation phase

Same convention as the other `docs/known_gaps*.md`. Beam's own port of
`BeamV0/GUIMatlab/BEAM/Stimulation/` (12 root files + `GeneralSonication/`
6 + `MRUserTriggeredSonication/` 4 = 22). Fourth phase, per Diadem's
migration order.

The split here is sharp: the transmit-delay / apodization / duty-cycle math
is self-contained and portable; the rest is GUI `app` glue (belongs to the
GUI phase) or dead legacy.

**On the Verasonics VSX code in this folder** (`focusedArrayStimulate.m`,
`MRUserTriggeredSonication/*`, `GeneralSonication/setBurstEvent.m` /
`setPauseEvent.m`): Beam does **not** run on Verasonics. Beam drives its
ultrasound over USB/serial — the live sonication path is
`GeneralSonication/generalSonicateMaster.m` (the only sonication function
`GUI/BeamV0.mlapp` calls), which builds a text command via
`setSerialCommandFromStimParams` (already ported, `libs/serialcom`) and
sends it with `sendSerialCommand`. The VSX files are unmodified copies from
DiademV0 (some still carry hardcoded `C:\Users\Verasonics\...\Diadem\...`
paths) that nothing in Beam's GUI reaches — they are **excluded dead
code**, not a deferred hardware port.

## Ported this phase (`libs/stimulation/`)

- **`calculateMultifrequencySuperpositionDelays`**, **`focusArrayAtPoint`**
  — `delays.{hpp,cpp}`. `focusArrayAtPoint`'s dead `delays = max(delays)-delays`
  line (operates on an all-zero vector) is dropped.
- **`getApodFromAtt`** — `apodization.{hpp,cpp}`.
- **`defineStimFreqs`** — `stim_freqs.{hpp,cpp}`. Only the `"650"` / `"high"`
  branches; `"MFS252"` / `"MFS21"` load a hardcoded lab-machine `.mat` and
  throw here (see Deferred).
- **`pressureToDutyCycleGivenTransmission`** — `duty_cycle.{hpp,cpp}`, plus
  a small `interp1` (`interp.{hpp,cpp}`, linear, NaN outside range — MATLAB
  default). The `max(NaN, 0.4)` MATLAB idiom is reproduced (out-of-range
  lookup clamps to 0.4).
- **`defineStimParams`** — `stim_params.{hpp,cpp}`. Orchestrates
  `focusArrayAtPoint` + `addVectors` + `calculateMultifrequencySuperpositionDelays`
  + `convertDelaysToCycles` (last two from Util) into the per-(tx-group,
  target) struct array. MATLAB's `single()` casts on the `freqs`/`apods`
  fields are not carried (kept double).
- **`getPauseIntervals`** — `pause_intervals.{hpp,cpp}` (from
  `GeneralSonication/`; pure timing math, no VSX). Throws on
  `burstTimeUnitLength <= 0` where MATLAB would loop forever.

`convertDelaysToCycles.m` here is byte-identical to the `Util/` copy and
was already ported in the Util phase (`libs/util`).

## Ported later (`events.{hpp,cpp}`)

**`GeneralSonication/getTxAndBurstEvents.m`** — expands each target's
schedule into individual burst / pulse events with absolute on/off times.
Pure timing math (no Verasonics calls, unlike the `setBurstEvent` /
`setPauseEvent` neighbours), so ported now. The source forces
`stimParams(iii).BLKI = 0`, making the block loop just repeat identical
events — preserved as-is.

## Ported in the GUI phase (`libs/gui/sonicate_orchestrator`)

**`GeneralSonication/generalSonicateMaster.m`** — its decision logic (not
the full `app`-state orchestrator): the two last-moment safety checks
(transmission below the coupling threshold; computed duty cycle too high
— the latter is dead, see below), the duty-cycle computation itself
(`pressureToDutyCycleGivenTransmission`, already ported here), and the
Immediate/External trigger toggle (`app.TriggerSwitch.Value`) ->
`beam::gui::prepareSonication`. Wired to a real "Sonicate" button in
`apps/beam_app` (`BeamMainWindow::setSonicateHandler`), which on success
sends the resulting command over the first available serial port via the
already-ported `setSerialCommandFromStimParams` + `sendSerialCommand`
(`libs/serialcom`) -- no simulated-hardware fallback; if no port is
available it reports that cleanly instead of faking a result. Disclosed
deviation: the source's `DutyCycle > 0.90` check is preserved verbatim but
is dead code -- `pressureToDutyCycleGivenTransmission` clamps to
`[0.4, 0.75]`, so it can never fire (covered by
`PrepareSonication.DutyCycleNeverExceedsNinetyPercentSoThatCheckIsDead`).
Not ported here: `testModeFlag` (hardcoded 0, dead), `startStandaloneCountdown`
(UI timer, wired separately -- see the countdown slice in
`docs/known_gaps_gui.md`), and `app.sys.stimParams`/`app.sys.log` session
bookkeeping (a later GUI slice added session save/load for the
stimParamTable/treatmentProtocolTable/fiducial grids, but not this
particular per-sonication log -- see `docs/known_gaps_gui.md`).
`setShamAudio`'s call here (masking-audio playback underneath a real
sonication) is a separate concern from `beam::gui::prepareSonication`'s
own decision logic -- see `unfocusedSonicate.m` below, which ports the
`setShamAudio` orchestration shared by both this function and the sham
path. As of 2026-09-11 it's also wired into the Sonicate button's own
handler in `apps/beam_app/main.cpp` (computed right after
`startSonicationCountdown`, matching the source's own call order, and
shown in the status text as `masking audio: N samples`) -- not actually
played back (no audio output in this demo app), but computed for
fidelity with the source's real side effect.

**`GeneralSonication/setBurstEventsFromStimParams.m`** — its pure
computational core (not the VSX `TW`/`TX` generation, gated behind an
optional `varargin` the source never receives from its real caller) ->
`beam::stimulation::computeSonicationEventTimeline`
(`libs/stimulation/events`). Matches the source's hardcoded `Block = 0`
(the `if Block` branch -- BLKD/BLKI asserts, block-level pausing -- is
therefore dead code, not ported) and drops `burstPauseIntervals`/
`durationPauseIntervals`, which the source computes but never actually
uses (`getTxAndBurstEvents` never reads them). Builds on the
already-ported `getTxAndBurstEvents`, then merges every target's burst
events into one sorted, gap-filled timeline -- the *result* matches the
source's `eventVector` exactly, though the source builds it via a MATLAB
struct-array `count` index this port doesn't reproduce mechanically (only
the final sequence, verified by hand-tracing the source's indexing).

Feeds a real new chart, `buildSonicationTimelineChart`
(`libs/gui_qt/sonication_tab_chart`) -- a step area plot of burst-on (1) /
pause (0) across the whole sonication duration, wired into the Sonicate
tab (`BeamMainWindow::setSonicationTimelineChart`) below the existing
pulse-waveform chart. Stands in for `updateSonicationPlots.m`'s
"burst/total-sonication plots", which were never ported (only its
single-pulse segment was, in the GUI Sonicate-tab slice). Verified via
`apps/beam_app --check` (`timelineSegments=39` for the demo's default
shown row) and a clean interactive run; falls back to an empty
(placeholder) chart rather than crashing if a row's parameters fail the
source's own assertions (PI>=PD, BI>=BD, BD>=PI, duration>=BI) --
realistic for an editable demo grid.

**`GeneralSonication/unfocusedSonicate.m`** — the sham path's real
content: computing the sonication's duration and assembling the masking-
audio track via the already-ported `beam::sham::setShamAudio`, with the
source's own fixed args (`randFlag=0, backgroundNoiseFlag=1`) ->
`beam::gui::prepareShamSonication` (`libs/gui/sham_orchestrator`). This
is the *exact same* `setShamAudio` call `generalSonicateMaster.m` makes
above -- so a real sonication also plays this masking audio underneath
the actual ultrasound send, presumably so a patient can't tell real vs.
sham sessions apart by sound; that side effect isn't wired into the
Sonicate button here (see the note above). Wired to a real "Sham" button
next to "Sonicate" in `apps/beam_app` (`BeamMainWindow::setShamHandler`),
which starts the same countdown display as a real sonication but sends
nothing over serial -- the source doesn't either. The source's own
burst-sound clip (`sonicationSound.mat`) isn't bundled with this repo; a
synthetic 200Hz tone burst stands in (disclosed in `main.cpp`). Not
ported: the ShamButton widget enable/disable and the surrounding
`app.sys.log`/CRF/treatmentProtocolTable/CSV-logging bookkeeping in
`ShamButtonPushed` -- app-state glue, the same scoping `prepareSonication`
already applies to `SonicateButtonPushed`'s own surrounding bookkeeping.
Verified via `apps/beam_app --check` (`shamAudioLen=441000` for the
demo's default 10s shown row at 44.1kHz) and a clean interactive run.

**`setStimParamsFromApp.m`** (2026-09-12) — its per-row field copy
(PD/PI/BD/BI/Amplitude/startTime/endTime, plus the hardcoded
`centerFrequencyMHz = 0.300`) is now a real, tested port --
`beam::gui::stimParamsFromTableRow` (`libs/gui/sonicate_orchestrator`),
replacing what had been an untested inline lambda in
`apps/beam_app/main.cpp`. Not carried: the source's own `txElements =
[1,2]`/`DC = 0`/`att = 0` field defaults, which nothing downstream in this
port's pipeline reads back (see the function's own header comment).

## Excluded

**`getSteeringAmplitudeCorrection.m`** — reclassified here 2026-09-11
after turning up no callers *anywhere*: not in the plain `.m` tree, not
in `GUI/BeamV0.mlapp`'s own embedded code (checked directly by grepping
the unzipped `.mlapp`'s `matlab/document.xml`, the same way the real
callers of `getRecieveWaveformFromSerial.m`/`generalSonicateMaster.m`
were found in earlier slices), not anywhere in BeamV0's git history.
Its hardcoded data path is also wrong for this project --
`Diadem\Stimulation\steeringAmplitudeField.mat` -- a leftover from
BeamV0 being forked from Diadem, never updated. Genuinely dead, not
merely deferred; not ported.

Dead legacy — Verasonics VSX code, none of it reachable from
`GUI/BeamV0.mlapp` (which drives sonication over serial via
`generalSonicateMaster`). Beam's hardware layer replaces none of this; it
replaces `SerialCom/`.

**`focusedArrayStimulate.m`** — ~230 lines of Verasonics VSX setup ending
in `save(...)` + `evalin('base','VSX')`. Only reachable from
`SingleStimulation.m`.

**`SingleStimulation.m`** — a bare script (hardcoded test values) that
calls `focusedArrayStimulate`.

**`GeneralSonication/setBurstEvent.m`**, **`GeneralSonication/setPauseEvent.m`**
— build Verasonics `Event` / `SeqControl` / `TW` sequences (`'noop'` delays,
`'sync'`). Called by nothing in the tree.

**`MRUserTriggeredSonication/extTriggeredSonication.m`**,
**`MRUserTriggeredSonication/userTriggeredSonication.m`** — Verasonics VSX
setup + `evalin('base','VSX')`, with hardcoded
`C:\Users\Verasonics\Documents\MATLAB\Diadem\Diadem\Verasonics\...` paths
(unedited copies from DiademV0). Not called by the mlapp.

**`MRUserTriggeredSonication/userTriggeredSonicationMaster.m`** — a script
with hardcoded `C:\Users\Verasonics\...` paths and a `loadArray()` call
(itself excluded, see `docs/known_gaps.md`).

**`MRUserTriggeredSonication/waitForUser.m`** — a console `input()` prompt
plus `VsClose` / `VSXquit`.

## Build status

`cmake --preset msvc && cmake --build --preset msvc`, **43/43** tests via
`.\build-msvc\tests\Debug\unit_tests.exe` (8 new this phase). Run the exe
directly — `ctest` is Application-Control-blocked on this machine.

## MATLAB parity

`matlab_verify/` diffs the C++ ports against the real BeamV0 `.m` functions
on the same synthetic array + parameters (atol/rtol 1e-9):
**28/28 matched** (`compare_parity.ps1 -Phase stimulation`, 2026-09-10).
Covers `focusArrayAtPoint`, `calculateMultifrequencySuperpositionDelays`,
`getApodFromAtt`, `defineStimFreqs`, `interp1`,
`pressureToDutyCycleGivenTransmission`, `getPauseIntervals`,
`defineStimParams` (centerFrequencyMHz / delaysCycle / delaysSeconds /
delaysSteering), and `getTxAndBurstEvents` (burst + tx event counts and
times). The `single()`-cast `freqs`/`apods` fields of `defineStimParams`
are not in the parity set (kept double here by design).
