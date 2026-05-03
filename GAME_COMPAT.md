# MetalSharp Game Compatibility (macOS ARM64)

## Working

### Terraria (105600) — arm64 mono + FNA + SDL3 Metal
- **Rendering**: SDL3 + Metal via FNA3D OpenGL backend
- **Audio**: Real FAudio (built from source, SDL3 audio backend) — XACT WaveBank/SoundBank playback works
- **Input**: Keyboard/mouse via SDL3, text input via TextInputEXT bridge
- **Launch**: `mono TerrariaLauncher.exe` with `FNA3D_DRIVER=OpenGL`
- **Launcher**: `src/fna/terraria/TerrariaLauncher.cs` — patches Platform.Current to OsxPlatform, bypasses ForceLoadThread, bridges SDL text input
- **Known issues**: Text input in name entry fields may not render (IME bridge incomplete)
- **Required libs**: libFAudio.dylib (real, from FAudio source build), libSDL3.dylib, libFNA3D.dylib, P/Invoke shims (kernel32, user32, gdi32, shell32, Carbon, ReLogic.Native, nfd, steam_api)

### Celeste (504230) — x86_64 mono (Rosetta) + FNA + SDL3 Metal
- **Rendering**: SDL3 + Metal via FNA3D — runs at full speed
- **Audio**: Real FMOD (x86_64 dylibs with 609 CSteamworks alias shims) — all audio working
- **Input**: Working
- **Launch**: `arch -x86_64 mono Celeste.exe` with x86_64 mono runtime + SDL3 + FNA3D (x86_64 builds)
- **Critical**: Requires x86_64 mono (`~/.metalsharp/runtime/mono-x86/`), Rosetta 2, CSteamworks shim with 609 alias flags, FMOD x86_64 dylibs
- **FMOD**: Real FMOD 1.10 x86_64 dylibs work with x86_64 mono — full audio playback

### Rain World (312520) — GPTK Wine + D3DMetal
- **Rendering**: D3D11 → Apple D3DMetal (Game Porting Toolkit) — native D3D→Metal
- **Audio**: Working via GPTK Wine audio bridge
- **Input**: Working via GPTK Wine input translation
- **Launch**: GPTK wine64 + `WINEPREFIX=~/.metalsharp/prefix-gptk`
- **Architecture**: 64-bit PE32+ (Unity engine) — D3DMetal works for 64-bit processes
- **Required**: Game Porting Toolkit (`/Applications/Game Porting Toolkit.app/`)

### Nidhogg 2 (535520) — Wine Devel + DXVK + MoltenVK
- **Rendering**: D3D11 → DXVK (Vulkan) → MoltenVK → Metal — 4-hop translation chain
- **Audio**: Working — GameMaker audio via Wine
- **Input**: Working via Wine input translation + Steam Input (xinput1_3 disabled via DllOverrides)
- **Launch**: Wine 11.7 (devel) + `VK_ICD_FILENAMES` pointing to MoltenVK ICD + custom DXVK DLLs in game dir
- **Architecture**: 32-bit PE32 (GameMaker: Studio) — D3DMetal not available for 32-bit, so DXVK+MoltenVK is required
- **Custom DXVK**: v2.7.1 cross-compiled from source with 4 MoltenVK compatibility patches + SUBOPTIMAL_KHR ignore + FIFO present mode force:
  - `geometryShader` → optional (Metal has no geometry shaders)
  - `shaderCullDistance` → optional
  - `robustBufferAccess2` + `nullDescriptor` → optional
  - `VK_KHR_pipeline_library` → disabled (MoltenVK lacks support)
  - `VK_SUBOPTIMAL_KHR` → ignored in acquire and present paths (prevents swapchain recreation flicker)
  - Present mode → FIFO forced
