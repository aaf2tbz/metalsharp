#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[nidhogg2-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
RUNTIME="$METALSHARP_HOME/runtime"
DXVK_DIR="$RUNTIME/dxvk-1.10.3"
GAME_ID="${1:-535520}"
STEAM_PREFIX="$METALSHARP_HOME/prefix-steam"

mkdir -p "$RUNTIME"

step() { echo ""; info "── $1 ──"; }

step "1/4: Checking MetalSharp Wine"
MS_WINE="$RUNTIME/wine/bin/metalsharp-wine"
if [[ ! -x "$MS_WINE" ]]; then
    fail "MetalSharp Wine not found — run setup to install the runtime"
fi
ok "MetalSharp Wine found"

step "2/4: Downloading DXVK 1.10.3 (Vulkan 1.1 compatible)"
if [[ -f "$DXVK_DIR/d3d11.dll" ]] && [[ -f "$DXVK_DIR/dxgi.dll" ]]; then
    ok "DXVK 1.10.3 DLLs already present"
else
    mkdir -p "$DXVK_DIR"
    DXVK_TAR="/tmp/dxvk-1.10.3.tar.gz"
    info "Downloading DXVK 1.10.3..."
    curl -sL -o "$DXVK_TAR" "https://github.com/doitsujin/dxvk/releases/download/v1.10.3/dxvk-1.10.3.tar.gz" || fail "DXVK download failed"
    tar xzf "$DXVK_TAR" -C /tmp/ || fail "DXVK extract failed"
    cp /tmp/dxvk-1.10.3/x32/d3d11.dll "$DXVK_DIR/"
    cp /tmp/dxvk-1.10.3/x32/dxgi.dll "$DXVK_DIR/"
    rm -f "$DXVK_TAR"
    rm -rf /tmp/dxvk-1.10.3
    ok "DXVK 1.10.3 downloaded and saved to $DXVK_DIR"
fi

step "3/4: Copying DXVK DLLs to game dir"
GAME_DIR=$(find "$STEAM_PREFIX/drive_c/Program Files (x86)/Steam/steamapps/common" -maxdepth 1 -name "Nidhogg 2" 2>/dev/null || true)
if [[ -z "$GAME_DIR" ]]; then
    GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"
    mkdir -p "$GAME_DIR"
fi

for dll in d3d11.dll dxgi.dll; do
    if [[ -f "$DXVK_DIR/$dll" ]]; then
        cp "$DXVK_DIR/$dll" "$GAME_DIR/"
        ok "Copied $dll"
    else
        fail "$dll not found in $DXVK_DIR"
    fi
done

step "4/4: Writing steam_appid.txt"
echo "$GAME_ID" > "$GAME_DIR/steam_appid.txt"
ok "steam_appid.txt written"

echo ""
ok "Nidhogg 2 MetalSharp Wine + DXVK runtime ready!"
info "Launch with: MetalSharp Wine + DXVK 1.10.3 (appid $GAME_ID)"
