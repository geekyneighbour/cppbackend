cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_MODULE_PATH})


include(${CMAKE_CURRENT_SOURCE_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

project(BookManager)

add_executable(book_manager
    main.cpp
    database.cpp
    json_parser.cpp
)


target_link_libraries(book_manager
    ${CONAN_LIBS}
)

target_include_directories(book_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)