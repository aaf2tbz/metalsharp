#include <metalsharp/DXGI.h>
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
    if (!ppSwapChain) return E_POINTER;
    return E_NOTIMPL;
}

}
