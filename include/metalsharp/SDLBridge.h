/// @file SDLBridge.h
/// @brief Compile-time abstraction layer over SDL2 / SDL3.
///
/// MetalSharp supports both SDL2 and SDL3 at the source level. The selected
/// version is set by CMake via `-DMETALSHARP_SDL_VERSION_MAJOR=2|3` (default 3)
/// and exposed to the C/C++ translation unit through the
/// `METALSHARP_SDL_VERSION_MAJOR` macro. The bridge is header-only and uses
/// inline wrappers so no .cpp/.mm translation unit is required.
///
/// SDL2 vs SDL3 API differences handled here:
///   - `SDL_Init` returns 0 on success in SDL2, bool in SDL3.
///   - `SDL_CreateWindow` does not take (x, y) in SDL3.
///   - `SDL_PollEvent` returns int (1/0) in SDL2, bool in SDL3.
///   - `SDL_GetJoysticks`/`SDL_IsGamepad`/`SDL_OpenGamepad` are the SDL3 names;
///     SDL2 uses `SDL_NumJoysticks`/`SDL_IsGameController`/`SDL_GameControllerOpen`.
///   - `SDL_GetVersion` takes a `SDL_version*` in SDL2, output params in SDL3.
///
/// All wrappers return values that are version-neutral where possible. Pointers
/// to SDL-owned structures are passed across the boundary as `void*` to keep
/// this header free of SDL-version-specific types in its public surface.
///
/// The mojoshader SDL GPU backend (`mojoshader_sdlgpu.c`) is intentionally
/// left SDL3-only: SDL2 does not provide the `SDL_GPUDevice` /
/// `SDL_ShaderCross` APIs that backend depends on. Guard it with
/// `-DMOJOSHADER_HAS_SDL_GPU` (set by the top-level CMake when SDL3 is
/// selected and located) when integrating mojoshader into the build.

#pragma once

#include <metalsharp/Platform.h>

#include <cstdio>

// METALSHARP_SDL_VERSION_MAJOR is set by CMake (2 or 3). Default to 3 if not set.
#ifndef METALSHARP_SDL_VERSION_MAJOR
#define METALSHARP_SDL_VERSION_MAJOR 3
#endif

#if METALSHARP_SDL_VERSION_MAJOR == 2
#include <SDL.h>
#elif METALSHARP_SDL_VERSION_MAJOR == 3
#include <SDL3/SDL.h>
#else
#error "METALSHARP_SDL_VERSION_MAJOR must be 2 or 3"
#endif

namespace metalsharp {
namespace sdl {

/// Initialize SDL with the given subsystem flags.
/// SDL2: SDL_Init(flags) — returns 0 on success.
/// SDL3: SDL_Init(flags) — returns true on success.
inline bool init(uint32_t flags) {
#if METALSHARP_SDL_VERSION_MAJOR == 2
    return SDL_Init(flags) == 0;
#elif METALSHARP_SDL_VERSION_MAJOR == 3
    return SDL_Init(flags);
#endif
}

/// Shutdown SDL.
inline void quit() {
    SDL_Quit();
}

/// Create a window. Returns the SDL_Window pointer (cast to void* for ABI stability).
/// SDL2: SDL_CreateWindow(title, x, y, w, h, flags)
/// SDL3: SDL_CreateWindow(title, w, h, flags)
inline void* createWindow(const char* title, int width, int height, uint32_t flags) {
#if METALSHARP_SDL_VERSION_MAJOR == 2
    return SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
#elif METALSHARP_SDL_VERSION_MAJOR == 3
    return SDL_CreateWindow(title, width, height, flags);
#endif
}

/// Destroy a window.
inline void destroyWindow(void* window) {
    SDL_DestroyWindow(static_cast<SDL_Window*>(window));
}

/// Poll for events. Returns true if there are more events.
inline bool pollEvent(void* event) {
#if METALSHARP_SDL_VERSION_MAJOR == 2
    return SDL_PollEvent(static_cast<SDL_Event*>(event)) == 1;
#elif METALSHARP_SDL_VERSION_MAJOR == 3
    return SDL_PollEvent(static_cast<SDL_Event*>(event));
#endif
}

/// Get number of connected gamepads.
inline int numGamepads() {
#if METALSHARP_SDL_VERSION_MAJOR == 2
    // SDL2 game controller API
    int count = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i))
            count++;
    }
    return count;
#elif METALSHARP_SDL_VERSION_MAJOR == 3
    int count = 0;
    int numJoysticks = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&numJoysticks);
    if (joysticks) {
        for (int i = 0; i < numJoysticks; i++) {
            if (SDL_IsGamepad(joysticks[i]))
                count++;
        }
        SDL_free(joysticks);
    }
    return count;
#endif
}

/// Check if a gamepad is currently connected at the given player index.
/// Uses SDL2's GameController or SDL3's Gamepad API.
inline bool gamepadConnected(int playerIndex) {
#if METALSHARP_SDL_VERSION_MAJOR == 2
    SDL_GameController* ctrl = SDL_GameControllerOpen(playerIndex);
    if (ctrl) {
        SDL_GameControllerClose(ctrl);
        return true;
    }
    return false;
#elif METALSHARP_SDL_VERSION_MAJOR == 3
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
    if (!joysticks)
        return false;
    int gamepadCount = 0;
    SDL_JoystickID targetId = 0;
    for (int i = 0; i < count; i++) {
        if (SDL_IsGamepad(joysticks[i])) {
            if (gamepadCount == playerIndex) {
                targetId = joysticks[i];
                break;
            }
            gamepadCount++;
        }
    }
    SDL_free(joysticks);
    if (targetId == 0 && gamepadCount == 0)
        return false;
    SDL_Gamepad* gamepad = SDL_OpenGamepad(targetId);
    if (gamepad) {
        SDL_CloseGamepad(gamepad);
        return true;
    }
    return false;
#endif
}

/// Get the SDL version string (e.g., "SDL 2.30.0" or "SDL 3.2.0").
inline const char* versionString() {
#if METALSHARP_SDL_VERSION_MAJOR == 2
    static char buf[64];
    SDL_version v;
    SDL_GetVersion(&v);
    std::snprintf(buf, sizeof(buf), "SDL %d.%d.%d", v.major, v.minor, v.patch);
    return buf;
#elif METALSHARP_SDL_VERSION_MAJOR == 3
    // SDL3 exposes the linked library version through the SDL_MAJOR_VERSION /
    // SDL_MINOR_VERSION / SDL_MICRO_VERSION macros. SDL_GetVersion() in SDL3
    // returns a packed int rather than out-parameters, so prefer the macros
    // here for a stable, allocation-free string.
    static char buf[64];
    std::snprintf(buf, sizeof(buf), "SDL %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    return buf;
#endif
}

} // namespace sdl
} // namespace metalsharp
