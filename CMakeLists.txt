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

# One function with ~110k registration calls: optimizing it takes Apple clang
# far longer than the rest of the build combined, and it only runs once.
if(NOT WIN32)
    # The precompiled header is built with optimization, which clang refuses
    # to mix with -O0 (__OPTIMIZE__ differs), so this file goes without it.
    set_property(SOURCE generated/default/ssx_register.cpp APPEND PROPERTY COMPILE_OPTIONS -O0)
    set_property(SOURCE generated/default/ssx_register.cpp PROPERTY SKIP_PRECOMPILE_HEADERS ON)
endif()
