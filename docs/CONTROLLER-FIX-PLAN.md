# Controller Input Pipeline Fix Plan

> **Branch:** `codex/controller-input-fix`
> **Base:** `main`
> **Status:** Planning — awaiting approval to implement

## Root Cause Analysis

### Architecture
MetalSharp has three game-launch paths:

| Path | Controller Chain | Status |
|---|---|---|
| **Wine** | `Game.exe` → Wine → `xinput1_4=native` → `xinput1_4.dylib` → `GameControllerBridge.mm` | **Broken** (init never called) |
| **Native PE** | `Game.exe` → `PELoader` → shim table | **Missing** (no xinput shim registered) |
| **FNA** | Native macOS → SDL3 → GCController | Works (unrelated to XInput) |

### Bugs Identified

1. **`EntryPoint.cpp` — `init()` never called**
   `static GameControllerBridge g_bridge` is created but nobody calls `g_bridge.init()`. The `init()` method in `GameControllerBridge.mm` registers `GCControllerDidConnectNotification` / `GCControllerDidDisconnectNotification` observers and enumerates already-connected controllers. Without it, zero controllers are ever detected. The `XInputEngine.cpp` file has a dead-code `ensureInit()` that is never invoked.

2. **No XInput shim in NativeLauncher (PE path)**
   `NativeLauncher.cpp` registers shims for `kernel32`, `user32`, `d3d11`, `d3d12`, `dxgi` etc. but never registers `xinput1_4.dll` or `xinput1_3.dll`. Games running through the native PE loader get zero controller support.

3. **Thread safety — data race**
   Notification blocks in `init()` run on `[NSOperationQueue mainQueue]` and write to `m_connected[]` and `impl->slots[]`. `poll()` runs on whatever game thread calls `XInputGetState()` and reads those same arrays with no synchronization.

4. **Haptics/vibration stubbed out**
   `setState()` stores values in `lastHaptics[][]` but never invokes `GCController.haptics` or `GCDeviceHaptics` to drive rumble motors.

5. **`getState()` calls `poll()` on every query**
   A game calling `getState(0)` then `getState(1)` results in `poll()` running twice per frame, iterating all 4 controllers each time. Should batch once per update cycle.

6. **Dist bundle ships zero-byte stubs**
   `prepare-native-placeholders.sh` creates 0-byte files. The cmake-built `xinput1_4.dylib` (33KB) exists in `build/macos-release/` but the release pipeline may not be deploying the real binary everywhere. The dist `scripts/tools/native/` directory has zero-byte stubs.

7. **DualSense touchpad double-maps `XINPUT_BACK`**
   For DualSense, `buttonOptions` maps to `XINPUT_BACK` then `touchpadButton` also maps to `XINPUT_BACK`, overwriting the value.

8. **Wrong error codes**
   `getState()` returns `E_FAIL` for disconnected controllers. Real XInput returns `ERROR_DEVICE_NOT_CONNECTED` (1167). Games check for this specific value.

9. **No wireless controller discovery**
   `startWirelessControllerDiscoveryWithCompletionHandler:` is never called — some wireless controllers require explicit discovery on macOS.

10. **`GameControllerBridge.cpp` is a dangerous stub**
    The no-op C++ stub file defines the same class with `m_impl = nullptr` and all methods returning false. CMake only compiles the `.mm`, so there is no active ODR violation, but the file is confusing and could silently break if re-added to the build.

---

## Phased Implementation Plan

### Phase 1: Critical Fixes — Controllers Never Work

- [ ] **Wire `init()` into `EntryPoint.cpp`**: Add `__attribute__((constructor))` or call `init()` lazily on first `XInputGetState`/`XInputSetState`/`XInputGetCapabilities` call with a `std::once_flag`
- [ ] **Add xinput shim to NativeLauncher**: Register `xinput1_4.dll` / `xinput1_3.dll` in the PELoader shim table in `NativeLauncher.cpp`, delegating to a GameControllerBridge singleton
- [ ] **Remove `GameControllerBridge.cpp` stub**: Delete the no-op C++ file to eliminate confusion
- [ ] **Fix `getState()` polling**: Decouple `poll()` from `getState()` — add a `tick()` or `refresh()` method; call `poll()` once per update cycle, cache results per controller slot
- [ ] **Fix return codes**: Return `ERROR_SUCCESS` (0) when connected, `ERROR_DEVICE_NOT_CONNECTED` (1167) when disconnected, matching Windows XInput behavior
- [ ] **Add wireless controller discovery**: Call `startWirelessControllerDiscoveryWithCompletionHandler:` in `init()` for macOS 11+

