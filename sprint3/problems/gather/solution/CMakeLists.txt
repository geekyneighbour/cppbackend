cmake_minimum_required(VERSION 3.11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
conan_basic_setup(TARGETS)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(collision_detection_lib STATIC
    src/collision_detector.cpp
)

target_include_directories(collision_detection_lib
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CONAN_PKG::boost
)

target_link_libraries(collision_detection_lib 
    PUBLIC 
    CONAN_PKG::boost 
    Threads::Threads
)


add_executable(collision_detection_tests
    tests/collision-detector-tests.cpp
)

target_include_directories(collision_detection_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CONAN_PKG::boost
)

target_link_libraries(collision_detection_tests
    PRIVATE
    CONAN_PKG::catch2
    CONAN_PKG::boost
    Threads::Threads
    collision_detection_lib
)

include(CTest)
include(Catch)
catch_discover_tests(collision_detection_tests)