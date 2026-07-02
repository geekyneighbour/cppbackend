#!/bin/bash
set -e

# Установка зависимостей через Conan
conan install . --build=missing -s build_type=Debug -s compiler=gcc -s compiler.version=11 -s compiler.libcxx=libstdc++11 -if=./build/

# Конфигурация CMake
cmake -D CMAKE_CXX_COMPILER=/usr/bin/g++ -D CMAKE_BUILD_TYPE=Debug -D CMAKE_CXX_FLAGS="-g -O0" -S . -B ./build

# Сборка проекта
cmake --build ./build -j 8

echo "Build complete! Run ./build/book_manager <connection_string>"