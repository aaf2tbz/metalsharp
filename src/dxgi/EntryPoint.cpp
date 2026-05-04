/// @file EntryPoint.cpp
/// @brief DXGI DLL entry points for factory creation.
///
/// Implements CreateDXGIFactory and CreateDXGIFactory1, the entry points that
/// games call to obtain IDXGIFactory instances for adapter and swap chain creation.

#include <dxgi/DXGI.h>
#include <metalsharp/DXGI.h>

using namespace metalsharp;

extern "C" {

HRESULT CreateDXGIFactory(const GUID& riid, void** ppFactory) {
    return DXGIFactory::create(riid, ppFactory);
}

HRESULT CreateDXGIFactory1(const GUID& riid, void** ppFactory) {
    return DXGIFactory::create(riid, ppFactory);
}

HRESULT CreateDXGIFactory2(UINT Flags, const GUID& riid, void** ppFactory) {
    return DXGIFactory::create(riid, ppFactory);
}

}
