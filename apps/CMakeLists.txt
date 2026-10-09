# Build/parity helper executables (not unit tests).

add_executable(parity_arrays parity_arrays/main.cpp)
target_link_libraries(parity_arrays PRIVATE array util)

add_executable(parity_correction parity_correction/main.cpp)
target_link_libraries(parity_correction PRIVATE correction array util)

add_executable(parity_stimulation parity_stimulation/main.cpp)
target_link_libraries(parity_stimulation PRIVATE stimulation array util)

add_executable(parity_mri parity_mri/main.cpp)
target_link_libraries(parity_mri PRIVATE mri)

# One-shot: regenerates testdata/..._T1_MRI.nii from the original DICOM series.
# The result is committed; a normal build never needs this.
add_executable(make_mri_fixture make_mri_fixture/main.cpp)
target_link_libraries(make_mri_fixture PRIVATE infra_dicom mri)

add_executable(parity_registration parity_registration/main.cpp)
# The six measured fiducials both sides of the check register from. A compile
# definition rather than a relative path: the parity binary runs from its own
# work directory under the build tree.
target_compile_definitions(parity_registration PRIVATE
    BEAM_FIDUCIAL_FIXTURE_CSV="${CMAKE_SOURCE_DIR}/testdata/beamai_fiducials_F040_T1_MRI.csv")
target_link_libraries(parity_registration PRIVATE registration array util)

# Scores the automatic donut detector the Registration tab's button runs
# against the operator's own measured fiducials.
add_executable(detect_fiducials detect_fiducials/main.cpp)
target_compile_definitions(detect_fiducials PRIVATE
    BEAM_MRI_FIXTURE_NII="${CMAKE_SOURCE_DIR}/testdata/beamai_F040_T1_MRI.nii"
    BEAM_FIDUCIAL_FIXTURE_CSV="${CMAKE_SOURCE_DIR}/testdata/beamai_fiducials_F040_T1_MRI.csv"
    BEAM_DEFAULT_SUBJECT_RECT_CSV="${CMAKE_SOURCE_DIR}/../DefaultSubjectV0/defaultSubjectArrayRect.csv")
target_link_libraries(detect_fiducials PRIVATE infra_dicom mri array registration util)

add_executable(parity_safety parity_safety/main.cpp)
target_link_libraries(parity_safety PRIVATE safety)

# Privacy-safe schema/dimension probe for legacy Beam MATLAB sessions.
add_executable(beam_mat_info beam_mat_info/main.cpp)
target_link_libraries(beam_mat_info PRIVATE infra_mat)

# --- Qt demo apps (skipped cleanly when Qt6 isn't provisioned) ---
find_package(Qt6 QUIET COMPONENTS Widgets Charts)
if(Qt6_FOUND AND TARGET gui_qt)
    # Qt apps need their platform plugin next to the .exe or they abort at
    # startup with "Could not find the Qt platform plugin \"windows\"".
    # vcpkg deploys the Qt6*.dll files but not plugins -- copy the one we
    # need explicitly (cheaper than pulling in windeployqt).
    function(beam_deploy_qt_platform target)
        if(WIN32)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${target}>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy
                    "$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms/qwindowsd.dll,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms/qwindows.dll>"
                    "$<TARGET_FILE_DIR:${target}>/platforms/"
            )
        endif()
    endfunction()

    # No AUTOMOC here: main.cpp declares no Q_OBJECT of its own; the
    # widget/chart mocs live in gui_qt.
    add_executable(beam_app beam_app/main.cpp beam_app/startup_data.cpp beam_app/check_diagnostics.cpp
                             beam_app/designer_startup.cpp beam_app/main_window_startup.cpp)
    target_link_libraries(beam_app PRIVATE gui gui_qt correction safety serialcom infra_dicom Qt6::Widgets Qt6::Charts)
    beam_deploy_qt_platform(beam_app)

    # New workflow-first application. Its layout is maintained in Qt Designer
    # XML; this remains separate from beam_app while BeamV0 behavior is audited.
    add_executable(workflow_app
        workflow_app/main.cpp
        workflow_app/mri_view.cpp
        workflow_app/mri_view.hpp
        workflow_app/registration_fiducial_layout.cpp
        workflow_app/registration_fiducial_layout.hpp
        workflow_app/workflow_window.cpp
        workflow_app/workflow_window.hpp
        # The Treatment plan and Treatment stage bodies (BeamV0's Sonicate tab,
        # re-homed into this workflow's stage model). Its own translation unit
        # so the shell's constructor does not grow another several hundred
        # lines.
        workflow_app/treatment_plan_body.cpp
        # "Open viewer in a new window": a larger, Escape-closable window for
        # one MRI plane, driving that pane's own slice controls.
        workflow_app/detached_viewer.cpp
        workflow_app/resources/workflow_resources.qrc
        ../designer/workflow_shell.ui
        # AUTORCC compiles this into the Targeting Examples sub-tab's
        # reference images -- BeamV0.mlapp's own SonicationTab/ExampleTargets.
        ../designer/resources/example_targets.qrc
    )
    target_include_directories(workflow_app PRIVATE workflow_app)
    target_link_libraries(workflow_app PRIVATE gui gui_qt_models gui_qt_views mri correction stimulation sham
                                                serialcom infra_dicom infra_mat Qt6::Widgets)
    set_target_properties(workflow_app PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
        AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../designer")
    beam_deploy_qt_platform(workflow_app)
else()
    message(STATUS "Qt6 not found -- skipping beam_app.")
endif()
