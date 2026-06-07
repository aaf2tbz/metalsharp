#include <windows.h>

typedef HRESULT(WINAPI* pCreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* pCreateDXGIFactory2)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* pDXGIDeclareAdapterRemovalSupport)(void);

static pCreateDXGIFactory g_CreateFactory;
static pCreateDXGIFactory g_CreateFactory1;
static pCreateDXGIFactory2 g_CreateFactory2;
static pDXGIDeclareAdapterRemovalSupport g_DeclareAdapterRemovalSupport;

static void resolve(void) {
    static int done;
    if (done)
        return;
    done = 1;

    HMODULE bridge = LoadLibraryA("d3dmetal_bridge.dll");
    if (!bridge)
        return;

    g_CreateFactory = (pCreateDXGIFactory)GetProcAddress(bridge, "bridge_CreateDXGIFactory");
    g_CreateFactory1 = (pCreateDXGIFactory)GetProcAddress(bridge, "bridge_CreateDXGIFactory1");
    g_CreateFactory2 = (pCreateDXGIFactory2)GetProcAddress(bridge, "bridge_CreateDXGIFactory2");
    g_DeclareAdapterRemovalSupport =
        (pDXGIDeclareAdapterRemovalSupport)GetProcAddress(bridge, "bridge_DXGIDeclareAdapterRemovalSupport");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve();
    }
    return TRUE;
}

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** factory) {
    if (g_CreateFactory)
        return g_CreateFactory(riid, factory);
    return E_FAIL;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory) {
    if (g_CreateFactory1)
        return g_CreateFactory1(riid, factory);
    return E_FAIL;
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    if (g_CreateFactory2)
        return g_CreateFactory2(flags, riid, factory);
    return E_FAIL;
}

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void) {
    if (g_DeclareAdapterRemovalSupport)
        return g_DeclareAdapterRemovalSupport();
    return E_FAIL;
}
