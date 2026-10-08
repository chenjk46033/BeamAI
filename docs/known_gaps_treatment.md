# Known gaps: treatment plan

## The Target List's parameters are not verified against BeamV0

**Status: names and order closed; 3 of 12 parameter rows verified.** The Target
List's names and order now match BeamV0 and are pinned by a test. The parameter
values are being read off BeamV0's grid one target at a time; three are done,
nine are still a guess, and the protocol schedule is untouched.

### The values are per-target, and are not the protocol CSVs'

The migration gave each target the first amplitude and duration the BEAM
protocol CSVs schedule it at: 0.75 for most targets, 60 s for SCC4-6. Reading
BeamV0's own stimParamTable grid (protocol `PainACC`) shows that was wrong, and
that the parameters vary per target:

| | Amplitude | startTime | endTime | BD | BI | PD | PI |
|---|---|---|---|---|---|---|---|
| SCC1 | 0.5 | 0 | 30 | 0.03 | 0.7 | 0.005 | 0.01 |
| SCC2 | 0.5 | 0 | 30 | 0.06 | 1.1 | *unconfirmed* | 0.01 |
| SCC4 | 0.5 | 0 | 30 | 0.03 | 0.7 | 0.005 | 0.01 |

SCC1 and SCC4 are `createStimParamTable.m`'s default table value for value;
SCC2 is not. So the stored tables are neither the CSVs' nor a single constant,
and **no target's values can be inferred from another's** -- each has to be
read. An earlier revision of this document claimed all twelve were
`createStimParamTable`'s defaults, generalising from SCC1 and SCC4 alone;
SCC2 disproved it.

SCC2's PD read as `0.05` against PI `0.01`, a 500% pulse duty cycle that
`getISPTAFromStimParams.m` would turn into a nonsense intensity. It is held
out of both the data and the test until re-read.

`startUpFunction.m:38-45` is why the stored table is what the operator sees:
`app.sys.protocolTables` always exists in `defaultSubjectMNIV1.mat`, so line 41
always replaces the freshly created table with the stored one, and the `else`
branch that would keep `createStimParamTable`'s output is dead for any real
session.

The 0.75/0.5 and 30/60/90/180 variation belongs to the protocol *schedule*,
in `data/treatment_protocols.csv`. BeamV0 pushes only the duration from a
schedule onto a target's rows (`updateSonicateSettingsForCurrentSonication.m`
assigns `startTime` and `endTime` down the whole column); the schedule's
`Amplitude` column is **not** applied, because its only call site,
`setStimulationAmplitudeFromResponses`, is commented out. A target therefore
sonicates at its own amplitude unless the operator edits the grid.

Which columns the readings above can actually speak to:

- **BD, BI, PD, PI** -- trustworthy. No write path touches them, so they can
  only be the stored table's own values. This is where SCC2 diverges.
- **endTime** -- written from the schedule, so it says nothing about the stored
  table.
- **amplitude** -- confounded. `PainACC` schedules every target at 0.5, which
  is exactly what was observed, so these readings cannot distinguish the
  target's own amplitude from the schedule's. Only the code argues that
  amplitude is not pushed. Reading SCC1 under `Default`, which schedules it at
  0.75, would settle it: a grid still showing 0.5 confirms the target's own
  value wins.

Pinned by `TreatmentMigration.VerifiedTargetsMatchBeamV0sStimParamGrid`, whose
`kVerifiedRows` and `kUnverifiedTargets` lists are the running record of which
targets have been checked. It needs no sibling checkout, so it never skips.

### A target owns a list of points

`setProtocolTableWithStimParamTable.m` assigns the *entire* grid into one
target's slot, and `createSonicationUpdateTable.m` appends a row by copying the
previous one (`removeSonicationUpdateTable.m` deletes, `sortSonicationTable.m`
sorts by `order`). The grid's `ColumnEditable` in `BeamV0.mlapp` is

```
Order Show  X     Y     Z     Amplitude startTime endTime BD   BI   PD   PI
true  true  false false false true      true      true    true true true true
```

