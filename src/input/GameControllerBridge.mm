#import <CoreHaptics/CoreHaptics.h>
#include <cstring>
#import <GameController/GameController.h>
#include <metalsharp/GameControllerBridge.h>
#include <metalsharp/Logger.h>
#include <mutex>

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
    std::mutex m_lock;
    // Lazily created per-slot CoreHaptics engines. Storing as `id` keeps the .mm
    // file self-contained without forcing CoreHaptics types into C++ layouts.
    id hapticEngines[4] = {};
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
                                                    std::lock_guard<std::mutex> lock(impl->m_lock);
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
                  std::lock_guard<std::mutex> lock(impl->m_lock);
                  for (uint32_t i = 0; i < 4; i++) {
                      if (impl->slots[i].controller == gc) {
                          MS_INFO("GameControllerBridge: controller %u disconnected", i);
                          impl->slots[i].controller = nil;
                          impl->slots[i].type = ControllerType::Unknown;
                          impl->slots[i].connected = false;
                          impl->hapticEngines[i] = nil;
                          impl->lastHaptics[i][0] = 0.0f;
                          impl->lastHaptics[i][1] = 0.0f;
                          m_connected[i] = false;
                          break;
                      }
                  }
                }];

    NSArray<GCController*>* connected = [GCController controllers];
    {
        std::lock_guard<std::mutex> lock(impl->m_lock);
        for (NSUInteger i = 0; i < connected.count && i < MAX_CONTROLLERS; i++) {
            uint32_t slot = assignSlot(impl->slots, connected[i]);
            if (slot < 4)
                m_connected[slot] = true;
        }
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
                buttons |= XINPUT_GUIDE;
        }
    } else if (type == ControllerType::DualShock4) {
        if (@available(macOS 11.0, *)) {
            GCDualShockGamepad* ds4 = (GCDualShockGamepad*)gamepad;
            if (ds4.touchpadButton.isPressed)
                buttons |= XINPUT_GUIDE;
        }
    }

    if (@available(macOS 11.0, *)) {
        if (gamepad.buttonHome && gamepad.buttonHome.isPressed)
            buttons |= XINPUT_GUIDE;
    }

    return buttons;
}

// Poll one slot into the provided state. Caller must hold Impl::m_lock.
// Pulled out so both poll() and getState() can refresh state without
// recursing on the (non-recursive) mutex.
static void pollOneSlot(uint32_t i, const ControllerSlot& slot, XInputState& state) {
    (void)i;
    if (!slot.controller)
        return;
    GCExtendedGamepad* gamepad = slot.controller.extendedGamepad;
    if (!gamepad)
        return;

    state.packetNumber++;
    state.Gamepad.wButtons = readButtons(gamepad, slot.controller, slot.type);
    state.Gamepad.bLeftTrigger = (BYTE)(gamepad.leftTrigger.value * 255.0f);
    state.Gamepad.bRightTrigger = (BYTE)(gamepad.rightTrigger.value * 255.0f);
    state.Gamepad.sThumbLX = (SHORT)(gamepad.leftThumbstick.xAxis.value * 32767.0f);
    state.Gamepad.sThumbLY = (SHORT)(gamepad.leftThumbstick.yAxis.value * 32767.0f);
    state.Gamepad.sThumbRX = (SHORT)(gamepad.rightThumbstick.xAxis.value * 32767.0f);
    state.Gamepad.sThumbRY = (SHORT)(gamepad.rightThumbstick.yAxis.value * 32767.0f);
}

void GameControllerBridge::poll() {
    std::lock_guard<std::mutex> lock(m_impl->m_lock);
    for (uint32_t i = 0; i < MAX_CONTROLLERS; i++) {
        pollOneSlot(i, m_impl->slots[i], m_states[i]);
    }
}

bool GameControllerBridge::getState(uint32_t index, XInputState* pState) {
    if (!pState || index >= MAX_CONTROLLERS)
        return false;

    std::lock_guard<std::mutex> lock(m_impl->m_lock);
    if (!m_connected[index]) {
        memset(pState, 0, sizeof(XInputState));
        return false;
    }

    // Refresh every slot so the returned state is current; preserves the
    // pre-existing behavior of getState triggering a full poll.
    for (uint32_t i = 0; i < MAX_CONTROLLERS; i++) {
        pollOneSlot(i, m_impl->slots[i], m_states[i]);
    }
    *pState = m_states[index];
    return true;
}

