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

mkdir -p "$GAME_DIR"

if [[ ! -d "$GAME_DIR" ]]; then
    fail "Game directory not found: $GAME_DIR. Download with steamcmd first."
fi

if [[ ! -f "$GAME_DIR/TerrariaLauncher.exe" ]]; then
    fail "TerrariaLauncher.exe not found in $GAME_DIR"
fi

step() { echo ""; info "── $1 ──"; }

step "1/5: Checking system mono"
if command -v mono &>/dev/null; then
    ok "mono found: $(mono --version | head -1)"
else
    info "Installing mono via Homebrew..."
    brew install mono || fail "mono install failed"
    ok "mono installed"
fi

step "2/5: Installing FAudio"
if [[ -f "$GAME_DIR/libFAudio.dylib" ]]; then
    ok "FAudio already present"
else
    if brew list faudio &>/dev/null 2>&1; then
        FAUDIO_LIB="$(brew --prefix faudio)/lib/libFAudio.dylib"
        cp "$FAUDIO_LIB" "$GAME_DIR/" 2>/dev/null && ok "FAudio copied from Homebrew"
    else
        info "Installing FAudio via Homebrew..."
        brew install faudio || fail "FAudio install failed"
        FAUDIO_LIB="$(brew --prefix faudio)/lib/libFAudio.dylib"
        cp "$FAUDIO_LIB" "$GAME_DIR/"
        ok "FAudio installed and copied"
    fi
fi

step "3/5: Checking SDL3 (arm64)"
if [[ -f "$GAME_DIR/libSDL3.0.dylib" ]] || [[ -f "/opt/homebrew/lib/libSDL3.0.dylib" ]]; then
    ok "SDL3 available"
else
    info "SDL3 will be loaded from /opt/homebrew/lib (install via Homebrew if needed)"
fi

step "4/5: Building Terraria launcher"
LAUNCHER_SRC="$METALSHARP_REPO/src/fna/terraria/TerrariaLauncher.cs"
if [[ -f "$GAME_DIR/TerrariaLauncher.exe" ]]; then
    ok "TerrariaLauncher.exe already present"
else
    info "Building TerrariaLauncher..."
    mcs -out:"$GAME_DIR/TerrariaLauncher.exe" -target:winexe "$LAUNCHER_SRC" 2>/dev/null && \
        ok "TerrariaLauncher built" || \
        info "Pre-built TerrariaLauncher.exe needed — place in $GAME_DIR/"
fi

step "5/5: Verifying dependencies"
cd "$GAME_DIR"
DYLD_LIBRARY_PATH="/opt/homebrew/lib:$GAME_DIR" \
MONO_CONFIG="$METALSHARP_REPO/configs/terraria-mono.config" \
mono --debug=casts TerrariaLauncher.exe --help 2>/dev/null && ok "Launcher verified" || true

echo ""
ok "Terraria setup complete!"
info "Launch with: $METALSHARP_REPO/scripts/launch-terraria.sh"
