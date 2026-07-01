cmake_minimum_required(VERSION 3.10)
project(BookManagerSolution)

find_package(libpqxx REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(book_manager
    main.cpp
    database.cpp
    json_parser.cpp
)

target_link_libraries(book_manager
    libpqxx::libpqxx
    nlohmann_json::nlohmann_json
)

target_compile_features(book_manager PRIVATE cxx_std_17)