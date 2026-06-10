#include <cstring>
#import <GameController/GameController.h>
#include <metalsharp/GameControllerBridge.h>
#include <metalsharp/Logger.h>

namespace metalsharp {

enum class ControllerType : uint8_t { Unknown, DualShock4, DualSense, Xbox, MFi };

struct ControllerSlot {
    __strong GCController* controller = nil;
    ControllerType type = ControllerType::Unknown;
    bool connected = false;
};

static ControllerType detectControllerType(GCController* gc) {
    if (@available(macOS 12.0, *)) {
        NSString* cat = gc.productCategory;
        if ([cat isEqualToString:GCProductCategoryDualSense])
            return ControllerType::DualSense;
        if ([cat isEqualToString:GCProductCategoryDualShock4])
            return ControllerType::DualShock4;
        if ([cat isEqualToString:GCProductCategoryXboxOne])
            return ControllerType::Xbox;
        if ([cat isEqualToString:GCProductCategoryMFi])
            return ControllerType::MFi;
    }
    if (@available(macOS 11.3, *)) {
        if ([gc.extendedGamepad isKindOfClass:[GCDualSenseGamepad class]])
            return ControllerType::DualSense;
    }
    if (@available(macOS 11.0, *)) {
        if ([gc.extendedGamepad isKindOfClass:[GCDualShockGamepad class]])
            return ControllerType::DualShock4;
    }
    return ControllerType::Unknown;
}

static uint32_t assignSlot(ControllerSlot* slots, GCController* gc) {
    for (uint32_t i = 0; i < 4; i++) {
        if (!slots[i].controller) {
            slots[i].controller = gc;
            slots[i].type = detectControllerType(gc);
            slots[i].connected = true;
            const char* typeName = "unknown";
            switch (slots[i].type) {
            case ControllerType::DualShock4:
                typeName = "DualShock 4";
                break;
            case ControllerType::DualSense:
                typeName = "DualSense";
                break;
            case ControllerType::Xbox:
                typeName = "Xbox";
                break;
            case ControllerType::MFi:
                typeName = "MFi";
                break;
            default:
                break;
            }
            MS_INFO("GameControllerBridge: controller %u connected (%s)", i, typeName);
            return i;
        }
    }
    return 4;
}

struct GameControllerBridge::Impl {
    ControllerSlot slots[4] = {};
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
    auto* impl = m_impl;

    [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification
                                                      object:nil
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification* note) {
                                                    GCController* gc = note.object;
                                                    uint32_t slot = assignSlot(impl->slots, gc);
                                                    if (slot < 4)
                                                        m_connected[slot] = true;
                                                  }];

    [[NSNotificationCenter defaultCenter]
        addObserverForName:GCControllerDidDisconnectNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                  GCController* gc = note.object;
                  for (uint32_t i = 0; i < 4; i++) {
                      if (impl->slots[i].controller == gc) {
                          MS_INFO("GameControllerBridge: controller %u disconnected", i);
                          impl->slots[i].controller = nil;
                          impl->slots[i].type = ControllerType::Unknown;
                          impl->slots[i].connected = false;
                          m_connected[i] = false;
                          break;
                      }
                  }
                }];

    NSArray<GCController*>* connected = [GCController controllers];
    for (NSUInteger i = 0; i < connected.count && i < MAX_CONTROLLERS; i++) {
        uint32_t slot = assignSlot(impl->slots, connected[i]);
        if (slot < 4)
            m_connected[slot] = true;
    }

    return true;
}

static const WORD XINPUT_DPAD_UP = 0x0001;
static const WORD XINPUT_DPAD_DOWN = 0x0002;
static const WORD XINPUT_DPAD_LEFT = 0x0004;
static const WORD XINPUT_DPAD_RIGHT = 0x0008;
static const WORD XINPUT_START = 0x0010;
static const WORD XINPUT_BACK = 0x0020;
static const WORD XINPUT_LEFT_THUMB = 0x0040;
static const WORD XINPUT_RIGHT_THUMB = 0x0080;
static const WORD XINPUT_LEFT_SHOULDER = 0x0100;
static const WORD XINPUT_RIGHT_SHOULDER = 0x0200;
static const WORD XINPUT_GUIDE = 0x0400;
static const WORD XINPUT_A = 0x1000;
static const WORD XINPUT_B = 0x2000;
static const WORD XINPUT_X = 0x4000;
static const WORD XINPUT_Y = 0x8000;

