add_library(correction
    src/correlation.cpp
    src/waveforms.cpp
    src/scan_params.cpp
    src/receive_elements.cpp
    src/attenuation.cpp
    src/peaks.cpp
    src/localize.cpp
    src/signal.cpp
    src/receive_waveform.cpp
)
target_include_directories(correction PUBLIC include)
target_link_libraries(correction PUBLIC Eigen3::Eigen array util)
