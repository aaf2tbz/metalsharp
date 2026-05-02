#!/usr/bin/env bash
set -euo pipefail

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
GAMES_DIR="$METALSHARP_HOME/games"

GAME_ID="${1:-312520}"
GAME_DIR="$GAMES_DIR/$GAME_ID"

if [[ ! -d "$GAME_DIR" ]]; then
    echo "[metalsharp] Game directory not found: $GAME_DIR"
    echo "[metalsharp] Download the game first with steamcmd"
    exit 1
fi

if [[ ! -f "$GAME_DIR/RainWorld.exe" ]]; then
    echo "[metalsharp] RainWorld.exe not found in $GAME_DIR"
    exit 1
fi

GPTK_WINE="/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
if [[ ! -x "$GPTK_WINE" ]]; then
    echo "[metalsharp] Game Porting Toolkit not found at $GPTK_WINE"
    echo "[metalsharp] Install with: brew install --cask gcenx/wine/game-porting-toolkit"
    exit 1
fi

WINEPREFIX="$METALSHARP_HOME/prefix-gptk"

if [[ ! -d "$WINEPREFIX" ]]; then
    echo "[metalsharp] Initializing GPTK Wine prefix..."
    WINEPREFIX="$WINEPREFIX" "$GPTK_WINE" wineboot
fi

cd "$GAME_DIR"

WINEPREFIX="$WINEPREFIX" "$GPTK_WINE" RainWorld.exe
