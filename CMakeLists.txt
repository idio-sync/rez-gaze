cmake_minimum_required(VERSION 3.20)
project(rez_gaze CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(gaze_probe
    probe/main.cpp
    probe/recorder.cpp
    probe/openvr_probe.cpp
    probe/openxr_probe.cpp
    probe/osc_probe.cpp
    probe/osc_replay.cpp
    probe/shm_probe.cpp)
target_include_directories(gaze_probe PRIVATE
    third_party/openvr/headers
    third_party/openxr/include)
target_compile_definitions(gaze_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(gaze_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib
    winmm d3d11 dxgi ws2_32)

# The gaze helper, in out\rez_gaze\ with its own OpenVR 2.15.6 runtime (REZ\rez_gaze\ in the game).
add_executable(rez_gaze_helper helper/gaze_helper.cpp)
set_target_properties(rez_gaze_helper PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/rez_gaze)
target_include_directories(rez_gaze_helper PRIVATE third_party/openvr/headers)
target_compile_definitions(rez_gaze_helper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rez_gaze_helper PRIVATE ${CMAKE_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib winmm)
add_custom_command(TARGET rez_gaze_helper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll
        $<TARGET_FILE_DIR:rez_gaze_helper>)

# Unit tests (ctest --test-dir out).
enable_testing()
add_executable(rez_gaze_tests tests/test_main.cpp tests/test_gaze_shm.cpp tests/test_aim.cpp proxy/aim.cpp)
target_compile_definitions(rez_gaze_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME unit COMMAND rez_gaze_tests)
add_test(NAME helper_smoke COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_SOURCE_DIR}/tests/helper_smoke.ps1 -Helper ${CMAKE_BINARY_DIR}/rez_gaze/rez_gaze_helper.exe)
add_test(NAME helper_smoke_fake COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_SOURCE_DIR}/tests/helper_smoke.ps1 -Helper ${CMAKE_BINARY_DIR}/rez_gaze/rez_gaze_helper.exe -Fake)

# Spike: stand-in REZ\openvr_api.dll (built into out\proxy so it doesn't clash with the probe's copy).
add_library(rez_gaze_proxy SHARED proxy/proxy.cpp proxy/aim.cpp proxy/forwards.cpp proxy/openvr_api.def)
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
