#include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* unix_call_fn)(void*, ULONG, void*);

extern void* __imp___wine_unix_call_dispatcher;
extern void* __imp___wine_unixlib_handle;

static NTSTATUS dispatch(ULONG ordinal, void* args) {
    unix_call_fn fn = (unix_call_fn)(uintptr_t)__imp___wine_unix_call_dispatcher;
    if (!fn)
        return 0x80004005;
    return fn(__imp___wine_unixlib_handle, ordinal, args);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}

HRESULT WINAPI bridge_D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    struct {
        void* a;
        unsigned b;
        void* c;
        void** d;
    } args = {a, b, c, d};
    return dispatch(4, &args);
}
HRESULT WINAPI bridge_D3D12GetDebugInterface(void* a, void** b) {
    struct {
        void* a;
        void** b;
    } args = {a, b};
    return dispatch(8, &args);
}
HRESULT WINAPI bridge_D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    struct {
        const void* a;
        unsigned b;
        void* c;
        void* d;
    } args = {a, b, c, d};
    return dispatch(9, &args);
}
HRESULT WINAPI bridge_D3D12CreateRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    struct {
        const void* a;
        unsigned long long b;
        void* c;
        void** d;
    } args = {a, b, c, d};
    return dispatch(5, &args);
}
HRESULT WINAPI bridge_D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    struct {
        const void* a;
        void* b;
        void* c;
    } args = {a, b, c};
    return dispatch(11, &args);
}
HRESULT WINAPI bridge_D3D12CreateVersionedRootSignatureDeserializer(const void* a, unsigned long long b, void* c,
                                                                    void** d) {
    struct {
        const void* a;
        unsigned long long b;
        void* c;
        void** d;
    } args = {a, b, c, d};
    return dispatch(6, &args);
}
HRESULT WINAPI bridge_D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    struct {
        unsigned a;
        void* b;
        void* c;
        unsigned* d;
    } args = {a, b, c, d};
    return dispatch(7, &args);
}
