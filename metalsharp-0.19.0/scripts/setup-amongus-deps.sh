#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[amongus-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
GAME_ID="${1:-945360}"
GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"

mkdir -p "$GAME_DIR"

step() { echo ""; info "── $1 ──"; }

step "1/3: Checking MetalSharp Wine"
MS_WINE="$METALSHARP_HOME/runtime/wine/bin/metalsharp-wine"
if [[ ! -x "$MS_WINE" ]]; then
    fail "MetalSharp Wine not found — run setup to install the runtime"
fi
ok "MetalSharp Wine found"

step "2/3: Initializing Wine prefix"
PREFIX="$METALSHARP_HOME/prefix-steam"
if [[ -d "$PREFIX/drive_c/windows/system32" ]]; then
    ok "Wine prefix already initialized"
else
    info "Creating MetalSharp Wine prefix at $PREFIX..."
    WINEPREFIX="$PREFIX" "$MS_WINE" wineboot --init 2>/dev/null || fail "Wine prefix init failed"
    ok "Wine prefix created"
fi

step "3/3: Removing DXVK DLLs (MetalSharp Wine provides its own D3D stack)"
for dll in d3d11.dll dxgi.dll; do
    if [[ -f "$GAME_DIR/$dll" ]]; then
        rm "$GAME_DIR/$dll"
        ok "Removed $dll"
    fi
done

echo "$GAME_ID" > "$GAME_DIR/steam_appid.txt"

echo ""
ok "Among Us MetalSharp Wine runtime ready!"
