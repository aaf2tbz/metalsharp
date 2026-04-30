#include <dxgi.h>
#include <string.h>

class MSCPSwapChain : public IDXGISwapChain {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=NULL; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG WINAPI Release() override { LONG r = InterlockedDecrement(&m_ref); return r?r:1; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI GetParent(REFIID, void** pp) override { if(pp)*pp=NULL; return E_NOINTERFACE; }
    HRESULT WINAPI GetDevice(REFIID, void** pp) override { if(pp)*pp=NULL; return E_NOINTERFACE; }
    HRESULT WINAPI Present(UINT, UINT) override { return S_OK; }
    HRESULT WINAPI GetBuffer(UINT, REFIID, void** pp) override { if(pp)*pp=(void*)1; return S_OK; }
    HRESULT WINAPI GetDesc(DXGI_SWAP_CHAIN_DESC* p) override { memset(p,0,sizeof(*p)); return S_OK; }
    HRESULT WINAPI ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override { return S_OK; }
    HRESULT WINAPI ResizeTarget(const DXGI_MODE_DESC*) override { return S_OK; }
    HRESULT WINAPI GetContainingOutput(IDXGIOutput** pp) override { if(pp)*pp=NULL; return E_NOTIMPL; }
    HRESULT WINAPI GetFrameStatistics(DXGI_FRAME_STATISTICS* p) override { memset(p,0,sizeof(*p)); return S_OK; }
    HRESULT WINAPI GetLastPresentCount(UINT* p) override { if(p)*p=0; return S_OK; }
    HRESULT WINAPI SetFullscreenState(BOOL, IDXGIOutput*) override { return S_OK; }
    HRESULT WINAPI GetFullscreenState(BOOL*, IDXGIOutput**) override { return S_OK; }
};

class MSCPADapter : public IDXGIAdapter {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=NULL; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG WINAPI Release() override { LONG r = InterlockedDecrement(&m_ref); return r?r:1; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI GetParent(REFIID, void** pp) override { if(pp)*pp=NULL; return E_NOINTERFACE; }
    HRESULT WINAPI EnumOutputs(UINT idx, IDXGIOutput** pp) override { if(pp)*pp=NULL; return DXGI_ERROR_NOT_FOUND; }
    HRESULT WINAPI GetDesc(DXGI_ADAPTER_DESC* p) override {
        memset(p,0,sizeof(*p));
        p->VendorId = 0x106B;
        p->DedicatedVideoMemory = 512*1024*1024;
        return S_OK;
    }
    HRESULT WINAPI CheckInterfaceSupport(REFGUID, LARGE_INTEGER* pv) override { if(pv)pv->QuadPart=1; return S_OK; }
};

static MSCPADapter g_adapter;
static MSCPSwapChain g_swapchain;

class MSCPGIFactory : public IDXGIFactory {
    LONG m_ref = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID, void** ppv) override { if(ppv)*ppv=NULL; return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG WINAPI Release() override { LONG r = InterlockedDecrement(&m_ref); return r?r:1; }
    HRESULT WINAPI SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
    HRESULT WINAPI GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
    HRESULT WINAPI GetParent(REFIID, void** pp) override { if(pp)*pp=NULL; return E_NOINTERFACE; }
    HRESULT WINAPI EnumAdapters(UINT idx, IDXGIAdapter** pp) override {
        if(idx==0){if(pp)*pp=&g_adapter; return S_OK;}
        if(pp)*pp=NULL; return DXGI_ERROR_NOT_FOUND;
    }
    HRESULT WINAPI MakeWindowAssociation(HWND, UINT) override { return S_OK; }
    HRESULT WINAPI GetWindowAssociation(HWND* p) override { if(p)*p=NULL; return S_OK; }
    HRESULT WINAPI CreateSwapChain(IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain** pp) override {
        if(pp)*pp=&g_swapchain; return S_OK;
    }
    HRESULT WINAPI CreateSoftwareAdapter(HMODULE, IDXGIAdapter** pp) override {
        if(pp)*pp=&g_adapter; return S_OK;
    }
};

static MSCPGIFactory g_factory;

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory(REFIID, void** pp) {
    if(pp) *pp = &g_factory;
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory1(REFIID, void** pp) {
    if(pp) *pp = &g_factory;
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory2(UINT, REFIID, void** pp) {
    if(pp) *pp = &g_factory;
    return S_OK;
}

}
