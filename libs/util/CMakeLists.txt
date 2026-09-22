add_library(util
    src/geometry_math.cpp
    src/camera.cpp
)
target_include_directories(util PUBLIC include)
target_link_libraries(util PUBLIC Eigen3::Eigen)