so X/Y/Z are the only read-only columns and every parameter is editable per
row. A target's points therefore differ in their parameters, not in their
placement -- the opposite of the obvious guess. The shipped seed has one point
per target (`createStimParamTable.m`'s own `N = 1`), and a new point starts as
a copy, so they only diverge once the operator edits one.

BeamAI loads the Target List and the protocol schedules from its own
`data/treatment_targets.csv` and `data/treatment_protocols.csv`. Those were
migrated once, by script, from

```
BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/TreatmentProtocols/*.csv
```

**BeamV0 does not read those files.** Two separate reasons:

1. `GUI/SonicationTab/initTreatmentProtocolTables.m` branches on whether the
   loaded session already carries the tables:

   ```matlab
   if isfield(app.sys,'treatmentProtocolTables')
       ... reuse what sys has            % <- taken
   else
       ... getTreatmentProtocolTable(name)
   ```

   `DefaultSubjectV0/defaultSubjectMNIV1.mat` ships a `sys` containing
   `treatmentProtocolTables`, so the first branch wins and no CSV is read at
   startup.

2. The fallback `getTreatmentProtocolTable.m` resolves
   `what('Diadem\GUI\SonicationTab\TreatmentProtocols\')` -- the sibling
   **DiademV0** checkout, not BeamV0's own BEAM folder. Those files differ
   from the BEAM ones for all six shared names, and the function's `switch`
   covers only 6 of the 9 protocols (`PainACCTwoTargets`, `PainAMCCandSCC`
   and `PainSCCandAMCC` are unreachable through it).

So the migration source is neither what BeamV0 loads nor what its own CSV
path would read.

### Where the authoritative data lives

| | BeamV0 field | Holds |
|---|---|---|
| Per-target stim parameters | `sys.protocolTables(i).stimParamTableData` | X/Y/Z, amplitude, start/end time, BD, BI, PD, PI -- indexed by Target List position (`setProtocolTableWithStimParamTable.m`) |
| Protocol schedules | `sys.treatmentProtocolTables(i).blank.Data` | one table per protocol name |
| Target List names | `sys.protocolTables(i).name` | the index that `protocolTables` is keyed by -- also `app.TargetListListBox.Items` |

`startUpFunction.m:41` then does
`app.stimParamTable.Data = app.sys.protocolTables(1).stimParamTableData(:,1:12)`,
which is why `createStimParamTable.m`'s hardcoded defaults are not what the
grid shows either.

### What was recovered without MATLAB

`beam_mat_info <session.mat> --plan` reads the plan's *index* straight out of
`sys`. The names are ordinary char arrays; only the table payloads are out of
reach. Three sessions agree:

| Session | Target List | Protocols |
|---|---|---|
| `DefaultSubjectV0/defaultSubjectMNIV1.mat` | SCC1-6, aMCC1-6 | 6 |
| `BeamExampleDataDICOMandMatFiles/SUtahF017Visit1…` | SCC1-6, aMCC1-6 | 6 |
| `ParticipantMatfilesDiadem/SMOSIC001Visit1…` (Diadem) | SCC1-6, aMCC1-6 | 4 |

So:

- **The Target List is twelve names**, in that order, everywhere. The
  migration's 28 was an artefact of deriving the list from the protocol CSVs,
  which schedule `NACC_*`, `VPL_*` and `T1`-`T6`. `data/treatment_targets.csv`
  is now the twelve; the other sixteen are reported by
  `TreatmentTargetStore::danglingTargetNames()` and the Treatment Plan page
  names them when such a protocol is selected.
- **BeamV0 carries six protocols**, not the nine CSV filenames: `MDD`,
  `PainACCTwoTargets` and `PainVPL` have a CSV and no `treatmentProtocolTables`
  entry. Diadem carries four -- no `PainSCCandAMCC` / `PainAMCCandSCC`.
- Every protocol holds 15 sessions, corroborating `app.maxTreatmentSessions`
  as `TreatmentSessionStore` ports it.

Pinned by `TreatmentMigration.TheAuthoritativeTargetListIsBeamV0sTwelve`,
which skips when the sibling checkout is absent.

### What is still missing

**The protocol schedule.** `treatmentProtocolTables(i).blank.Data` and
`.sessions(j).Data` are MATLAB `table` objects, stored as MCOS references under
the file's `#subsystem#`; matio resolves them to nothing, and
`loadLegacyBeamPlan` reports that as `tablePayloadsUnreadable` rather than
returning a plausible-looking blank. So `data/treatment_protocols.csv` -- the
Name/Duration/Amplitude of all 208 entries -- is still only verified against
the BEAM CSVs, which BeamV0 does not read.

That check is worth something: it is entry-for-entry and mutation-tested, and
it caught a UTF-8 BOM discrepancy between the PowerShell migration and a C++
reader. But the per-target values turned out to differ from the CSVs entirely,
so the same could be true here and the test would not show it.

**Nine of the twelve target rows.** SCC3, SCC5, SCC6 and aMCC1-aMCC6 have never
been read off BeamV0, and SCC2 showed that they cannot be inferred from the
three that have. They currently hold `createStimParamTable.m`'s defaults, which
is a guess that is already known to be wrong for at least one target in three.

### To close this

1. Read the remaining nine targets' rows off BeamV0's stimParamTable grid and
   add them to `kVerifiedRows`. Reading one target at a time is 9 more
   round-trips, so this is now the point where the export is cheaper.
2. Run `matlab_verify/export_treatment_tables.m`. It dumps every
   `stimParamTableData` and every protocol table at once, which settles both
   remaining items; then repoint `ProtocolsMatchBeamV0EntryForEntry` at the
   export so it becomes a parity test rather than a transcription check.
3. Decide what to do with `MDD`, `PainACCTwoTargets` and `PainVPL`: BeamV0
   ships a CSV for each but never loads it, and each schedules targets no
   Target List defines. They are the source of all 16 dangling names.

Related: `docs/known_gaps_session.md` -- `protocolTables` and
`treatmentProtocolTables` are two of the 36 `sys` fields BeamAI does not
persist.
