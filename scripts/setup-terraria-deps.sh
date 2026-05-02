#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[terraria-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
METALSHARP_REPO="$(cd "$(dirname "$0")/.." && pwd)"
GAMES_DIR="$METALSHARP_HOME/games"
GAME_ID="${1:-105600}"
GAME_DIR="$GAMES_DIR/$GAME_ID"
TERRARIA_SRC="$METALSHARP_REPO/src/fna/terraria"

mkdir -p "$GAME_DIR"

[[ -d "$GAME_DIR" ]] || fail "Game directory not found: $GAME_DIR"
[[ -f "$GAME_DIR/Terraria.exe" ]] || fail "Terraria.exe not found in $GAME_DIR"

step() { echo ""; info "── $1 ──"; }

step "1/7: Checking system mono"
if command -v mono &>/dev/null; then
    ok "mono $(mono --version | head -1 | grep -o '[0-9.]*' | head -1)"
else
    fail "mono not found. Install with: brew install mono"
fi

step "2/7: Copying native libraries from macOS Terraria"
MAC_TERRARIA="$HOME/Library/Application Support/Steam/steamapps/common/Terraria"
MAC_LIBS="$MAC_TERRARIA/Terraria.app/Contents/MacOS/osx"

if [[ -d "$MAC_LIBS" ]]; then
    for lib in libsteam_api.dylib libSDL3.0.dylib libFAudio.0.dylib libFNA3D.0.dylib libnfd.dylib; do
        if [[ -f "$MAC_LIBS/$lib" ]]; then
            cp -f "$MAC_LIBS/$lib" "$GAME_DIR/"
            ok "Copied $lib"
        fi
    done
    ln -sf libSDL3.0.dylib "$GAME_DIR/libSDL3.dylib" 2>/dev/null || true
    ln -sf libFAudio.0.dylib "$GAME_DIR/libFAudio.dylib" 2>/dev/null || true
    ln -sf libFNA3D.0.dylib "$GAME_DIR/libFNA3D.dylib" 2>/dev/null || true
    ok "macOS Terraria native libs installed"
else
    fail "macOS Terraria not found at $MAC_TERRARIA. Install Terraria (macOS) via Steam first, then re-run. The macOS native libs are required for Windows Terraria to work."
fi

step "3/7: Building gdiplus stub (prevents GLib crash)"
if [[ -f "$GAME_DIR/libgdiplus.dylib" ]] && file "$GAME_DIR/libgdiplus.dylib" | grep -q "arm64"; then
    ok "gdiplus stub already present"
else
    clang -shared -arch arm64 -o "$GAME_DIR/libgdiplus.dylib" \
        "$TERRARIA_SRC/gdiplus_stub.c" \
        -install_name @loader_path/libgdiplus.dylib \
        2>/dev/null && ok "gdiplus stub built" || fail "gdiplus stub build failed"
fi

step "4/7: Building TerrariaLauncher"
if [[ -f "$GAME_DIR/TerrariaLauncher.exe" ]]; then
    ok "TerrariaLauncher.exe already present"
else
    mcs -out:"$GAME_DIR/TerrariaLauncher.exe" -target:winexe \
        "$TERRARIA_SRC/TerrariaLauncher.cs" \
        2>/dev/null && ok "TerrariaLauncher built" || fail "TerrariaLauncher build failed"
fi

step "5/7: Building ContentPipeline stub"
PIPELINE_DLL="$GAME_DIR/Microsoft.Xna.Framework.Content.Pipeline.dll"
if [[ -f "$PIPELINE_DLL" ]] && [[ $(stat -f%z "$PIPELINE_DLL" 2>/dev/null || echo 0) -lt 10000 ]]; then
    ok "ContentPipeline stub already present"
else
    mcs -out:"$PIPELINE_DLL" -target:library \
        "$TERRARIA_SRC/ContentPipelineStub.cs" \
        2>/dev/null && ok "ContentPipeline stub built" || fail "ContentPipeline stub build failed"
fi

step "6/7: Installing Xact assembly"
if [[ -f "$GAME_DIR/Microsoft.Xna.Framework.Xact.dll" ]]; then
    ok "Xact assembly already present"
else
    cp "$TERRARIA_SRC/Microsoft.Xna.Framework.Xact.dll" "$GAME_DIR/"
    ok "Xact assembly installed"
fi

step "7/7: Writing steam_appid.txt"
echo "$GAME_ID" > "$GAME_DIR/steam_appid.txt"
ok "steam_appid.txt written"

echo ""
ok "Terraria setup complete!"
info "Launch with: $METALSHARP_REPO/scripts/launch-terraria.sh"
