cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")

set(CONAN_DISABLE_CHECK_COMPILER TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wno-enum-constexpr-conversion)

project(book_manager CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
conan_basic_setup(TARGETS)


add_executable(book_manager
    main.cpp
    database.cpp
    json_parser.cpp
)


target_include_directories(game_server_tests 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
