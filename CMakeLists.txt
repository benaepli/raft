cmake_minimum_required(VERSION 3.30)
project(raft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Build options
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_CLI "Build the CLI application" ON)

add_subdirectory(raft)

if (BUILD_CLI)
    add_subdirectory(raft-cli)
endif ()

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()