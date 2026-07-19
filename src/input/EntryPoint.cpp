/// @file EntryPoint.cpp
/// @brief XInput DLL entry point with COM class factory exports.
///
/// Exports DllGetClassObject and DllCanUnloadNow for xinput1_4.dll loading. Routes XInputGetState/XInputSetState calls
/// to the GameControllerBridge.
#include <metalsharp/GameControllerBridge.h>
#include <mutex>

using namespace metalsharp;

namespace {
// HRESULT returned by XInput when the requested user index has no controller connected.
constexpr HRESULT kXInputErrorDeviceNotConnected = (HRESULT)0x48F;
} // namespace

extern "C" {

static GameControllerBridge g_bridge;
static std::once_flag g_initFlag;

static void ensureBridgeInit() {
    std::call_once(g_initFlag, [] { g_bridge.init(); });
}

HRESULT XInputGetState(DWORD dwUserIndex, void* pState) {
    ensureBridgeInit();
    if (!pState)
        return E_POINTER;
    if (dwUserIndex >= 4)
        return E_FAIL;
    auto* state = static_cast<XInputState*>(pState);
    return g_bridge.getState(dwUserIndex, state) ? S_OK : kXInputErrorDeviceNotConnected;
}

HRESULT XInputSetState(DWORD dwUserIndex, void* pVibration) {
    ensureBridgeInit();
    if (!pVibration)
        return E_POINTER;
    if (dwUserIndex >= 4)
        return E_FAIL;
    auto* vib = static_cast<uint16_t*>(pVibration);
    return g_bridge.setState(dwUserIndex, vib[0], vib[1]) ? S_OK : kXInputErrorDeviceNotConnected;
}

HRESULT XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCapabilities) {
    ensureBridgeInit();
    if (!pCapabilities)
        return E_POINTER;
    if (dwUserIndex >= 4)
        return E_FAIL;
    auto* caps = static_cast<XInputCapabilities*>(pCapabilities);
    return g_bridge.getCapabilities(dwUserIndex, caps) ? S_OK : kXInputErrorDeviceNotConnected;
}

HRESULT XInputRefresh(void) {
    ensureBridgeInit();
    g_bridge.refresh();
    return S_OK;
}
}
