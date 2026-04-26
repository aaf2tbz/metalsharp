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
