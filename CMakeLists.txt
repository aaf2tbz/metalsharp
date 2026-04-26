cmake_minimum_required(VERSION 3.24)
project(metalsharp VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)
find_library(METALFX_FRAMEWORK MetalFX)

find_package(SDL2 QUIET)

add_library(metalsharp_core STATIC
    src/metal/device/Device.cpp
    src/metal/device/Device.mm
    src/metal/command/CommandQueue.cpp
    src/metal/command/CommandQueue.mm
    src/metal/command/CommandBuffer.cpp
    src/metal/command/CommandBuffer.mm
    src/metal/pipeline/PipelineState.cpp
    src/metal/pipeline/PipelineState.mm
    src/metal/pipeline/RenderPipeline.cpp
    src/metal/pipeline/RenderPipeline.mm
    src/metal/pipeline/ComputePipeline.cpp
    src/metal/pipeline/ComputePipeline.mm
    src/metal/shader/ShaderTranslator.mm
    src/metal/Buffer.mm
    src/metal/Texture.mm
    src/metal/Sampler.mm
    src/metal/Framebuffer.mm
)
target_include_directories(metalsharp_core PUBLIC include)
target_link_libraries(metalsharp_core PUBLIC
    ${METAL_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
)
target_compile_options(metalsharp_core PRIVATE -fobjc-arc)

add_library(metalsharp_d3d11 SHARED
    src/d3d/d3d11/D3D11Device.cpp
    src/d3d/d3d11/D3D11DeviceContext.cpp
    src/d3d/d3d11/D3D11Texture2D.cpp
    src/d3d/d3d11/D3D11Buffer.cpp
    src/d3d/d3d11/D3D11RenderTargetView.cpp
    src/d3d/d3d11/D3D11DepthStencilView.cpp
    src/d3d/d3d11/D3D11ShaderResourceView.cpp
    src/d3d/d3d11/D3D11InputLayout.cpp
    src/d3d/d3d11/D3D11VertexShader.cpp
    src/d3d/d3d11/D3D11PixelShader.cpp
    src/d3d/d3d11/D3D11ComputeShader.cpp
    src/d3d/d3d11/D3D11SamplerState.cpp
    src/d3d/d3d11/D3D11RasterizerState.cpp
    src/d3d/d3d11/D3D11DepthStencilState.cpp
    src/d3d/d3d11/D3D11BlendState.cpp
    src/d3d/d3d11/EntryPoint.cpp
)
target_link_libraries(metalsharp_d3d11 PRIVATE metalsharp_core)
target_compile_options(metalsharp_d3d11 PRIVATE -fobjc-arc)
set_target_properties(metalsharp_d3d11 PROPERTIES OUTPUT_NAME "d3d11" PREFIX "")

add_library(metalsharp_d3d12 SHARED
    src/d3d/d3d12/D3D12Device.cpp
    src/d3d/d3d12/D3D12CommandQueue.cpp
    src/d3d/d3d12/D3D12CommandList.cpp
    src/d3d/d3d12/D3D12Resource.cpp
    src/d3d/d3d12/D3D12DescriptorHeap.cpp
    src/d3d/d3d12/D3D12RootSignature.cpp
    src/d3d/d3d12/D3D12PipelineState.cpp
    src/d3d/d3d12/D3D12Fence.cpp
    src/d3d/d3d12/EntryPoint.cpp
)
target_link_libraries(metalsharp_d3d12 PRIVATE metalsharp_core)
target_compile_options(metalsharp_d3d12 PRIVATE -fobjc-arc)
set_target_properties(metalsharp_d3d12 PROPERTIES OUTPUT_NAME "d3d12" PREFIX "")

add_library(metalsharp_dxgi SHARED
    src/dxgi/DXGIFactory.cpp
    src/dxgi/DXGIAdapter.cpp
    src/dxgi/DXGIOutput.cpp
    src/dxgi/DXGISwapChain.mm
    src/dxgi/EntryPoint.cpp
)
target_link_libraries(metalsharp_dxgi PRIVATE metalsharp_core)
target_compile_options(metalsharp_dxgi PRIVATE -fobjc-arc)
set_target_properties(metalsharp_dxgi PROPERTIES OUTPUT_NAME "dxgi" PREFIX "")

add_library(metalsharp_audio SHARED
    src/audio/XAudio2Engine.cpp
    src/audio/XAudio2Voice.cpp
    src/audio/XAudio2Buffer.cpp
    src/audio/CoreAudioBackend.cpp
    src/audio/CoreAudioBackend.mm
    src/audio/EntryPoint.cpp
)
target_link_libraries(metalsharp_audio PRIVATE metalsharp_core ${AUDIOUNIT_FRAMEWORK})
target_compile_options(metalsharp_audio PRIVATE -fobjc-arc)
set_target_properties(metalsharp_audio PROPERTIES OUTPUT_NAME "xaudio2_9" PREFIX "")

add_library(metalsharp_input SHARED
    src/input/XInputEngine.cpp
    src/input/GameControllerBridge.cpp
    src/input/GameControllerBridge.mm
    src/input/EntryPoint.cpp
)
target_link_libraries(metalsharp_input PRIVATE metalsharp_core ${GAMECONTROLLER_FRAMEWORK})
target_compile_options(metalsharp_input PRIVATE -fobjc-arc)
set_target_properties(metalsharp_input PROPERTIES OUTPUT_NAME "xinput1_4" PREFIX "")

add_executable(metalsharp_launcher
    tools/launcher/main.cpp
    tools/launcher/WinePrefix.cpp
    tools/launcher/WinePrefix.mm
    tools/launcher/Config.cpp
)
target_link_libraries(metalsharp_launcher PRIVATE metalsharp_core ${FOUNDATION_FRAMEWORK})

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
