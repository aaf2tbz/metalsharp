#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[rainworld-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
GAMES_DIR="$METALSHARP_HOME/games"
GAME_ID="${1:-312520}"
GAME_DIR="$GAMES_DIR/$GAME_ID"

if [[ ! -d "$GAME_DIR" ]]; then
    fail "Game directory not found: $GAME_DIR. Download with steamcmd first."
fi

if [[ ! -f "$GAME_DIR/RainWorld.exe" ]]; then
    fail "RainWorld.exe not found in $GAME_DIR"
fi

step() { echo ""; info "── $1 ──"; }

step "1/4: Checking Rosetta 2"
if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
    info "Installing Rosetta 2..."
    softwareupdate --install-rosetta --agree-to-license || fail "Rosetta 2 install failed"
fi
ok "Rosetta 2 available"

step "2/4: Installing Game Porting Toolkit"
GPTK_WINE="/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
if [[ -x "$GPTK_WINE" ]]; then
    ok "Game Porting Toolkit already installed"
else
    info "Installing Game Porting Toolkit via Homebrew..."
    brew install --cask gcenx/wine/game-porting-toolkit || fail "GPTK install failed"
    ok "Game Porting Toolkit installed"
fi

step "3/4: Initializing Wine prefix"
WINEPREFIX="$METALSHARP_HOME/prefix-gptk"
if [[ -d "$WINEPREFIX/drive_c" ]]; then
    ok "Wine prefix already initialized at $WINEPREFIX"
else
    info "Initializing Wine prefix..."
    WINEPREFIX="$WINEPREFIX" "$GPTK_WINE" wineboot
    ok "Wine prefix initialized"
fi

step "4/4: Verifying D3DMetal"
if [[ -f "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-windows/d3d11.dll" ]]; then
    ok "D3D11.dll present (GPTK D3D→Metal translation)"
else
    fail "D3D11.dll not found in GPTK — reinstall may be needed"
fi

echo ""
ok "Rain World setup complete!"
info "Launch with: $(cd "$(dirname "$0")/.." && pwd)/scripts/launch-rainworld.sh"
