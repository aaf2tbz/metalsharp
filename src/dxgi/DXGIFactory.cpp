/// @file DXGIFactory.cpp
/// @brief DXGI factory COM implementation — adapter enumeration and swap chain creation.
///
/// Implements IDXGIFactory (and IDXGIFactory1/2/3/4) COM interfaces. Enumerates display
/// adapters and provides the entry point for swap chain creation against a D3D device.

#include <cstring>
#include <metalsharp/D3D11Device.h>
#include <metalsharp/DXGI.h>

namespace metalsharp {

MetalDevice* metalDeviceForD3D12CommandQueue(IUnknown* queue);

HRESULT DXGIFactory::create(const GUID& riid, void** ppFactory) {
    if (!ppFactory)
        return E_POINTER;
    auto* factory = new DXGIFactory();
    *ppFactory = factory;
    return S_OK;
}

HRESULT DXGIFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject)
        return E_POINTER;
    AddRef();
    *ppvObject = this;
    return S_OK;
}

ULONG DXGIFactory::AddRef() {
    return ++m_refCount;
}
ULONG DXGIFactory::Release() {
    ULONG c = --m_refCount;
    if (c == 0)
        delete this;
    return c;
}

HRESULT DXGIFactory::EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) {
    if (!ppAdapter)
        return E_POINTER;
    if (Adapter > 0)
        return DXGI_ERROR_NOT_FOUND;

    struct DXGIAdapter : public IDXGIAdapter {
        ULONG refCount = 1;
        HRESULT QueryInterface(REFIID, void** ppv) {
            AddRef();
            *ppv = this;
            return S_OK;
        }
        ULONG AddRef() { return ++refCount; }
        ULONG Release() {
            ULONG c = --refCount;
            if (c == 0)
                delete this;
            return c;
        }
        HRESULT GetDevice(const GUID&, void**) { return E_NOTIMPL; }
        HRESULT GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
        HRESULT SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
        HRESULT SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }
        HRESULT GetParent(const GUID&, void**) { return E_NOTIMPL; }
        HRESULT EnumOutputs(UINT, IDXGIOutput**) { return DXGI_ERROR_NOT_FOUND; }
        HRESULT GetDesc(void* pDesc) {
            if (!pDesc)
                return E_POINTER;
            memset(pDesc, 0, 280);
            return S_OK;
        }
        HRESULT CheckInterfaceSupport(const GUID&, UINT64*) { return E_NOTIMPL; }
    };

    *ppAdapter = new DXGIAdapter();
    return S_OK;
}

HRESULT DXGIFactory::CreateSwapChain(IUnknown* pDevice, void* pDesc, IDXGISwapChain** ppSwapChain) {
    if (!ppSwapChain || !pDesc)
        return E_POINTER;

    auto* desc = static_cast<DXGI_SWAP_CHAIN_DESC*>(pDesc);
    return createSwapChainForMetalDevice(pDevice, desc->OutputWindow, desc->BufferDesc.Width, desc->BufferDesc.Height,
                                         desc->BufferCount, desc->BufferDesc.Format, ppSwapChain);
}

HRESULT DXGIFactory::createSwapChainForMetalDevice(IUnknown* pDevice, HWND hWnd, uint32_t width, uint32_t height,
                                                   uint32_t bufferCount, DXGI_FORMAT format,
                                                   IDXGISwapChain** ppSwapChain) {
    if (!ppSwapChain)
        return E_POINTER;
    *ppSwapChain = nullptr;
    if (!pDevice)
        return E_INVALIDARG;

    MetalDevice* metalDev = nullptr;
    D3D11Device* d3d11Device = nullptr;
    HRESULT hr = pDevice->QueryInterface(__uuidof(ID3D11Device), (void**)&d3d11Device);
    if (SUCCEEDED(hr) && d3d11Device) {
        metalDev = &d3d11Device->metalDevice();
    } else {
        metalDev = metalDeviceForD3D12CommandQueue(pDevice);
    }

    if (!metalDev) {
        if (d3d11Device)
            d3d11Device->Release();
        return E_INVALIDARG;
    }

    if (width == 0)
        width = 1920;
    if (height == 0)
        height = 1080;
    if (format == 0)
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bufferCount = bufferCount > 0 ? bufferCount : 2;

    hr = DXGISwapChainImpl::create(metalDev, hWnd, width, height, bufferCount, format, ppSwapChain);

    if (d3d11Device)
        d3d11Device->Release();
    return hr;
}

HRESULT DXGIFactory::CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                            IDXGISwapChain1** ppSwapChain) {
    if (!ppSwapChain || !pDesc)
        return E_POINTER;
    IDXGISwapChain* swapChain = nullptr;
    HRESULT hr = createSwapChainForMetalDevice(pDevice, hWnd, pDesc->Width, pDesc->Height, pDesc->BufferCount,
                                               pDesc->Format, &swapChain);
    if (FAILED(hr))
        return hr;
    *ppSwapChain = static_cast<IDXGISwapChain1*>(swapChain);
    return S_OK;
}

HRESULT DXGIFactory::CreateSwapChainForCoreWindow(IUnknown* pDevice, IUnknown* pWindow,
                                                  const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                  IDXGISwapChain1** ppSwapChain) {
    return CreateSwapChainForHwnd(pDevice, reinterpret_cast<HWND>(pWindow), pDesc, nullptr, pRestrictToOutput,
                                  ppSwapChain);
}

HRESULT DXGIFactory::CreateSwapChainForComposition(IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                   IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    return CreateSwapChainForHwnd(pDevice, nullptr, pDesc, nullptr, pRestrictToOutput, ppSwapChain);
}

HRESULT DXGIFactory::GetSharedResourceAdapterLuid(HANDLE, LUID* pLuid) {
    if (!pLuid)
        return E_POINTER;
    pLuid->LowPart = 0x106b;
    pLuid->HighPart = 0;
    return S_OK;
}

HRESULT DXGIFactory::RegisterStereoStatusWindow(HWND, UINT, DWORD* pdwCookie) {
    if (!pdwCookie)
        return E_POINTER;
    *pdwCookie = 1;
    return S_OK;
}

HRESULT DXGIFactory::RegisterStereoStatusEvent(HANDLE, DWORD* pdwCookie) {
    if (!pdwCookie)
        return E_POINTER;
    *pdwCookie = 1;
    return S_OK;
}

HRESULT DXGIFactory::RegisterOcclusionStatusWindow(HWND, UINT, DWORD* pdwCookie) {
    if (!pdwCookie)
        return E_POINTER;
    *pdwCookie = 2;
    return S_OK;
}

HRESULT DXGIFactory::RegisterOcclusionStatusEvent(HANDLE, DWORD* pdwCookie) {
    if (!pdwCookie)
        return E_POINTER;
    *pdwCookie = 2;
    return S_OK;
}

HRESULT DXGIFactory::EnumAdapterByLuid(LUID, const GUID&, void** ppvAdapter) {
    if (!ppvAdapter)
        return E_POINTER;
    IDXGIAdapter* adapter = nullptr;
    HRESULT hr = EnumAdapters(0, &adapter);
    *ppvAdapter = adapter;
    return hr;
}

HRESULT DXGIFactory::EnumWarpAdapter(const GUID&, void** ppvAdapter) {
    if (!ppvAdapter)
        return E_POINTER;
    *ppvAdapter = nullptr;
    return DXGI_ERROR_NOT_FOUND;
}

} // namespace metalsharp
