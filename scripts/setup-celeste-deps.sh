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
GAMES_DIR="$METALSHARP_HOME/games"
GAME_ID="${1:-504230}"
GAME_DIR="$GAMES_DIR/$GAME_ID"
RUNTIME="$METALSHARP_HOME/runtime"
SHIMS="$METALSHARP_HOME/shims"

mkdir -p "$RUNTIME" "$SHIMS"

if [[ ! -d "$GAME_DIR" ]]; then
    fail "Game directory not found: $GAME_DIR. Download with steamcmd first."
fi

if [[ ! -f "$GAME_DIR/Celeste.exe" ]]; then
    fail "Celeste.exe not found in $GAME_DIR"
fi

step() { echo ""; info "── $1 ──"; }

step "1/6: Checking Rosetta 2"
if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
    info "Installing Rosetta 2..."
    softwareupdate --install-rosetta --agree-to-license || fail "Rosetta 2 install failed"
fi
ok "Rosetta 2 available"

step "2/6: Installing x86_64 Mono"
MONO_DIR="$RUNTIME/mono-x86"
if [[ -x "$MONO_DIR/bin/mono" ]]; then
    ok "x86_64 mono already installed at $MONO_DIR/bin/mono"
else
    info "Downloading Mono 6.12.0.206 (x86_64)..."
    MONO_PKG="/tmp/mono-x86.pkg"
    curl -L -o "$MONO_PKG" "https://download.mono-project.com/archive/6.12.0.206/macos-10-x86/MonoFramework-MDK-6.12.0.206.macos10.x86.xpkg" || fail "Download failed"
    
    info "Extracting mono to $MONO_DIR..."
    mkdir -p /tmp/mono-x86-pkg
    pkgutil --expand "$MONO_PKG" /tmp/mono-x86-pkg
    
    mkdir -p "$MONO_DIR"
    cd /tmp/mono-x86-pkg
    for pkg in *.pkg; do
        if [[ -f "$pkg/Payload" ]]; then
            (cd "$MONO_DIR" && gzip -dc "$PWD/$pkg/Payload" | cpio -id 2>/dev/null) || true
        fi
    done
    
    rm -rf /tmp/mono-x86-pkg "$MONO_PKG"
    
    if [[ ! -x "$MONO_DIR/bin/mono" ]]; then
        find "$MONO_DIR" -name mono -type f
        fail "Mono extraction failed — binary not found"
    fi
    ok "x86_64 mono installed: $($MONO_DIR/bin/mono --version | head -1)"
fi

step "3/6: Building SDL3 (x86_64)"
if [[ -f "$GAME_DIR/libSDL3.0.dylib" ]]; then
    ok "SDL3 already present in game dir"
else
    SDL_BUILD="/tmp/SDL3-x86-build"
    if [[ ! -f "$SDL_BUILD/libSDL3.0.dylib" ]]; then
        info "Cloning SDL3..."
        if [[ ! -d /tmp/SDL3-src ]]; then
            git clone --branch release-3.2.0 --depth 1 https://github.com/libsdl-org/SDL.git /tmp/SDL3-src
        fi
        mkdir -p "$SDL_BUILD"
        cmake -S /tmp/SDL3-src -B "$SDL_BUILD" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_BUILD_TYPE=Release \
            -DSDL_TEST=OFF \
            -DSDL_TESTS=OFF
        cmake --build "$SDL_BUILD" -j$(sysctl -n hw.ncpu)
    fi
    cp "$SDL_BUILD/libSDL3.0.dylib" "$GAME_DIR/"
    cp "$SDL_BUILD/libSDL3.dylib" "$GAME_DIR/" 2>/dev/null || true
    ok "SDL3 x86_64 copied to game dir"
fi

step "4/6: Building FNA3D (x86_64)"
if [[ -f "$GAME_DIR/libFNA3D.dylib" ]]; then
    ok "FNA3D already present in game dir"
else
    FNA3D_SRC="$METALSHARP_REPO/src/fna/FNA3D"
    FNA3D_BUILD="/tmp/fna3d-x86-build"
    if [[ ! -f "$FNA3D_BUILD/libFNA3D.dylib" ]]; then
        mkdir -p "$FNA3D_BUILD"
        cmake -S "$FNA3D_SRC" -B "$FNA3D_BUILD" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_BUILD_TYPE=Release \
            -DSDL3_DIR="/tmp/SDL3-x86-build"
        cmake --build "$FNA3D_BUILD" -j$(sysctl -n hw.ncpu)
    fi
    cp "$FNA3D_BUILD/libFNA3D.0.26.04.dylib" "$GAME_DIR/"
    cp "$FNA3D_BUILD/libFNA3D.0.dylib" "$GAME_DIR/" 2>/dev/null || true
    cp "$FNA3D_BUILD/libFNA3D.dylib" "$GAME_DIR/" 2>/dev/null || true
    ok "FNA3D x86_64 copied to game dir"
fi

step "5/6: Building CSteamworks shim (x86_64)"
if [[ -f "$GAME_DIR/libCSteamworks.dylib" ]]; then
    ok "CSteamworks already present in game dir"
else
    SHIM_SRC="$METALSHARP_REPO/src/fna/shims/csteamworks_shim.c"
    x86_64-w64-mingw32-g++ -shared -o "$GAME_DIR/libCSteamworks.dylib" "$SHIM_SRC" \
        -I"$GAME_DIR" \
        -framework Foundation \
        -arch x86_64 \
        -dynamiclib \
        -undefined dynamic_lookup 2>/dev/null || \
    clang -shared -o "$GAME_DIR/libCSteamworks.dylib" "$SHIM_SRC" \
        -arch x86_64 \
        -dynamiclib \
        -undefined dynamic_lookup \
        -framework Foundation
    ok "CSteamworks shim built"
fi

step "6/6: Setting up FMOD 1.10 (x86_64)"
if [[ -f "$GAME_DIR/libfmod.dylib" ]]; then
    ok "FMOD already present in game dir"
else
    info "FMOD 1.10 requires manual download from fmod.com"
    info "Download the FMOD SoundSystem API 1.10 for macOS"
    info "Extract and copy libfmod.dylib and libfmodstudio.dylib to $GAME_DIR/"
    info ""
    info "Or build stubs for silent fallback:"
    STUB_SRC="$METALSHARP_REPO/src/fna/shims"
    clang -shared -o "$GAME_DIR/libfmod.dylib" "$STUB_SRC/fmod_stub.c" \
        -arch x86_64 -dynamiclib -undefined dynamic_lookup
    clang -shared -o "$GAME_DIR/libfmodstudio.dylib" "$STUB_SRC/fmodstudio_stub.c" \
        -arch x86_64 -dynamiclib -undefined dynamic_lookup
    ok "FMOD stubs built (no audio — get real FMOD 1.10 for audio)"
fi

echo ""
ok "Celeste setup complete!"
info "Launch with: $METALSHARP_REPO/scripts/launch-celeste.sh"
