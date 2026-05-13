/// @file XInputEngine.cpp
/// @brief XInput API shim delegating to GameController bridge.
///
/// Implements XInputGetState, XInputSetState, and XInputGetCapabilities by polling the GameControllerBridge singleton. Converts controller connection/disconnection events into XInput's device index model.
#include <metalsharp/GameControllerBridge.h>

namespace metalsharp {

static GameControllerBridge g_bridge;
static bool g_initialized = false;

static void ensureInit() {
    if (!g_initialized) {
        g_bridge.init();
        g_initialized = true;
    }
}

}
