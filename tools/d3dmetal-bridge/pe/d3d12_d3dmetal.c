#include <stdint.h>
#include <stdio.h>
#include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* NtQueryVirtualMemory_t)(HANDLE, void*, ULONG, void*, ULONG, ULONG*);
typedef NTSTATUS(WINAPI* unix_call_fn)(uint64_t, unsigned int, void*);

static HMODULE g_hModule;
static uint64_t g_handle;
static unix_call_fn g_unix_call;
static int g_initialized;

#define GFX_DISPATCH_SIZE 0x160
static uint8_t g_gfx_dispatch[GFX_DISPATCH_SIZE];

static void dbg(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        unsigned long w;
        WriteFile(h, buf, (unsigned long)n, &w, NULL);
    }
}

static void** dispatch_entry(unsigned int index) {
    if ((unsigned int)(index * 8) >= GFX_DISPATCH_SIZE) return NULL;
    return &((void**)g_gfx_dispatch)[index];
}

static void* dispatch_fn(unsigned int index) {
    void** e = dispatch_entry(index);
    return e ? *e : NULL;
}

static BOOL init(void) {
    if (g_initialized) return g_unix_call != NULL;
    g_initialized = 1;

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) { dbg("[d3d12] no ntdll\n"); return FALSE; }

    NtQueryVirtualMemory_t NtQVM = (NtQueryVirtualMemory_t)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    if (!NtQVM) { dbg("[d3d12] no NtQueryVirtualMemory\n"); return FALSE; }

    g_handle = 0;
    NTSTATUS status = NtQVM((HANDLE)(LONG_PTR)-1, (void*)g_hModule, 1000, &g_handle, sizeof(g_handle), NULL);
    dbg("[d3d12] NtQVM(1000) = 0x%lx handle = 0x%llx\n", (long)status, (unsigned long long)g_handle);
    if (!g_handle) { dbg("[d3d12] no handle\n"); return FALSE; }

    void** ptr = (void**)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    if (!ptr || !*ptr) { dbg("[d3d12] no dispatcher\n"); return FALSE; }
    g_unix_call = (unix_call_fn)*ptr;

    dbg("[d3d12] calling unix_call(0, dispatch_buf)\n");
    memset(g_gfx_dispatch, 0, GFX_DISPATCH_SIZE);
    status = g_unix_call(g_handle, 0, g_gfx_dispatch);
    dbg("[d3d12] unix_call(0) = 0x%lx\n", (long)status);

    if (status == 0) {
        void** d = (void**)g_gfx_dispatch;
        dbg("[d3d12] dispatch[0]=%p [1]=%p [2]=%p [3]=%p\n", d[0], d[1], d[2], d[3]);
    }

    return TRUE;
}

#define JUMP_DISPATCH(idx)                                \
    if (!g_initialized && !init()) return E_FAIL;        \
    void* _fn = dispatch_fn(idx);                         \
    if (!_fn) return E_FAIL;                              \
    typedef HRESULT(WINAPI* _fn_t)();                     \
    return ((_fn_t)_fn)()

static BOOL CALLBACK monitor_cb(HMONITOR m, HDC dc, LPRECT r, LPARAM p) {
    (void)m; (void)dc; (void)r; (void)p;
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID ctx) {
    (void)ctx;
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = inst;
        EnumDisplayMonitors(NULL, NULL, monitor_cb, 0);
        DisableThreadLibraryCalls(inst);
        LoadLibraryA("d3d12.dll");
        LoadLibraryA("dxgi.dll");
        init();
    }
    return TRUE;
}

__declspec(dllexport) HRESULT WINAPI D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    if (!g_initialized && !init()) return E_FAIL;
    void* fn = dispatch_fn(1);
    if (!fn) { dbg("[d3d12] D3D12CreateDevice: no dispatch fn at index 1\n"); return E_FAIL; }
    typedef HRESULT(WINAPI* fn_t)(void*, unsigned, void*, void**);
    return ((fn_t)fn)(a, b, c, d);
}

__declspec(dllexport) HRESULT WINAPI D3D12GetDebugInterface(void* a, void** b) {
    return E_FAIL;
}

__declspec(dllexport) HRESULT WINAPI D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    if (!g_initialized && !init()) return E_FAIL;
    void* fn = dispatch_fn(9);
    if (!fn) return E_FAIL;
    typedef HRESULT(WINAPI* fn_t)(const void*, unsigned, void*, void*);
    return ((fn_t)fn)(a, b, c, d);
}

__declspec(dllexport) HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void* a, uint64_t b, void* c, void** d) {
    if (!g_initialized && !init()) return E_FAIL;
    void* fn = dispatch_fn(5);
    if (!fn) return E_FAIL;
    typedef HRESULT(WINAPI* fn_t)(const void*, uint64_t, void*, void**);
    return ((fn_t)fn)(a, b, c, d);
}

__declspec(dllexport) HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    if (!g_initialized && !init()) return E_FAIL;
    void* fn = dispatch_fn(11);
    if (!fn) return E_FAIL;
    typedef HRESULT(WINAPI* fn_t)(const void*, void*, void*);
    return ((fn_t)fn)(a, b, c);
}

__declspec(dllexport) HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void* a, uint64_t b, void* c, void** d) {
    if (!g_initialized && !init()) return E_FAIL;
    void* fn = dispatch_fn(6);
    if (!fn) return E_FAIL;
    typedef HRESULT(WINAPI* fn_t)(const void*, uint64_t, void*, void**);
    return ((fn_t)fn)(a, b, c, d);
}

__declspec(dllexport) HRESULT WINAPI D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    return 0;
}
