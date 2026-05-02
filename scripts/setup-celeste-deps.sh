#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${CYAN}[celeste-setup]${NC} $*"; }
ok()   { echo -e "${GREEN}[ok]${NC} $*"; }
fail() { echo -e "${RED}[fail]${NC} $*"; exit 1; }

METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
METALSHARP_REPO="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME="$METALSHARP_HOME/runtime"
SHIMS_DIR="$RUNTIME/shims"
GAME_ID="${1:-504230}"
GAME_DIR="$METALSHARP_HOME/games/$GAME_ID"
SHIM_SRC="$METALSHARP_REPO/src/fna/shims"
ALIAS_FILE="$SHIM_SRC/csteamworks_aliases.txt"

mkdir -p "$RUNTIME" "$SHIMS_DIR" "$RUNTIME/mono-x86" "$GAME_DIR"

step() { echo ""; info "── $1 ──"; }

step "1/8: Checking Rosetta 2"
if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
    info "Installing Rosetta 2..."
    softwareupdate --install-rosetta --agree-to-license 2>/dev/null || fail "Rosetta 2 install failed (requires sudo)"
fi
ok "Rosetta 2 available"

step "2/8: Installing x86_64 Mono"
MONO_DIR="$RUNTIME/mono-x86"
if [[ -x "$MONO_DIR/bin/mono" ]]; then
    ok "x86_64 mono already installed"
else
    MONO_TAR="$RUNTIME/mono-x86_64.tar.gz"
    if [[ ! -f "$MONO_TAR" ]]; then
        info "Downloading Mono 6.12.0.206 (x86_64)..."
        curl -L -o "$MONO_TAR" \
            "https://download.mono-project.com/archive/6.12.0.206/macos-10-x86/MonoFramework-MDK-6.12.0.206.macos10.x86.xpkg" \
            || fail "Mono download failed"
    fi

    info "Extracting mono..."
    mkdir -p /tmp/mono-x86-pkg
    pkgutil --expand "$MONO_TAR" /tmp/mono-x86-pkg 2>/dev/null || fail "Mono pkg expansion failed"

    mkdir -p "$MONO_DIR"
    cd /tmp/mono-x86-pkg
    for pkg in *.pkg; do
        if [[ -f "$pkg/Payload" ]]; then
            (cd "$MONO_DIR" && gzip -dc "$PWD/$pkg/Payload" | cpio -id 2>/dev/null) || true
        fi
    done
    rm -rf /tmp/mono-x86-pkg

    [[ -x "$MONO_DIR/bin/mono" ]] || fail "Mono extraction failed"
    ok "x86_64 mono installed"
fi

step "3/8: Fixing mono native library symlink"
NATIVE="$MONO_DIR/lib/libmono-native.dylib"
NATIVE_UNIFIED="$MONO_DIR/lib/libmono-native-unified.dylib"
if [[ -f "$NATIVE" ]] && [[ ! -L "$NATIVE_UNIFIED" ]]; then
    ln -sf "$(basename "$NATIVE")" "$NATIVE_UNIFIED"
    ok "libmono-native-unified symlink created"
else
    ok "mono native symlink OK"
fi

step "4/8: Building SDL3 (x86_64)"
if [[ -f "$SHIMS_DIR/libSDL3.0.dylib" ]] && file "$SHIMS_DIR/libSDL3.0.dylib" | grep -q "x86_64"; then
    ok "SDL3 x86_64 already built"
else
    SDL_BUILD="/tmp/SDL3-x86-build"
    if [[ ! -f "$SDL_BUILD/libSDL3.0.dylib" ]]; then
        if [[ ! -d /tmp/SDL3-src ]]; then
            git clone --branch release-3.2.0 --depth 1 https://github.com/libsdl-org/SDL.git /tmp/SDL3-src 2>/dev/null || true
        fi
        mkdir -p "$SDL_BUILD"
        cmake -S /tmp/SDL3-src -B "$SDL_BUILD" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_BUILD_TYPE=Release \
            -DSDL_TEST=OFF -DSDL_TESTS=OFF 2>/dev/null || fail "SDL3 cmake failed"
        cmake --build "$SDL_BUILD" -j$(sysctl -n hw.ncpu) 2>/dev/null || fail "SDL3 build failed"
    fi
    cp "$SDL_BUILD/libSDL3.0.dylib" "$SHIMS_DIR/"
    cp "$SDL_BUILD/libSDL3.dylib" "$SHIMS_DIR/" 2>/dev/null || true
    ok "SDL3 x86_64 built"
fi

step "5/8: Building FNA3D (x86_64)"
FNA3D_SRC="$METALSHARP_REPO/src/fna/FNA3D"
if [[ -d "$FNA3D_SRC" ]]; then
    if [[ -f "$SHIMS_DIR/libFNA3D.dylib" ]] && file "$SHIMS_DIR/libFNA3D.dylib" | grep -q "x86_64"; then
        ok "FNA3D x86_64 already built"
    else
        FNA3D_BUILD="/tmp/fna3d-x86-build"
        mkdir -p "$FNA3D_BUILD"
        cmake -S "$FNA3D_SRC" -B "$FNA3D_BUILD" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_BUILD_TYPE=Release \
            -DSDL3_DIR="/tmp/SDL3-x86-build" 2>/dev/null || fail "FNA3D cmake failed"
        cmake --build "$FNA3D_BUILD" -j$(sysctl -n hw.ncpu) 2>/dev/null || fail "FNA3D build failed"
        cp "$FNA3D_BUILD"/libFNA3D*.dylib "$SHIMS_DIR/" 2>/dev/null || true
        ok "FNA3D x86_64 built"
    fi
