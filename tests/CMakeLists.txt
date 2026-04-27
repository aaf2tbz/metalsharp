cmake_minimum_required(VERSION 3.24)

add_executable(test_metal_device
    test_metal_device.cpp
)
target_link_libraries(test_metal_device PRIVATE metalsharp_core)
target_compile_options(test_metal_device PRIVATE -fobjc-arc)

add_executable(test_format_translation
    test_format_translation.cpp
)
target_link_libraries(test_format_translation PRIVATE metalsharp_core)

add_executable(test_d3d11_device
    test_d3d11_device.cpp
)
target_link_libraries(test_d3d11_device PRIVATE metalsharp_d3d11 metalsharp_core)

add_executable(test_triangle
    test_triangle.mm
)
target_link_libraries(test_triangle PRIVATE metalsharp_d3d11 metalsharp_core)
target_compile_options(test_triangle PRIVATE -fobjc-arc)

add_executable(test_phase2
    test_phase2.mm
)
target_link_libraries(test_phase2 PRIVATE metalsharp_d3d11 metalsharp_core)
target_compile_options(test_phase2 PRIVATE -fobjc-arc)

add_executable(test_dxbc
    test_dxbc.cpp
)
target_link_libraries(test_dxbc PRIVATE metalsharp_core)

add_executable(test_deferred_context
    test_deferred_context.cpp
)
target_link_libraries(test_deferred_context PRIVATE metalsharp_d3d11 metalsharp_core)

add_executable(test_d3d12
    test_d3d12.cpp
)
target_link_libraries(test_d3d12 PRIVATE metalsharp_d3d12 metalsharp_core)

add_executable(test_runtime
    test_runtime.cpp
)
target_link_libraries(test_runtime PRIVATE metalsharp_core)

add_executable(test_audio
    test_audio.cpp
)
target_link_libraries(test_audio PRIVATE metalsharp_audio metalsharp_core)

add_executable(test_input
    test_input.cpp
)
target_link_libraries(test_input PRIVATE metalsharp_input metalsharp_core)
target_compile_options(test_input PRIVATE -fobjc-arc)

add_executable(test_phase5
    test_phase5.cpp
)
target_link_libraries(test_phase5 PRIVATE metalsharp_core)

add_executable(test_tiled_resources
    test_tiled_resources.cpp
)
target_link_libraries(test_tiled_resources PRIVATE metalsharp_core)

add_test(NAME metal_device COMMAND test_metal_device)
add_test(NAME format_translation COMMAND test_format_translation)
add_test(NAME d3d11_device COMMAND test_d3d11_device)
add_test(NAME triangle COMMAND test_triangle)
add_test(NAME phase2 COMMAND test_phase2)
add_test(NAME dxbc COMMAND test_dxbc)
add_test(NAME deferred_context COMMAND test_deferred_context)
add_test(NAME d3d12 COMMAND test_d3d12)
add_test(NAME runtime COMMAND test_runtime)
add_test(NAME audio COMMAND test_audio)
add_test(NAME input COMMAND test_input)
add_test(NAME phase5 COMMAND test_phase5)
add_test(NAME tiled_resources COMMAND test_tiled_resources)
