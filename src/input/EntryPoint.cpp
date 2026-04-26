#include <metalsharp/GameControllerBridge.h>

using namespace metalsharp;

extern "C" {

static GameControllerBridge g_bridge;

HRESULT XInputGetState(DWORD dwUserIndex, void* pState) {
    if (!pState) return E_POINTER;
    if (dwUserIndex >= 4) return E_FAIL;
    auto* state = static_cast<XInputState*>(pState);
    return g_bridge.getState(dwUserIndex, state) ? S_OK : E_FAIL;
}

HRESULT XInputSetState(DWORD dwUserIndex, void* pVibration) {
    if (!pVibration) return E_POINTER;
    if (dwUserIndex >= 4) return E_FAIL;
    auto* vib = static_cast<uint16_t*>(pVibration);
    return g_bridge.setState(dwUserIndex, vib[0], vib[1]) ? S_OK : E_FAIL;
}

HRESULT XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCapabilities) {
    if (!pCapabilities) return E_POINTER;
    if (dwUserIndex >= 4) return E_FAIL;
    auto* caps = static_cast<XInputCapabilities*>(pCapabilities);
    return g_bridge.getCapabilities(dwUserIndex, caps) ? S_OK : E_FAIL;
}

}
