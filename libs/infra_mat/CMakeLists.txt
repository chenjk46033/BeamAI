find_package(matio CONFIG REQUIRED)

add_library(infra_mat src/legacy_beam_session.cpp)
target_include_directories(infra_mat PUBLIC include)
target_link_libraries(infra_mat PUBLIC mri matio::matio)
