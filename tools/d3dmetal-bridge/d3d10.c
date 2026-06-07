#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID ctx) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}
HRESULT WINAPI D3D10CreateDevice(void) {
    return 0x80004005;
}
