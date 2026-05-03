#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[portal2-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
GAME_ID="${1:-620}"
GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"

mkdir -p "$GAME_DIR"

step() { echo ""; info "── $1 ──"; }

step "1/4: Checking Wine Devel"
WINE="/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"
if [[ ! -x "$WINE" ]]; then
    fail "Wine Devel not found — install with: brew install --cask wine-crossover"
fi
ok "Wine Devel found"

step "2/4: Initializing Wine prefix"
PREFIX="$METALSHARP_HOME/prefix-$GAME_ID"
if [[ -d "$PREFIX/drive_c/windows/system32" ]]; then
    ok "Prefix already exists at $PREFIX"
else
    mkdir -p "$PREFIX"
    WINEPREFIX="$PREFIX" "$WINE" wineboot --init
    ok "Prefix created at $PREFIX"
fi

step "3/4: Installing Goldberg emulator"
GOLDBERG_X86="$METALSHARP_HOME/runtime/goldberg/x86/steam_api.dll"
GOLDBERG_X64="$METALSHARP_HOME/runtime/goldberg/x64/steam_api64.dll"

mkdir -p "$(dirname "$GOLDBERG_X86")" "$(dirname "$GOLDBERG_X64")"

if [[ ! -f "$GOLDBERG_X86" ]]; then
    GOLDBERG_URL=$(curl -sL https://api.github.com/repos/Detanup01/gbe_fork/releases/latest | python3 -c "import sys,json; [print(a['browser_download_url']) for a in json.load(sys.stdin)['assets'] if 'win-release.7z' in a['name']]" 2>/dev/null)
    if [[ -z "$GOLDBERG_URL" ]]; then
        fail "Could not find Goldberg release URL"
    fi
    info "Downloading Goldberg emulator..."
    curl -sL -o /tmp/goldberg.7z "$GOLDBERG_URL"
    7z x -o/tmp/goldberg -y /tmp/goldberg.7z > /dev/null 2>&1
    cp /tmp/goldberg/release/regular/x32/steam_api.dll "$GOLDBERG_X86"
    cp /tmp/goldberg/release/regular/x64/steam_api64.dll "$GOLDBERG_X64"
    ok "Goldberg downloaded"
else
    ok "Goldberg already cached"
fi

if [[ -f "$GAME_DIR/bin/steam_api.dll" && ! -f "$GAME_DIR/bin/steam_api.dll.orig" ]]; then
    cp "$GAME_DIR/bin/steam_api.dll" "$GAME_DIR/bin/steam_api.dll.orig"
fi
if [[ -f "$GAME_DIR/bin/win64/steam_api64.dll" && ! -f "$GAME_DIR/bin/win64/steam_api64.dll.orig" ]]; then
    cp "$GAME_DIR/bin/win64/steam_api64.dll" "$GAME_DIR/bin/win64/steam_api64.dll.orig"
fi

cp "$GOLDBERG_X86" "$GAME_DIR/bin/steam_api.dll"
cp "$GOLDBERG_X64" "$GAME_DIR/bin/win64/steam_api64.dll"
mkdir -p "$GAME_DIR/bin/steam_settings" "$GAME_DIR/steam_settings"
echo "$GAME_ID" > "$GAME_DIR/bin/steam_settings/force_steam_appid.txt"
echo "$GAME_ID" > "$GAME_DIR/steam_settings/force_steam_appid.txt"
ok "Goldberg installed for app ID $GAME_ID"

step "4/4: Done"
ok "Portal 2 ready to launch"
