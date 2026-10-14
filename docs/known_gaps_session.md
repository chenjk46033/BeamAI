# Known gaps: sessions

## BeamV0 sessions are not implemented

A BeamV0 session is a `.mat` holding one `sys` struct with **38 fields**:

```
mrPath  aImg  aRes  headers  ax  ay  az  window  txRxFreeField  txRxSubject
arrayData  speedOfSound  arrayDataPreLocalize  simulation  log  target  RTT
stimParams  stimParamTable  fiducialMarkerTable  anatomy  referenceCoordinate
arrayFramePosition  niftiInfo  focusImage  niftiinfo  niftiV  MNI
protocolTables  treatmentProtocolTables  frame  sessionNumber  originArrayData
Subject  registration  imageRegStruct  visitNumber  CRF
```

BeamAI reads **two** of them. `beam::infra::mat::loadLegacyBeamMri` extracts the
MRI (`aImg`, `ax`, `ay`, `az`) and the six fiducials; everything else is
ignored. The Imaging tab's "Load Beam MRI session…" button is that import, not
session restore, and is kept as a standing reminder that the real thing is
still missing.

The operator's manual states the requirement directly:

> The software logs the position, stimulation parameters, and correction values
> for every sonication during a stimulation session. Saving parameters will
> export this log, the registration values, and all other settings to a .mat
> file in a user-chosen directory. Subsequent sessions can load that saved
> session data back into the GUI by clicking 'File->Load Subjects'.

The first sentence is built. `TreatmentLog` records position, the parameters as
sent, start and end timestamps, the operator's responses and the correction
values, as one row per scheduled sonication per visit.

The second and third are not. Nothing writes `.mat`, and `Save Log...` exports
the log alone, as CSV. Writing a session means writing MATLAB `table` objects
into the file's `#subsystem#` -- which is also why `loadLegacyBeamPlan` cannot
read them, and why the protocol tables had to be exported from MATLAB by hand.

What a session carries that nothing in BeamAI currently persists:

| | Where it lives in BeamV0 | BeamAI today |
|---|---|---|
| MRI image | `aImg`, `ax/ay/az` | loaded from DICOM or NIfTI each time |
| Fiducials | `fiducialMarkerTable` | `Save fiducials…` / `Import fiducials…` CSV |
| Lock positions | `frame` | in the same CSV |
| Registered array | `arrayData`, `originArrayData` | recomputed from the fiducials |
| Sonication protocols | `protocolTables`, `treatmentProtocolTables` | mirrored into `data/treatment_targets.csv` and `treatment_protocols.csv` |
| What each sonication delivered | `sessions(visit).Data`, exported by `setLogInfoToCSV.m` | `treatment_log.csv`, in that function column shape, plus a Save Log export |
| Correction / transmit data | `txRxSubject`, `txRxFreeField` | the per-sonication values reach the log (AttenuationVal, DC); the measured transmit arrays do not |
| Subject and visit identity | `Subject`, `visitNumber`, `sessionNumber`, `CRF` | not persisted |
| Targets and anatomy | `target`, `anatomy`, `MNI`, `focusImage` | not persisted |

The fiducial CSV deliberately stores inputs rather than results — the
registered array is a pure function of the MRI, the six fiducials and the two
lock positions, so it is recomputed on load and cannot drift from the code. A
full session would have to decide the same question for every row above.

## Migration

`beam_mat_info <session.mat> --csv <out.csv>` writes a session's fiducials in
the format `Import fiducials…` reads. The seven sessions in
`BeamExampleDataDICOMandMatFiles` have been exported this way, so their
measurements are usable without the `.mat` reader.

One file has no DICOM beside it and so is still reachable only through the
button: `SUtahF017Visit1DateM9D24Y2026.mat`. Its image exists nowhere else.
