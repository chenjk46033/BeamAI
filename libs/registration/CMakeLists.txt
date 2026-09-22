add_library(registration
    src/affine_registration.cpp
    src/fiducial_markers.cpp
    src/array_transform.cpp
    src/image_model.cpp
)
target_include_directories(registration PUBLIC include)
target_link_libraries(registration PUBLIC Eigen3::Eigen array util)
