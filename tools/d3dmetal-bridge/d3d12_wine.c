#include <stdio.h>
#include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* unix_call_fn)(void*, ULONG, void*);

extern void* __imp___wine_unixlib_handle;

static unix_call_fn g_dispatcher;
static int g_initialized;

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

static void ensure_init(void) {
    if (g_initialized)
        return;
    g_initialized = 1;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    dbg("[d3d12] ntdll=%p\n", ntdll);
    if (!ntdll)
        return;
    void* p = (void*)(uintptr_t)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    dbg("[d3d12] GetProcAddress(dispatcher)=%p\n", p);
    if (p) {
        g_dispatcher = *(unix_call_fn*)p;
        dbg("[d3d12] *dispatcher=%p\n", g_dispatcher);
    }
    dbg("[d3d12] __imp___wine_unixlib_handle=%p\n", (void*)(uintptr_t)__imp___wine_unixlib_handle);
}

static NTSTATUS call(ULONG ordinal, void* args) {
    ensure_init();
    if (!g_dispatcher) {
        dbg("[d3d12] call(%lu) FAIL: no dispatcher\n", ordinal);
        return 0x80004005;
    }
    void* handle = (void*)(uintptr_t)__imp___wine_unixlib_handle;
    if (!handle) {
        dbg("[d3d12] call(%lu) FAIL: no unixlib handle\n", ordinal);
        return 0x80004005;
    }
    dbg("[d3d12] dispatch(handle=%p, ordinal=%lu)\n", handle, ordinal);
    NTSTATUS r = g_dispatcher(handle, ordinal, args);
    dbg("[d3d12] dispatch returned 0x%08lx\n", (long)r);
    return r;
}

HRESULT WINAPI D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    struct {
        void* a;
        unsigned b;
        void* c;
        void** d;
    } args = {a, b, c, d};
    return call(4, &args);
}
HRESULT WINAPI D3D12GetDebugInterface(void* a, void** b) {
    struct {
        void* a;
        void** b;
    } args = {a, b};
    return call(8, &args);
}
HRESULT WINAPI D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    struct {
        const void* a;
        unsigned b;
        void* c;
        void* d;
    } args = {a, b, c, d};
    return call(9, &args);
}
HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    struct {
        const void* a;
        unsigned long long b;
        void* c;
        void** d;
    } args = {a, b, c, d};
    return call(5, &args);
}
HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    struct {
        const void* a;
        void* b;
        void* c;
    } args = {a, b, c};
    return call(11, &args);
}
HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    struct {
        const void* a;
        unsigned long long b;
        void* c;
        void** d;
    } args = {a, b, c, d};
    return call(6, &args);
}
HRESULT WINAPI D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    struct {
        unsigned a;
        void* b;
        void* c;
        unsigned* d;
    } args = {a, b, c, d};
    return call(7, &args);
}
void WINAPI D3D12CoreRegisterLayers(void) {
    call(3, NULL);
}
HRESULT WINAPI D3D12CoreCreateLayeredDevice(void) {
    return call(1, NULL);
}
unsigned long WINAPI D3D12CoreGetLayeredDeviceSize(void) {
    return (unsigned long)call(2, NULL);
}
long WINAPI GetBehaviorValue(void) {
    return (long)call(10, NULL);
}
