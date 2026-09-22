# MATLAB-parity checks

Every `docs/known_gaps_*.md` notes that the C++ ports are checked by
hand-computed unit tests but not against a real MATLAB run. This directory
closes that gap for the modules it covers: it runs the **real BeamV0
`.m` functions** and the Beam C++ ports on the *same* synthetic input and
diffs the numbers.

Requires a MATLAB install (tested with R2024a) and `../../BeamV0` present.

## Running

```powershell
cmake --build --preset msvc --target parity_arrays parity_correction parity_stimulation parity_mri parity_registration parity_safety
powershell -File matlab_verify\compare_parity.ps1 -Phase arrays
powershell -File matlab_verify\compare_parity.ps1 -Phase correction
powershell -File matlab_verify\compare_parity.ps1 -Phase stimulation
powershell -File matlab_verify\compare_parity.ps1 -Phase mri
powershell -File matlab_verify\compare_parity.ps1 -Phase registration
powershell -File matlab_verify\compare_parity.ps1 -Phase safety
```

`compare_parity.ps1`:
1. runs `parity_arrays.exe` -> `parity_rect.csv` (the input) and
   `parity_cpp.csv` (C++ results, `label,value`)
2. runs `verify_arrays.m` in `matlab -batch` on `parity_rect.csv` ->
   `parity_matlab.csv`
3. diffs the two (default `atol = rtol = 1e-9`)

Working files land in `build-msvc/parity_arrays/`. Exit 0 = every value
matched.

Override paths / tolerance with the script's params
(`-MatlabExe`, `-AbsTol`, `-RelTol`, ...).

## Coverage

**`arrays`** (`verify_arrays.m` / `apps/parity_arrays`) -- 88 values:
- affine matrices (`zRotAffineMatrix`, `defineTranslateAffineMatrix`,
  `applyAffineToRect`)
- `defineArrayStruct` -- element positions / normals / opposing element /
  receive-element counts (elements 1, 50, 100)
- `getElementPositionsFromArrayStruct`, `normalVectorFrom3Points`,
  `calculateRectNormalVector`, `spatiallySampleElement`, `getReceiveElements`,
  `defineArrayData` + `getOpposingElements`,
  `defineArrayTxElements('firstThenSecond')`, `arrayElementsToVSXElements`
- Util: `addVectors`, `angleBetweenTwoVectors`, `vectorRange`,
  `distancePointToLine`, `convertDelaysToCycles`, and camera math
  (`cam2targetSpace`, `mm2pixel`, `mm3Dpnp`, `rotm2eul_simple`)

**`correction`** (`verify_correction.m` / `apps/parity_correction`) -- 35
values: `xcorr`, `corrcoef`, `nanmean`, `xcorrS1ToS2`, `corrSpeedUp`,
`computeMaxCorrelationDelays`, `abs(hilbert)` vs `analyticEnvelope`,
`filterTransmitSignal`, `shiftAndSumWaveforms`, `setAdjustedAttValues`,
`calculateAttenuation`, `findPeakNegativeVoltage`, `defineTxRxScanParams`,
`getReceiveElementsUnderAngle`.

**`stimulation`** (`verify_stimulation.m` / `apps/parity_stimulation`) -- 28
values: `focusArrayAtPoint`, `calculateMultifrequencySuperpositionDelays`,
`getApodFromAtt`, `defineStimFreqs`, `interp1`,
`pressureToDutyCycleGivenTransmission`, `getPauseIntervals`,
`defineStimParams` (centerFrequencyMHz / delaysCycle / delaysSeconds /
delaysSteering), `getTxAndBurstEvents` (burst + tx event counts and times).

**`mri`** (`verify_mri.m` / `apps/parity_mri`) -- 69 values:
`get_ras_xform_fromHdr` (qform + negative-qfac + sform branches, full 3x4),
`getRASAxisVectorsFromNifti` (LR/AP/IS axis endpoints), `getSliceImage`
(sagital / coronal / axial -- confirms `imrotate(.,90)` == `rot90`). The
DICOM-assembly path has no MATLAB counterpart (it replaces vendored
`dicm2nii`), so it's not in the parity set.

**`registration`** (`verify_registration.m` / `apps/parity_registration`) --
89 values: `affineRegistration` (Horn's quaternion method -- R/t/q,
unweighted + BeamV0's weighted path), `getAffineMatrixFromRegistration`
(both `meters2mmFlag` branches), `getTranslationMatrixFromTransducerFiducials`
(X/Y/Z basis from 6 fiducials), `getFiducialPositionFromName`, the
per-point transform of `applyAffineMatrixToFrameData`, and
`setArrayFiducialMarkers` (6-marker geometry off a synthetic 90-element
array). The `ImageBasedModel/` perimeter-walk / scalp-marker functions
(unit-tested, replicated offset-for-offset) are not in the parity set.

**`safety`** (`verify_safety.m` / `apps/parity_safety`) -- 14 values:
`isppa` (both source densities), `mechanicalIndex`, `isptaFromParams`
(burst-duration / burst-interval), `maxSteeringRangeDegrees`,
`kMaxSonicationAmplitudeMPa`. BeamV0's Safety `.m` are `app`/Verasonics
GUI callbacks, so `verify_safety.m` evaluates their acoustic formulae
verbatim from source rather than invoking them (except the two directly
callable limit getters); see `docs/known_gaps_gui.md`.

Last run: 2026-09-10 -- **arrays 88/88, correction 35/35, stimulation
28/28, mri 69/69, registration 89/89, safety 14/14**.

The Correction run caught a real bug: `angleBetweenTwoVectors` used
`std::clamp`, which propagates NaN, where MATLAB's `max(min(x,1),-1)`
treats a 0/0 ratio (zero-length input) as 1 -> angle 0. Fixed;
`getReceiveElementsUnderAngle` now matches.

All non-GUI phases now have a parity target. GUI proper (App Designer
layout, widget callbacks) has no portable math to check.
