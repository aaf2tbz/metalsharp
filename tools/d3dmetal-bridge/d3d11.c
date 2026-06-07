#include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI *dispatch_fn)(void*, ULONG, void*);

extern void* __imp___wine_unix_call_dispatcher;
extern void* __imp___wine_unixlib_handle;

static NTSTATUS call(ULONG ordinal, void* args) {
    dispatch_fn fn = (dispatch_fn)(uintptr_t)__imp___wine_unix_call_dispatcher;
    if (!fn) return 0x80004005;
    return fn(__imp___wine_unixlib_handle, ordinal, args);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}

HRESULT WINAPI D3D11CreateDevice(void* a, unsigned b, void* c, unsigned d, const void* e, unsigned f, unsigned g,
                                 void** h, void* i, void** j) {
    struct { void* a; unsigned b; void* c; unsigned d; const void* e; unsigned f; unsigned g; void** h; void* i; void** j; }
        args = {a, b, c, d, e, f, g, h, i, j};
    return call(1, &args);
}
HRESULT WINAPI D3D11CreateDeviceAndSwapChain(void* a, unsigned b, void* c, unsigned d, const void* e, unsigned f,
                                             unsigned g, void* h, void** i, void** j, void* k, void** l) {
    struct { void* a; unsigned b; void* c; unsigned d; const void* e; unsigned f; unsigned g; void* h; void** i; void** j;
             void* k; void** l; }
        args = {a, b, c, d, e, f, g, h, i, j, k, l};
    return call(2, &args);
}
HRESULT WINAPI D3D11On12CreateDevice(void* a, unsigned b, const void* c, void** d, unsigned e, void* f, unsigned g,
                                     void** h, void** i, void** j) {
    struct { void* a; unsigned b; const void* c; void** d; unsigned e; void* f; unsigned g; void** h; void** i; void** j; }
        args = {a, b, c, d, e, f, g, h, i, j};
    return call(3, &args);
}
