# Qt is provisioned via the vcpkg manifest (build-msvc/). Skip cleanly with
# a warning if it isn't found (e.g. a toolchain without the manifest), the
# same optional-dependency pattern the parity/demo apps use -- rather than
# failing the whole configure.
find_package(Qt6 QUIET COMPONENTS Widgets Charts)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 (Widgets/Charts) not found -- skipping libs/gui_qt. "
        "Expected via the vcpkg manifest on the msvc preset.")
    return()
endif()

add_library(gui_qt STATIC
    src/safety_report_view.cpp
    src/correction_tab_chart.cpp
    src/chart_axis_utils.cpp
    src/sonication_tab_chart.cpp
    src/stim_param_table_model.cpp
    src/treatment_protocol_table_model.cpp
    src/fiducial_table_model.cpp
    src/mri_slice_view.cpp
    src/total_sonication_view.cpp
    src/pulse_waveform_view.cpp
    src/zoomable_image_view.cpp
    src/main_window.cpp
    src/startup_screen_window.cpp
    # AUTOUIC compiles this into ui_startup_screen.h, #include'd by
    # startup_screen_window.cpp -- the Qt Designer XML conversion of
    # BeamV0.mlapp's startup screen (see its own header comment).
    ../../designer/startup_screen.ui
    # AUTORCC compiles this into qrc_example_targets.cpp, linked in
    # automatically -- the Targeting Examples sub-tab's reference images
    # (BeamV0.mlapp's own SonicationTab/ExampleTargets/*.PNG).
    ../../designer/resources/example_targets.qrc
    # Q_OBJECT headers listed so AUTOMOC scans them (they aren't next to
    # their .cpp, so the include-path heuristic alone misses them):
    include/gui_qt/safety_report_view.hpp
    include/gui_qt/main_window.hpp
    include/gui_qt/stim_param_table_model.hpp
    include/gui_qt/treatment_protocol_table_model.hpp
    include/gui_qt/fiducial_table_model.hpp
    include/gui_qt/mri_slice_view.hpp
    include/gui_qt/total_sonication_view.hpp
    include/gui_qt/zoomable_image_view.hpp
    include/gui_qt/startup_screen_window.hpp
)
target_include_directories(gui_qt PUBLIC include)
target_link_libraries(gui_qt PUBLIC gui mri Qt6::Widgets Qt6::Charts)
set_target_properties(gui_qt PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../designer")
