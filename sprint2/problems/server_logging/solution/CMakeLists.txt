cmake_minimum_required(VERSION 3.11)
project(game_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan 1
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()  # без TARGETS для совместимости

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/json_loader.cpp
    src/model.cpp
)

# Явное подключение библиотек Boost.Log и Boost.JSON
target_link_libraries(game_server
    PRIVATE
    ${CONAN_LIBS}
    Threads::Threads
    boost_log
    boost_log_setup
    boost_thread
    boost_system
    boost_json
)