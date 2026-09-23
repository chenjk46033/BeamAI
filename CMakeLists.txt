cmake_minimum_required(VERSION 3.20)
project(Beam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Decided 2026-09-09: Beam is converted from BeamV0 as fully separate code --
# no dependency on Diadem's libs/, no shared platform layer. An earlier
# version of this file depended on Diadem/libs/common (see git history if
# that ever needs revisiting); that dependency and the apps/reuse_proof/
# proof-of-concept that exercised it have been removed.

find_package(Eigen3 REQUIRED NO_MODULE)

# libs/ and apps/ subdirectories get added here as the BeamV0 -> Beam port
# produces them, following the same one-module-at-a-time phase structure
# Diadem used (see Diadem/README.md's Migration plan) -- as a process
# template only, not as a code or build dependency.
add_subdirectory(libs/util)
add_subdirectory(libs/array)
add_subdirectory(libs/correction)
add_subdirectory(libs/stimulation)
add_subdirectory(libs/mri)
add_subdirectory(libs/infra_dicom)
add_subdirectory(libs/infra_mat)
add_subdirectory(libs/registration)
add_subdirectory(libs/serialcom)
add_subdirectory(libs/sham)
add_subdirectory(libs/safety)
add_subdirectory(libs/gui)
add_subdirectory(libs/gui_qt)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)
