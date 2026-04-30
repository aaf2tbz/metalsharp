# User Guide

## Installation

### Quick Install

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp
./install.sh
```

The installer checks for dependencies (Xcode CLI tools, Homebrew, CMake, Wine, SteamCMD), builds MetalSharp, runs the test suite, and sets up the Wine prefix.

### Manual Install

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

## Requirements

- macOS 13+ (Ventura or later)
- Apple Silicon Mac (M1/M2/M3/M4) or Intel Mac with Metal support
- Xcode Command Line Tools
- CMake 3.24+
- Wine (optional, for DLL injection mode)

## Launching Games

### Method 1: Electron App (Recommended)

```bash
cd app
npm install
npm run build:all
npm run start
```

The Electron app provides a graphical interface for:
- Browsing your game library (auto-detected from Steam, Epic, GOG)
- Downloading games via SteamCMD
- Launching with one click
- Viewing logs and crash reports
- Configuring settings (resolution, upscaling, shader cache)

### Method 2: Native PE Loader (CLI)

```bash
./build/metalsharp path/to/game.exe
```

Loads the Windows executable directly through MetalSharp's PE loader. No Wine needed.

### Method 3: Wine Launcher (CLI)

```bash
./build/metalsharp_launcher game.exe
```

Uses Wine to run the executable with MetalSharp's DLL shims injected.

## Configuration

Settings are stored at `~/.metalsharp/settings.json`:

| Setting | Default | Description |
|---------|---------|-------------|
| render_width | 1920 | Internal render resolution width |
| render_height | 1080 | Internal render resolution height |
| window_mode | fullscreen | `fullscreen`, `borderless`, or `windowed` |
| upscaling_quality | off | MetalFX upscaling: `off`, `low`, `medium`, `high`, `ultra` |
| vsync | true | Synchronize to display refresh |
| shader_cache_enabled | true | Cache compiled shaders to disk |
| pipeline_cache_enabled | true | Cache pipeline state objects |
| launch_mode | native | `native` (PE loader) or `wine` |
| crash_reporting | true | Auto-collect crash diagnostics |

Per-game profiles are stored in `~/.metalsharp/profiles/<game>.json` and override global settings.

## Directory Layout

```
~/.metalsharp/
├── settings.json       Global settings
├── config.json         Legacy config (used by Rust backend)
├── games/              Manually added game directories
├── cache/
│   ├── shader_cache/   Compiled Metal shader cache
│   └── pipeline_cache/ Pipeline state cache
├── crashes/            Crash dump files
├── logs/               Runtime logs
├── diag/               Diagnostic bundles
├── profiles/           Per-game config overrides
└── prefix/             Wine prefix (DLL injection mode)
```

## Updating

The Electron app checks for updates automatically on startup. You can also check manually in Settings > Updates.

To update manually:

```bash
cd metalsharp
git pull
cmake --build build
```

## Getting Help

- Check [Troubleshooting](TROUBLESHOOTING.md) for common issues
- Open a [GitHub issue](https://github.com/aaf2tbz/metalsharp/issues)
