#!/usr/bin/env bash
# Line coverage for the unit tests.
#
# Deliberately outside the test binary: OpenCppCoverage reads debug info and
# watches the process, so nothing is linked into unit_tests.exe and the binary
# measured is the one that normally runs. Release has no usable line mapping,
# so this builds and measures Debug.
#
#   tools/coverage.sh                     whole suite
#   tools/coverage.sh 'TreatmentLog*'     one gtest filter
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-ai"
tool="/c/Program Files/OpenCppCoverage/OpenCppCoverage.exe"
exe="$build/tests/Debug/unit_tests.exe"
out="$root/coverage"

[[ -x "$tool" ]] || { echo "OpenCppCoverage not found at $tool" >&2; exit 1; }

if pgrep -f 'unit_tests.exe|OpenCppCoverage.exe' >/dev/null 2>&1; then
    echo "A test or coverage run is already live; finish or kill it first." >&2
    exit 1
fi

echo "building Debug unit_tests..."
cmake --build "$build" --config Debug --parallel --target unit_tests >/dev/null

rm -rf "$out"
mkdir -p "$out"

# Only our own code: the generated moc/ui files and vcpkg's headers would
# otherwise dominate the totals and tell us nothing.
args=(
    --sources "$(cygpath -w "$root/libs")"
    --sources "$(cygpath -w "$root/apps")"
    --excluded_sources "$(cygpath -w "$root/tests")"
    --excluded_sources "$(cygpath -w "$build")"
    --excluded_sources vcpkg_installed
    # Its one test parses a 124 MB .mat and is excluded below, and nothing in
    # the app calls it -- measuring it would only report a standing 0%.
    --excluded_sources "$(cygpath -w "$root/libs/infra_mat")"
    --export_type "html:$(cygpath -w "$out/html")"
    --export_type "cobertura:$(cygpath -w "$out/coverage.xml")"
)

# TreatmentMigration.TheAuthoritativeTargetListIsBeamV0sTwelve parses a 124 MB
# .mat: milliseconds in Release, but under Debug plus line tracing it does not
# finish. Name a filter explicitly to include it.
filter="${1:--TreatmentMigration.TheAuthoritativeTargetListIsBeamV0sTwelve}"
echo "measuring${filter:+ (filter: $filter)}..."
if [[ -n "$filter" ]]; then
    "$tool" "${args[@]}" -- "$(cygpath -w "$exe")" "--gtest_filter=$filter"
else
    "$tool" "${args[@]}" -- "$(cygpath -w "$exe")"
fi


perl "$root/tools/coverage_report.pl" "$out/coverage.xml" "$out/summary.html"
echo "  per-file:  $out/html/index.html"
echo
