# Troubleshooting

## Game won't launch

### "Unresolved import" errors

The game is calling a Win32 function that MetalSharp doesn't implement yet.

**Fix:** Check `~/.metalsharp/logs/` for the list of missing imports. You can:
1. File an issue with the missing function names
2. Implement the shim yourself (see [Developer Guide](DEVELOPER-GUIDE.md))

### Crash on startup

Check `~/.metalsharp/crashes/` for crash dumps. Common causes:
- **NULL RIP** — An unresolved import was called. The game jumped to address 0.
- **Inside PE image** — An unsupported instruction or unimplemented API path
- **Kernel anti-cheat** — The game uses a kernel-level anti-cheat that can't run on macOS

### "No Metal device found"

MetalSharp requires a Metal-capable GPU. All Apple Silicon Macs and most Intel Macs (2012+) support Metal.

**Fix:** Ensure your macOS version is 13.0 (Ventura) or later.

## Graphics issues

### Black screen / no rendering

1. Check the game is using D3D11 or D3D12 (not OpenGL or Vulkan)
2. Try disabling MetalFX upscaling in Settings
3. Clear shader cache: Settings > Shader Cache > Clear
4. Enable Metal debug mode in Settings for detailed error output

### Flickering / visual artifacts

1. Try disabling VSync
2. Switch between fullscreen and borderless windowed mode
3. Reduce render resolution or enable upscaling
4. Clear both shader and pipeline caches

### Low performance

1. Enable MetalFX upscaling (Settings > Upscaling)
2. Check if Metal validation is enabled (disable it — it's slow)
3. Reduce render resolution
4. Set max frame rate if GPU is being overworked

## Audio issues

### No sound

MetalSharp translates XAudio2 calls to CoreAudio. If the game uses a different audio API:
1. Check if the game has a DirectSound fallback option
2. Check `~/.metalsharp/logs/` for audio initialization errors

### Crackling / stuttering

1. Close other audio-using applications
2. Check if the game has an audio buffer size setting (increase it)

## Steam issues

### "Steam not found"

MetalSharp looks for Steam at `~/Library/Application Support/Steam/`.

**Fix:** If Steam is installed elsewhere, create a symlink:
```bash
ln -s /path/to/Steam ~/Library/Application\ Support/Steam
```

### SteamCMD download fails

1. Check internet connectivity
2. SteamCMD may need authentication for some games:
```bash
~/steamcmd/steamcmd.sh +login <username>
```

### Can't detect installed games

MetalSharp reads `steamapps/libraryfolders.vdf` and `steamapps/appmanifest_*.acf` files.

**Fix:**
1. Make sure Steam is installed and has downloaded at least one game
2. Check `~/Library/Application Support/Steam/steamapps/` exists
3. If games are on an external drive, Steam library folders should be in the VDF file

## Wine issues

### "Wine not found"

```bash
brew install --cask wine-stable
```

### DLL injection not working

1. Check dylibs are in the Wine prefix:
```bash
ls ~/.metalsharp/prefix/drive_c/windows/system32/
# Should show: d3d11.dylib d3d12.dylib dxgi.dylib xaudio2_9.dylib xinput1_4.dylib
```

2. Re-run the installer: `./install.sh`

## Crash reports

Crash dumps are written to `~/.metalsharp/crashes/`. Each `.crash` file contains:
- Timestamp and game name
- Exit code and fault address
- System information
- Runtime log excerpt
- Import resolution log

To submit a crash report:
1. Open the file in a text editor
2. Copy the contents to a GitHub issue

## Build errors

### "Metal framework not found"

Install Xcode Command Line Tools:
```bash
xcode-select --install
```

### "CMake 3.24+ required"

```bash
brew install cmake
```

### Compiler errors with Objective-C++

Ensure you're using Apple Clang 15+ (comes with Xcode CLI tools on macOS 13+):
```bash
clang++ --version
```

## Getting Help

- [GitHub Issues](https://github.com/aaf2tbz/metalsharp/issues)
- [Architecture Overview](ARCHITECTURE.md)
- [Developer Guide](DEVELOPER-GUIDE.md)
