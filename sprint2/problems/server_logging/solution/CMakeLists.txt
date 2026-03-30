cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ищем Boost
find_package(Boost 1.71.0 REQUIRED COMPONENTS system thread json log)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/json_loader.cpp
    src/model.cpp
)

# Линкуем с Boost
target_link_libraries(game_server
    PRIVATE
    Threads::Threads
    ${Boost_LIBRARIES}
)

# Добавляем пути для include
target_include_directories(game_server
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)