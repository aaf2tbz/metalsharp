#include <windows.h>

static LONG(NTAPI* g_unix_call)(int, void*);
static void(NTAPI* g_query_vm)(void);

static void resolve(void) {
    static int done;
    if (done)
        return;
    done = 1;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        return;
    g_unix_call = (void*)GetProcAddress(ntdll, "__wine_unix_call");
    g_query_vm = (void*)GetProcAddress(ntdll, "NtQueryVirtualMemory");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve();
    }
    return TRUE;
}

HRESULT WINAPI D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    void* args[] = {a, (void*)(uintptr_t)b, c, d};
    if (g_unix_call)
        return g_unix_call(1, args);
    return E_FAIL;
}

HRESULT WINAPI D3D12GetDebugInterface(void* a, void** b) {
    void* args[] = {a, b};
    if (g_unix_call)
        return g_unix_call(2, args);
    return E_FAIL;
}

HRESULT WINAPI D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    void* args[] = {(void*)a, (void*)(uintptr_t)b, c, d};
    if (g_unix_call)
        return g_unix_call(3, args);
    return E_FAIL;
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    void* args[] = {(void*)a, (void*)b, c, d};
    if (g_unix_call)
        return g_unix_call(4, args);
    return E_FAIL;
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    void* args[] = {(void*)a, b, c};
    if (g_unix_call)
        return g_unix_call(5, args);
    return E_FAIL;
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    void* args[] = {(void*)a, (void*)b, c, d};
    if (g_unix_call)
        return g_unix_call(6, args);
    return E_FAIL;
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    void* args[] = {(void*)(uintptr_t)a, b, c, d};
    if (g_unix_call)
        return g_unix_call(7, args);
    return E_FAIL;
}
