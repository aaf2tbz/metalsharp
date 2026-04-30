#include <dxgi.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

typedef long (*MetalSharpUnixCall_t)(unsigned int, void*);
static MetalSharpUnixCall_t g_unix_call = nullptr;
static void* (*g_get_swapchain)() = nullptr;

enum ms_func_id {
    MS_FUNC_CREATE_SWAP_CHAIN = 2,
    MS_FUNC_PRESENT = 18,
    MS_FUNC_RESIZE_BUFFERS = 19,
    MS_FUNC_GET_BUFFER = 20,
};

struct ms_create_swap_chain_params {
    uint64_t hwnd;
    uint32_t width;
    uint32_t height;
    int windowed;
    int buffer_count;
    int format;
};

struct ms_resize_params {
    uint32_t width;
    uint32_t height;
    uint32_t buffer_count;
};

struct ms_get_buffer_params {
    uint32_t buffer_index;
    uint64_t out_handle;
};

static bool load_d3d11() {
    if (g_unix_call) return true;
    HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
    if (!d3d11) d3d11 = LoadLibraryA("d3d11.dll");
    if (!d3d11) { fprintf(stderr, "[metalsharp-dxgi] cannot load d3d11.dll\n"); return false; }
    
    g_unix_call = (MetalSharpUnixCall_t)GetProcAddress(d3d11, "MetalSharpUnixCall");
    g_get_swapchain = (void*(*)())GetProcAddress(d3d11, "MetalSharpGetSwapChain");
    
    if (!g_unix_call) { fprintf(stderr, "[metalsharp-dxgi] MetalSharpUnixCall not found\n"); return false; }
    fprintf(stderr, "[metalsharp-dxgi] bridge to d3d11.dll OK\n");
    return true;
}

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
    
    HRESULT WINAPI Present(UINT, UINT) override {
        if (g_unix_call) g_unix_call(MS_FUNC_PRESENT, nullptr);
        return S_OK;
    }
    
    HRESULT WINAPI GetBuffer(UINT idx, REFIID, void** pp) override {
        if (!g_unix_call || !pp) { if(pp)*pp=NULL; return E_FAIL; }
        struct ms_get_buffer_params p = {};
        p.buffer_index = idx;
        g_unix_call(MS_FUNC_GET_BUFFER, &p);
        if (p.out_handle) {
            *pp = (void*)(uintptr_t)p.out_handle;
            return S_OK;
        }
        *pp = NULL;
        return E_FAIL;
    }
    
    HRESULT WINAPI GetDesc(DXGI_SWAP_CHAIN_DESC* p) override { memset(p,0,sizeof(*p)); return S_OK; }
    HRESULT WINAPI ResizeBuffers(UINT, UINT w, UINT h, DXGI_FORMAT, UINT) override {
        if (g_unix_call) {
            struct ms_resize_params p = {w, h, 2};
            g_unix_call(MS_FUNC_RESIZE_BUFFERS, &p);
        }
        return S_OK;
    }
    HRESULT WINAPI ResizeTarget(const DXGI_MODE_DESC*) override { return S_OK; }
    HRESULT WINAPI GetContainingOutput(IDXGIOutput** pp) override { if(pp)*pp=NULL; return E_NOTIMPL; }
    HRESULT WINAPI GetFrameStatistics(DXGI_FRAME_STATISTICS* p) override { memset(p,0,sizeof(*p)); return S_OK; }
    HRESULT WINAPI GetLastPresentCount(UINT* p) override { if(p)*p=0; return S_OK; }
    HRESULT WINAPI SetFullscreenState(BOOL, IDXGIOutput*) override { return S_OK; }
    HRESULT WINAPI GetFullscreenState(BOOL*, IDXGIOutput**) override { return S_OK; }
};

static MSCPSwapChain g_swapchain;

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
    HRESULT WINAPI CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) override {
        if (!ppSwapChain) return E_POINTER;
        load_d3d11();
        
        if (g_unix_call && pDesc) {
            struct ms_create_swap_chain_params p = {};
            p.width = pDesc->BufferDesc.Width;
            p.height = pDesc->BufferDesc.Height;
            p.windowed = pDesc->Windowed;
            p.buffer_count = pDesc->BufferCount;
            p.format = (int)pDesc->BufferDesc.Format;
            p.hwnd = (uint64_t)(uintptr_t)pDesc->OutputWindow;
            g_unix_call(MS_FUNC_CREATE_SWAP_CHAIN, &p);
        }
        
        *ppSwapChain = &g_swapchain;
        return S_OK;
    }
    HRESULT WINAPI CreateSoftwareAdapter(HMODULE, IDXGIAdapter** pp) override {
        if(pp)*pp=&g_adapter; return S_OK;
    }
};

static MSCPGIFactory g_factory;

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) load_d3d11();
    return TRUE;
}

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
