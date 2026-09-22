# Known gaps and deliberate exclusions — Util phase

Same convention as `docs/known_gaps.md` (Arrays phase). Beam's own port of
`BeamV0/GUIMatlab/BEAM/Util/`, traced from that folder's real `.m` files.
This is the second phase, following the one-module-at-a-time dependency
order Diadem's migration plan uses (`Diadem/README.md`) — Util's ports are
the vector/geometry helpers whose real consumers live in the not-yet-ported
Correction and Stimulation modules.

## Ported this phase

- **`addVectors`**, **`angleBetweenTwoVectors`**, **`convertDelaysToCycles`**
  — `libs/util/geometry_math.{hpp,cpp}`, alongside `vectorRange` and
  `distancePointToLine` (pulled in during the Arrays phase). Consumers:
  `Correction/getReceiveElementsUnderAngle.m` (`addVectors`,
  `angleBetweenTwoVectors`), `Stimulation/defineStimParams.m`
  (`addVectors`, `convertDelaysToCycles`).
  - `convertDelaysToCycles.m` is byte-identical between `Util/` and
    `Stimulation/` in BeamV0 — ported once as the canonical implementation,
    not duplicated. `frequencyHz` is taken as a scalar (every real call
    site passes one); MATLAB's `.*` would also broadcast against a
    same-length vector, but nothing does that.
  - `addVectors.m`'s `reshape(...,[],1)` on both inputs is a MATLAB-ism for
    tolerating mixed row/column orientation; Eigen's type system makes it
    unnecessary, so the C++ port is a plain `u1 + u2` and callers pass
    column vectors.

- **`getOpposingElements`** — ported into `libs/array/`
  (`array_struct.{hpp,cpp}`), not `libs/util/`, because it needs the
  `ArrayData` type and `util` cannot depend on `array`. Same placement
  decision Diadem made (`libs/common` vs `libs/array`). Consumers:
  `Correction/getReceiveElementsUnderAngle.m`,
  `Correction/localizeArrays/getLocalizeArraysSystemOfEquations.m`.
  - The BeamV0 source leaves `currentArray` unassigned if the element
    number isn't found in any array, then errors at runtime when it's
    used. The port throws `std::invalid_argument` at that point instead —
    equivalent loud failure, not a silent wrong answer.
  - The source loops over `1:length(arrayData.array)` generally; Beam's
    `ArrayData` fixes that at 2 arrays, but the port keeps the general loop
    over `arrayData.array` so the translation stays line-for-line.

## Already ported in the Arrays phase (not re-done here)

- **`vectorRange`**, **`distancePointToLine`** — `libs/util/geometry_math`,
  pulled in as Arrays' own prerequisites (see `docs/known_gaps.md`).
- **`normalVectorFrom3Points`** — BeamV0 has identical copies in `Util/`
  and `Arrays/`; the single canonical implementation lives in
  `libs/array/geometry.{hpp,cpp}`.

## Ported later (`libs/util/camera.{hpp,cpp}`)

**`cam2targetSpace.m`**, **`mm2pixel.m`**, **`mm3Dpnp.m`**,
**`rotm2eul_simple.m`** — camera/pixel↔3D coordinate math. Originally
deferred (their one consumer,
`BEAM/Registration/ImageBasedModel/getImageData.m`, was unported), but
they're pure math with no dependencies, so ported now as `cam2targetSpace`,
`mm2pixel`, `mm3Dpnp`, `rotm2eulSimple`.

## Excluded from this phase, with reasons

**`fixSlots.m`**, **`managePoints.m`** — interactive figure code
(`imshow`, `getpts`, `drawpoint`, `uicontrol`/`uiwait`, mouse-callback
point editing). Manual marking UI, not computation. Belongs to the GUI
phase if anything, and even then needs a real interaction design, not a
translation.

**`getOffColor.m`** — returns a hardcoded RGB triple `[0.85 0.33 0.10]`
for UI styling. Presentation constant, no computation.

**`show_transducer.m`** — `surf`/`quiver3`/`text` 3D plotting of the
transducer surface. Visualization only.

## Build status

Builds and passes cleanly: `cmake --preset msvc && cmake --build --preset msvc`,
18/18 tests passing via `.\build-msvc\tests\Debug\unit_tests.exe` (run the
exe directly, not `ctest` — blocked by Application Control on this machine,
see project memory). 5 new tests over the Arrays phase's 13
(`AddVectors`, `AngleBetweenTwoVectors` ×2, `ConvertDelaysToCycles`,
`ArrayData.GetOpposingElementsReturnsOtherArraysElementNumbers`).

## MATLAB parity

Covered by `matlab_verify/` alongside Arrays (88/88 values matched,
2026-09-10): `addVectors`, `angleBetweenTwoVectors`, `vectorRange`,
`distancePointToLine`, `convertDelaysToCycles`, and the camera math
(`cam2targetSpace`, `mm2pixel`, `mm3Dpnp`, `rotm2eul_simple`) all diffed
against the real BeamV0 `Util/` functions at atol/rtol 1e-9.

The Correction parity run caught a bug here: `angleBetweenTwoVectors` used
`std::clamp(x, -1, 1)`, which returns NaN when `x` is NaN (a 0/0 ratio
from a zero-length input). MATLAB's `max(min(x, 1), -1)` ignores NaN, so
`min(NaN, 1) == 1` -> `acos(1) == 0`. Fixed to special-case NaN -> 1.
Only affects degenerate zero-vector input (which `getReceiveElementsUnderAngle`
feeds for the transmit element itself).
