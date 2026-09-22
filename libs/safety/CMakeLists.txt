add_library(safety
    src/limits.cpp
    src/intensity.cpp
    src/checks.cpp
)
target_include_directories(safety PUBLIC include)
target_link_libraries(safety PUBLIC Eigen3::Eigen)
