# BeamV0's two Sonicate-tab table models (stimParamTable and
# treatmentProtocolTable), split out of libs/gui_qt so both applications can
# use them.  They are the data behind the Sonicate tab in beam_app and behind
# the Treatment plan stage in workflow_app; linking all of libs/gui_qt to
# reach them would drag BeamMainWindow, the startup screen and Qt6::Charts
# into workflow_app for nothing.
#
# The headers keep their `gui_qt/` include prefix and `beam::gui_qt`
# namespace: they are the same models in the same Qt layer, only built
# separately, and changing either would churn every caller for no gain.
#
# Qt is provisioned via the vcpkg manifest.  Skip cleanly if it isn't found,
# the same optional-dependency pattern libs/gui_qt uses.
find_package(Qt6 QUIET COMPONENTS Core Gui)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 (Core/Gui) not found -- skipping libs/gui_qt_models. "
        "Expected via the vcpkg manifest on the msvc preset.")
    return()
endif()

add_library(gui_qt_models STATIC
    src/stim_param_table_model.cpp
    src/treatment_protocol_table_model.cpp
    # Q_OBJECT headers listed so AUTOMOC scans them (they aren't next to
    # their .cpp, so the include-path heuristic alone misses them):
    include/gui_qt/stim_param_table_model.hpp
    include/gui_qt/treatment_protocol_table_model.hpp
)
target_include_directories(gui_qt_models PUBLIC include)
# Widgets are deliberately absent: a QAbstractTableModel needs Core, and
# treatment_protocol_table_model's row colouring needs QColor from Gui.
target_link_libraries(gui_qt_models PUBLIC gui Qt6::Core Qt6::Gui)
set_target_properties(gui_qt_models PROPERTIES AUTOMOC ON)
