# Adding a Game to MetalSharp

This guide walks through adding support for a new Windows game. You don't need AI assistance — just follow the steps, test, and submit a PR.

## Overview

Adding a game means touching three files:

| File | What you add |
|------|-------------|
| `app/src-rust/src/launch.rs` | Launch entry (maps App ID → exe + runtime) |
| `app/src-rust/src/setup.rs` | Setup function (runs automatically after download) |
| `GAME_COMPAT.md` | Compatibility notes |

If the game needs new native libraries or a build-from-source step, you'll also add a script in `scripts/`.

## Step 1: Identify the game

Find the Steam App ID. Easiest way: look at the game's Steam store page URL. `https://store.steampowered.com/app/1139900/Ghostrunner/` → App ID is `1139900`.

Then find the main executable name. Download the game through MetalSharp and check `~/.metalsharp/games/<appid>/` for the `.exe` file.

```bash
ls ~/.metalsharp/games/1139900/*.exe
# Ghostrunner.exe
```

## Step 2: Pick a runtime

MetalSharp supports these launch pipelines:

| Pipeline | Function | Best for | Requirements |
|----------|----------|----------|-------------|
| CrossOver Wine | `launch_crossover_wine()` | 64-bit Unity/UE4 D3D11 games | CrossOver installed |
| Wine Devel | `launch_wine_devel()` | 32-bit D3D9 games | Wine Devel installed |
| DXVK + MoltenVK | `launch_dxvk_wine()` | 32-bit D3D11 games | Wine Devel, MoltenVK, DXVK DLLs |
| GPTK | `launch_gptk()` | 64-bit Unity D3D11 games | Game Porting Toolkit |
| FNA arm64 | `launch_fna_arm64()` | XNA games (arm64) | Mono arm64, FNA, SDL3 |
| FNA x86 | `launch_fna_x86()` | XNA games (x86_64, e.g. Celeste) | Mono x86, FNA, SDL3 x86 |

**How to choose:**

1. **Is it an XNA/FNA game?** (look for `_Data/Managed/` folder) → Use FNA pipeline
2. **Is it 64-bit and uses D3D11?** → Try CrossOver first. If that fails, try GPTK.
3. **Is it 32-bit and uses D3D11?** → Use Wine Devel + DXVK + MoltenVK
4. **Is it 32-bit and uses D3D9?** → Use Wine Devel (built-in wined3d)
5. **Does it need Steam auth to run?** → Add Goldberg emulator (see Portal 2 as example)

Check the exe architecture:
```bash
file ~/.metalsharp/games/<appid>/<game>.exe
# PE32+ executable = 64-bit
# PE32 executable = 32-bit
```

## Step 3: Test manually first

Before writing any code, verify the game actually runs:

```bash
# CrossOver test
CX="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
WINE="$CX/lib/wine/x86_64-unix/wine"
mkdir -p ~/.metalsharp/prefix-test
WINEPREFIX="$HOME/.metalsharp/prefix-test" "$WINE" wineboot --init
CX_ROOT="$CX" \
DYLD_FALLBACK_LIBRARY_PATH="$CX/lib64/apple_gptk/external:$CX/lib64:/opt/homebrew/lib" \
WINEPREFIX="$HOME/.metalsharp/prefix-test" \
"$WINE" "/path/to/game.exe"

# Wine Devel test
WINE="/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"
mkdir -p ~/.metalsharp/prefix-test
WINEPREFIX="$HOME/.metalsharp/prefix-test" "$WINE" wineboot --init
WINEPREFIX="$HOME/.metalsharp/prefix-test" "$WINE" "/path/to/game.exe"

# GPTK test
GPTK="/Applications/Game Porting Toolkit.app/Contents/Resources"
WINE="$GPTK/wine/bin/wine64"
GPTK_EXT="$GPTK/wine/lib/external"
mkdir -p ~/.metalsharp/prefix-test
WINEPREFIX="$HOME/.metalsharp/prefix-test" "$WINE" wineboot --init
METAL_DEVICE_WRAPPER_TYPE=0 \
DYLD_LIBRARY_PATH="$GPTK_EXT:$GPTK/wine/lib" \
DYLD_FALLBACK_LIBRARY_PATH="$GPTK_EXT:$GPTK/wine/lib:/opt/homebrew/lib" \
WINEPREFIX="$HOME/.metalsharp/prefix-test" \
"$WINE" "/path/to/game.exe"
```

If the game window appears and renders, you've found the right pipeline. Move on to Step 4.

## Step 4: Add the launch entry

Open `app/src-rust/src/launch.rs`. Find the `launch_auto` function's `match appid` block and add your game:

```rust
// In launch_auto(), add before the _ => fallback:
YOUR_APPID => {
    let exe = game_dir.join("YourGame.exe");
    let pid = launch_crossover_wine(&exe.to_string_lossy(), &game_dir)?;
    Ok((pid, "crossover_wine"))
}
```

