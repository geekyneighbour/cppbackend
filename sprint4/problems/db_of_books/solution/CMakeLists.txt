cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libpqxx REQUIRED)
find_package(nlohmann_json REQUIRED)

project(BookManager)

add_executable(book_manager
    main.cpp
    database.cpp
    json_parser.cpp
)


target_link_libraries(book_manager
    libpqxx::libpqxx
    nlohmann_json::nlohmann_json
)


target_include_directories(book_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)