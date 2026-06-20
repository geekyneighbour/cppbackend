cmake_minimum_required(VERSION 3.11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

add_executable(game_server
	src/main.cpp
	src/http_server.cpp
	src/model.cpp
	src/boost_json.cpp
	src/json_loader.cpp 
    	src/loot_generator.cpp
)


target_include_directories(game_server PRIVATE CONAN_PKG::boost)
target_link_libraries(game_server CONAN_PKG::boost)