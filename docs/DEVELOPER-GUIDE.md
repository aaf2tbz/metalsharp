# Developer Guide

## Architecture

MetalSharp has four main layers:

```
Windows Game (.exe)
    │
    ├── Native PE Loader (Rosetta 2, x86_64)
    │   └── Loads PE directly, resolves imports to shims
    │
    ├── Win32 Shim Layer (14+ DLLs, 400+ functions)
    │   └── Translates Win32 API calls to POSIX/macOS
    │
    ├── D3D/DXGI Shim Layer (d3d11, d3d12, dxgi)
    │   └── Translates D3D calls to Metal API calls
    │
    └── Metal Backend (MTLDevice, MTLBuffer, etc.)
        └── RAII wrappers around Metal objects
```

## Adding Win32 API Shims

### 1. Identify the missing API

Check crash logs at `~/.metalsharp/crashes/` for unresolved imports. The `ImportReporter` tracks all missing functions.

### 2. Add the shim function

In the appropriate DLL shim file (`src/win32/kernel32/`, `src/win32/user32/`, etc.):

```cpp
// In Kernel32Shim.cpp or appropriate file
extern "C" __attribute__((ms_abi)) void* WINAPI MyNewFunction(void* param) {
    // Implement using POSIX/macOS APIs
    return nullptr;
}
```

Key rules:
- Use `__attribute__((ms_abi))` for Windows calling convention
- Use `extern "C"` to avoid name mangling
- Use `WINAPI` macro (expands to `__attribute__((ms_abi))`)
- Return HRESULT-style values matching D3D patterns

### 3. Register the shim

Add the function to the import resolver in `Kernel32Shim.cpp` (or the appropriate shim file):

```cpp
{"MyNewFunction", (void*)MyNewFunction},
```

### 4. Test

Add a test in `tests/` that verifies the shim works correctly.

## Extending D3D Coverage

### Adding a new D3D11 method

1. Add the method declaration to `D3D11Device.h` or `D3D11DeviceContext.h`
2. Implement in `D3D11Device.cpp` or `D3D11DeviceContext.mm`
3. Map D3D concepts to Metal equivalents:
   - Textures → `MTLTexture`
   - Buffers → `MTLBuffer`
   - Shaders → `MTLLibrary` / `MTLFunction`
   - Pipeline state → `MTLRenderPipelineState`
   - Draw calls → `MTLRenderCommandEncoder`

### Adding a new DXGI format

Add the mapping in `FormatTranslation.h` / `FormatTranslation.cpp`:

```cpp
case DXGI_FORMAT_R8G8B8A8_UNORM:
    return MTLPixelFormatRGBA8Unorm;
```

### Shader translation

The shader translator lives in `src/metal/shader/`:
- `DXBCParser.cpp` — Parses DXBC bytecode chunks
- `DXBCtoMSL.cpp` — Translates DXBC tokens to MSL source
- `IRConverterBridge.cpp/mm` — Apple's DXIL→Metal converter (when available)

## Build System

### Adding new source files

1. Add `.cpp` files to `metalsharp_core` in `CMakeLists.txt` (or the appropriate target)
2. Add `.mm` files for Metal/ObjC API calls
3. Add headers to `include/metalsharp/`

### Adding new test files

1. Create `tests/test_<name>.cpp`
2. Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_<name> test_<name>.cpp)
target_link_libraries(test_<name> PRIVATE metalsharp_core ...)
add_test(NAME <name> COMMAND test_<name>)
```

## Code Conventions

- `.cpp` for portable C++ logic
- `.mm` for Objective-C++ (Metal/ObjC API calls only)
- COM-style reference counting (`AddRef`/`Release`/`QueryInterface`)
- No exceptions — use HRESULT return codes
- No comments unless genuinely non-obvious
- `PascalCase` classes, `m_camelCase` members, `camelCase` locals/functions

## Performance Subsystems

- **ShaderCache** — FNV-1a hash → metallib on disk, LRU eviction (configurable max MB)
- **PipelineCache** — Descriptor hash → serialized binary, LRU deque
- **BufferPool** — MTLBuffer recycling via `makeAliasable`
- **FramePacer** — Present modes, triple buffering, frame time percentiles
- **GPUProfiler** — Metal GPU timing via `GPUStartTime/GPUEndTime`
- **MetalFXUpscaler** — Runtime `dlopen` for MetalFX framework

## Debug Tips

1. Enable Metal validation: set `metal_validation: true` in settings
2. Check shader cache at `~/.metalsharp/cache/shader_cache/`
3. Use GPU profiler: `gpu_profile: true` in game profile
4. Check import reporter for missing APIs
5. Use DRM detector to scan executables for incompatible systems
