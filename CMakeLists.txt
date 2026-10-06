# ssx - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(FATAL_ERROR
        "Run the patched SDK's rexglue codegen ssx_manifest.toml before configuring. "
        "A clean checkout has no generated build glue or game code. "
        "On Windows, scripts/build_windows.ps1 handles this step; see README.md.")
endif()
include(generated/rexglue.cmake)

# Sources
set(SSX_SOURCES
    src/main.cpp
    src/overrides.cpp
    src/frame_timing.cpp
    src/scene_resolution.cpp
    src/crash_handler.cpp
)

if(WIN32)
    add_executable(ssx WIN32 ${SSX_SOURCES})
else()
    add_executable(ssx ${SSX_SOURCES})
endif()

rexglue_setup_target(ssx GPU_PLUGINS xenos)

option(SSX_BUILD_HOOK_TESTS "Test scene-size hooks against locally generated guest code" OFF)
if(SSX_BUILD_HOOK_TESTS)
    enable_testing()
    add_executable(ssx_scene_resolution_test
        tests/scene_resolution_test.cpp src/scene_resolution.cpp)
    target_link_libraries(ssx_scene_resolution_test PRIVATE ssx_recomp rex::runtime)
    rexglue_apply_target_settings(ssx_scene_resolution_test)
    if(WIN32)
        add_custom_command(TARGET ssx_scene_resolution_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx_scene_resolution_test>"
            VERBATIM)
    endif()
    add_test(NAME ssx_scene_resolution COMMAND ssx_scene_resolution_test)
    if(EXISTS "${rexglue_DIR}/../../../include/rex/ui/guest_frame_timing.h" OR
       EXISTS "${REXSDK_DIR}/include/rex/ui/guest_frame_timing.h")
        add_executable(ssx_frame_timing_hook_test
            tests/frame_timing_hook_test.cpp src/frame_timing.cpp)
        target_link_libraries(ssx_frame_timing_hook_test PRIVATE rex::runtime)
        rexglue_apply_target_settings(ssx_frame_timing_hook_test)
        add_test(NAME ssx_frame_timing_hook COMMAND ssx_frame_timing_hook_test)
    endif()
endif()

if(WIN32)
    add_custom_target(ssx_launchers ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ssx>"
        # The GPU plugin is loaded dynamically. A plugin-only SDK change does
        # not relink ssx, so POST_BUILD staging alone leaves an obsolete DLL.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::gpu-xenos>" "$<TARGET_FILE_DIR:ssx>"
        VERBATIM)
    add_custom_command(TARGET ssx_launchers POST_BUILD
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_options.ps1"
            -OutputDirectory "$<TARGET_FILE_DIR:ssx>"
        VERBATIM)
    foreach(launcher play_1x_120fps.bat play_qhd.bat play_4k.bat launch_ssx.ps1 play_options.bat)
        add_custom_command(TARGET ssx_launchers POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${launcher}"
                "$<TARGET_FILE_DIR:ssx>/${launcher}"
            VERBATIM)
    endforeach()
    add_dependencies(ssx ssx_launchers)
endif()