Replace `YOUR_APPID` with the Steam App ID, `"YourGame.exe"` with the exe name, and use the launch function you confirmed works in Step 3.

## Step 5: Add setup logic

Open `app/src-rust/src/setup.rs`. There are two places to update:

### 5a: Game type detection

In `prepare_game()`, find the `game_type` match block:

```rust
let game_type = match appid {
    // existing entries...
    YOUR_APPID => "crossover_wine",  // or "dxvk_wine", "gptk_wine", etc.
    _ => if is_dotnet { "xna_fna" } else { "native" },
};
```

### 5b: Setup function

In the same function's `match appid` block, add your setup call:

```rust
match appid {
    // existing entries...
    YOUR_APPID => prepare_your_game(&game_dir, &home)?,
    _ => { ... }
}
```

Then write the setup function. What goes in it depends on the pipeline:

**CrossOver games** (simplest):
```rust
fn prepare_your_game(game_dir: &PathBuf, home: &PathBuf, appid: u32) -> Result<(), Box<dyn std::error::Error>> {
    let crossover_base = PathBuf::from("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver");
    let wine = crossover_base.join("lib/wine/x86_64-unix/wine");
    let prefix = home.join(".metalsharp").join(format!("prefix-{}", appid));

    if !prefix.exists() {
        std::fs::create_dir_all(&prefix)?;
        if wine.exists() {
            let _ = std::process::Command::new(&wine)
                .env("WINEPREFIX", prefix.to_string_lossy().to_string())
                .arg("wineboot").arg("--init")
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
    }

    // Remove DXVK DLLs if present — CrossOver has its own D3D stack
    for dll in &["d3d11.dll", "dxgi.dll"] {
        let game_dll = game_dir.join(dll);
        if game_dll.exists() {
            let _ = std::fs::remove_file(&game_dll);
        }
    }

    Ok(())
}
```

**DXVK games** — copy DXVK DLLs into game dir, init Wine prefix with registry overrides. See `prepare_nidhogg_2()` as an example.

**Goldberg games** — replace steam_api DLLs, create steam_settings. See `prepare_portal_2()` as an example.

**FNA games** — build native dylibs, compile shims. These are complex — see `prepare_terrarria()` and the scripts in `scripts/`.

## Step 6: Update GAME_COMPAT.md

Add your game to the Working section:

```markdown
### Your Game (APPID) — Runtime Used
- **Rendering**: How graphics work
- **Audio**: How audio works
- **Input**: How input works
- **Launch**: Which Wine/runtime + env vars
- **Architecture**: 32-bit PE32 or 64-bit PE32+
- **Required**: What needs to be installed
```

Also update the tables (Audio Status, Rendering Pipeline) at the bottom of the file.

## Step 7: Build and test

```bash
# Build backend
cd app/src-rust && cargo build --release

# Build full app
cd app && npm run build && npx electron-builder --mac dmg

# Launch and test
open dist/electron/mac-arm64/MetalSharp.app
```

Test the full flow: Install → Setup → Play → Stop.

## Step 8: Commit and PR

```bash
git checkout -b feat/your-game-name
git add -A
git commit -m "feat: Your Game support via <runtime>"
git push origin feat/your-game-name
```

Then open a PR against `main`.

## Quick reference: existing games as templates

| Want to add... | Copy from |
|----------------|-----------|
| CrossOver game (64-bit D3D11) | Ghostrunner (`1139900`) — simplest example |
| CrossOver game with DXVK removal | Among Us (`945360`) |
| Wine Devel + Goldberg | Portal 2 (`620`) |
| Wine + DXVK + MoltenVK | Nidhogg 2 (`535520`) |
| GPTK Wine | Rain World (`312520`) |
| FNA arm64 | Terraria (`105600`) |
| FNA x86 + FMOD | Celeste (`504230`) — most complex |

## Troubleshooting

**Game exits immediately with no error** — Likely a graphics API issue. Check if the game uses D3D12 (look for a `D3D12/` folder or `D3D12Core.dll`). D3D12 games need VKD3D-Proton or GPTK with D3DMetal, which is still experimental.

**"D3D11-compatible GPU required"** — The game can't find a working D3D11 implementation. Make sure DXVK DLLs are in the game dir (for Wine Devel) or removed (for CrossOver).

**Steam Guard keeps triggering** — MetalSharp caches credentials after first login. If it keeps asking, check that `~/.metalsharp/cache/steam_config.json` has your username and password saved.

**Game downloads but shows "error"** — Some games fail SteamCMD's `app_update` due to subscription checks. MetalSharp falls back to depot-level download automatically. Check `download_progress.json` for status.

**Audio not working** — Unity/UE4 games usually work out of the box via Wine's audio bridge. FNA games need FMOD (x86) or FAudio (arm64) dylibs built separately.
