#!/usr/bin/env bash
# Regenerates docs/unit-tests.md from a real test run.
#
# Release, because that is the configuration the suite is normally run and
# reported in; the inventory records the run it was built from.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-ai"
exe="$build/tests/Release/unit_tests.exe"
xml="$build/tests/Release/gtest_results.xml"
doc="$root/docs/unit-tests.md"

echo "building unit_tests..."
cmake --build "$build" --config Release --parallel --target unit_tests >/dev/null

echo "running..."
# A failing suite still produces the XML, and a failed test belongs in the
# inventory, so the non-zero exit is recorded rather than fatal.
set +e
"$exe" --gtest_output="xml:$(cygpath -w "$xml")" >/dev/null
status=$?
set -e
[[ -f "$xml" ]] || { echo "no results at $xml" >&2; exit 1; }

perl "$root/tools/test_inventory.pl" "$xml" "$doc" "$(cygpath -w "$root")" "$root/docs/unit-tests.html"
[[ $status -eq 0 ]] || echo "  note: the suite exited $status — see the Result column"
