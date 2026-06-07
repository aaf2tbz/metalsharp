#include <stdint.h>
#include <windows.h>

typedef LONG NTSTATUS;
typedef unsigned long long uint64_t;
typedef NTSTATUS(WINAPI* unix_call_fn)(uint64_t, unsigned int, void*);

static HMODULE g_hModule;
static uint64_t g_handle;
static unix_call_fn g_unix_call;

#define GFX_DISPATCH_SIZE 0x4000
static uint8_t g_gfx_dispatch[GFX_DISPATCH_SIZE];

static BOOL init(void) {
    HMODULE ntdll;
    NTSTATUS(NTAPI * NtQVM)(HANDLE, void*, ULONG, void*, unsigned long, unsigned long*);
    void** ptr;

    if (g_unix_call)
        return TRUE;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return FALSE;

    NtQVM = (void*)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    if (!NtQVM)
        return FALSE;

    g_handle = 0;
    NtQVM((HANDLE)(LONG_PTR)-1, (void*)g_hModule, 0x3E8, &g_handle, sizeof(g_handle), NULL);
    if (!g_handle)
        return FALSE;

    ptr = (void**)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    if (!ptr || !*ptr)
        return FALSE;
    g_unix_call = (unix_call_fn)*ptr;

    memset(g_gfx_dispatch, 0, GFX_DISPATCH_SIZE);
    g_unix_call(g_handle, 0, g_gfx_dispatch);

    return TRUE;
}

static void* dispatch_fn(unsigned int offset) {
    if (offset + 8 > GFX_DISPATCH_SIZE)
        return NULL;
    void** table = (void**)g_gfx_dispatch;
    return table[offset / 8];
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID ctx) {
    (void)ctx;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        init();
    }
    return TRUE;
}

static HRESULT WINAPI _CreateDXGIFactory2(unsigned flags, const void* riid, void** factory) {
    if (!g_unix_call && !init()) return E_FAIL;
    void* fn = dispatch_fn(0x8);
    if (!fn) return E_FAIL;
    typedef HRESULT(WINAPI* fn_t)(unsigned, const void*, void**);
    return ((fn_t)fn)(flags, riid, factory);
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory(const void* riid, void** factory) {
    return _CreateDXGIFactory2(0, riid, factory);
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory1(const void* riid, void** factory) {
    return _CreateDXGIFactory2(0, riid, factory);
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory2(unsigned flags, const void* riid, void** factory) {
    return _CreateDXGIFactory2(flags, riid, factory);
}

__declspec(dllexport) HRESULT WINAPI DXGID3D10CreateDevice(void) {
    return 0x80004005;
}

__declspec(dllexport) HRESULT WINAPI DXGID3D10RegisterLayers(void) {
    return 0x80004005;
}

__declspec(dllexport) HRESULT WINAPI DXGIGetDebugInterface1(unsigned a, const void* b, void** c) {
    return 0x80004005;
}

__declspec(dllexport) HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void) {
    return 0;
}