**Files:** `src/input/EntryPoint.cpp`, `src/input/GameControllerBridge.mm`, `src/input/GameControllerBridge.cpp`, `tools/launcher/NativeLauncher.cpp`, `include/metalsharp/GameControllerBridge.h`

---

### Phase 2: Robustness & Multi-Controller

- [ ] **Thread safety**: Add `std::mutex` or `os_unfair_lock` protecting `m_connected[]`, `m_states[]`, and `impl->slots[]`
- [ ] **Fix DualSense button mapping**: Use `buttonOptions` for `XINPUT_BACK`, use `touchpadButton` for `XINPUT_GUIDE` or a dedicated touchpad press flag; fix DualSense/DualShock4 controller-type detection to not double-assign
- [ ] **Implement haptics**: Call `[gc.haptics createEngineWithLocality:...]` or use `GCDeviceHaptics` to actually drive rumble motors when `setState()` is called; handle the async engine-creation callback
- [ ] **Controller hot-plug robustness**: Handle `GCControllerDidDisconnectNotification` cleanly — clear the slot, zero the state, log the event
- [ ] **Multi-controller validation**: Verify slot assignment correctly handles 2+ controllers being connected simultaneously; test that `assignSlot` doesn't reorder/conflict on reconnects

**Files:** `src/input/GameControllerBridge.mm`, `src/input/GameControllerBridge.cpp` (removal), `include/metalsharp/GameControllerBridge.h`

---

### Phase 3: Build & Distribution

- [ ] **Fix release pipeline**: Ensure real `xinput1_4.dylib` is included in the split-bundle and combined DMG paths; verify in `tools/bundles/create-split-bundles.py`, `tools/dmg/create-dmg.sh`, `tools/dmg/create-bundles.sh`
- [ ] **Replace zero-byte stubs**: Update `tools/package/prepare-native-placeholders.sh` to copy the cmake-built binary instead of creating empty files, or add a post-build step that overwrites them
- [ ] **Add CI test**: Build + load-test the xinput dylib (verify symbols export, verify `init()` succeeds without crashing)

**Files:** `tools/package/prepare-native-placeholders.sh`, `tools/bundles/create-split-bundles.py`, `tools/dmg/create-bundles.sh`, `.github/workflows/release.yml`

---

### Phase 4: SDL2 / SDL3 / OpenGL 2-4 Support

- [ ] **SDL abstraction layer** (`src/input/SDLBridge.h/.cpp/.mm`):
  - Compile-time selection (`-DMETALSHARP_SDL_VERSION=2|3`)
  - Unified init, window creation, event pump, gamepad input
  - SDL2: `SDL_GameController` API
  - SDL3: `SDL_Gamepad` API + `SDL_GetGamepads()`
- [ ] **CMake integration**:
  - `find_package(SDL2)` and `find_package(SDL3)`
  - `-DMETALSHARP_SDL_VERSION=` option
  - Link appropriate SDL library
- [ ] **OpenGL → Metal translation**:
  - GL 2.x/3.x: Route through `mojoshader`'s existing GLSL→HLSL path, then through DXBC→MSL pipeline
  - GL 4.x: Investigate SPIRV-Cross (GLSL → SPIR-V → MSL) as an alternative
  - Add `metalsharp_opengl32` shim DLL for Wine override path (`opengl32=native`)
- [ ] **CMake option**: `-DMETALSHARP_OPENGL_VERSION=2|3|4` to select backend

**Files:** `src/input/SDLBridge.h`, `src/input/SDLBridge.mm`, `CMakeLists.txt`, new `src/opengl/` directory

---

### Dependency Graph

```
Phase 1 (Critical Fixes)
 └── Phase 2 (Robustness)
      └── Phase 3 (Distribution)
Phase 4 (SDL/GL) ── independent, can run in parallel with Phase 2-3
```

---

## Verification Plan (per phase)

- **Phase 1**: Single-controller XInput game connects and responds; `METALSHARP_BUILD_DIR` set, `WINEDLLOVERRIDES=xinput1_4=native`
- **Phase 2**: Two-controller game detects both controllers; rumble/vibration works; controller disconnect/reconnect handled without crashing
- **Phase 3**: Fresh install from release DMG shows real `xinput1_4.dylib` binary (not zero-byte); `file` reports Mach-O; `nm` shows exported XInput symbols
- **Phase 4**: SDL2 FNA game builds and runs; SDL3 FNA game still works; GL game launches through opengl32 override
