# MetalSharp Reference

Critical technical details for running Windows games on macOS via MetalSharp. Everything here was learned the hard way.

---

## Platform

- **Hardware:** Apple M4 (arm64)
- **macOS:** 26.4.1 (Build 25E253)
- **Rosetta 2:** Required for x86_64 translation (Celeste, GPTK Wine)

## Runtime Locations

| Component | Path |
|---|---|
| Game install dir | `~/.metalsharp/games/{appid}/` |
| Mono arm64 (system) | `/opt/homebrew/bin/mono` (v6.14.1) |
| Mono x86_64 | `~/.metalsharp/runtime/mono-x86/bin/mono` (v6.13.0) |
| Mono x86 libs | `~/.metalsharp/runtime/mono-x86/lib/` |
| Native shims (Celeste) | `~/.metalsharp/runtime/shims/` |
| GPTK Wine prefix | `~/.metalsharp/prefix-gptk/` |
| GPTK Wine binary | `/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64` |
| Configs | `~/metalsharp/configs/` |
| Setup scripts | `~/metalsharp/scripts/` |
| FNA source/stubs | `~/metalsharp/src/fna/` |
| Terraria source | `~/metalsharp/src/fna/terraria/` |
| Celeste shims source | `~/metalsharp/src/fna/shims/` |
| macOS Terraria install | `~/Library/Application Support/Steam/steamapps/common/Terraria/Terraria.app/Contents/MacOS/osx/` |

---

## Game Launch Commands (verified working)

### Terraria (appid 105600)

```bash
cd ~/.metalsharp/games/105600 && \
DYLD_LIBRARY_PATH="$HOME/.metalsharp/games/105600:/opt/homebrew/lib" \
MONO_CONFIG=~/metalsharp/configs/terraria-mono.config \
FNA3D_DRIVER=OpenGL \
/opt/homebrew/bin/mono TerrariaLauncher.exe
```

- **Runtime:** arm64 system mono
- **Game type:** XNA/FNA via mono P/Invoke
- **Graphics:** SDL3 Metal backend via FNA3D
- **Audio:** FAudio (from macOS Terraria native libs)
- **Steam:** libsteam_api.dylib (triple-arch i386+x86_64+arm64 from macOS Terraria)

### Celeste (appid 504230)

```bash
cd ~/.metalsharp/games/504230 && \
DYLD_LIBRARY_PATH="$HOME/.metalsharp/runtime/mono-x86/lib:/opt/homebrew/lib:.:$HOME/.metalsharp/runtime/shims" \
MONO_CONFIG=~/metalsharp/configs/celeste-x86-mono.config \
MONO_PATH="$HOME/.metalsharp/runtime/mono-x86/lib/mono/4.5" \
FNA3D_DRIVER=OpenGL \
METAL_DEVICE_WRAPPER_TYPE=0 \
arch -x86_64 ~/.metalsharp/runtime/mono-x86/bin/mono Celeste.exe
```

- **Runtime:** x86_64 mono via Rosetta 2 (`arch -x86_64`)
- **Why x86:** Celeste uses FMOD audio which only has x86_64 macOS builds. Real FMOD 1.10.20 gives full audio.
- **Game type:** XNA/FNA via mono P/Invoke
- **Graphics:** SDL3 Metal backend via FNA3D
- **Audio:** Real FMOD 1.10.20 x86_64 (with Carbon dep removed)

### Rain World (appid 312520)

```bash
cd ~/.metalsharp/games/312520 && \
WINEPREFIX=~/.metalsharp/prefix-gptk \
"/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64" RainWorld.exe
```

