add_library(stimulation
    src/delays.cpp
    src/apodization.cpp
    src/interp.cpp
    src/duty_cycle.cpp
    src/stim_freqs.cpp
    src/pause_intervals.cpp
    src/stim_params.cpp
    src/events.cpp
)
target_include_directories(stimulation PUBLIC include)
target_link_libraries(stimulation PUBLIC Eigen3::Eigen array util)
