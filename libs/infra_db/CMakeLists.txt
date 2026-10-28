find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(infra_db
    src/database.cpp
    src/schema.cpp
)
target_include_directories(infra_db PUBLIC include)
target_link_libraries(infra_db PUBLIC unofficial::sqlite3::sqlite3)
