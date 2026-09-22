# Conversion status terms

Each BeamV0 `.m` file, as counted in `README.md`'s conversion table, is one of:

- **Converted** — ported to C++ in `libs/`, with tests.
- **Deferred** — not ported *yet*; still real work, but blocked on
  something (a UI framework, an app-state object, hardware) or lower
  priority. Will be revisited.
- **Excluded** — deliberately *not* ported and never will be: dead code
  (no callers), bare scripts with hardcoded paths, stubs, Verasonics
  legacy, or vendored third-party toolboxes replaced by a `libs/` module.

Per-file reasons are in the `docs/known_gaps_*.md` files. "% Converted" =
Converted / (Converted + Deferred) — Excluded files are removed from the
denominator entirely, since they were never going to be ported and
including them would understate progress on the work that's actually in
scope.
