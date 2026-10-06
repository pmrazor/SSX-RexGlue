cmake_minimum_required(VERSION 3.25)
project(ssx_hdr_layer_probe LANGUAGES CXX)
find_package(rexglue 0.10.0 REQUIRED CONFIG)
set(SSX_SDK_SOURCE "" CACHE PATH "Modified ReXGlue SDK source checkout")
if(NOT EXISTS "${SSX_SDK_SOURCE}/src/graphics/pipeline/shader/dxbc_translator.cpp")
  message(FATAL_ERROR "Set SSX_SDK_SOURCE to the modified checkout")
endif()
add_executable(ssx_hdr_layer_probe main.cpp)
foreach(source flags.cpp format/ucode.cpp util/draw.cpp registers.cpp register_file.cpp pipeline/shader/shader.cpp
    pipeline/shader/translator.cpp pipeline/shader/translator_disasm.cpp
    pipeline/shader/dxbc.cpp pipeline/shader/ssx_analysis.cpp pipeline/shader/dxbc_translator.cpp
    pipeline/shader/dxbc_translator_alu.cpp pipeline/shader/dxbc_translator_fetch.cpp
    pipeline/shader/dxbc_translator_memexport.cpp pipeline/shader/dxbc_translator_om.cpp)
  target_sources(ssx_hdr_layer_probe PRIVATE "${SSX_SDK_SOURCE}/src/graphics/${source}")
endforeach()
target_sources(ssx_hdr_layer_probe PRIVATE "${SSX_SDK_SOURCE}/thirdparty/dxbc/DXBCChecksum.cpp")
target_include_directories(ssx_hdr_layer_probe BEFORE PRIVATE "${SSX_SDK_SOURCE}/include" "${SSX_SDK_SOURCE}" "${SSX_SDK_SOURCE}/thirdparty/renderdoc")
target_compile_features(ssx_hdr_layer_probe PRIVATE cxx_std_23)
target_link_libraries(ssx_hdr_layer_probe PRIVATE rex::runtime d3d12 d3dcompiler)
rexglue_apply_target_settings(ssx_hdr_layer_probe)
add_custom_command(TARGET ssx_hdr_layer_probe POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:ssx_hdr_layer_probe>" VERBATIM)
