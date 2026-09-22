# Known gaps and deliberate exclusions — Correction phase

Same convention as `docs/known_gaps.md` / `docs/known_gaps_util.md`. Beam's
own port of `BeamV0/GUIMatlab/BEAM/Correction/` (15 files) and its
`localizeArrays/` subfolder (7 files), traced from those `.m` files. Third
phase, per Diadem's migration order (Correction after Arrays + Util).

Most of these functions have no live caller in the BeamV0 app — they're
either dead code in the current GUI or invoked only from `localizeArrays/`.
Ported anyway where the math is self-contained, same call the Arrays/Util
phases made for their consumer-less helpers.

## Ported this phase (`libs/correction/`)

**Cross-correlation / DSP — `correlation.{hpp,cpp}`**
- `xcorrS1ToS2`, `corrSpeedUp`, `computeMaxCorrelationDelays`, on top of
  three primitives ported from MATLAB built-ins: `xcorr` (direct O(n²), not
  FFT), `corrCoefficient` (= `corrcoef(a,b)(1,2)`), `nanmean`.
- `corrSpeedUp`'s `varargin` bounds override is dropped (no call site uses
  it); default `lb=0, ub=-150` hardcoded. Modified-signal outputs of both
  `corrSpeedUp` and `xcorrS1ToS2` dropped — never captured.

**Waveforms — `waveforms.{hpp,cpp}`**
- `shiftAndSumWaveforms`. `varargin` weights → optional argument (default
  all-ones).

**Scan params — `scan_params.{hpp,cpp}`**
- `defineTxRxScanParams`. Literal constants, evaluated.

**Receive geometry — `receive_elements.{hpp,cpp}`**
- `getReceiveElementsUnderAngle`. Deps (`addVectors`,
  `angleBetweenTwoVectors`, `getOpposingElements`) came from the Util
  phase. `targetPos`'s `'norm'` string case → `std::nullopt`.

**Attenuation — `attenuation.{hpp,cpp}`**
- `setAdjustedAttValues` — faithful port of the descending-sort /
  deficit-tracking loop; `disp(...)` prints and the unused `maxTotal`
  local dropped.
- `calculateAttenuation` — `varargin` → `usePeakNegativeVoltage` bool. The
  commented-out `'noise'` branch is not ported (dead in the source).

**Peaks — `peaks.{hpp,cpp}`**
- `findPeaks` (= bare `findpeaks(v)`: strict interior local maxima) and
  `findPeakNegativeVoltage`. The latter is actually
  `BeamV0/BEAMANALYSIS/analysisUtil/findPeakNegativeVoltage.m`, not a
  `Correction/` file — pulled in as a dependency the way the Arrays phase
  pulled in Util helpers, so it is not counted against the 22.

**Array localization helpers — `localize.{hpp,cpp}`**
- `distance` (single-point case only — the row-wise-matrix generality
  isn't used anywhere), `updateArrayPositions`, `nonlinRelativeDistanceFun`
  (the residual `lsqnonlin` would minimize).

## Ported later (unblocked-defer sweep)

**`filterTransmitSignal.m`** — `signal.{hpp,cpp}`, now that `hilbert` is
available: `analyticEnvelope` computes `abs(hilbert(x))` via Eigen's
bundled kiss-fft (MATLAB's FFT method), and `filterTransmitSignal` does
the threshold / run-detection / blanking on top. The source's bare
`try`/`catch` around the blank (leave the waveform untouched if the window
runs off an end) is preserved.

**`localizeArrays/getLocalizeArraysSystemOfEquations.m`** —
`localize.{hpp,cpp}`. `wvData` is `std::vector<Eigen::MatrixXd>` (one
matrix per transmit element, `nSamples x nElements`), so `wvData(:,k,i)` ->
`wvData[i-1].col(k-1)`. The source's dead `getOpposingElements(arrayData, i)`
call (result never used) is dropped. `element.receiveElements(1:20)` takes
`min(20, size)` rather than erroring on a short list.

**`localizeArrays/localizeArraysMaster.m`** — its solver core is now
`localizeArrays` (`localize.{hpp,cpp}`): the
`lsqnonlin(nonlinRelativeDistanceFun, initial, lb, ub)` fit, as a
hand-rolled **bounded Levenberg-Marquardt** (LM step + projection onto the
`[lb, ub]` box; no new dependency -- Eigen's unbounded LM would not respect
the source's bounds). Bounds are built exactly as the script does (c in
`c0 +/- 20`; every element on the top/bottom row of the 9-per-column layout
pinned; the rest `+/- 2.3 mm`). The script's hardcoded-path `.mat`
load/save, `plot3`/`show_transducer`, and `transmitReceiveScan(2)` calls
are excluded -- those are the file-I/O / plotting / hardware wrapper, not
the fit.

## Ported in the GUI phase (`libs/correction/receive_waveform`)

**`getRecieveWaveformFromSerial.m`** — the "Run Correction" button's real
hardware path (operator manual, Correction Tab section; called from
`RunCorrectionButtonPushed`, embedded in `BeamV0.mlapp` itself -- not found
by grepping the plain `.m` tree, only by grepping the App Designer file's
own embedded code). `butter`/`filter` are MATLAB toolbox builtins, not
separate `.m` files, so implementing them (`butterBandpass`, a from-scratch
buttap -> lp2bp -> bilinear design, and `filterIir`, the direct-form-II-
transposed recursion) was new infrastructure needed to port this file, not
itself a `.m` port. Cross-checked against a real MATLAB
`butter(2, [200000 400000]/(1316800/2))` + `filter(B,A,·)` run --
`ButterBandpass.MatchesMatlabReferenceCoefficients` pins the 5 coefficients
byte-for-byte, and `matlab_verify/verify_correction.m` now diffs the full
filtered-waveform output too (see MATLAB parity below). Also ported:
`parseCorrectionWaveformLine`, the per-line comma-separated float parsing
from the source's serial-polling loop (that loop itself -- `NumBytesAvailable`
polling, `readline` -- is hardware glue, not ported, matching every other
serial function here).

Disclosed deviation: the source's own `N < 2` fallback is broken (a
typo'd `ch01rcv` instead of `ch0rcv` leaves `ch0rcv` a scalar, then the
next line indexing it would itself error in MATLAB) -- `splitAndFilterReceiveWaveform`
throws `std::invalid_argument` there instead of replicating a crash. The
source's own off-by-one (index N appears in both channel halves) is
reproduced as-is.

