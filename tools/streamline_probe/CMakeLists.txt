cmake_minimum_required(VERSION 3.25)
project(ssx_streamline_probe LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This probe requires Windows x64.")
endif()
find_package(rexglue 0.10.0 REQUIRED CONFIG)
add_executable(ssx_streamline_probe main.cpp sr_smoke.cpp fg_smoke.cpp fg_color_smoke.cpp)
set(SSX_SDK_SOURCE_DIR "" CACHE PATH "Patched SDK source for Quality scale shader GPU tests")
if(SSX_SDK_SOURCE_DIR)
    target_include_directories(ssx_streamline_probe PRIVATE
        "${SSX_SDK_SOURCE_DIR}/src/graphics/shaders/bytecode/d3d12_5_1")
    target_compile_definitions(ssx_streamline_probe PRIVATE SSX_TEST_QUALITY_SCALE=1)
endif()
target_compile_features(ssx_streamline_probe PRIVATE cxx_std_23)
target_link_libraries(ssx_streamline_probe PRIVATE rex::runtime user32 d3dcompiler d3d12)
rexglue_apply_target_settings(ssx_streamline_probe)
add_custom_command(TARGET ssx_streamline_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx_streamline_probe>"
    VERBATIM)
