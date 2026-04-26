#include <metalsharp/GameControllerBridge.h>
#import <GameController/GameController.h>
#include <cstring>

namespace metalsharp {

struct GameControllerBridge::Impl {
    GCController* controllers[4] = {};
    bool connected[4] = {};
    float lastHaptics[4][2] = {};
};

GameControllerBridge::GameControllerBridge() : m_impl(new Impl()) {
    memset(m_states, 0, sizeof(m_states));
    memset(m_connected, 0, sizeof(m_connected));
}

GameControllerBridge::~GameControllerBridge() {
    delete m_impl;
}

bool GameControllerBridge::init() {
    [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification
                                                      object:nil
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification* note) {
        GCController* gc = note.object;
        for (uint32_t i = 0; i < MAX_CONTROLLERS; i++) {
            if (!m_impl->controllers[i]) {
                m_impl->controllers[i] = gc;
                m_impl->connected[i] = true;
                m_connected[i] = true;
                break;
            }
        }
    }];

    [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidDisconnectNotification
                                                      object:nil
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification* note) {
        GCController* gc = note.object;
        for (uint32_t i = 0; i < MAX_CONTROLLERS; i++) {
            if (m_impl->controllers[i] == gc) {
                m_impl->controllers[i] = nil;
                m_impl->connected[i] = false;
                m_connected[i] = false;
                break;
            }
        }
    }];

    NSArray<GCController*>* connected = [GCController controllers];
    for (NSUInteger i = 0; i < connected.count && i < MAX_CONTROLLERS; i++) {
        m_impl->controllers[i] = connected[i];
        m_impl->connected[i] = true;
        m_connected[i] = true;
    }

    return true;
}

static WORD readButtons(GCExtendedGamepad* gamepad) {
    WORD buttons = 0;
    if (gamepad.buttonA.isPressed)               buttons |= 0x1000;
    if (gamepad.buttonB.isPressed)               buttons |= 0x2000;
    if (gamepad.buttonX.isPressed)               buttons |= 0x4000;
    if (gamepad.buttonY.isPressed)               buttons |= 0x8000;
    if (gamepad.leftShoulder.isPressed)          buttons |= 0x0100;
    if (gamepad.rightShoulder.isPressed)         buttons |= 0x0200;
    if (gamepad.leftTrigger.value > 0.1f)        buttons |= 0x0040;
    if (gamepad.rightTrigger.value > 0.1f)       buttons |= 0x0080;
    if (gamepad.dpad.up.isPressed)               buttons |= 0x0001;
    if (gamepad.dpad.down.isPressed)             buttons |= 0x0002;
    if (gamepad.dpad.left.isPressed)             buttons |= 0x0004;
    if (gamepad.dpad.right.isPressed)            buttons |= 0x0008;
    if (gamepad.leftThumbstickButton.isPressed)  buttons |= 0x0040;
    if (gamepad.rightThumbstickButton.isPressed) buttons |= 0x0080;
    if (@available(macOS 11.0, *)) {
        if (gamepad.buttonOptions.isPressed)      buttons |= 0x0010;
        if (gamepad.buttonMenu.isPressed)         buttons |= 0x0020;
    }
    return buttons;
}

void GameControllerBridge::poll() {
    for (uint32_t i = 0; i < MAX_CONTROLLERS; i++) {
        if (!m_impl->controllers[i]) continue;

        GCExtendedGamepad* gamepad = m_impl->controllers[i].extendedGamepad;
        if (!gamepad) continue;

        m_states[i].packetNumber++;
        m_states[i].Gamepad.wButtons = readButtons(gamepad);
        m_states[i].Gamepad.bLeftTrigger  = (BYTE)(gamepad.leftTrigger.value * 255.0f);
        m_states[i].Gamepad.bRightTrigger = (BYTE)(gamepad.rightTrigger.value * 255.0f);
        m_states[i].Gamepad.sThumbLX = (SHORT)(gamepad.leftThumbstick.xAxis.value * 32767.0f);
        m_states[i].Gamepad.sThumbLY = (SHORT)(gamepad.leftThumbstick.yAxis.value * 32767.0f);
        m_states[i].Gamepad.sThumbRX = (SHORT)(gamepad.rightThumbstick.xAxis.value * 32767.0f);
        m_states[i].Gamepad.sThumbRY = (SHORT)(gamepad.rightThumbstick.yAxis.value * 32767.0f);
    }
}

bool GameControllerBridge::getState(uint32_t index, XInputState* pState) {
    if (!pState || index >= MAX_CONTROLLERS) return false;
    if (!m_connected[index]) {
        memset(pState, 0, sizeof(XInputState));
        return false;
    }
    poll();
    *pState = m_states[index];
    return true;
}

bool GameControllerBridge::setState(uint32_t index, uint16_t leftMotor, uint16_t rightMotor) {
    if (index >= MAX_CONTROLLERS || !m_impl->controllers[index]) return false;

    m_impl->lastHaptics[index][0] = leftMotor / 65535.0f;
    m_impl->lastHaptics[index][1] = rightMotor / 65535.0f;

    return true;
}

bool GameControllerBridge::getCapabilities(uint32_t index, XInputCapabilities* pCaps) {
    if (!pCaps || index >= MAX_CONTROLLERS) return false;
    if (!m_connected[index]) return false;

    memset(pCaps, 0, sizeof(XInputCapabilities));
    pCaps->Type = 0;
    pCaps->SubType = 1;

    GCController* gc = m_impl->controllers[index];
    if (gc.extendedGamepad) {
        pCaps->Flags = 0x0004;
    }

    return true;
}

}
