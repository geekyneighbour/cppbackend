cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/json_loader.cpp
    src/model.cpp
)

target_link_libraries(game_server
    PRIVATE
    Threads::Threads
    CONAN_PKG::boost_log
    CONAN_PKG::boost_log_setup
    CONAN_PKG::boost_thread
    CONAN_PKG::boost_filesystem
    CONAN_PKG::boost_system
    CONAN_PKG::boost_regex
)


target_include_directories(game_server
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)