#include <windows.h>

typedef HRESULT(WINAPI* pD3D11CreateDevice)(void*, unsigned, void*, unsigned, const void*, unsigned, unsigned, void**,
                                            void*, void**);
typedef HRESULT(WINAPI* pD3D11CreateDeviceAndSwapChain)(void*, unsigned, void*, unsigned, const void*, unsigned,
                                                        unsigned, void*, void**, void**, void*, void**);

static pD3D11CreateDevice g_CreateDevice;
static pD3D11CreateDeviceAndSwapChain g_CreateDeviceAndSwapChain;

static void resolve(void) {
    static int done;
    if (done)
        return;
    done = 1;

    HMODULE bridge = LoadLibraryA("d3dmetal_bridge.dll");
    if (!bridge)
        return;

    g_CreateDevice = (pD3D11CreateDevice)GetProcAddress(bridge, "bridge_D3D11CreateDevice");
    g_CreateDeviceAndSwapChain =
        (pD3D11CreateDeviceAndSwapChain)GetProcAddress(bridge, "bridge_D3D11CreateDeviceAndSwapChain");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve();
    }
    return TRUE;
}

HRESULT WINAPI D3D11CreateDevice(void* a, unsigned b, void* c, unsigned d, const void* e, unsigned f, unsigned g,
                                 void** h, void* i, void** j) {
    if (g_CreateDevice)
        return g_CreateDevice(a, b, c, d, e, f, g, h, i, j);
    return E_FAIL;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(void* a, unsigned b, void* c, unsigned d, const void* e, unsigned f,
                                             unsigned g, void* h, void** i, void** j, void* k, void** l) {
    if (g_CreateDeviceAndSwapChain)
        return g_CreateDeviceAndSwapChain(a, b, c, d, e, f, g, h, i, j, k, l);
    return E_FAIL;
}
