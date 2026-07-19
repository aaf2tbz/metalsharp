cmake_minimum_required(VERSION 3.24)
project(metalsharp VERSION 0.56.1 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(METALSHARP_WINE_ARCH "x86_64" CACHE STRING "Architecture for Wine/Rosetta PE-facing targets")
set(METALSHARP_HOST_ARCH "arm64" CACHE STRING "Architecture for macOS host-side targets")
# Existing native tests and PE-facing targets inherit the Wine/Rosetta default;
# host-only targets below explicitly override it to arm64.
set(CMAKE_OSX_ARCHITECTURES "${METALSHARP_WINE_ARCH}" CACHE STRING "Default architecture for Wine/Rosetta targets" FORCE)

function(metalsharp_wine_target target)
    set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${METALSHARP_WINE_ARCH}")
endfunction()

function(metalsharp_host_target target)
    set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${METALSHARP_HOST_ARCH}")
endfunction()

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)

find_library(SECURITY_FRAMEWORK Security REQUIRED)
find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)
find_library(METALFX_FRAMEWORK MetalFX)

find_path(METALIRCONVERTER_INCLUDE_DIR
    NAMES metal_irconverter/metal_irconverter.h
    PATHS /usr/local/include
)
find_library(METALIRCONVERTER_LIBRARY
    NAMES metalirconverter
    PATHS /usr/local/lib
)

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

    src/metal/pipeline/ComputePipeline.cpp
    src/metal/pipeline/ComputePipeline.mm
    src/metal/shader/ShaderTranslator.mm
    src/metal/shader/DXBCParser.cpp
    src/metal/shader/DXBCtoMSL.cpp
    src/metal/shader/IRConverterBridge.cpp
    src/metal/shader/IRConverterBridge.mm
    src/metal/shader/ArgumentBufferBinding.cpp
    src/metal/Buffer.mm
    src/metal/Texture.mm
    src/metal/Sampler.mm
    src/metal/Framebuffer.mm
    src/runtime/Logger.cpp
    src/runtime/PEHook.cpp
    src/runtime/CompatDatabase.cpp
    src/runtime/ImportReporter.cpp
    src/runtime/CrashDiagnostics.cpp
    src/runtime/DRMDetector.cpp
    src/runtime/GameValidator.cpp
    src/runtime/GameDetector.cpp
    src/runtime/CrashReporter.cpp
    src/runtime/UpdateChecker.cpp
    src/runtime/SettingsManager.cpp
    src/runtime/StubTelemetry.cpp
    src/runtime/host/DarwinSyncMap.cpp
    src/runtime/host/HostRuntimeABI.cpp
    src/win32/kernel32/SyncContext.cpp
    src/perf/ShaderCache.cpp
    src/perf/ShaderCache.mm
    src/perf/PipelineCache.cpp
    src/perf/BufferPool.mm
    src/perf/FramePacer.cpp
    src/perf/GPUProfiler.mm
    src/perf/CommandBatcher.cpp
    src/perf/MetalCapabilities.mm
    src/perf/MetalFXUpscaler.mm
    src/perf/RenderThreadPool.cpp
)
target_include_directories(metalsharp_core PUBLIC include)
if(METALIRCONVERTER_INCLUDE_DIR)
    target_include_directories(metalsharp_core PUBLIC ${METALIRCONVERTER_INCLUDE_DIR})
    target_compile_definitions(metalsharp_core PUBLIC METALSHARP_HAS_IRCONVERTER)
    message(STATUS "MetalSharp: found metal_irconverter headers at ${METALIRCONVERTER_INCLUDE_DIR}")
endif()
if(METALIRCONVERTER_LIBRARY)
    target_link_libraries(metalsharp_core PUBLIC ${METALIRCONVERTER_LIBRARY})
    message(STATUS "MetalSharp: found libmetalirconverter at ${METALIRCONVERTER_LIBRARY}")
