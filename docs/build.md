# Build and run (Release)

All commands from the repo root, in bash.

## One-time configure

Only needed on a fresh clone, or after the repo folder is renamed or moved --
CMake stores absolute paths, so a rename makes the cache and the test binary
point at a directory that no longer exists.

    rm -rf build-ai/CMakeCache.txt build-ai/CMakeFiles
    cmake --preset ai

`build-ai/vcpkg_installed/` holds Qt, DCMTK and ITK. Leave it alone and the
configure takes about 30s; delete it and vcpkg rebuilds everything.

## Build

    cmake --build build-ai --config Release --target beam
    cmake --build build-ai --config Release --target unit_tests

Both at once:

    cmake --build build-ai --config Release --target beam unit_tests

Omitting `--target` also builds every parity and probe app, which is slower and
rarely what you want.

## Run

    ./build-ai/apps/Release/beam.exe
    ./build-ai/tests/Release/unit_tests.exe

One test suite only:

    ./build-ai/tests/Release/unit_tests.exe --gtest_filter='HardwareLink.*'

## Coverage

Needs Debug, because line mapping comes from the PDBs.

    bash tools/coverage.sh

## When a build fails

`LNK1104` or `LNK1168` on beam.exe, or `Permission denied` copying
`qwindows.dll`: the app is still running. Close it.

A build that seems to hang for ten minutes is usually an orphaned vcpkg holding
its lock:

    tasklist | grep -i vcpkg
    taskkill //F //IM vcpkg.exe
