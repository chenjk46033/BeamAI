find_package(DCMTK REQUIRED)

add_library(infra_dicom
    src/dicom_tags.cpp
    src/dicom_slice.cpp
    src/dicom_series.cpp
    src/load_mri_ras.cpp
)
target_include_directories(infra_dicom PUBLIC include)
target_link_libraries(infra_dicom PUBLIC DCMTK::dcmdata Eigen3::Eigen mri)
