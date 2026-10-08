find_package(GTest REQUIRED)
find_package(DCMTK REQUIRED)
include(GoogleTest)

add_executable(unit_tests
    util_tests.cpp
    array_tests.cpp
    correction_tests.cpp
    stimulation_tests.cpp
    mri_tests.cpp
    fiducial_detect_tests.cpp
    treatment_target_store_tests.cpp
    treatment_migration_tests.cpp
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
    infra_mat
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
    BEAM_MRI_FIXTURE_NII="${CMAKE_SOURCE_DIR}/testdata/beamai_F040_T1_MRI.nii"
    BEAM_FIDUCIAL_FIXTURE_CSV="${CMAKE_SOURCE_DIR}/testdata/beamai_fiducials_F040_T1_MRI.csv"
    BEAM_DETECTED_FIDUCIAL_CSV="${CMAKE_SOURCE_DIR}/testdata/beamai_fiducials_F040_detected.csv"
    BEAM_TREATMENT_TARGETS_CSV="${CMAKE_SOURCE_DIR}/data/treatment_targets.csv"
    BEAM_TREATMENT_PROTOCOLS_CSV="${CMAKE_SOURCE_DIR}/data/treatment_protocols.csv"
    BEAM_TEST_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}"
    # BeamV0's own protocol tables, the migration source for data/*.csv.
    # A sibling checkout, so the comparison test skips when it is absent.
    BEAM_V0_PROTOCOL_DIR="${CMAKE_SOURCE_DIR}/../BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/TreatmentProtocols"
    # The shipped default subject, whose sys carries the authoritative Target
    # List and protocol names. Same sibling checkout, same skip-if-absent.
    BEAM_DEFAULT_SUBJECT_MAT="${CMAKE_SOURCE_DIR}/../DefaultSubjectV0/defaultSubjectMNIV1.mat"
)
