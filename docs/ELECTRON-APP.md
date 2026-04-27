# Electron App

MetalSharp includes an Electron frontend with a Rust HTTP backend for game library management, Steam integration, and launch configuration.

## Architecture

```
Electron Main Process (Node.js)
    │
    ├── RustBridge — spawns metalsharp-backend binary
    │       │
    │       ▼
    │   Rust HTTP Server (tiny_http, port 9274)
    │       ├── /scan          — detect installed games
    │       ├── /launch        — launch game via metalsharp binary
    │       ├── /kill          — kill running game process
    │       ├── /config        — get/set launch mode (native/wine)
    │       ├── /steam/status  — Steam install + login state
    │       ├── /steam/install — download and install Windows Steam
    │       ├── /steam/download-game    — download game via SteamCMD
    │       ├── /steam/download-progress — live download progress
    │       └── /logs          — runtime log files
    │
    ├── IPC Bridge (contextBridge)
    │       └── window.metalsharp.request(method, url, body)
    │
    └── BrowserWindow (Chromium)
            └── Renderer (index.ts)
                ├── Library — game grid with Play/Stop
                ├── Store — download games by Steam App ID
                ├── Logs — runtime log viewer
                └── Settings — launch mode, Steam config, status
```

## Directory Structure

```
app/
├── package.json
├── tsconfig.json
├── src/
│   ├── main/
│   │   ├── index.ts        — Electron main process, window creation
│   │   ├── rust-bridge.ts  — spawns Rust binary, HTTP request wrapper
│   │   └── preload.ts      — contextBridge for IPC
│   ├── renderer/
│   │   ├── index.html      — app shell with sidebar nav
│   │   ├── index.ts        — App class with all view rendering
│   │   ├── styles.css      — dark theme, game cards, settings
│   │   └── api-types.ts    — TypeScript interfaces for API responses
│   └── shared/
│       └── types.ts        — shared type definitions
└── src-rust/
    ├── Cargo.toml
    └── src/
        ├── main.rs          — HTTP router, request parsing
        ├── launch.rs        — game launching (native + wine)
        ├── steam.rs         — Steam status, install, download
        └── scan.rs          — game detection and library scanning
```

## API Endpoints

### Game Management

| Method | Path | Description |
|--------|------|-------------|
| GET | `/scan` | Detect all games (Steam prefix + local) |
| POST | `/launch` | Launch a game executable |
| POST | `/kill` | Kill a running game by PID |

### Configuration

| Method | Path | Description |
|--------|------|-------------|
| GET | `/config` | Get current config (launch mode, availability) |
| POST | `/config` | Set launch mode (`native` or `wine`) |

### Steam

| Method | Path | Description |
|--------|------|-------------|
| GET | `/steam/status` | Steam install status, login state, running state |
| POST | `/steam/install` | Download and launch Steam installer |
| POST | `/steam/download-game` | Download game via SteamCMD |
| GET | `/steam/download-progress` | Live download progress percentage |

### System

| Method | Path | Description |
|--------|------|-------------|
| GET | `/status` | Backend health check |
| GET | `/logs` | Runtime log files |

## Launch Modes

### Native (PE Loader)

Launches the game directly through MetalSharp's native PE loader:

```
metalsharp-backend → spawn("metalsharp", [exePath])
```

No Wine dependency. The PE loader handles everything: section mapping, imports, D3D translation.

### Wine (Legacy)

Launches the game through Wine with MetalSharp DLL injection:

```
metalsharp-backend → spawn("wine64", [exePath], { WINEPREFIX: prefix })
```

Used as a fallback for executables not yet supported by the native loader.

## Game Detection

The scanner checks:

1. **Steam prefix** — `~/.metalsharp/prefix/drive_c/Program Files (x86)/Steam/steamapps/`
   - Parses `appmanifest_*.acf` files for game name and app ID
   - Reads `libraryfolders.vdf` for additional library paths
   - Scans each game directory for `.exe` files
2. **Local games** — `~/.metalsharp/games/` (downloaded via SteamCMD)
   - Walks subdirectories for `.exe` files
3. **Steam login** — Reads `Steam/config/loginusers.vdf` for account names

## Building

```bash
cd app

# Build Rust backend
npm run rust:build

# Build TypeScript + HTML
npm run build

# Build everything
npm run build:all

# Run
npm run start
```

## Configuration

Config is stored at `~/.metalsharp/config.json`:

```json
{
  "launchMode": "native"
}
```

## Data Paths

| Path | Purpose |
|------|---------|
| `~/.metalsharp/prefix/` | Wine prefix with virtual C: drive |
| `~/.metalsharp/games/` | Downloaded games (SteamCMD) |
| `~/.metalsharp/cache/shader_cache/` | Persisted DXBC→MSL translations |
| `~/.metalsharp/cache/pipeline_cache/` | Persisted pipeline state index |
| `~/.metalsharp/config.json` | Launch configuration |
| `~/.metalsharp/download_progress.json` | Transient download progress |
| `~/.metalsharp/logs/` | Runtime log files |
