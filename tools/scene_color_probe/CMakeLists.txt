cmake_minimum_required(VERSION 3.25)
project(ssx_scene_color_probe LANGUAGES CXX)
find_package(rexglue 0.10.0 REQUIRED CONFIG)
add_executable(ssx_scene_color_probe main.cpp)
target_compile_features(ssx_scene_color_probe PRIVATE cxx_std_23)
target_link_libraries(ssx_scene_color_probe PRIVATE rex::runtime)
rexglue_apply_target_settings(ssx_scene_color_probe)
add_custom_command(TARGET ssx_scene_color_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx_scene_color_probe>"
    VERBATIM)
