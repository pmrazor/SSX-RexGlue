cmake_minimum_required(VERSION 3.25)
project(ssx_hdr_probe LANGUAGES CXX)
find_package(rexglue 0.10.0 REQUIRED CONFIG)
add_executable(ssx_hdr_probe main.cpp)
target_compile_features(ssx_hdr_probe PRIVATE cxx_std_23)
target_link_libraries(ssx_hdr_probe PRIVATE rex::runtime)
rexglue_apply_target_settings(ssx_hdr_probe)
add_custom_command(TARGET ssx_hdr_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx_hdr_probe>" VERBATIM)
