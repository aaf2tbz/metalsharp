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
METALSHARP_REPO="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME="$METALSHARP_HOME/runtime"
DXVK_DIR="$RUNTIME/dxvk-moltenvk"
GAME_ID="${1:-535520}"
GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"

mkdir -p "$RUNTIME" "$DXVK_DIR" "$GAME_DIR"

step() { echo ""; info "── $1 ──"; }

step "1/6: Checking dependencies"
command -v i686-w64-mingw32-gcc >/dev/null 2>&1 || {
    info "Installing MinGW cross-compiler..."
    brew install mingw-w64 2>/dev/null || fail "MinGW install failed"
}
ok "MinGW cross-compiler available"

command -v meson >/dev/null 2>&1 || {
    info "Installing meson..."
    brew install meson 2>/dev/null || fail "meson install failed"
}
ok "meson available"

WINE="/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"
if [[ ! -x "$WINE" ]]; then
    fail "Wine (devel) not found at $WINE — install with: brew install --cask wine-stable"
fi
ok "Wine (devel) found"

MOLTENVK_ICD="/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
if [[ ! -f "$MOLTENVK_ICD" ]]; then
    info "Installing MoltenVK..."
    brew install molten-vk 2>/dev/null || fail "MoltenVK install failed"
fi
ok "MoltenVK found"

step "2/6: Checking for prebuilt DXVK DLLs"
if [[ -f "$DXVK_DIR/d3d11.dll" ]] && [[ -f "$DXVK_DIR/dxgi.dll" ]]; then
    ok "Prebuilt DXVK DLLs already present"
else
    step "3/6: Building DXVK from source (cross-compile for 32-bit Windows)"
    DXVK_SRC="$METALSHARP_REPO/scripts/dxvk"
    PATCHED_SRC="$DXVK_SRC/device_info.cpp.patched"
    CROSS_FILE="$DXVK_SRC/build-x32-custom.txt"

    if [[ ! -f "$PATCHED_SRC" ]]; then
        fail "Patched DXVK source not found at $PATCHED_SRC"
    fi

    BUILD_DIR="/tmp/dxvk-build-nidhogg2"
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
    fi

    DXVK_VERSION="v2.7.1"
    info "Cloning DXVK $DXVK_VERSION..."
    git clone --branch "$DXVK_VERSION" --depth 1 https://github.com/doitsujin/dxvk.git "$BUILD_DIR" 2>/dev/null || fail "DXVK clone failed"

    info "Applying MoltenVK compatibility patches..."
    cp "$PATCHED_SRC" "$BUILD_DIR/src/dxvk/dxvk_device_info.cpp"

    PRESENTER_PATCH="$DXVK_SRC/dxvk_presenter.cpp.patch"
    if [[ -f "$PRESENTER_PATCH" ]]; then
        cd "$BUILD_DIR"
        patch -p1 < "$PRESENTER_PATCH" 2>/dev/null || fail "Presenter patch failed"
        ok "Applied SUBOPTIMAL_KHR fix"
    fi

    mkdir -p "$BUILD_DIR/build-x32"
    cp "$CROSS_FILE" "$BUILD_DIR/build-x32-custom.txt"

    info "Configuring meson (32-bit cross-compile)..."
    cd "$BUILD_DIR"
    meson setup build-x32 \
        --cross-file build-x32-custom.txt \
        --buildtype debugoptimized \
        --strip \
        -Denable_tests=false \
        2>/dev/null || fail "meson setup failed"

    info "Building DXVK..."
    ninja -C build-x32 -j$(sysctl -n hw.ncpu) 2>/dev/null || fail "DXVK build failed"

    cp "$BUILD_DIR/build-x32/src/d3d11/d3d11.dll" "$DXVK_DIR/"
    cp "$BUILD_DIR/build-x32/src/dxgi/dxgi.dll" "$DXVK_DIR/"
    ok "DXVK built and copied to $DXVK_DIR"
fi

step "4/6: Copying DXVK DLLs to game dir"
for dll in d3d11.dll dxgi.dll; do
    if [[ -f "$DXVK_DIR/$dll" ]]; then
        cp "$DXVK_DIR/$dll" "$GAME_DIR/"
        ok "Copied $dll"
    else
        fail "$dll not found in $DXVK_DIR"
    fi
done

step "5/6: Initializing Wine prefix"
PREFIX="$METALSHARP_HOME/prefix-$GAME_ID"
if [[ -d "$PREFIX/drive_c/windows/system32" ]]; then
    ok "Wine prefix already initialized"
else
    info "Creating Wine prefix at $PREFIX..."
    WINEPREFIX="$PREFIX" "$WINE" wineboot --init 2>/dev/null || fail "Wine prefix init failed"

    info "Configuring Wine registry..."
    WINEPREFIX="$PREFIX" "$WINE" reg add "HKCU\\Software\\Wine\\X11 Driver" /v Managed /d N /f >/dev/null 2>&1
    WINEPREFIX="$PREFIX" "$WINE" reg add "HKCU\\Software\\Wine\\DllOverrides" /v xinput1_3 /d "" /f >/dev/null 2>&1
    ok "Wine prefix created with DXVK optimizations"
fi

step "6/6: Writing steam_appid.txt"
echo "$GAME_ID" > "$GAME_DIR/steam_appid.txt"
ok "steam_appid.txt written"

echo ""
ok "Nidhogg 2 DXVK runtime ready!"
info "Launch with: Wine + DXVK + MoltenVK (appid $GAME_ID)"
