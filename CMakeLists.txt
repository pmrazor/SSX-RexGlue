# ssx - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SSX_SOURCES
    src/main.cpp
    src/overrides.cpp
    src/crash_handler.cpp
)

if(WIN32)
    add_executable(ssx WIN32 ${SSX_SOURCES})
else()
    add_executable(ssx ${SSX_SOURCES})
endif()

rexglue_setup_target(ssx)
