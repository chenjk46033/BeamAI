# BeamAI

C++/Qt port of the MATLAB **BeamV0** focused-ultrasound treatment application.

The application is `workflow_app`: one window that walks a session through its
stages — Case Setup, System Check, Imaging, Registration, Coupling, Correction,
Treatment plan, Safety Review, Treatment, Report. BeamV0 does the same work from
a single Sonicate tab; the stages here are the same logic re-homed, with firing
gated behind Safety Review rather than always available.

## Build and run

Prerequisites: Visual Studio 2022 (x64), CMake 3.21+, and vcpkg at
`C:/vcpkg` (the toolchain path the presets use). Dependencies come from
`vcpkg.json` and are restored on the first configure — the first build takes a
while because Qt is built from the manifest.

**Close any running copy of the app before building.** The linker cannot
overwrite `workflow_app.exe` while it is open, and the build fails with
`LNK1168: cannot open ... for writing`.

Configure once:

```powershell
cmake --preset ai
```

Build and run Release:

```powershell
cmake --build build-ai --config Release --target workflow_app
.\build-ai\apps\Release\workflow_app.exe
```

Build and run Debug:

```powershell
cmake --build build-ai --config Debug --target workflow_app
.\build-ai\apps\Debug\workflow_app.exe
```

Tests — build the `unit_tests` target and run the executable directly
(`ctest` is AppLocker-blocked on this machine):

```powershell
cmake --build build-ai --config Debug --target unit_tests
.\build-ai\tests\Debug\unit_tests.exe
```

Equivalent build presets exist for both configurations:

```powershell
cmake --build --preset ai            # Debug
cmake --build --preset ai-release    # Release
```

## Data the app reads and writes

| Path | What it is |
|---|---|
| `data/treatment_targets.csv` | Seed Target List — BeamV0's twelve targets and their parameters |
| `data/treatment_protocols.csv` | Seed protocols — the six BeamV0 schedules |
| `data/sham_burst.wav` | Masking audio a sham plays, converted from `sonicationSound.mat` |
| `data/runtime/treatment_targets.csv` | The live Target List, as edited in the app |
| `data/runtime/treatment_protocols.csv` | The live protocols |
| `data/runtime/treatment_log.csv` | The visit log: what was actually delivered, in `setLogInfoToCSV.m`'s column shape |
| `data/runtime/treatment_visit_state.csv` | Which sonication each visit of each protocol is on |

`data/runtime/` is the working copy and takes precedence; targets and protocols
fall back to the seeds in `data/` when a runtime copy is missing, so deleting a
runtime file restores the shipped values. The log has no seed — delete
`treatment_log.csv` and `treatment_visit_state.csv` (with the app closed) to
start a patient from a clean slate.

MRI test data lives in `testdata/` (`beamai_F040_T1_MRI.nii` plus fiducial
CSVs). The imaging dialogs open in the site's MRI library when it is present and
fall back to the usual default otherwise.

## Conversion status

_As of 2026-09-21_

| # | Category | `.m` files | Converted | Deferred | Excluded | % `.m` logic converted |
|---|----------|-----------:|----------:|---------:|---------:|------------:|
| 1 | Arrays       | 19  | 16 | 0  | [3](docs/known_gaps.md#excluded-from-this-phase-with-reasons) | 100% |
| 2 | Util         | 15  | 11 | 0  | [4](docs/known_gaps_util.md#excluded-from-this-phase-with-reasons) | 100% |
| 3 | Correction   | 22  | 17 | 0  | [5](docs/known_gaps_correction.md#excluded) | 100% |
| 4 | Stimulation  | 22  | 13 | 0  | [9](docs/known_gaps_stimulation.md#excluded) | 100% |
| 5 | MRI          | 22  | 9  | 1  | [12](docs/known_gaps_mri.md#excluded) | 90% |
| 6 | Registration | 27  | 18 | 0  | [9](docs/known_gaps_registration.md#excluded) | 100% |
| 7 | SerialCom    | 8   | 8  | 0  | 0 | 100% |
| 8 | Sham         | 3   | 2  | 0  | [1](docs/known_gaps_serialcom_sham.md#excluded-sham) | 100% |
| 9 | GUI          | 88  | 43 | 0  | [45](docs/known_gaps_gui.md#excluded-dead-code) | 100% (logic) / **[~75% (app)](docs/gui_widget_inventory.md)** |
|   | **Total**    | **226** | **137** | **1** | **88** | 99% (logic) / **~75% (GUI app)** |

## Test results

- **Unit tests** — 300/300 passing, Debug and Release.
- **Per-function coverage** — [one row per converted function](docs/unit-test-coverage.md),
  its test, and result.
- **MATLAB parity** — the C++ ports diffed against the real BeamV0 `.m`
  functions on the same input (atol/rtol 1e-9), every non-GUI phase:
  Arrays + Util 88/88, Correction 48/48, Stimulation 28/28, MRI 69/69,
  Registration 89/89, Safety 14/14. See `matlab_verify/`.

## Known gaps

Session save/load to `.mat` — the operator manual's File → Load Subjects — is
scoped but not built; see [docs/known_gaps_session.md](docs/known_gaps_session.md).
Per-phase reasons for everything deferred or excluded are in
[Deferred & excluded](docs/deferred-and-excluded.md).

## The other app

`beam_app` is the earlier single-window port and still builds from this tree
(`--target beam_app`, `--designer-startup` for its startup screen). `workflow_app`
is the one under active development.
