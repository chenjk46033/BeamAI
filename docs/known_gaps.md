# Known gaps and deliberate exclusions — Arrays phase

Mirrors the convention `Diadem/README.md` uses ("Known gaps and deliberate
deviations from the MATLAB source"), written independently for Beam's own
port of `BeamV0/GUIMatlab/BEAM/Arrays/`. Nothing here references Diadem's
actual exclusions or code — these are Beam's own, traced from BeamV0's real
`.m` files.

## Excluded from this phase, with reasons

**`loadArray.m`** — hardcoded, lab-machine-specific absolute paths
(`C:\Users\Verasonics\Documents\MATLAB\Diadem\...`, `C:\Users\Tom\Documents\MATLAB\UltrasoundArrays\...`),
tried in sequence via a bare `try`/`catch`. Not portable as written on any
other machine. If this capability is needed later, it needs a real
configurable-path design, not a translation of this function.

**`adjustTransducerSeparation.m`** — reads `sys.arrayData`, but `sys` is
never a parameter, global, or otherwise defined anywhere in the function
(signature is `adjustTransducerSeparation()`, zero arguments). Calling this
as written would throw `Undefined variable 'sys'` in MATLAB itself before
doing anything else. Also has no output argument, so even its final
`arrayData = defineArrayData(rect);` line computes a value nothing outside
the function can ever see. Not a candidate to port — the real MATLAB source
can't run.

**`transformArrayDataToHFSRAS.m`** — empty-bodied: the function signature
and a block of comments/TODOs, no executable statements at all beyond
implicitly returning its input unchanged. Nothing to port yet.

## Ported later (Registration phase)

**`applyAffineToArrayData.m`** — calls `setArrayFiducialMarkers`
(`Registration/`), so it was deferred out of the Arrays-only slice. Now
ported as `beam::registration::applyAffineToArrayData`
(`libs/registration/array_transform.{hpp,cpp}`) — placed in
`libs/registration` because `libs/array` can't depend on it. Returns
`{ArrayData, std::vector<FiducialMarker>}`.

## Ported this phase

Everything else in `Arrays/`: `defineTranslateAffineMatrix`,
`{x,y,z}RotAffineMatrix`, `applyAffineToRect`, `normalVectorFrom3Points`,
`calculateRectNormalVector`, `calculateArrayNormalVectors`,
`getElementPositionsFromArrayStruct`, `spatiallySampleElement`,
`getReceiveElements`, `defineArrayStruct`, `defineArrayData`,
`defineArrayTxElements`, `arrayElementsToVSXElements` — plus two Util
dependencies pulled in because Arrays needs them: `vectorRange` and
`distancePointToLine` (`libs/util/`).

`defineArrayData`'s hardcoded 150kHz frequency / 60mm element dimensions are
Beam's real values (confirmed directly in `BeamV0/GUIMatlab/BEAM/Arrays/defineArrayData.m`)
— ported as literal constants, not parameterized, since there's no
multi-product config layer in this codebase to parameterize them into (see
`docs/platform/02-proposed-architecture.md`'s superseded note for why not).

## Build status

Builds and passes cleanly: `cmake --preset msvc && cmake --build --preset msvc`,
13/13 tests passing via `.\build-msvc\tests\Debug\unit_tests.exe` (run the
exe directly, not `ctest` — blocked by Application Control on this machine,
see project memory). Two real MSVC/Eigen gotchas hit and fixed along the
way, worth knowing before writing more of this port: `MatrixBase::cross()`
needs `#include <Eigen/Geometry>` (`<Eigen/Core>` alone compiles but fails
template resolution with a confusing error), and `.determinant()` needs
`#include <Eigen/LU>`.

## MATLAB parity

`matlab_verify/` runs the real BeamV0 `Arrays/` (and `Util/`) functions and
the C++ ports on the same synthetic rect and diffs the numbers:
**88/88 values matched** at atol/rtol 1e-9 (2026-09-10) -- covering
`defineArrayStruct` (positions / normals / opposing / receive counts),
`applyAffineToRect`, the rotation/translation matrices,
`getElementPositionsFromArrayStruct`, `normalVectorFrom3Points`,
`calculateRectNormalVector`, `spatiallySampleElement`, `getReceiveElements`,
`defineArrayData` + `getOpposingElements`, `defineArrayTxElements`,
`arrayElementsToVSXElements`.

Still synthetic input, not BeamV0's real `.mat`/Field-II array geometry --
but both sides now provably compute the same thing on it.
