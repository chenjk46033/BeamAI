add_library(serialcom
    src/command.cpp
    src/serial_port.cpp
)
target_include_directories(serialcom PUBLIC include)

if(WIN32)
    target_link_libraries(serialcom PRIVATE advapi32)
endif()
