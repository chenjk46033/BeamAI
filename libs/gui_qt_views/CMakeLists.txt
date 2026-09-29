# BeamV0's Pulse Details plots, split out of libs/gui_qt so both
# applications can use them: PulseWaveformView (axPulseWaveformPlot and
# axBurstWaveformPlot) and TotalSonicationView (axTotalSonicationPlot),
# plus the axis-tick math they share.
#
# These are hand-painted QWidgets, not QtCharts (see each header for why),
# so this target needs Qt6::Widgets alone -- which is what lets
# workflow_app show the same plots without taking on Qt6::Charts.  The
# QtCharts builders in libs/gui_qt (sonication_tab_chart.cpp) stay there
# and link back to this target for niceAxisRange.
#
# Headers keep their `gui_qt/` include prefix and `beam::gui_qt` namespace
# for the same reason libs/gui_qt_models does: same Qt layer, built
# separately, no caller churn.
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 (Widgets) not found -- skipping libs/gui_qt_views. "
        "Expected via the vcpkg manifest on the msvc preset.")
    return()
endif()

add_library(gui_qt_views STATIC
    src/chart_axis_utils.cpp
    src/pulse_waveform_view.cpp
    src/total_sonication_view.cpp
    # Q_OBJECT header listed so AUTOMOC scans it (it isn't next to its
    # .cpp, so the include-path heuristic alone misses it):
    include/gui_qt/total_sonication_view.hpp
)
target_include_directories(gui_qt_views PUBLIC include)
# gui supplies PulseWaveformPlot; stimulation supplies the burst timeline
# TotalSonicationView draws.
target_link_libraries(gui_qt_views PUBLIC gui stimulation Qt6::Widgets)
set_target_properties(gui_qt_views PROPERTIES AUTOMOC ON)
