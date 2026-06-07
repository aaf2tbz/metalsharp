#include <windows.h>


static LONG (NTAPI *g_unix_call)(int, void *);

static void resolve(void) {
    static int done;
    if (done) return;
    done = 1;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;
    g_unix_call = (void *)GetProcAddress(ntdll, "__wine_unix_call");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve();
    }
    return TRUE;
}

HRESULT WINAPI D3D11CreateDevice(void *a, unsigned b, void *c, unsigned d,
                                  const void *e, unsigned f, unsigned g,
                                  void **h, void *i, void **j) {
    void *args[] = {a, (void*)(uintptr_t)b, c, (void*)(uintptr_t)d, (void*)e, (void*)(uintptr_t)f, (void*)(uintptr_t)g, h, i, j};
    if (g_unix_call) return g_unix_call(1, args);
    return E_FAIL;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(void *a, unsigned b, void *c, unsigned d,
                                              const void *e, unsigned f, unsigned g,
                                              void *h, void **i, void **j, void *k, void **l) {
    void *args[] = {a, (void*)(uintptr_t)b, c, (void*)(uintptr_t)d, (void*)e, (void*)(uintptr_t)f, (void*)(uintptr_t)g, h, i, j, k, l};
    if (g_unix_call) return g_unix_call(2, args);
    return E_FAIL;
}
