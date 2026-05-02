#!/usr/bin/env bash
set -euo pipefail

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
GAMES_DIR="$METALSHARP_HOME/games"

GAME_ID="${1:-105600}"
GAME_DIR="$GAMES_DIR/$GAME_ID"

if [[ ! -d "$GAME_DIR" ]]; then
    echo "[metalsharp] Game directory not found: $GAME_DIR"
    echo "[metalsharp] Download the game first with steamcmd"
    exit 1
fi

if [[ ! -f "$GAME_DIR/TerrariaLauncher.exe" ]]; then
    echo "[metalsharp] TerrariaLauncher.exe not found in $GAME_DIR"
    exit 1
fi

MONO="/opt/homebrew/bin/mono"
if ! command -v "$MONO" &>/dev/null; then
    echo "[metalsharp] mono not found. Install with: brew install mono"
    exit 1
fi

METALSHARP_REPO="$(cd "$(dirname "$0")/.." && pwd)"

cd "$GAME_DIR"

DYLD_LIBRARY_PATH="$GAME_DIR:/opt/homebrew/lib" \
MONO_CONFIG="$METALSHARP_REPO/configs/terraria-mono.config" \
FNA3D_DRIVER=OpenGL \
"$MONO" TerrariaLauncher.exe