void GameControllerBridge::refresh() {
    poll();
}

bool GameControllerBridge::setState(uint32_t index, uint16_t leftMotor, uint16_t rightMotor) {
    if (index >= MAX_CONTROLLERS)
        return false;

    float left = leftMotor / 65535.0f;
    float right = rightMotor / 65535.0f;
    GCController* gc = nil;
    bool haveEngine = false;

    {
        std::lock_guard<std::mutex> lock(m_impl->m_lock);
        gc = m_impl->slots[index].controller;
        if (!gc) {
            // Controller was disconnected (or never connected). Clear any
            // cached haptic engine and bail out.
            m_impl->hapticEngines[index] = nil;
            m_impl->lastHaptics[index][0] = 0.0f;
            m_impl->lastHaptics[index][1] = 0.0f;
            return false;
        }
        m_impl->lastHaptics[index][0] = left;
        m_impl->lastHaptics[index][1] = right;
        haveEngine = (m_impl->hapticEngines[index] != nil);
    }

    if (@available(macOS 11.0, *)) {
        GCDeviceHaptics* deviceHaptics = gc.haptics;
        if (!deviceHaptics)
            return true;

        if (!haveEngine) {
            // Lazily create the haptic engine. Synchronous on macOS 11+.
            CHHapticEngine* createdEngine = [deviceHaptics createEngineWithLocality:GCHapticsLocalityDefault];
            if (!createdEngine) {
                MS_WARN("GameControllerBridge: haptic engine creation failed (slot %u)", index);
                return true;
            }
            std::lock_guard<std::mutex> lock(m_impl->m_lock);
            m_impl->hapticEngines[index] = createdEngine;
            haveEngine = true;
        }

        CHHapticEngine* engine = nil;
        {
            std::lock_guard<std::mutex> lock(m_impl->m_lock);
            engine = (CHHapticEngine*)m_impl->hapticEngines[index];
        }
        if (!engine)
            return true;

        if (leftMotor == 0 && rightMotor == 0) {
            [engine stopWithCompletionHandler:nil];
            return true;
        }

        // Average motor intensity drives the continuous haptic; the high-frequency
        // (right) motor drives sharpness, matching typical XInput rumble curves.
        float intensity = (left + right) / 2.0f;
        float sharpness = right;

        CHHapticEventParameter* intensityParam =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                          value:intensity];
        CHHapticEventParameter* sharpnessParam =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                          value:sharpness];
        CHHapticEvent* event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                             parameters:@[ intensityParam, sharpnessParam ]
                                                           relativeTime:0
                                                               duration:GCHapticDurationInfinite];

        NSError* patternError = nil;
        CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ]
                                                                parameters:@[]
                                                                     error:&patternError];
        if (!pattern || patternError) {
            MS_WARN("GameControllerBridge: haptic pattern creation failed (slot %u): %s", index,
                    patternError ? patternError.localizedDescription.UTF8String : "unknown error");
            return true;
        }

        NSError* playerError = nil;
        id<CHHapticPatternPlayer> player = [engine createPlayerWithPattern:pattern error:&playerError];
        if (!player || playerError) {
            MS_WARN("GameControllerBridge: haptic player creation failed (slot %u): %s", index,
                    playerError ? playerError.localizedDescription.UTF8String : "unknown error");
            return true;
        }

        [engine startWithCompletionHandler:nil];
        NSError* startError = nil;
        [player startAtTime:0 error:&startError];
        if (startError) {
            MS_WARN("GameControllerBridge: haptic player start failed (slot %u): %s", index,
                    startError.localizedDescription.UTF8String);
        }
    }

    return true;
}

bool GameControllerBridge::getCapabilities(uint32_t index, XInputCapabilities* pCaps) {
    if (!pCaps || index >= MAX_CONTROLLERS)
        return false;

    std::lock_guard<std::mutex> lock(m_impl->m_lock);
    if (!m_connected[index])
        return false;

    memset(pCaps, 0, sizeof(XInputCapabilities));
    pCaps->Type = 0;
    pCaps->SubType = 1;

    GCController* gc = m_impl->slots[index].controller;
    if (gc && gc.extendedGamepad) {
        pCaps->Flags = 0x0004;
    }

    return true;
}

} // namespace metalsharp
