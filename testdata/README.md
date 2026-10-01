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
