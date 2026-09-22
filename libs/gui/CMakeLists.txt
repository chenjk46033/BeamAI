# Framework-independent GUI presenter logic -- the data transforms behind
# BeamV0's GUI/*.m, with no Qt dependency. Unit-tested on its own; the Qt
# views live in libs/gui_qt.
add_library(gui STATIC
    src/safety_presenter.cpp
    src/correction_tab_presenter.cpp
    src/case_report_form.cpp
    src/registration_tab_presenter.cpp
    src/sonication_tab_presenter.cpp
    src/top_targets.cpp
    src/mri_overlay_presenter.cpp
    src/sonicate_orchestrator.cpp
    src/session_io.cpp
    src/countdown_presenter.cpp
    src/initial_placement_presenter.cpp
    src/sham_orchestrator.cpp
    src/treatment_session_store.cpp
    src/registration_check_presenter.cpp
    src/treatment_workflow.cpp
)
target_include_directories(gui PUBLIC include)
target_link_libraries(gui PUBLIC safety correction registration array mri stimulation sham Eigen3::Eigen)
