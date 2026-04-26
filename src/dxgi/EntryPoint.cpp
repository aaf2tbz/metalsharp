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
