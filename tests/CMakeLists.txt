find_package(GTest REQUIRED)
find_package(DCMTK REQUIRED)
include(GoogleTest)

add_executable(unit_tests
    util_tests.cpp
    array_tests.cpp
    correction_tests.cpp
    stimulation_tests.cpp
    mri_tests.cpp
    infra_dicom_tests.cpp
    registration_tests.cpp
    serialcom_tests.cpp
    sham_tests.cpp
    safety_tests.cpp
    gui_tests.cpp
)

target_link_libraries(unit_tests PRIVATE
    util
    array
    correction
    stimulation
    mri
    infra_dicom
    registration
    serialcom
    sham
    safety
    gui
    DCMTK::dcmdata
    GTest::gtest
    GTest::gtest_main
)

# PRE_TEST: discover tests at ctest time, not build time. Building must not
# depend on executing the freshly-linked binary -- this machine's
# Application Control policy intermittently blocks fresh binaries (see
# project memory), which otherwise fails the build at the discovery step.
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST)

# BeamV0's shipped transducer geometry, used by the nominal-fiducial
# regression test. Lives in a sibling checkout, so the test skips (loudly)
# rather than failing when it is not present.
target_compile_definitions(unit_tests PRIVATE
    BEAM_DEFAULT_SUBJECT_RECT_CSV="${CMAKE_SOURCE_DIR}/../DefaultSubjectV0/defaultSubjectArrayRect.csv"
)
