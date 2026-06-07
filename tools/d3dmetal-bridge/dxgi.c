#include <windows.h>

LONG(NTAPI *p_unix_call)(int ordinal, void *args);

static void resolve(void) {
    static int done;
    if (done) return;
    done = 1;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;
    p_unix_call = (void *)GetProcAddress(ntdll, "__wine_unix_call");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve();
    }
    return TRUE;
}

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory) {
    struct { REFIID a; void **b; } args = { riid, factory };
    if (p_unix_call) return p_unix_call(1, &args);
    return E_FAIL;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory) {
    struct { REFIID a; void **b; } args = { riid, factory };
    if (p_unix_call) return p_unix_call(2, &args);
    return E_FAIL;
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **factory) {
    struct { UINT a; REFIID b; void **c; } args = { flags, riid, factory };
    if (p_unix_call) return p_unix_call(3, &args);
    return E_FAIL;
}

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void) {
    if (p_unix_call) return p_unix_call(4, NULL);
    return E_FAIL;
}
