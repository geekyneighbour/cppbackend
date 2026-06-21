cmake_minimum_required(VERSION 3.11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)


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
    src/request_handler.h  
    src/ticker.h           
)

target_link_libraries(game_server
    PRIVATE
    game_model
    CONAN_PKG::boost
)

# ==================== ТЕСТЫ ====================
enable_testing()

add_executable(game_server_tests
    tests/loot_generator_tests.cpp
)

target_link_libraries(game_server_tests
    PRIVATE
    game_model
    CONAN_PKG::boost
    CONAN_PKG::catch2
)

target_include_directories(game_server_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CONAN_INCLUDE_DIRS_CATCH2}
)


include(CTest)


if(EXISTS "${CONAN_BUILD_DIRS_CATCH2_DEBUG}/Catch.cmake")
    include(${CONAN_BUILD_DIRS_CATCH2_DEBUG}/Catch.cmake)
elseif(EXISTS "${CONAN_BUILD_DIRS_CATCH2}/Catch.cmake")
    include(${CONAN_BUILD_DIRS_CATCH2}/Catch.cmake)
else()
    add_test(NAME LootGeneratorTests COMMAND game_server_tests)
endif()

if(COMMAND catch_discover_tests)
    catch_discover_tests(game_server_tests)
endif()