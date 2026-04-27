#include <d3d/D3D11.h>
#include <metalsharp/D3D11Device.h>
#include <metalsharp/DXGI.h>
#include <cstring>

using namespace metalsharp;

static HRESULT CreateSwapChainForDevice(D3D11Device* device, const void* pSwapChainDesc, void** ppSwapChain) {
    if (!ppSwapChain || !pSwapChainDesc) return E_POINTER;

    auto* desc = static_cast<const DXGI_SWAP_CHAIN_DESC*>(pSwapChainDesc);

    uint32_t width = desc->BufferDesc.Width;
    uint32_t height = desc->BufferDesc.Height;
    uint32_t bufferCount = desc->BufferCount > 0 ? desc->BufferCount : 2;
    DXGI_FORMAT format = desc->BufferDesc.Format;

    if (width == 0) width = 1920;
    if (height == 0) height = 1080;
    if (format == 0) format = DXGI_FORMAT_R8G8B8A8_UNORM;

    IDXGISwapChain* swapChain = nullptr;
    HRESULT hr = DXGISwapChainImpl::create(&device->metalDevice(), desc->OutputWindow, width, height, bufferCount, format, &swapChain);
    if (SUCCEEDED(hr)) {
        *ppSwapChain = swapChain;
    }
    return hr;
}

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

    if (ppSwapChain && pSwapChainDesc) {
        hr = CreateSwapChainForDevice(device, pSwapChainDesc, ppSwapChain);
        if (FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

}
