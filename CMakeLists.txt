cmake_minimum_required(VERSION 3.20)
project(rez_gaze CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(gaze_probe
    probe/main.cpp
    probe/recorder.cpp
    probe/openvr_probe.cpp
    probe/openxr_probe.cpp
    probe/osc_probe.cpp)
target_include_directories(gaze_probe PRIVATE
    third_party/openvr/headers
    third_party/openxr/include)
target_compile_definitions(gaze_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(gaze_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib
    winmm d3d11 dxgi ws2_32)

# Spike: stand-in REZ\openvr_api.dll (built into out\proxy so it doesn't clash with the probe's copy).
add_library(rez_gaze_proxy SHARED proxy/proxy.cpp proxy/forwards.cpp proxy/openvr_api.def)
set_target_properties(rez_gaze_proxy PROPERTIES
    OUTPUT_NAME openvr_api
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/proxy
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/proxy)
target_include_directories(rez_gaze_proxy PRIVATE third_party/openvr/headers)
target_compile_definitions(rez_gaze_proxy PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rez_gaze_proxy PRIVATE ws2_32)

add_custom_command(TARGET gaze_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll
        $<TARGET_FILE_DIR:gaze_probe>)
