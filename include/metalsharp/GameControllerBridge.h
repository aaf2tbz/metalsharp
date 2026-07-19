/// @file GameControllerBridge.h
/// @brief XInput to macOS GameController framework translation.
///
/// Bridges the XInput API (XInputGetState, XInputSetState, XInputGetCapabilities) to
/// Apple's GCController framework, mapping thumbstick axes, triggers, and button states
/// to XINPUT_GAMEPAD structures. Supports up to 4 concurrent controllers with vibration
/// (haptic feedback) translation. The xinput1_4.dll shim delegates all controller queries
/// to this bridge, enabling Xbox controller support on macOS without game modifications.

#pragma once

#include <metalsharp/Platform.h>

namespace metalsharp {

struct XInputGamepad {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct XInputState {
    DWORD packetNumber;
    XInputGamepad Gamepad;
};

struct XInputCapabilities {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XInputGamepad Gamepad;
    struct Vibration {
        WORD wLeftMotorSpeed;
        WORD wRightMotorSpeed;
    } Vibration;
};

class GameControllerBridge {
  public:
    GameControllerBridge();
    ~GameControllerBridge();

    /// @brief Initialize the bridge and register for GCController connect/disconnect notifications.
    /// @return true on success.
    bool init();

    /// @brief Poll every connected controller and refresh m_states in place. Cheap when no controllers are connected.
    void poll();

    /// @brief Per-frame refresh hook for game loops. Reads every controller once; call this once per frame before
    /// any getState() calls in that frame. Equivalent to poll().
    void refresh();

    /// @brief Poll every connected controller and return the latest state for @p index.
    /// @note Each call performs a poll before reading the cache. Use refresh() once per frame as an
    /// optimization so subsequent getState() calls within the same frame don't need to re-poll.
    bool getState(uint32_t index, XInputState* pState);

    /// @brief Set vibration motor speeds for controller @p index.
    bool setState(uint32_t index, uint16_t leftMotor, uint16_t rightMotor);

    /// @brief Query capabilities for controller @p index (gamepad subtype, supported flags, etc.).
    bool getCapabilities(uint32_t index, XInputCapabilities* pCaps);

  private:
    struct Impl;
    static constexpr uint32_t MAX_CONTROLLERS = 4;
    XInputState m_states[MAX_CONTROLLERS] = {};
    bool m_connected[MAX_CONTROLLERS] = {};
    Impl* m_impl;
};

} // namespace metalsharp
