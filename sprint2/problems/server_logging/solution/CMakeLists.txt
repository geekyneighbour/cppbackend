cmake_minimum_required(VERSION 3.11)
project(game_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


find_package(Boost 1.81 REQUIRED COMPONENTS
    log log_setup thread filesystem date_time regex json
)

add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/json_loader.cpp
    src/model.cpp
)

target_include_directories(game_server PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(game_server
    PRIVATE
    ${CONAN_LIBS}
    Threads::Threads
    ${Boost_LIBRARIES}
)