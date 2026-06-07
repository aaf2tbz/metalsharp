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

__declspec(dllexport) HRESULT WINAPI D3D11CreateDevice(void* adapter, unsigned driver_type, void* software,
                                                       unsigned flags, const void* feature_levels,
                                                       unsigned feature_levels_count, unsigned sdk_version,
                                                       void** device, void* feature_level, void** immediate_context) {
    if (!g_unix_call && !init())
        return E_FAIL;
    void* fn = dispatch_fn(0x38);
    if (!fn)
        return E_FAIL;
    typedef HRESULT(WINAPI * fn_t)(void*, unsigned, void*, unsigned, const void*, unsigned, unsigned, void**, void*,
                                   void**);
    return ((fn_t)fn)(adapter, driver_type, software, flags, feature_levels, feature_levels_count, sdk_version, device,
                      feature_level, immediate_context);
}

__declspec(dllexport) HRESULT WINAPI D3D11CreateDeviceAndSwapChain(void* adapter, unsigned driver_type, void* software,
                                                                   unsigned flags, const void* feature_levels,
                                                                   unsigned feature_levels_count, unsigned sdk_version,
                                                                   const void* swap_chain_desc, void** swap_chain,
                                                                   void** device, void* feature_level,
                                                                   void** immediate_context) {
    if (!g_unix_call && !init())
        return E_FAIL;
    void* fn = dispatch_fn(0x40);
    if (!fn)
        return E_FAIL;
    typedef HRESULT(WINAPI * fn_t)(void*, unsigned, void*, unsigned, const void*, unsigned, unsigned, const void*,
                                   void**, void**, void*, void**);
    return ((fn_t)fn)(adapter, driver_type, software, flags, feature_levels, feature_levels_count, sdk_version,
                      swap_chain_desc, swap_chain, device, feature_level, immediate_context);
}

__declspec(dllexport) HRESULT WINAPI D3D11On12CreateDevice(void) {
    return 0x80004005;
}
