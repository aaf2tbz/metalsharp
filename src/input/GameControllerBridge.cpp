#include <metalsharp/GameControllerBridge.h>
#include <cstring>

namespace metalsharp {

GameControllerBridge::GameControllerBridge() {
    memset(m_states, 0, sizeof(m_states));
    memset(m_connected, 0, sizeof(m_connected));
    m_impl = nullptr;
}

GameControllerBridge::~GameControllerBridge() = default;

bool GameControllerBridge::init() { return false; }
void GameControllerBridge::poll() {}

bool GameControllerBridge::getState(uint32_t index, XInputState* pState) {
    if (!pState || index >= MAX_CONTROLLERS) return false;
    memset(pState, 0, sizeof(XInputState));
    return false;
}

bool GameControllerBridge::setState(uint32_t, uint16_t, uint16_t) { return false; }
bool GameControllerBridge::getCapabilities(uint32_t, XInputCapabilities*) { return false; }

}
