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
    struct Vibration { WORD wLeftMotorSpeed; WORD wRightMotorSpeed; } Vibration;
};

class GameControllerBridge {
public:
    GameControllerBridge();
    ~GameControllerBridge();

    bool init();
    void poll();
    bool getState(uint32_t index, XInputState* pState);
    bool setState(uint32_t index, uint16_t leftMotor, uint16_t rightMotor);
    bool getCapabilities(uint32_t index, XInputCapabilities* pCaps);

private:
    struct Impl;
    static constexpr uint32_t MAX_CONTROLLERS = 4;
    XInputState m_states[MAX_CONTROLLERS] = {};
    bool m_connected[MAX_CONTROLLERS] = {};
    Impl* m_impl;
};

}