- **Runtime:** GPTK Wine (Apple's D3D→Metal translation layer)
- **Game type:** Unity/D3D11 via Wine
- **Graphics:** D3D11→Metal via GPTK (Apple's translation)
- **Audio:** CoreAudio via Wine

---

## Critical Technical Details

### DYLD_LIBRARY_PATH Order Matters

**Game dir MUST be first.** macOS resolves dylibs left-to-right in DYLD_LIBRARY_PATH. If `/opt/homebrew/lib` comes before the game dir, Homebrew's real `libgdiplus.dylib` (which pulls in GLib → Pango → assertion crash) gets loaded instead of our stub.

```
CORRECT:   DYLD_LIBRARY_PATH="$GAME_DIR:/opt/homebrew/lib"
WRONG:     DYLD_LIBRARY_PATH="/opt/homebrew/lib:$GAME_DIR"
```

### Terraria gdiplus Stub

Terraria doesn't use GDI+ for rendering (uses FNA/Metal), but mono's config maps gdiplus to `libgdiplus.dylib`. Homebrew's real `libgdiplus` pulls in GLib, which hits `gpath.c:115: assertion 'filename != NULL' failed` and aborts. Our stub (`src/fna/terraria/gdiplus_stub.c`) provides 85 symbols as no-ops. The GLib assertion still prints but doesn't abort.

### Terraria Native Libs from macOS Steam Install

The macOS Terraria Steam install at `~/Library/Application Support/Steam/steamapps/common/Terraria/Terraria.app/Contents/MacOS/osx/` contains universal native dylibs that are **required** for Windows Terraria to work:

- `libsteam_api.dylib` — triple-arch (i386+x86_64+arm64), 1078 symbols including `GetISteamGameSearch` which the Steam client version is missing (only 418 symbols)
- `libSDL3.0.dylib` — universal, 6.8MB
- `libFAudio.0.dylib` — universal, 543KB
- `libFNA3D.0.dylib` — universal, 1MB
- `libnfd.dylib` — native file dialog

These are self-contained with no Homebrew dependencies. The setup script copies them from the macOS install.

### CSteamworks for Celeste

Celeste's Steamworks.NET uses **custom P/Invoke entry points** (`as "Init"`, `as "Shutdown"`) — NOT standard Steam API names. The CSteamworks shim must be built with 609 `-Wl,-alias` flags that map standard Steam API names to the custom entry points. The alias list is in `src/fna/shims/csteamworks_aliases.txt`.

Build command pattern:
```bash
clang -shared -arch x86_64 \
  -o libCSteamworks.dylib csteamworks_shim.c \
  -L$GAME_DIR -lsteam_api \
  -install_name @loader_path/libCSteamworks.dylib \
  $(cat csteamworks_aliases.txt) \
  -undefined dynamic_lookup
```

CSteamworks must be in the **game directory** — mono resolves `@loader_path` relative to dylib location.

### Mono x86_64 Setup

1. Download from `https://download.mono-project.com/archive/6.12.0.206/macos-10-x86/MonoFramework-MDK-6.12.0.206.macos10.x86.xpkg`
2. Extract with `pkgutil --expand` then `gzip -dc Payload | cpio -id`
3. Create symlink: `libmono-native-unified.dylib → libmono-native.dylib` (without this, mono x86 crashes)

### FMOD for Celeste

Real FMOD 1.10.20 x86_64 works for full audio. The setup script builds stubs by default (no audio but game runs). Real FMOD requires removing Carbon framework dependency and fixing install names with `install_name_tool`. The stubs are at `src/fna/shims/fmod_stub.c` and `fmodstudio_stub.c`.

---

## Game IDs

| Game | App ID | Type | Runtime |
|---|---|---|---|
| Terraria | 105600 | XNA/FNA | arm64 mono |
| Celeste | 504230 | XNA/FNA | x86_64 mono (Rosetta) |
| Rain World | 312520 | Unity/D3D11 | GPTK Wine |

---

## Install Flow (Electron App)

1. User clicks **Install** on a game card
2. Frontend calls `POST /steam/download-game` with appid
3. Rust backend spawns steamcmd with `+@sSteamCmdForcePlatformType windows`
4. steamcmd downloads Windows game files to `~/.metalsharp/games/{appid}/`
5. Progress tracked via `~/.metalsharp/download_progress.json` (frontend polls every 2s)
6. After download completes, backend writes `setting_up` status and calls `prepare_game(appid)`
7. `prepare_game()` runs game-specific setup (copy native libs, build stubs, compile launchers)
8. Status set to `complete`, frontend reloads library
9. User clicks **Play** → `POST /game/launch-auto` dispatches to correct launcher

## Setup Wizard Dependencies

Required for all games:
- **Homebrew** — package manager
- **Rosetta 2** — x86_64 translation (`softwareupdate --install-rosetta`)
- **Xcode CLI Tools** — clang for building native stubs (`xcode-select --install`)
- **Mono arm64** — Terraria runtime (`brew install mono`)
- **GPTK Wine** — Rain World runtime (`brew install --cask gcenx/wine/game-porting-toolkit`)

Optional:
- **SteamCMD** — downloads Windows game files
- **Steam Client (macOS)** — provides native dylibs (especially for Terraria)

---

## Known Issues

- **GLib assertion spam:** Terraria prints `gpath.c:115: assertion 'filename != NULL' failed` many times. Harmless with our gdiplus stub. Comes from Homebrew's libgdiplus still being in DYLD path at index 1.
- **Terraria RGB warnings:** Razer/Corsair/Logitech RGB peripherals not found. Harmless.
- **Rain World QueryDisplayConfig spam:** GPTK Wine semi-stub, floods stderr. Harmless.
- **Celeste without real FMOD:** FMOD stubs = no audio. Real FMOD 1.10.20 x86_64 gives full audio but requires manual Carbon dep removal.
- **Carbon framework:** macOS 26 has NO Carbon binary, only empty directory structure. Any native lib depending on Carbon will fail to load.
- **steam_api.dylib:** The Steam client's `libsteam_api.dylib` is **missing 60 critical symbols** (including `GetISteamGameSearch`). Must use the macOS Terraria version instead (triple-arch, 1078 symbols).
- **`ld -force_load` loses symbols** when combining dylib with object files.
- **DYLD_INSERT_LIBRARIES doesn't work** for mono P/Invoke — mono uses `dlsym` on specific libraries.

---

## File Reference

| File | Purpose |
|---|---|
| `configs/terraria-mono.config` | Mono dllmap for Terraria (gdiplus→libgdiplus.dylib, System.Native→__Internal) |
| `configs/celeste-x86-mono.config` | Mono dllmap for Celeste x86_64 |
| `src/fna/terraria/gdiplus_stub.c` | 85-symbol stub preventing GLib assertion crash |
| `src/fna/terraria/TerrariaLauncher.cs` | Reflection launcher (sets OsxPlatform, LoadedEverything, bridges text input) |
| `src/fna/terraria/ContentPipelineStub.cs` | Prevents ForceLoadThread ReflectionTypeLoadException |
| `src/fna/terraria/Microsoft.Xna.Framework.Xact.dll` | Pre-built Xact assembly (Terraria audio system) |
| `src/fna/shims/csteamworks_shim.c` | Celeste SteamWorks shim with custom entry points |
| `src/fna/shims/csteamworks_aliases.txt` | 609 alias flags for CSteamworks build |
| `src/fna/shims/fmod_stub.c` | FMOD stub (no audio, game still runs) |
| `src/fna/shims/fmodstudio_stub.c` | FMOD Studio stub |
| `src/fna/shims/build_csteamworks.sh` | Reference build command (hardcoded paths, use setup script instead) |
| `scripts/setup-terraria-deps.sh` | Complete Terraria setup (idempotent, safe to re-run) |
| `scripts/setup-celeste-deps.sh` | Complete Celeste setup (idempotent) |
| `scripts/setup-rainworld-deps.sh` | Rain World setup (GPTK prefix init) |
| `scripts/launch-terraria.sh` | Terraria launcher script |
| `scripts/launch-celeste.sh` | Celeste launcher script |
| `scripts/launch-rainworld.sh` | Rain World launcher script |
| `scripts/install-steamcmd.sh` | SteamCMD installer |