Wired to a real button (`BeamMainWindow::setRunCorrectionHandler`) the
same way Sonicate/Register are: tries the first available serial port,
sends `"Correction"`, accumulates the reply, and on success replaces the
Correction tab's (until then synthetic) charts with the real
split/filtered data -- reporting "no serial port available" rather than
fabricating a result if none is connected. Verified via `apps/beam_app --check`
(`correction ch0Len=401 ch1Len=402` from a simulated 20-line/50-value-per-line
reply) and a clean interactive run. (The Correction tab's chart grid got
the same nested-layout fix the Sonicate tab's pulse chart got in an
earlier slice -- `setCorrectionCharts`'s clear-and-rebuild loop would
otherwise have swept away this new button/status label too.)

**`initializeCorrectionValues.m`** (2026-09-12) — its one piece of real
content beyond the already-ported `updateAttenuationPlots` call
(`computeRfPlotYLimit`, above): the literal startup defaults --
`app.sys.arrayData.arrayTotal.medianAttenuation = [1,1]` and
`app.sys.RTT(1)`'s 5 fields, all 1 -- the state before any real correction
has ever run. Ported as `beam::gui::correctionInitialState()`
(`libs/gui/correction_tab_presenter`), exposed via `apps/beam_app --check`
(`correctionInitAtt=1.0`). Not wired as the Correction tab's actual boot
display -- that already deliberately shows synthetic post-run-looking data
for demo purposes (a pre-existing UX choice, not changed here).

## Deferred

**`getTransmissionAfterThroughTransmit.m`** — **ported** during the GUI
Correction-tab slice as `throughTransmitAmplitude(ch0rcv, ch1rcv)` in
`libs/correction/signal` (the two waveforms passed in instead of read off
`app.sys.RTT(1)`). Its `BEAMANALYSIS/analysisUtil/getTxRxSignalAmplitude.m`
dependency came along as `txRxSignalAmplitude` (out-of-`Correction/`
helper, like `findPeakNegativeVoltage`). See `docs/known_gaps_gui.md`.

## Excluded

**`correctionMain.m`** — empty body.

**`transmitReceiveScan.m`** — body is the single token `app.` — a syntax
error; the source cannot run.

**`calculateAverageElementAttenuation.m`** — ignores all three inputs,
returns hardcoded `[0.3, 0.4]`. A stub.

**`localizeArrays/getElementPositionsFromArrayStruct.m`** — a second copy
of `Arrays/getElementPositionsFromArrayStruct.m`, already ported in the
Arrays phase (`libs/array/geometry.hpp`).

**`localizeArrays/localizeArraysGenerateNewArray.m`** — a script (not a
function) with hardcoded `C:\Users\Verasonics\...` paths that `load`s and
`save`s `.mat` files. Same class as `Arrays/loadArray.m`.

## Build status

`cmake --preset msvc && cmake --build --preset msvc`; run the exe directly
(`.\build-msvc\tests\Debug\unit_tests.exe`) — `ctest` is
Application-Control-blocked on this machine.

## MATLAB parity

`matlab_verify/verify_correction.m` diffs the C++ ports against the real
BeamV0 functions (and MATLAB's own `xcorr` / `corrcoef` / `hilbert` /
`findpeaks` / `butter` / `filter`) on synthetic waveforms — **48/48 values
matched** at atol/rtol 1e-9 (2026-09-11, up from 35/35 on 2026-09-10),
covering `xcorr`, `xcorrS1ToS2`, `corrSpeedUp`, `computeMaxCorrelationDelays`,
`analyticEnvelope`, `filterTransmitSignal`, `shiftAndSumWaveforms`,
`setAdjustedAttValues`, `calculateAttenuation`, `findPeakNegativeVoltage`,
`defineTxRxScanParams`, `getReceiveElementsUnderAngle`, and (new)
`butterBandpass`'s 5+5 `[B,A]` coefficients plus 3 samples of `filterIir`
applied to a real waveform through MATLAB's actual `filter`. The 2026-09-10
run found and fixed the `angleBetweenTwoVectors` NaN bug (see
`docs/known_gaps_util.md`).

`getLocalizeArraysSystemOfEquations` and `localizeArrays` are not in the
parity set — the former needs a 3D receive-waveform fixture that's awkward
to feed identically, the latter uses a different solver than MATLAB's
`lsqnonlin` so an exact match isn't expected.
