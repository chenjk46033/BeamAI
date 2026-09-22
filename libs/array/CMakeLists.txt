add_library(array
    src/affine.cpp
    src/array_types.cpp
    src/geometry.cpp
    src/array_struct.cpp
    src/array_data.cpp
)
target_include_directories(array PUBLIC include)
target_link_libraries(array PUBLIC Eigen3::Eigen util)
