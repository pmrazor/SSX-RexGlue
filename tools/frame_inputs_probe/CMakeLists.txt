cmake_minimum_required(VERSION 3.25)
project(ssx_frame_inputs_probe LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This probe requires Windows x64.")
endif()
find_package(rexglue 0.10.0 REQUIRED CONFIG)
add_executable(ssx_frame_inputs_probe main.cpp)
target_compile_features(ssx_frame_inputs_probe PRIVATE cxx_std_23)
target_link_libraries(ssx_frame_inputs_probe PRIVATE rex::runtime)
rexglue_apply_target_settings(ssx_frame_inputs_probe)
add_custom_command(TARGET ssx_frame_inputs_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx_frame_inputs_probe>"
    VERBATIM)
