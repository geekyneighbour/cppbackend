cmake_minimum_required(VERSION 3.11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
conan_basic_setup(TARGETS)


include(CTest)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(${CONAN_BUILD_DIRS_CATCH2_DEBUG}/Catch.cmake)
else()
    include(${CONAN_BUILD_DIRS_CATCH2_RELEASE}/Catch.cmake)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


add_library(game_model STATIC
    src/model.cpp
    src/loot_generator.cpp
)

target_include_directories(game_model 
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CONAN_PKG::boost
)

target_link_libraries(game_model 
    PUBLIC 
    CONAN_PKG::boost
)

target_compile_definitions(game_model PUBLIC BOOST_JSON_HEADER_ONLY)


add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/boost_json.cpp
    src/json_loader.cpp
)

target_include_directories(game_server PRIVATE CONAN_PKG::boost)
target_link_libraries(game_server
    PRIVATE
    game_model
    CONAN_PKG::boost
)


enable_testing()

add_executable(game_server_tests
    tests/loot_generator_tests.cpp
)

target_include_directories(game_server_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CONAN_PKG::boost
)

target_link_libraries(game_server_tests
    PRIVATE
    game_model
    CONAN_PKG::boost
    CONAN_PKG::catch2
    Threads::Threads
)


catch_discover_tests(game_server_tests)


set_target_properties(game_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(game_server_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)