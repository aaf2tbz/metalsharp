# MetalSharp Game Compatibility (macOS ARM64)

## Working

### Terraria (105600) — mono + FNA + SDL3 Metal
- **Rendering**: SDL3 + Metal via FNA3D OpenGL backend
- **Audio**: Real FAudio (built from source, SDL3 audio backend) — XACT WaveBank/SoundBank playback works
- **Input**: Keyboard/mouse via SDL3, text input via TextInputEXT bridge
- **Launch**: `mono TerrariaLauncher.exe` with `FNA3D_DRIVER=OpenGL`
- **Launcher**: `src/fna/terraria/TerrariaLauncher.cs` — patches Platform.Current to OsxPlatform, bypasses ForceLoadThread, bridges SDL text input
- **Known issues**: Text input in name entry fields may not render (IME bridge incomplete)
- **Required libs**: libFAudio.dylib (real, from FAudio source build), libSDL3.dylib, libFNA3D.dylib, P/Invoke shims (kernel32, user32, gdi32, shell32, Carbon, ReLogic.Native, nfd, steam_api)

### Celeste (504230) — mono + FNA + SDL3 Metal (no audio)
- **Rendering**: SDL3 + Metal via FNA3D — runs at full speed
- **Audio**: BLOCKED — FMOD Studio bank format 103 requires FMOD 1.10 API, which only ships x86_64 dylibs. FMOD 2.03 (universal) gives ERR_HEADER_MISMATCH. Needs x86_64 mono or bank re-encoding.
- **Input**: Working
- **Launch**: `mono Celeste.exe` with `FNA3D_DRIVER=OpenGL` and `MONO_CONFIG` including `System.Native → libmono-native.dylib` dllmap
- **Critical fix**: `System.Native → libmono-native.dylib` dllmap required (mono 6.14 on macOS needs this for System.IO)
- **FMOD status**: Real FMOD 2.03 dylibs installed (universal, ad-hoc signed) but bank format mismatch

## Blocked

### Rain World (312520) — Unity D3D11 via Wine
- **Status**: D3D11 device creation fails — Wine's OpenGL backend doesn't support required feature levels on Apple Silicon
- **Blocker**: Needs D3D11 → Metal translation shim (core MetalSharp feature, not yet wired for Wine games)
- **Alternative**: DXVK (D3D11 → Vulkan → MoltenVK → Metal) could work but adds latency
- **Log**: `wined3d_select_feature_level: None of the requested D3D feature levels is supported on this GPU with the current shader backend`

## Architecture Notes

### FNA Game Pipeline (Terraria, Celeste, etc.)
```
Game.exe (.NET Framework)
    → mono (arm64, /opt/homebrew/bin/mono)
    → FNA.dll (C# XNA compatibility layer)
    → SDL3 (window, input, audio via Metal)
    → FNA3D (OpenGL → Metal rendering)
    → FAudio (XACT audio, only Terraria — built from source with SDL3 backend)
    → P/Invoke shims (kernel32, user32, etc. → native dylibs)
```

### Audio Status per Game
| Game | Audio Engine | Native Lib | Status |
|------|-------------|-----------|--------|
| Terraria | XACT (FAudio) | libFAudio.dylib (arm64, built from source) | Working |
| Celeste | FMOD Studio | libfmod.dylib (needs 1.10 x86_64) | Blocked — arch mismatch |
| Rain World | FMOD (Unity) | N/A (D3D11 blocked first) | Blocked — D3D11 |

### Next Steps
1. **Celeste audio**: Install x86_64 mono (Intel homebrew prefix) + use x86_64 FMOD 1.10 API dylibs
2. **Rain World**: Wire MetalSharp D3D11 → Metal shim into Wine game launch path
3. **Generalize**: Extract common FNA game launcher from Terraria-specific code