endif()
target_link_libraries(metalsharp_core PUBLIC
    ${METAL_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
)
target_compile_options(metalsharp_core PRIVATE -fobjc-arc)
metalsharp_wine_target(metalsharp_core)

add_library(metalsharp_host_runtime SHARED
    src/runtime/host/HostRuntimeABI.cpp
)
target_include_directories(metalsharp_host_runtime PUBLIC include)
set_target_properties(metalsharp_host_runtime PROPERTIES OUTPUT_NAME "metalsharp_host_runtime")
metalsharp_host_target(metalsharp_host_runtime)

set(METALSHARP_D3D11_SOURCES
    src/d3d/d3d11/D3D11Device.cpp src/d3d/d3d11/D3D11DeviceContext.mm src/d3d/d3d11/D3D11Texture2D.cpp
    src/d3d/d3d11/D3D11Buffer.cpp src/d3d/d3d11/D3D11RenderTargetView.cpp src/d3d/d3d11/D3D11DepthStencilView.cpp
    src/d3d/d3d11/D3D11ShaderResourceView.cpp src/d3d/d3d11/D3D11InputLayout.cpp src/d3d/d3d11/D3D11VertexShader.cpp
    src/d3d/d3d11/D3D11PixelShader.cpp src/d3d/d3d11/D3D11ComputeShader.cpp src/d3d/d3d11/D3D11SamplerState.cpp
    src/d3d/d3d11/D3D11RasterizerState.cpp src/d3d/d3d11/D3D11DepthStencilState.cpp src/d3d/d3d11/D3D11BlendState.cpp
    src/d3d/d3d11/DeferredContext.cpp src/d3d/d3d11/EntryPoint.cpp)
set(METALSHARP_D3D12_SOURCES
    src/d3d/d3d12/D3D12Device.cpp src/d3d/d3d12/D3D12CommandQueue.cpp src/d3d/d3d12/D3D12CommandList.mm
    src/d3d/d3d12/D3D12ResourceStateTracker.cpp src/d3d/d3d12/D3D12Resource.cpp src/d3d/d3d12/D3D12DescriptorHeap.cpp
    src/d3d/d3d12/D3D12RootSignature.cpp src/d3d/d3d12/D3D12PipelineState.cpp src/d3d/d3d12/D3D12Fence.cpp src/d3d/d3d12/EntryPoint.cpp)
set(METALSHARP_DXGI_SOURCES
    src/dxgi/DXGIFactory.cpp src/dxgi/DXGIAdapter.cpp src/dxgi/DXGIOutput.mm src/dxgi/DXGISwapChain.mm src/dxgi/EntryPoint.cpp)

add_library(metalsharp_d3d11 SHARED ${METALSHARP_D3D11_SOURCES})
target_link_libraries(metalsharp_d3d11 PRIVATE metalsharp_core metalsharp_dxgi)
target_compile_options(metalsharp_d3d11 PRIVATE -fobjc-arc)
set_target_properties(metalsharp_d3d11 PROPERTIES OUTPUT_NAME "d3d11" PREFIX "")
metalsharp_wine_target(metalsharp_d3d11)

add_library(metalsharp_d3d12 SHARED ${METALSHARP_D3D12_SOURCES})
target_link_libraries(metalsharp_d3d12 PRIVATE metalsharp_core)
target_compile_options(metalsharp_d3d12 PRIVATE -fobjc-arc)
set_target_properties(metalsharp_d3d12 PROPERTIES OUTPUT_NAME "d3d12" PREFIX "")
metalsharp_wine_target(metalsharp_d3d12)

add_library(metalsharp_dxgi SHARED ${METALSHARP_DXGI_SOURCES})
target_link_libraries(metalsharp_dxgi PRIVATE metalsharp_core)
target_compile_options(metalsharp_dxgi PRIVATE -fobjc-arc)
set_target_properties(metalsharp_dxgi PROPERTIES OUTPUT_NAME "dxgi" PREFIX "")
metalsharp_wine_target(metalsharp_dxgi)

add_library(metalsharp_audio SHARED
    src/audio/XAudio2Engine.cpp
    src/audio/XAudio2Voice.cpp
    src/audio/XAudio2Buffer.cpp
    src/audio/X3DAudioEngine.cpp
    src/audio/DirectSoundBackend.cpp
    src/audio/CoreAudioBackend.cpp
    src/audio/EntryPoint.cpp
)
target_link_libraries(metalsharp_audio PRIVATE metalsharp_core ${AUDIOUNIT_FRAMEWORK})
set_target_properties(metalsharp_audio PROPERTIES OUTPUT_NAME "xaudio2_9" PREFIX "")
metalsharp_wine_target(metalsharp_audio)

add_library(metalsharp_input SHARED
    src/input/XInputEngine.cpp
    src/input/GameControllerBridge.mm
    src/input/EntryPoint.cpp
)
target_link_libraries(metalsharp_input PRIVATE metalsharp_core ${GAMECONTROLLER_FRAMEWORK})
target_compile_options(metalsharp_input PRIVATE -fobjc-arc)
set_target_properties(metalsharp_input PROPERTIES OUTPUT_NAME "xinput1_4" PREFIX "")
metalsharp_wine_target(metalsharp_input)

add_executable(metalsharp_launcher
    tools/launcher/main.cpp
    tools/launcher/WinePrefix.cpp
    tools/launcher/WinePrefix.mm
    tools/launcher/Config.cpp
    tools/launcher/SteamIntegration.cpp
)
target_link_libraries(metalsharp_launcher PRIVATE metalsharp_core ${FOUNDATION_FRAMEWORK})
metalsharp_wine_target(metalsharp_launcher)

add_library(metalsharp_loader STATIC
    src/loader/PELoader.cpp
    src/loader/D3DShims.cpp
    ${METALSHARP_D3D11_SOURCES}
    ${METALSHARP_D3D12_SOURCES}
    ${METALSHARP_DXGI_SOURCES}
    src/win32/kernel32/Kernel32Shim.cpp
    src/win32/kernel32/NtdllShim.cpp
    src/win32/kernel32/Kernel32Extra.cpp
    src/win32/kernel32/DRMShims.cpp
    src/win32/kernel32/VirtualFileSystem.cpp
    src/win32/kernel32/Registry.cpp
    src/win32/kernel32/NetworkContext.cpp
    src/win32/kernel32/SecureTransport.mm
    src/win32/user32/User32Shim.cpp
    src/win32/user32/WindowManager.mm
    src/win32/extra/ExtraShims.cpp
)
target_include_directories(metalsharp_loader PUBLIC include)
target_link_libraries(metalsharp_loader PRIVATE metalsharp_core ${SECURITY_FRAMEWORK})
target_compile_options(metalsharp_loader PRIVATE -fobjc-arc)
target_compile_definitions(metalsharp_loader PRIVATE METALSHARP_NATIVE_LOADER)
metalsharp_wine_target(metalsharp_loader)

add_executable(metalsharp_native
    tools/launcher/NativeLauncher.cpp
    src/input/GameControllerBridge.mm
)
target_link_libraries(metalsharp_native PRIVATE metalsharp_loader metalsharp_core ${GAMECONTROLLER_FRAMEWORK})
target_compile_options(metalsharp_native PRIVATE -fobjc-arc)
set_target_properties(metalsharp_native PROPERTIES OUTPUT_NAME "metalsharp")
metalsharp_wine_target(metalsharp_native)

add_executable(metalsharp_migrator
    tools/migrator/main.m
)
target_link_libraries(metalsharp_migrator PRIVATE ${APPKIT_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
set_target_properties(metalsharp_migrator PROPERTIES OUTPUT_NAME "MetalSharpMigrator")
metalsharp_host_target(metalsharp_migrator)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
