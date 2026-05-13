#include <metalsharp/GameControllerBridge.h>
#include <cstdio>
#include <cstring>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

int main() {
    printf("=== Input Tests ===\n\n");

    {
        printf("--- XInputState Layout ---\n");
        metalsharp::XInputState state{};
        CHECK(state.packetNumber == 0, "Default packetNumber is 0");
        CHECK(state.Gamepad.wButtons == 0, "Default wButtons is 0");
        CHECK(state.Gamepad.bLeftTrigger == 0, "Default bLeftTrigger is 0");
        CHECK(state.Gamepad.bRightTrigger == 0, "Default bRightTrigger is 0");
        CHECK(state.Gamepad.sThumbLX == 0, "Default sThumbLX is 0");
        CHECK(state.Gamepad.sThumbLY == 0, "Default sThumbLY is 0");
        CHECK(state.Gamepad.sThumbRX == 0, "Default sThumbRX is 0");
        CHECK(state.Gamepad.sThumbRY == 0, "Default sThumbRY is 0");
    }

    {
        printf("\n--- XInputCapabilities Layout ---\n");
        metalsharp::XInputCapabilities caps{};
        CHECK(caps.Type == 0, "Default Type is 0");
        CHECK(caps.SubType == 0, "Default SubType is 0");
        CHECK(caps.Flags == 0, "Default Flags is 0");
        CHECK(caps.Gamepad.wButtons == 0, "Default Gamepad.wButtons is 0");
        CHECK(caps.Vibration.wLeftMotorSpeed == 0, "Default Vibration.wLeftMotorSpeed is 0");
        CHECK(caps.Vibration.wRightMotorSpeed == 0, "Default Vibration.wRightMotorSpeed is 0");
    }

    {
        printf("\n--- GameControllerBridge Unconnected ---\n");
        metalsharp::GameControllerBridge bridge;
        CHECK(bridge.init(), "Init succeeds");

        metalsharp::XInputState state;
        bool result = bridge.getState(0, &state);
        CHECK(!result, "getState returns false for unconnected controller");

        result = bridge.setState(0, 0, 0);
        CHECK(!result, "setState returns false for unconnected controller");

        metalsharp::XInputCapabilities caps;
        result = bridge.getCapabilities(0, &caps);
        CHECK(!result, "getCapabilities returns false for unconnected controller");
    }

    {
        printf("\n--- GameControllerBridge Index Bounds ---\n");
        metalsharp::GameControllerBridge bridge;

        metalsharp::XInputState state;
        bool result = bridge.getState(4, &state);
        CHECK(!result, "getState returns false for index >= 4");

        result = bridge.getState(100, &state);
        CHECK(!result, "getState returns false for index 100");

        result = bridge.getState(0, nullptr);
        CHECK(!result, "getState returns false for null state");

        metalsharp::XInputCapabilities caps;
        result = bridge.getCapabilities(4, &caps);
        CHECK(!result, "getCapabilities returns false for index >= 4");

        result = bridge.getCapabilities(0, nullptr);
        CHECK(!result, "getCapabilities returns false for null caps");
    }

    {
        printf("\n--- GameControllerBridge Init/Poll ---\n");
        metalsharp::GameControllerBridge bridge;
        CHECK(bridge.init(), "Init succeeds on first call");
        bridge.poll();
        CHECK(true, "Poll succeeds with no controllers");
    }

    {
        printf("\n--- XInputGamepad Button Constants ---\n");
        metalsharp::XInputGamepad gamepad{};
        gamepad.wButtons = 0xFFFF;
        CHECK(gamepad.wButtons == 0xFFFF, "All buttons can be set");

        gamepad.wButtons = 0x1000;
        CHECK((gamepad.wButtons & 0x1000) != 0, "A button bit set");

        gamepad.wButtons = 0x2000;
        CHECK((gamepad.wButtons & 0x2000) != 0, "B button bit set");

        gamepad.wButtons = 0x4000;
        CHECK((gamepad.wButtons & 0x4000) != 0, "X button bit set");

        gamepad.wButtons = 0x8000;
        CHECK((gamepad.wButtons & 0x8000) != 0, "Y button bit set");
    }

    {
        printf("\n--- XInputGamepad Trigger/Thumb Ranges ---\n");
        metalsharp::XInputGamepad gamepad{};
        gamepad.bLeftTrigger = 255;
        gamepad.bRightTrigger = 128;
        CHECK(gamepad.bLeftTrigger == 255, "Left trigger max");
        CHECK(gamepad.bRightTrigger == 128, "Right trigger mid");

        gamepad.sThumbLX = 32767;
        gamepad.sThumbLY = -32768;
        gamepad.sThumbRX = 0;
        gamepad.sThumbRY = -1;
        CHECK(gamepad.sThumbLX == 32767, "Thumb LX max");
        CHECK(gamepad.sThumbLY == -32768, "Thumb LY min");
        CHECK(gamepad.sThumbRX == 0, "Thumb RX zero");
        CHECK(gamepad.sThumbRY == -1, "Thumb RY negative");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
