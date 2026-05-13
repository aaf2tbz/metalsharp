/// @file D3DShims.cpp
/// @brief D3D DLL shim registration for import resolution.
///
/// Registers d3d11.dll, d3d10.dll, and d3d9.dll exports with the PE loader so that imported D3D functions resolve to
/// MetalSharp's COM implementations. Acts as the bridge between the PE loader's import resolver and D3D shim objects.
#include <cstring>
#include <d3d/D3D11.h>
#include <metalsharp/D3DShims.h>

extern "C" {
HRESULT D3D11CreateDevice(void*, UINT, HMODULE, UINT, const void*, UINT, UINT, ID3D11Device**, void*,
                          ID3D11DeviceContext**);
HRESULT D3D11CreateDeviceAndSwapChain(void*, UINT, HMODULE, UINT, const void*, UINT, UINT, const void*, void**,
                                      ID3D11Device**, void*, ID3D11DeviceContext**);
}

extern "C" HRESULT D3D12CreateDevice(void*, UINT, const GUID&, void**);
extern "C" HRESULT CreateDXGIFactory(const GUID&, void**);
extern "C" HRESULT CreateDXGIFactory1(const GUID&, void**);
extern "C" HRESULT CreateDXGIFactory2(UINT, const GUID&, void**);

namespace metalsharp {
namespace win32 {

ShimLibrary createD3D11Shim() {
    ShimLibrary lib;
    lib.name = "d3d11.dll";

    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["D3D11CreateDevice"] = fn((void*)D3D11CreateDevice);
    lib.functions["D3D11CreateDeviceAndSwapChain"] = fn((void*)D3D11CreateDeviceAndSwapChain);

    return lib;
}

ShimLibrary createD3D12Shim() {
    ShimLibrary lib;
    lib.name = "d3d12.dll";

    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["D3D12CreateDevice"] = fn((void*)D3D12CreateDevice);

    return lib;
}

ShimLibrary createDxgiShim() {
    ShimLibrary lib;
    lib.name = "dxgi.dll";

    auto fn = [](void* ptr) -> ExportedFunction { return [ptr]() -> void* { return ptr; }; };

    lib.functions["CreateDXGIFactory"] = fn((void*)CreateDXGIFactory);
    lib.functions["CreateDXGIFactory1"] = fn((void*)CreateDXGIFactory1);
    lib.functions["CreateDXGIFactory2"] = fn((void*)CreateDXGIFactory2);

    return lib;
}

} // namespace win32
} // namespace metalsharp
