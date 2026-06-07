#include <windows.h>
#include <stdio.h>

typedef LONG NTSTATUS;
typedef struct _UNICODE_STRING { USHORT Length; USHORT MaximumLength; WCHAR* Buffer; } UNICODE_STRING;
typedef NTSTATUS(NTAPI* NtQueryVirtualMemory_t)(HANDLE, PVOID, ULONG, PVOID, ULONG, unsigned long*);
typedef NTSTATUS(NTAPI* unix_call_fn)(void*, ULONG, void*);

static NtQueryVirtualMemory_t g_NtQueryVirtualMemory;
static unix_call_fn g_dispatcher;
static void* g_handle;

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)

static void dbg(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        unsigned long written;
        WriteFile(h, buf, (unsigned long)n, &written, NULL);
    }
}

static int init_unix_bridge(void) {
    static int attempted = 0;
    if (attempted) return g_handle != NULL;
    attempted = 1;

    dbg("[d3d12] init_unix_bridge starting\n");

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) { dbg("[d3d12] FAIL: ntdll not found\n"); return 0; }
    dbg("[d3d12] ntdll=%p\n", ntdll);

    g_NtQueryVirtualMemory = (NtQueryVirtualMemory_t)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    g_dispatcher = (unix_call_fn)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    dbg("[d3d12] NtQueryVirtualMemory=%p dispatcher=%p\n", g_NtQueryVirtualMemory, g_dispatcher);
    if (!g_NtQueryVirtualMemory || !g_dispatcher) {
        dbg("[d3d12] FAIL: missing ntdll exports\n");
        return 0;
    }

    const char* so_path = getenv("MS_D3DMETAL_UNIXLIB");
    if (!so_path || !so_path[0]) { dbg("[d3d12] FAIL: MS_D3DMETAL_UNIXLIB not set\n"); return 0; }
    dbg("[d3d12] so_path='%s'\n", so_path);

    WCHAR wpath[1024];
    int len = MultiByteToWideChar(CP_UTF8, 0, so_path, -1, wpath, 1024);
    dbg("[d3d12] MultiByteToWideChar len=%d\n", len);
    if (len <= 0) { dbg("[d3d12] FAIL: MultiByteToWideChar returned %d err=%lu\n", len, GetLastError()); return 0; }

    UNICODE_STRING us;
    us.Buffer = wpath;
    us.Length = (USHORT)((len - 1) * sizeof(WCHAR));
    us.MaximumLength = (USHORT)(len * sizeof(WCHAR));

    dbg("[d3d12] calling NtQueryVirtualMemory(0x3EA) path='%hs' Length=%u MaxLen=%u\n",
        so_path, (unsigned)us.Length, (unsigned)us.MaximumLength);

    g_handle = NULL;
    NTSTATUS status = g_NtQueryVirtualMemory((HANDLE)-1, &us, 0x3EA, &g_handle, sizeof(void*), NULL);

    dbg("[d3d12] NtQueryVirtualMemory returned 0x%08lx handle=%p\n", (long)status, g_handle);
    if (status != STATUS_SUCCESS || !g_handle) {
        g_handle = NULL;
        dbg("[d3d12] FAIL: NtQueryVirtualMemory(0x3EA) failed\n");

        dbg("[d3d12] trying info class 0x1000...\n");
        g_handle = NULL;
        status = g_NtQueryVirtualMemory((HANDLE)-1, &us, 0x1000, &g_handle, sizeof(void*), NULL);
        dbg("[d3d12] NtQueryVirtualMemory(0x1000) returned 0x%08lx handle=%p\n", (long)status, g_handle);

        dbg("[d3d12] trying info class 0x3EB...\n");
        g_handle = NULL;
        status = g_NtQueryVirtualMemory((HANDLE)-1, &us, 0x3EB, &g_handle, sizeof(void*), NULL);
        dbg("[d3d12] NtQueryVirtualMemory(0x3EB) returned 0x%08lx handle=%p\n", (long)status, g_handle);

        dbg("[d3d12] trying info class 0x3E9...\n");
        g_handle = NULL;
        status = g_NtQueryVirtualMemory((HANDLE)-1, &us, 0x3E9, &g_handle, sizeof(void*), NULL);
        dbg("[d3d12] NtQueryVirtualMemory(0x3E9) returned 0x%08lx handle=%p\n", (long)status, g_handle);

        return 0;
    }

    dbg("[d3d12] SUCCESS: unix bridge handle=%p\n", g_handle);
    return 1;
}

static NTSTATUS dispatch(ULONG ordinal, void* args) {
    if (!init_unix_bridge()) return E_FAIL;
    return g_dispatcher(g_handle, ordinal, args);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}

HRESULT WINAPI D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    struct { void* a; unsigned b; void* c; void** d; } args = {a, b, c, d};
    return dispatch(4, &args);
}

HRESULT WINAPI D3D12GetDebugInterface(void* a, void** b) {
    struct { void* a; void** b; } args = {a, b};
    return dispatch(8, &args);
}

HRESULT WINAPI D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    struct { const void* a; unsigned b; void* c; void* d; } args = {a, b, c, d};
    return dispatch(9, &args);
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    struct { const void* a; unsigned long long b; void* c; void** d; } args = {a, b, c, d};
    return dispatch(5, &args);
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    struct { const void* a; void* b; void* c; } args = {a, b, c};
    return dispatch(11, &args);
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    struct { const void* a; unsigned long long b; void* c; void** d; } args = {a, b, c, d};
    return dispatch(6, &args);
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    struct { unsigned a; void* b; void* c; unsigned* d; } args = {a, b, c, d};
    return dispatch(7, &args);
}
