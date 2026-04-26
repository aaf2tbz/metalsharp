#include <d3d/D3D11.h>
#include <metalsharp/D3D11Device.h>
#include <cstring>

using namespace metalsharp;

extern "C" {

HRESULT D3D11CreateDevice(
    void* pAdapter,
    UINT DriverType,
    HMODULE Software,
    UINT Flags,
    const void* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    void* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    D3D11Device* device = nullptr;
    HRESULT hr = D3D11Device::create(&device);
    if (FAILED(hr)) return hr;

    if (ppDevice) {
        *ppDevice = device;
    } else {
        device->Release();
        return E_POINTER;
    }

    if (ppImmediateContext) {
        device->GetImmediateContext(ppImmediateContext);
    }

    if (pFeatureLevel) {
        *reinterpret_cast<UINT*>(pFeatureLevel) = 0xb000;
    }

    return S_OK;
}

HRESULT D3D11CreateDeviceAndSwapChain(
    void* pAdapter,
    UINT DriverType,
    HMODULE Software,
    UINT Flags,
    const void* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const void* pSwapChainDesc,
    void** ppSwapChain,
    ID3D11Device** ppDevice,
    void* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = D3D11CreateDevice(pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);
    if (FAILED(hr)) return hr;

    return S_OK;
}

}
