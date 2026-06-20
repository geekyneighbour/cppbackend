cmake_minimum_required(VERSION 3.11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)


add_library(game_model STATIC
    src/model.cpp
    src/loot_generator.cpp
    # src/boost_json.cpp не включаем - он только для boost::json
)


target_include_directories(game_model PUBLIC CONAN_PKG::boost)
target_link_libraries(game_model PUBLIC CONAN_PKG::boost)


target_compile_definitions(game_model PUBLIC BOOST_JSON_HEADER_ONLY)


add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/boost_json.cpp
    src/json_loader.cpp
)


target_link_libraries(game_server
    PRIVATE
    game_model
    CONAN_PKG::boost
)


enable_testing()

find_package(Catch2 REQUIRED)

add_executable(game_server_tests
    tests/loot_generator_tests.cpp
    # другие тесты можно добавить сюда
)

target_link_libraries(game_server_tests
    PRIVATE
    game_model
    Catch2::Catch2WithMain
    CONAN_PKG::boost
)


include(CTest)
include(${CONAN_BUILD_DIRS_CATCH2_DEBUG}/Catch.cmake)


catch_discover_tests(game_server_tests)