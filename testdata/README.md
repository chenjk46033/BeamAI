# testdata

Checked-in inputs for the parity harness and unit tests. Unlike the
`test_fixture*` files that appear in the repo root, these are authored
fixtures, not artefacts a test writes at run time.

## beamai_fiducials_F040_T1_MRI.csv

Six fiducials placed by hand in BeamAI's registration tab on
`BEAM MRIs/F040/T1_MRI`, exported by "Confirm all located fiducials" at 17
significant digits so an IEEE double round-trips exactly.

Both sides of the registration parity check read this file --
`apps/parity_registration/main.cpp` and
`matlab_verify/verify_registration.m` -- so there is one source of truth for
the comparison's input and no literals to drift apart.

Kept separate from the `beamai_fiducials.csv` the application writes into its
working directory: that one is overwritten by every Confirm, so a check
reading it would silently change meaning whenever someone measures a
different subject.

## beamai_F040_T1_MRI.nii

The same subject's T1 series, de-identified, so the overlay figures reproduce
from a clean checkout without the original DICOM folder. 192x192x192, int16,
~14 MB. `matlab_verify/render_registration_overlay.m` reads it by default.

Written by `apps/make_mri_fixture` (a one-shot tool; a normal build never
needs it) from
`BeamExampleDataDICOMandMatFiles/BEAM MRIs/F040/T1_MRI`. **The source series
is only ever read** -- the tool opens it read-only and writes a new file here.

De-identification is structural, not a tag blacklist: the volume is
round-tripped through `beam::mri::writeNiftiVolume`, and `NiftiHeader` holds
only geometry and datatype -- it has no `descrip`, `aux_file`, `db_name` or
`intent_name` field in which text could survive. The written file contains no
printable strings at all. The original DICOM headers carry a patient name and
a referring physician, which is why `.gitignore` refuses `*.dcm` wholesale and
why only this one derived path is un-ignored.

### Orientation

Stored exactly as `beam::infra::dicom::loadMriRas` returns it -- LR ascending,
AP and IS descending -- and NOT canonicalised to all-ascending axes. An
earlier attempt did canonicalise, producing a self-consistent file that was
nonetheless wrong for this purpose: sampling the MRI under the 160 registered
element centres gave 0.08x the volume mean (the array sitting in air) versus
2.98x for the loader's own orientation (the array sitting on the transducer
blocks visible in the scan). The fiducials were measured in BeamAI's frame, so
the fixture has to keep it.

Note that BeamV0's DICOM path reports the opposite direction on all three
axes for this same series (ratio 1.93). See
`docs/known_gaps_mri.md` -- the registration results are unaffected, because
fiducial and array coordinates are millimetres in the patient frame and never
pass through the voxel storage order.
