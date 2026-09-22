add_library(mri
    src/ras_transform.cpp
    src/nifti_orientation.cpp
    src/nifti_file.cpp
    src/mri_loader.cpp
    src/dicom_coords.cpp
    src/dicom_series_geometry.cpp
    src/slice.cpp
    src/affine_volume.cpp
)
target_include_directories(mri PUBLIC include)
target_link_libraries(mri PUBLIC Eigen3::Eigen)
