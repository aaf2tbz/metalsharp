#include <windows.h>

typedef LONG(NTAPI* unix_dispatch_fn)(ULONG module, ULONG ordinal, void* args);

extern ULONG __wine_unixlib_handle;
extern unix_dispatch_fn __wine_unix_call_dispatcher;

static LONG dispatch(ULONG ordinal, void* args) {
    if (!__wine_unix_call_dispatcher)
        return E_FAIL;
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, ordinal, args);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}

HRESULT WINAPI bridge_D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    void* args[] = {a, (void*)(uintptr_t)b, c, d};
    return dispatch(1, args);
}

HRESULT WINAPI bridge_D3D12GetDebugInterface(void* a, void** b) {
    void* args[] = {a, b};
    return dispatch(2, args);
}

HRESULT WINAPI bridge_D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    void* args[] = {(void*)a, (void*)(uintptr_t)b, c, d};
    return dispatch(3, args);
}

HRESULT WINAPI bridge_D3D12CreateRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    void* args[] = {(void*)a, (void*)b, c, d};
    return dispatch(4, args);
}

HRESULT WINAPI bridge_D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    void* args[] = {(void*)a, b, c};
    return dispatch(5, args);
}

HRESULT WINAPI bridge_D3D12CreateVersionedRootSignatureDeserializer(const void* a, unsigned long long b, void* c,
                                                                    void** d) {
    void* args[] = {(void*)a, (void*)b, c, d};
    return dispatch(6, args);
}

HRESULT WINAPI bridge_D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    void* args[] = {(void*)(uintptr_t)a, b, c, d};
    return dispatch(7, args);
}

HRESULT WINAPI bridge_CreateDXGIFactory(REFIID riid, void** factory) {
    struct {
        REFIID a;
        void** b;
    } args = {riid, factory};
    return dispatch(8, &args);
}

HRESULT WINAPI bridge_CreateDXGIFactory1(REFIID riid, void** factory) {
    struct {
        REFIID a;
        void** b;
    } args = {riid, factory};
    return dispatch(9, &args);
}

HRESULT WINAPI bridge_CreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    struct {
        UINT a;
        REFIID b;
        void** c;
    } args = {flags, riid, factory};
    return dispatch(10, &args);
}

HRESULT WINAPI bridge_DXGIDeclareAdapterRemovalSupport(void) {
    return dispatch(11, NULL);
}

HRESULT WINAPI bridge_D3D11CreateDevice(void* a, unsigned b, void* c, unsigned d, const void* e, unsigned f, unsigned g,
                                        void** h, void* i, void** j) {
    void* args[] = {
        a, (void*)(uintptr_t)b, c, (void*)(uintptr_t)d, (void*)e, (void*)(uintptr_t)f, (void*)(uintptr_t)g, h, i, j};
    return dispatch(12, args);
}

HRESULT WINAPI bridge_D3D11CreateDeviceAndSwapChain(void* a, unsigned b, void* c, unsigned d, const void* e, unsigned f,
                                                    unsigned g, void* h, void** i, void** j, void* k, void** l) {
    void* args[] = {
        a, (void*)(uintptr_t)b, c, (void*)(uintptr_t)d, (void*)e, (void*)(uintptr_t)f, (void*)(uintptr_t)g, h, i, j, k,
        l};
    return dispatch(13, args);
}