static WORD readButtons(GCExtendedGamepad* gamepad, GCController* gc, ControllerType type) {
    WORD buttons = 0;

    if (gamepad.buttonA.isPressed)
        buttons |= XINPUT_A;
    if (gamepad.buttonB.isPressed)
        buttons |= XINPUT_B;
    if (gamepad.buttonX.isPressed)
        buttons |= XINPUT_X;
    if (gamepad.buttonY.isPressed)
        buttons |= XINPUT_Y;

    if (gamepad.leftShoulder.isPressed)
        buttons |= XINPUT_LEFT_SHOULDER;
    if (gamepad.rightShoulder.isPressed)
        buttons |= XINPUT_RIGHT_SHOULDER;

    if (gamepad.dpad.up.isPressed)
        buttons |= XINPUT_DPAD_UP;
    if (gamepad.dpad.down.isPressed)
        buttons |= XINPUT_DPAD_DOWN;
    if (gamepad.dpad.left.isPressed)
        buttons |= XINPUT_DPAD_LEFT;
    if (gamepad.dpad.right.isPressed)
        buttons |= XINPUT_DPAD_RIGHT;

    if (gamepad.leftThumbstickButton.isPressed)
        buttons |= XINPUT_LEFT_THUMB;
    if (gamepad.rightThumbstickButton.isPressed)
        buttons |= XINPUT_RIGHT_THUMB;

    if (@available(macOS 11.0, *)) {
        if (gamepad.buttonMenu.isPressed)
            buttons |= XINPUT_START;
        if (gamepad.buttonOptions.isPressed)
            buttons |= XINPUT_BACK;
    }

    if (type == ControllerType::DualSense) {
        if (@available(macOS 11.3, *)) {
            GCDualSenseGamepad* ds = (GCDualSenseGamepad*)gamepad;
            if (ds.touchpadButton.isPressed)
                buttons |= XINPUT_BACK;
        }
    } else if (type == ControllerType::DualShock4) {
        if (@available(macOS 11.0, *)) {
            GCDualShockGamepad* ds4 = (GCDualShockGamepad*)gamepad;
            if (ds4.touchpadButton.isPressed)
                buttons |= XINPUT_BACK;
        }
    }

    if (@available(macOS 11.0, *)) {
        if (gamepad.buttonHome && gamepad.buttonHome.isPressed)
            buttons |= XINPUT_GUIDE;
    }

    return buttons;
}

void GameControllerBridge::poll() {
    for (uint32_t i = 0; i < MAX_CONTROLLERS; i++) {
        if (!m_impl->slots[i].controller)
            continue;

        GCExtendedGamepad* gamepad = m_impl->slots[i].controller.extendedGamepad;
        if (!gamepad)
            continue;

        m_states[i].packetNumber++;
        m_states[i].Gamepad.wButtons = readButtons(gamepad, m_impl->slots[i].controller, m_impl->slots[i].type);
        m_states[i].Gamepad.bLeftTrigger = (BYTE)(gamepad.leftTrigger.value * 255.0f);
        m_states[i].Gamepad.bRightTrigger = (BYTE)(gamepad.rightTrigger.value * 255.0f);
        m_states[i].Gamepad.sThumbLX = (SHORT)(gamepad.leftThumbstick.xAxis.value * 32767.0f);
        m_states[i].Gamepad.sThumbLY = (SHORT)(gamepad.leftThumbstick.yAxis.value * 32767.0f);
        m_states[i].Gamepad.sThumbRX = (SHORT)(gamepad.rightThumbstick.xAxis.value * 32767.0f);
        m_states[i].Gamepad.sThumbRY = (SHORT)(gamepad.rightThumbstick.yAxis.value * 32767.0f);
    }
}

bool GameControllerBridge::getState(uint32_t index, XInputState* pState) {
    if (!pState || index >= MAX_CONTROLLERS)
        return false;
    if (!m_connected[index]) {
        memset(pState, 0, sizeof(XInputState));
        return false;
    }
    poll();
    *pState = m_states[index];
    return true;
}

bool GameControllerBridge::setState(uint32_t index, uint16_t leftMotor, uint16_t rightMotor) {
    if (index >= MAX_CONTROLLERS || !m_impl->slots[index].controller)
        return false;

    m_impl->lastHaptics[index][0] = leftMotor / 65535.0f;
    m_impl->lastHaptics[index][1] = rightMotor / 65535.0f;

    return true;
}

bool GameControllerBridge::getCapabilities(uint32_t index, XInputCapabilities* pCaps) {
    if (!pCaps || index >= MAX_CONTROLLERS)
        return false;
    if (!m_connected[index])
        return false;

    memset(pCaps, 0, sizeof(XInputCapabilities));
    pCaps->Type = 0;
    pCaps->SubType = 1;

    GCController* gc = m_impl->slots[index].controller;
    if (gc.extendedGamepad) {
        pCaps->Flags = 0x0004;
    }

    return true;
}

} // namespace metalsharp
