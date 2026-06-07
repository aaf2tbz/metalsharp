#include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* dispatch_fn)(void*, ULONG, void*);

extern void* __imp___wine_unix_call_dispatcher;
extern void* __imp___wine_unixlib_handle;

static NTSTATUS call(ULONG ordinal, void* args) {
    dispatch_fn fn = (dispatch_fn)(uintptr_t)__imp___wine_unix_call_dispatcher;
    if (!fn)
        return 0x80004005;
    return fn(__imp___wine_unixlib_handle, ordinal, args);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** factory) {
    struct {
        REFIID a;
        void** b;
    } args = {riid, factory};
    return call(1, &args);
}
HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory) {
    struct {
        REFIID a;
        void** b;
    } args = {riid, factory};
    return call(2, &args);
}
HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    struct {
        UINT a;
        REFIID b;
        void** c;
    } args = {flags, riid, factory};
    return call(3, &args);
}
HRESULT WINAPI DXGID3D10CreateDevice(void) {
    return call(4, NULL);
}
HRESULT WINAPI DXGID3D10RegisterLayers(void) {
    return call(5, NULL);
}
HRESULT WINAPI DXGIGetDebugInterface1(UINT a, REFIID b, void** c) {
    struct {
        UINT a;
        REFIID b;
        void** c;
    } args = {a, b, c};
    return call(6, &args);
}
HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void) {
    return call(7, NULL);
}
