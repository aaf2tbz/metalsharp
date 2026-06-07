#include <windows.h>

typedef HRESULT(WINAPI* pD3D12CreateDevice)(void*, unsigned, void*, void**);
typedef HRESULT(WINAPI* pD3D12GetDebugInterface)(void*, void**);
typedef HRESULT(WINAPI* pD3D12SerializeRootSignature)(const void*, unsigned, void*, void*);
typedef HRESULT(WINAPI* pD3D12CreateRootSignatureDeserializer)(const void*, unsigned long long, void*, void**);
typedef HRESULT(WINAPI* pD3D12SerializeVersionedRootSignature)(const void*, void*, void*);
typedef HRESULT(WINAPI* pD3D12CreateVersionedRootSignatureDeserializer)(const void*, unsigned long long, void*, void**);
typedef HRESULT(WINAPI* pD3D12EnableExperimentalFeatures)(unsigned, void*, void*, unsigned*);

static pD3D12CreateDevice g_CreateDevice;
static pD3D12GetDebugInterface g_GetDebugInterface;
static pD3D12SerializeRootSignature g_SerializeRootSignature;
static pD3D12CreateRootSignatureDeserializer g_CreateRootSignatureDeserializer;
static pD3D12SerializeVersionedRootSignature g_SerializeVersionedRootSignature;
static pD3D12CreateVersionedRootSignatureDeserializer g_CreateVersionedRootSignatureDeserializer;
static pD3D12EnableExperimentalFeatures g_EnableExperimentalFeatures;

static void resolve(void) {
    static int done;
    if (done)
        return;
    done = 1;

    HMODULE bridge = LoadLibraryA("d3dmetal_bridge.dll");
    if (!bridge)
        return;

    g_CreateDevice = (pD3D12CreateDevice)GetProcAddress(bridge, "bridge_D3D12CreateDevice");
    g_GetDebugInterface = (pD3D12GetDebugInterface)GetProcAddress(bridge, "bridge_D3D12GetDebugInterface");
    g_SerializeRootSignature =
        (pD3D12SerializeRootSignature)GetProcAddress(bridge, "bridge_D3D12SerializeRootSignature");
    g_CreateRootSignatureDeserializer =
        (pD3D12CreateRootSignatureDeserializer)GetProcAddress(bridge, "bridge_D3D12CreateRootSignatureDeserializer");
    g_SerializeVersionedRootSignature =
        (pD3D12SerializeVersionedRootSignature)GetProcAddress(bridge, "bridge_D3D12SerializeVersionedRootSignature");
    g_CreateVersionedRootSignatureDeserializer = (pD3D12CreateVersionedRootSignatureDeserializer)GetProcAddress(
        bridge, "bridge_D3D12CreateVersionedRootSignatureDeserializer");
    g_EnableExperimentalFeatures =
        (pD3D12EnableExperimentalFeatures)GetProcAddress(bridge, "bridge_D3D12EnableExperimentalFeatures");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve();
    }
    return TRUE;
}

HRESULT WINAPI D3D12CreateDevice(void* a, unsigned b, void* c, void** d) {
    if (g_CreateDevice)
        return g_CreateDevice(a, b, c, d);
    return E_FAIL;
}

HRESULT WINAPI D3D12GetDebugInterface(void* a, void** b) {
    if (g_GetDebugInterface)
        return g_GetDebugInterface(a, b);
    return E_FAIL;
}

HRESULT WINAPI D3D12SerializeRootSignature(const void* a, unsigned b, void* c, void* d) {
    if (g_SerializeRootSignature)
        return g_SerializeRootSignature(a, b, c, d);
    return E_FAIL;
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    if (g_CreateRootSignatureDeserializer)
        return g_CreateRootSignatureDeserializer(a, b, c, d);
    return E_FAIL;
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void* a, void* b, void* c) {
    if (g_SerializeVersionedRootSignature)
        return g_SerializeVersionedRootSignature(a, b, c);
    return E_FAIL;
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void* a, unsigned long long b, void* c, void** d) {
    if (g_CreateVersionedRootSignatureDeserializer)
        return g_CreateVersionedRootSignatureDeserializer(a, b, c, d);
    return E_FAIL;
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(unsigned a, void* b, void* c, unsigned* d) {
    if (g_EnableExperimentalFeatures)
        return g_EnableExperimentalFeatures(a, b, c, d);
    return E_FAIL;
}
