#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[goldberg-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
GAME_ID="${1:-620}"

GAME_NAME=$(case "$GAME_ID" in
    620)   echo "Portal 2" ;;
    265930) echo "Goat Simulator" ;;
    *)     echo "App $GAME_ID" ;;
esac)

info "Setting up Goldberg emulator for $GAME_NAME (appid $GAME_ID)"

step() { echo ""; info "── $1 ──"; }

step "1/4: Downloading Goldberg emulator"
GOLDBERG_DIR="$METALSHARP_HOME/runtime/goldberg"
GOLDBERG_X86="$GOLDBERG_DIR/x86/steam_api.dll"
GOLDBERG_X64="$GOLDBERG_DIR/x64/steam_api64.dll"

mkdir -p "$(dirname "$GOLDBERG_X86")" "$(dirname "$GOLDBERG_X64")"

if [[ ! -f "$GOLDBERG_X86" || ! -f "$GOLDBERG_X64" ]]; then
    GOLDBERG_URL=$(curl -sL https://api.github.com/repos/Detanup01/gbe_fork/releases/latest | python3 -c "import sys,json; assets=json.load(sys.stdin).get('assets',[]); matching=[a['browser_download_url'] for a in assets if 'win-release.7z' in a.get('name','')]; print(matching[0] if matching else '')" 2>/dev/null || true)
    if [[ -z "$GOLDBERG_URL" ]]; then
        GOLDBERG_URL="https://gitlab.com/Mr_Goldberg/goldberg_emulator/-/jobs/artifacts/master/download?job=win_release"
        info "Falling back to official Goldberg GitLab build..."
    fi
    info "Downloading from $GOLDBERG_URL ..."
    TMPDIR_GOLDBERG=$(mktemp -d)
    if [[ "$GOLDBERG_URL" == *.7z ]]; then
        curl -sL -o "$TMPDIR_GOLDBERG/goldberg.7z" "$GOLDBERG_URL"
        if command -v 7z &>/dev/null; then
            7z x -o"$TMPDIR_GOLDBERG/extracted" -y "$TMPDIR_GOLDBERG/goldberg.7z" > /dev/null 2>&1
        elif command -v bsdtar &>/dev/null; then
            mkdir -p "$TMPDIR_GOLDBERG/extracted"
            bsdtar -xf "$TMPDIR_GOLDBERG/goldberg.7z" -C "$TMPDIR_GOLDBERG/extracted" 2>/dev/null
        else
            fail "Need 7z or bsdtar to extract Goldberg archive. Install with: brew install p7zip"
        fi
        SEARCH_DIR="$TMPDIR_GOLDBERG/extracted"
    else
        curl -sL -o "$TMPDIR_GOLDBERG/goldberg.zip" "$GOLDBERG_URL"
        mkdir -p "$TMPDIR_GOLDBERG/extracted"
        unzip -q -o "$TMPDIR_GOLDBERG/goldberg.zip" -d "$TMPDIR_GOLDBERG/extracted" 2>/dev/null || true
        SEARCH_DIR="$TMPDIR_GOLDBERG/extracted"
    fi

    X86_SRC=$(find "$SEARCH_DIR" -path "*/x32/steam_api.dll" -o -path "*/regular/x32/steam_api.dll" 2>/dev/null | head -1)
    X64_SRC=$(find "$SEARCH_DIR" -path "*/x64/steam_api64.dll" -o -path "*/regular/x64/steam_api64.dll" 2>/dev/null | head -1)

    if [[ -z "$X86_SRC" || -z "$X64_SRC" ]]; then
        X86_SRC=$(find "$SEARCH_DIR" -name "steam_api.dll" -not -name "steam_api64.dll" 2>/dev/null | head -1)
        X64_SRC=$(find "$SEARCH_DIR" -name "steam_api64.dll" 2>/dev/null | head -1)
    fi

    if [[ -z "$X86_SRC" || -z "$X64_SRC" ]]; then
        fail "Could not find Goldberg DLLs in downloaded archive"
    fi

    cp "$X86_SRC" "$GOLDBERG_X86"
    cp "$X64_SRC" "$GOLDBERG_X64"
    rm -rf "$TMPDIR_GOLDBERG"
    ok "Goldberg emulator downloaded and cached"
else
    ok "Goldberg emulator already cached"
fi

step "2/4: Resolving game directory"
GAME_DIR=""

resolve_steam_dir() {
    local manifest_name="appmanifest_${GAME_ID}.acf"
    local search_dirs=()
    if [[ -d "$HOME/Library/Application Support/Steam/steamapps" ]]; then
        search_dirs+=("$HOME/Library/Application Support/Steam/steamapps")
    fi
    if [[ -d "$HOME/.steam/steam/steamapps" ]]; then
        search_dirs+=("$HOME/.steam/steam/steamapps")
    fi
    if [[ -d "$HOME/.local/share/Steam/steamapps" ]]; then
        search_dirs+=("$HOME/.local/share/Steam/steamapps")
    fi
    local wine_steamapps="$METALSHARP_HOME/prefix-steam/drive_c/Program Files (x86)/Steam/steamapps"
    if [[ -d "$wine_steamapps" ]]; then
        search_dirs+=("$wine_steamapps")
    fi
    for steamapps in "${search_dirs[@]}"; do
        local manifest="$steamapps/$manifest_name"
        if [[ -f "$manifest" ]]; then
            local install_dir
            install_dir=$(grep -A1 '"installdir"' "$manifest" | tail -1 | sed 's/.*"\(.*\)".*/\1/')
            if [[ -n "$install_dir" && -d "$steamapps/common/$install_dir" ]]; then
                echo "$steamapps/common/$install_dir"
                return
            fi
        fi
    done
}

STEAM_DIR=$(resolve_steam_dir)
if [[ -n "$STEAM_DIR" ]]; then
    GAME_DIR="$STEAM_DIR"
    ok "Found game at $GAME_DIR"
elif [[ -d "$METALSHARP_HOME/games/$GAME_ID" ]]; then
    GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"
    ok "Using local game dir $GAME_DIR"
else
    GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"
    mkdir -p "$GAME_DIR"
    info "Created game dir at $GAME_DIR (game not found in Steam library)"
fi

step "3/4: Deploying Goldberg emulator to game directory"

deploy_goldberg() {
    local target_dir="$1"
    local appid="$2"

    if [[ ! -d "$target_dir" ]]; then
        mkdir -p "$target_dir"
    fi

    local x86_dst="$target_dir/steam_api.dll"
    local x64_dst_dir="$target_dir"
    local x64_dst="$target_dir/steam_api64.dll"

    if [[ -d "$target_dir/win64" ]]; then
        x64_dst_dir="$target_dir/win64"
        x64_dst="$target_dir/win64/steam_api64.dll"
    fi

    if [[ -f "$x86_dst" && ! -f "${x86_dst}.orig" ]]; then
        mv "$x86_dst" "${x86_dst}.orig"
    fi
    if [[ -f "$x64_dst" && ! -f "${x64_dst}.orig" ]]; then
        mv "$x64_dst" "${x64_dst}.orig"
    fi

    cp "$GOLDBERG_X86" "$x86_dst"
    cp "$GOLDBERG_X64" "$x64_dst"

    local settings_dir="$target_dir/steam_settings"
    mkdir -p "$settings_dir"
    echo "$appid" > "$settings_dir/force_steam_appid.txt"

    ok "Goldberg deployed to $target_dir"
}

case "$GAME_ID" in
    620)
        if [[ -d "$GAME_DIR/bin" ]]; then
            deploy_goldberg "$GAME_DIR/bin" "$GAME_ID"
        else
            deploy_goldberg "$GAME_DIR" "$GAME_ID"
        fi
        ;;
    265930)
        if [[ -d "$GAME_DIR/Binaries/Win32" ]]; then
            deploy_goldberg "$GAME_DIR/Binaries/Win32" "$GAME_ID"
        else
            deploy_goldberg "$GAME_DIR" "$GAME_ID"
        fi
        ;;
    *)
        deploy_goldberg "$GAME_DIR" "$GAME_ID"
        ;;
esac

step "4/4: Initializing Wine prefix (if needed)"
PREFIX="$METALSHARP_HOME/prefix-steam"
MS_WINE="$METALSHARP_HOME/runtime/wine/bin/metalsharp-wine"
WINE_DEVEL="/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"

if [[ ! -d "$PREFIX/drive_c/windows/system32" ]]; then
    if [[ -x "$MS_WINE" ]]; then
        WINEPREFIX="$PREFIX" "$MS_WINE" wineboot --init 2>/dev/null
        ok "Wine prefix initialized with MetalSharp Wine"
    elif [[ -x "$WINE_DEVEL" ]]; then
        WINEPREFIX="$PREFIX" "$WINE_DEVEL" wineboot --init 2>/dev/null
        ok "Wine prefix initialized with Wine Devel"
    else
        info "No Wine found — prefix will be created on first launch"
    fi
else
    ok "Wine prefix exists"
fi

echo ""
ok "$GAME_NAME (appid $GAME_ID) ready to launch with Goldberg emulator"
