cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")

set(CONAN_DISABLE_CHECK_COMPILER TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wno-enum-constexpr-conversion)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
conan_basic_setup(TARGETS)

# Отключаем Boost.Test (конфликтует с Catch2)
add_definitions(-DBOOST_TEST_DYN_LINK=0)

include(CTest)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(${CONAN_BUILD_DIRS_CATCH2_DEBUG}/Catch.cmake)
else()
    include(${CONAN_BUILD_DIRS_CATCH2_RELEASE}/Catch.cmake)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


add_library(models STATIC
    src/model.cpp
    src/loot_generator.cpp
)

target_include_directories(models 
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CONAN_PKG::boost
)

target_link_libraries(models 
    PUBLIC 
    CONAN_PKG::boost
)

target_compile_definitions(models PUBLIC BOOST_JSON_HEADER_ONLY)


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


add_executable(game_server
    src/main.cpp
    src/sdk.h
    src/boost_json.cpp
    src/http_server.cpp
    src/http_server.h
    src/json_loader.cpp
    src/json_loader.h
    src/request_handler.h
    src/ticker.h
    src/logging.h
    src/collision_detector.h
)

target_include_directories(game_server PRIVATE CONAN_PKG::boost)
target_link_libraries(game_server 
    PRIVATE 
    CONAN_PKG::boost 
    models
    collision_detection_lib
)


add_executable(collision_detection_tests
    tests/collision-detector-tests.cpp
)

target_include_directories(game_server_tests 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CONAN_PKG::boost
)

target_link_libraries(game_server_tests 
    PRIVATE 
    CONAN_PKG::catch2 
    CONAN_PKG::boost 
    Threads::Threads 
    models
    collision_detection_lib
)

catch_discover_tests(collision_detection_tests)


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