else
    ok "FNA3D source not present (using pre-built if available)"
fi

step "6/8: Copying steam_api.dylib to game dir"
if [[ -f "$GAME_DIR/libsteam_api.dylib" ]]; then
    ok "steam_api.dylib already in game dir"
else
    STEAM_DYLIB=""
    MAC_STEAM="$HOME/Library/Application Support/Steam"
    CANDIDATES=(
        "$MAC_STEAM/Steam.AppBundle/Steam/Contents/MacOS/Frameworks/Steam Helper.app/Contents/MacOS/libsteam_api.dylib"
        "$MAC_STEAM/steamapps/common/Terraria/Terraria.app/Contents/MacOS/osx/libsteam_api.dylib"
    )
    for c in "${CANDIDATES[@]}"; do
        if [[ -f "$c" ]]; then
            STEAM_DYLIB="$c"
            break
        fi
    done
    if [[ -n "$STEAM_DYLIB" ]]; then
        cp "$STEAM_DYLIB" "$GAME_DIR/"
        cp "$STEAM_DYLIB" "$SHIMS_DIR/" 2>/dev/null || true
        ok "steam_api.dylib copied"
    else
        info "steam_api.dylib not found — CSteamworks build may fail"
    fi
fi

step "7/8: Building CSteamworks and FMOD shims"
if [[ -f "$GAME_DIR/libCSteamworks.dylib" ]] && file "$GAME_DIR/libCSteamworks.dylib" | grep -q "x86_64"; then
    ok "CSteamworks already built"
else
    info "Building CSteamworks with 609 aliases (x86_64)..."

    ALIAS_FLAGS=""
    if [[ -f "$ALIAS_FILE" ]]; then
        ALIAS_FLAGS=$(cat "$ALIAS_FILE" | tr '\n' ' ')
    fi

    if [[ -n "$ALIAS_FLAGS" ]]; then
        clang -shared -arch x86_64 \
            -o "$GAME_DIR/libCSteamworks.dylib" \
            "$SHIM_SRC/csteamworks_shim.c" \
            -L"$GAME_DIR" -lsteam_api \
            -install_name @loader_path/libCSteamworks.dylib \
            $ALIAS_FLAGS \
            2>/dev/null && ok "CSteamworks built with aliases" || {
                info "Alias build failed, trying simple build..."
                clang -shared -arch x86_64 \
                    -o "$GAME_DIR/libCSteamworks.dylib" \
                    "$SHIM_SRC/csteamworks_shim.c" \
                    -undefined dynamic_lookup \
                    -install_name @loader_path/libCSteamworks.dylib \
                    2>/dev/null && ok "CSteamworks built (simple)" || fail "CSteamworks build failed"
            }
    else
        clang -shared -arch x86_64 \
            -o "$GAME_DIR/libCSteamworks.dylib" \
            "$SHIM_SRC/csteamworks_shim.c" \
            -undefined dynamic_lookup \
            -install_name @loader_path/libCSteamworks.dylib \
            2>/dev/null && ok "CSteamworks built" || fail "CSteamworks build failed"
    fi
    cp "$GAME_DIR/libCSteamworks.dylib" "$SHIMS_DIR/" 2>/dev/null || true
fi

if [[ -f "$GAME_DIR/libfmod.dylib" ]]; then
    ok "FMOD already present"
else
    clang -shared -arch x86_64 \
        -o "$GAME_DIR/libfmod.dylib" \
        "$SHIM_SRC/fmod_stub.c" \
        -undefined dynamic_lookup \
        -install_name @loader_path/libfmod.dylib \
        2>/dev/null && ok "FMOD stub built" || fail "FMOD stub build failed"

    clang -shared -arch x86_64 \
        -o "$GAME_DIR/libfmodstudio.dylib" \
        "$SHIM_SRC/fmodstudio_stub.c" \
        -undefined dynamic_lookup \
        -install_name @loader_path/libfmodstudio.dylib \
        2>/dev/null && ok "FMOD Studio stub built" || fail "FMOD Studio stub build failed"

    cp "$GAME_DIR/libfmod.dylib" "$SHIMS_DIR/" 2>/dev/null || true
    cp "$GAME_DIR/libfmodstudio.dylib" "$SHIMS_DIR/" 2>/dev/null || true
fi

step "8/8: Writing steam_appid.txt"
echo "$GAME_ID" > "$GAME_DIR/steam_appid.txt"
ok "steam_appid.txt written"

echo ""
ok "Celeste x86_64 runtime ready!"
info "Launch with: $METALSHARP_REPO/scripts/launch-celeste.sh"
