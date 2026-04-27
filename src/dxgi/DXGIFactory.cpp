#include <metalsharp/DXGI.h>
#include <metalsharp/D3D11Device.h>
#include <cstring>

namespace metalsharp {

HRESULT DXGIFactory::create(const GUID& riid, void** ppFactory) {
    if (!ppFactory) return E_POINTER;
    auto* factory = new DXGIFactory();
    *ppFactory = factory;
    return S_OK;
}

HRESULT DXGIFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    AddRef();
    *ppvObject = this;
    return S_OK;
}

ULONG DXGIFactory::AddRef() { return ++m_refCount; }
ULONG DXGIFactory::Release() { ULONG c = --m_refCount; if (c == 0) delete this; return c; }

HRESULT DXGIFactory::EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) {
    if (!ppAdapter) return E_POINTER;
    if (Adapter > 0) return DXGI_ERROR_NOT_FOUND;

    struct DXGIAdapter : public IDXGIAdapter {
        ULONG refCount = 1;
        HRESULT QueryInterface(REFIID, void** ppv) { AddRef(); *ppv = this; return S_OK; }
        ULONG AddRef() { return ++refCount; }
        ULONG Release() { ULONG c = --refCount; if (c == 0) delete this; return c; }
        HRESULT GetDevice(const GUID&, void**) { return E_NOTIMPL; }
        HRESULT GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
        HRESULT SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
        HRESULT SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }
        HRESULT GetParent(const GUID&, void**) { return E_NOTIMPL; }
        HRESULT EnumOutputs(UINT, IDXGIOutput**) { return DXGI_ERROR_NOT_FOUND; }
        HRESULT GetDesc(void* pDesc) {
            if (!pDesc) return E_POINTER;
            memset(pDesc, 0, 280);
            return S_OK;
        }
        HRESULT CheckInterfaceSupport(const GUID&, UINT64*) { return E_NOTIMPL; }
    };

    *ppAdapter = new DXGIAdapter();
    return S_OK;
}

HRESULT DXGIFactory::CreateSwapChain(IUnknown* pDevice, void* pDesc, IDXGISwapChain** ppSwapChain) {
    if (!ppSwapChain || !pDesc) return E_POINTER;

    auto* desc = static_cast<DXGI_SWAP_CHAIN_DESC*>(pDesc);

    D3D11Device* d3d11Device = nullptr;
    HRESULT hr = pDevice->QueryInterface(__uuidof(ID3D11Device), (void**)&d3d11Device);
    if (FAILED(hr) || !d3d11Device) {
        return E_INVALIDARG;
    }

    MetalDevice* metalDev = &d3d11Device->metalDevice();

    uint32_t width = desc->BufferDesc.Width;
    uint32_t height = desc->BufferDesc.Height;
    uint32_t bufferCount = desc->BufferCount > 0 ? desc->BufferCount : 2;
    DXGI_FORMAT format = desc->BufferDesc.Format;

    if (width == 0) width = 1920;
    if (height == 0) height = 1080;
    if (format == 0) format = DXGI_FORMAT_R8G8B8A8_UNORM;

    hr = DXGISwapChainImpl::create(metalDev, desc->OutputWindow, width, height, bufferCount, format, ppSwapChain);

    d3d11Device->Release();
    return hr;
}

}