- **Shader warmup**: ~45 second flicker/stutter on first launch while MoltenVK compiles SPIR-V→MSL. Stable after that. No fix possible without MoltenVK upstream changes.
- **Required**: Wine (devel), MoltenVK (`brew install molten-vk`), custom DXVK DLLs (`~/.metalsharp/runtime/dxvk-moltenvk/`)
- **Setup script**: `scripts/setup-nidhogg2-deps.sh` — builds DXVK from source or copies prebuilt DLLs
- **Patched source**: `scripts/dxvk/device_info.cpp.patched` — reproducible build artifact
- **Presenter patch**: `scripts/dxvk/dxvk_presenter.cpp.patch` — SUBOPTIMAL_KHR + FIFO fix

### Among Us (945360) — CrossOver Wine + Vulkan
- **Rendering**: Unity IL2CPP via CrossOver Wine built-in Vulkan/D3D11 support — native Metal via MoltenVK
- **Audio**: Working via CrossOver Wine audio bridge
- **Input**: Working via CrossOver Wine input translation + Steam Input
- **Launch**: CrossOver Wine (`/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/wine`) + per-game prefix
- **Architecture**: 64-bit PE32+ (Unity IL2CPP) — CrossOver provides 64-bit Wine runtime
- **Critical**: Uses CrossOver Wine (not GPTK or Wine Devel) — CrossOver has better Unity IL2CPP compatibility
- **DXVK removed** — CrossOver provides its own D3D/Vulkan translation, no DXVK DLLs needed in game dir
- **Required**: CrossOver (`/Applications/CrossOver.app/`)
- **Setup script**: `scripts/setup-amongus-deps.sh` — CrossOver prefix init, DXVK DLL removal

## Architecture Notes

### FNA Game Pipeline (Terraria, Celeste)
```
Game.exe (.NET Framework)
    → mono (arm64 or x86_64 via Rosetta)
    → FNA.dll (C# XNA compatibility layer)
    → SDL3 (window, input, audio via Metal)
    → FNA3D (OpenGL → Metal rendering)
    → FAudio (XACT audio, Terraria) / FMOD (Celeste)
    → P/Invoke shims (kernel32, user32, etc. → native dylibs)
```

### GPTK Pipeline (Rain World)
```
RainWorld.exe (64-bit PE32+, Unity)
    → GPTK Wine (Win32 API translation)
    → D3DMetal (Apple's D3D→Metal, works for 64-bit)
    → Apple Metal → GPU
```

### DXVK Pipeline (Nidhogg 2)
```
Nidhogg_2.exe (32-bit PE32, GameMaker)
    → Wine 11.7 Devel (Win32 API translation, 32-bit support)
    → DXVK d3d11.dll (D3D11 → Vulkan, patched for MoltenVK)
    → MoltenVK (Vulkan → Metal)
    → Apple Metal → GPU
```

### CrossOver Pipeline (Among Us)
```
Among Us.exe (64-bit PE32+, Unity IL2CPP)
    → CrossOver Wine (Win32 API translation)
    → Built-in Vulkan/D3D11 support
    → MoltenVK (Vulkan → Metal)
    → Apple Metal → GPU
```

### Audio Status per Game
| Game | Audio Engine | Native Lib | Status |
|------|-------------|-----------|--------|
| Terraria | XACT (FAudio) | libFAudio.dylib (arm64) | Working |
| Celeste | FMOD Studio | libfmod.dylib (x86_64) | Working |
| Rain World | FMOD (Unity) | via GPTK Wine | Working |
| Nidhogg 2 | GameMaker audio | via Wine | Working |
| Among Us | FMOD (Unity) | via CrossOver Wine | Working |

### Rendering Pipeline per Game
| Game | Pipeline | Hops | Notes |
|------|----------|------|-------|
| Terraria | FNA → SDL3 Metal | 1 | Native arm64, fastest |
| Celeste | FNA → SDL3 Metal | 1 | x86_64 via Rosetta, full speed |
| Rain World | D3D11 → D3DMetal | 2 | Apple GPTK, 64-bit only |
| Nidhogg 2 | D3D11 → DXVK → MoltenVK → Metal | 4 | Cross-compiled DXVK, 32-bit only |
| Among Us | D3D11 → CrossOver Vulkan → Metal | 3 | CrossOver Wine, 64-bit |
