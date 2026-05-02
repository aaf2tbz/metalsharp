#!/usr/bin/env bash
set -euo pipefail

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
METALSHARP_REPO="$(cd "$(dirname "$0")/.." && pwd)"
GAMES_DIR="$METALSHARP_HOME/games"

GAME_ID="${1:-504230}"
GAME_DIR="$GAMES_DIR/$GAME_ID"

if [[ ! -d "$GAME_DIR" ]]; then
    echo "[metalsharp] Game directory not found: $GAME_DIR"
    echo "[metalsharp] Download the game first with steamcmd"
    exit 1
fi

if [[ ! -f "$GAME_DIR/Celeste.exe" ]]; then
    echo "[metalsharp] Celeste.exe not found in $GAME_DIR"
    exit 1
fi

MONO_X86="$METALSHARP_HOME/runtime/mono-x86/bin/mono"
if [[ ! -x "$MONO_X86" ]]; then
    echo "[metalsharp] x86_64 mono not found at $MONO_X86"
    echo "[metalsharp] Run: scripts/setup-celeste-deps.sh"
    exit 1
fi

cd "$GAME_DIR"

DYLD_LIBRARY_PATH="$METALSHARP_HOME/runtime/mono-x86/lib:/opt/homebrew/lib:.:$METALSHARP_HOME/shims" \
MONO_CONFIG="$METALSHARP_REPO/configs/celeste-x86-mono.config" \
MONO_PATH="$METALSHARP_HOME/runtime/mono-x86/lib/mono/4.5" \
FNA3D_DRIVER=OpenGL \
METAL_DEVICE_WRAPPER_TYPE=0 \
arch -x86_64 "$MONO_X86" Celeste.exe
