#include <metalsharp/Platform.h>

extern "C" {

HRESULT XInputGetState(DWORD dwUserIndex, void* pState) {
    return E_NOTIMPL;
}

HRESULT XInputSetState(DWORD dwUserIndex, void* pVibration) {
    return E_NOTIMPL;
}

HRESULT XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCapabilities) {
    return E_NOTIMPL;
}

}
