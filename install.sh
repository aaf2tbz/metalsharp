#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

METALSHARP_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX_DIR="$HOME/.metalsharp/prefix"

info()  { echo -e "${CYAN}[metalsharp]${NC} $*"; }
ok()    { echo -e "${GREEN}[ok]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC} $*"; }
fail()  { echo -e "${RED}[fail]${NC} $*"; exit 1; }

cmd_exists() { command -v "$1" &>/dev/null; }

confirm() {
    local prompt="$1"
    local default="${2:-y}"
    if [[ "$default" == "y" ]]; then
        echo -e "${BOLD}${prompt}${NC} [Y/n] "
    else
        echo -e "${BOLD}${prompt}${NC} [y/N] "
    fi
    read -r resp
    resp="${resp:-$default}"
    [[ "$resp" =~ ^[Yy] ]]
}

step() {
    echo ""
    echo -e "${BOLD}${CYAN}── $1 ──${NC}"
}

# ── Banner ──
echo -e "${BOLD}${CYAN}"
echo "  __  __  ___  ___  _       ___ __  __  ___  ___  ___ _    ___ "
echo " |  \\/  |/ __||_ _|/ \\     / __|  \\/  |/ __|| __)| _ \\ |  | __|"
echo " | |\\/| |\\__ \\ | |/ _ \\   | (__| |\\/| |\\__ \\| _| |   / |__| _| "
echo " |_|  |_||___/___/_/ \\_\\   \\___|_|  |_||___/|___||_|_\\\\____|___|"
echo -e "${NC}"
echo -e "  ${DIM}Direct3D → Metal translation layer. Single-hop, no Vulkan middleman.${NC}"
echo ""

# ── Check macOS ──
if [[ "$(uname)" != "Darwin" ]]; then
    fail "MetalSharp requires macOS with Apple Silicon or Intel Metal support."
fi

ARCH=$(uname -m)
info "Detected: macOS $(sw_vers -productVersion) on $ARCH"

# ── Step 1: Xcode CLI Tools ──
step "Checking Xcode Command Line Tools"
if xcode-select -p &>/dev/null; then
    ok "Xcode CLI tools installed"
else
    if confirm "Xcode CLI tools are required. Install now?"; then
        info "Installing (may prompt for sudo)..."
        xcode-select --install 2>/dev/null || true
        info "After installation completes, re-run this script."
        exit 0
    else
        fail "Cannot continue without Xcode CLI tools"
    fi
fi

# ── Step 2: Homebrew ──
step "Checking Homebrew"
if cmd_exists brew; then
    ok "Homebrew $(brew --version | head -1 | awk '{print $2}')"
else
    if confirm "Homebrew is required. Install now?"; then
        info "Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        eval "$(/opt/homebrew/bin/brew shellenv 2>/dev/null || /usr/local/bin/brew shellenv 2>/dev/null)"
    else
        fail "Cannot continue without Homebrew"
    fi
fi

# ── Step 3: CMake ──
step "Checking CMake"
if cmd_exists cmake; then
    ok "CMake $(cmake --version | head -1 | awk '{print $3}')"
else
    if confirm "CMake is required for building. Install via Homebrew?"; then
        brew install cmake
    else
        fail "Cannot continue without CMake"
    fi
fi

# ── Step 4: Game Porting Toolkit (Wine + D3D→Metal) ──
step "Checking Game Porting Toolkit"
GPTK_WINE="/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
if [[ -x "$GPTK_WINE" ]]; then
    ok "Game Porting Toolkit found"
else
    echo ""
    info "Game Porting Toolkit provides Wine + Apple's D3D→Metal translation."
    info "Required for D3D11/12 games (Rain World, etc.)"
    echo ""
    if confirm "Install Game Porting Toolkit via Homebrew? (free, from Apple/GCenx)" "y"; then
        brew install --cask gcenx/wine/game-porting-toolkit || {
            warn "GPTK install failed. Install manually:"
            echo "  brew install --cask gcenx/wine/game-porting-toolkit"
        }
        if [[ -x "$GPTK_WINE" ]]; then
            ok "Game Porting Toolkit installed"
        else
            warn "GPTK binary not found. May need terminal restart."
            SKIP_GPTK=1
        fi
    else
        info "Skipping GPTK. D3D11/12 games won't run."
        info "Install later: brew install --cask gcenx/wine/game-porting-toolkit"
        SKIP_GPTK=1
    fi
fi

# ── Step 4b: Wine (fallback for non-D3D11 games) ──
step "Checking Wine (fallback)"
if cmd_exists wine || cmd_exists wine64; then
    WINE_VER=$(wine --version 2>/dev/null || wine64 --version 2>/dev/null)
    ok "Wine found: $WINE_VER"
else
    if confirm "Install wine-stable for non-GPTK Wine support?" "n"; then
        brew install --cask wine-stable 2>/dev/null || warn "wine-stable install skipped"
    fi
fi

# ── Step 5: SteamCMD (optional) ──
step "Checking SteamCMD"
if cmd_exists steamcmd || [[ -f "$HOME/steamcmd/steamcmd.sh" ]]; then
    ok "SteamCMD found"
else
    if confirm "Install SteamCMD? (needed for downloading Windows games via Steam)" "n"; then
        mkdir -p "$HOME/steamcmd"
        info "Downloading SteamCMD..."
        curl -sqL "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz" | tar xz -C "$HOME/steamcmd"
        ok "SteamCMD installed to ~/steamcmd/"
    else
        info "Skipping SteamCMD. Install later from:"
        echo "  https://developer.valvesoftware.com/wiki/SteamCMD"
    fi
fi

# ── Step 6: Build MetalSharp ──
step "Building MetalSharp"
cd "$METALSHARP_DIR"

if [[ ! -d build ]]; then
    mkdir build
fi

info "Configuring..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON 2>&1 | tail -3

info "Building..."
cmake --build build 2>&1 | tail -5

ok "Build complete"

# ── Step 7: Run Tests ──
step "Running Tests"
TOTAL_PASS=0
TOTAL_FAIL=0
cd build
for t in test_*; do
    if [[ -x "$t" && -f "$t" ]]; then
        result=$(./"$t" 2>&1)
        if echo "$result" | grep -q "Results:"; then
            count=$(echo "$result" | grep -oE '[0-9]+ passed' | head -1 | grep -oE '^[0-9]+')
            fails=$(echo "$result" | grep -oE '[0-9]+ failed' | head -1 | grep -oE '^[0-9]+')
            TOTAL_PASS=$((TOTAL_PASS + ${count:-0}))
            TOTAL_FAIL=$((TOTAL_FAIL + ${fails:-0}))
        elif echo "$result" | grep -q "PASS\|PASSED"; then
            TOTAL_PASS=$((TOTAL_PASS + 1))
        fi
    fi
done
cd ..

if [[ $TOTAL_FAIL -eq 0 ]]; then
    ok "All tests passed ($TOTAL_PASS checks)"
else
    warn "Some tests failed ($TOTAL_PASS passed, $TOTAL_FAIL failed)"
fi

# ── Step 8: Set up Wine Prefix ──
if [[ -z "${SKIP_WINE:-}" ]]; then
    step "Setting up Wine Prefix"
    info "Creating prefix at $PREFIX_DIR..."
    ./build/metalsharp_launcher --prefix "$PREFIX_DIR" --help &>/dev/null || true
    mkdir -p "$PREFIX_DIR/drive_c/windows/system32"
    mkdir -p "$PREFIX_DIR/drive_c/windows/syswow64"

    info "Copying MetalSharp dylibs into prefix..."
    for dylib in d3d11 d3d12 dxgi xaudio2_9 xinput1_4; do
        if [[ -f "build/$dylib.dylib" ]]; then
            cp "build/$dylib.dylib" "$PREFIX_DIR/drive_c/windows/system32/"
            ok "  $dylib.dylib -> system32/"
        fi
    done
fi

# ── Step 9: Mono ──
step "Checking Mono"
if cmd_exists mono; then
    ok "System mono: $(mono --version | head -1)"
else
    if confirm "Install Mono via Homebrew? (needed for XNA/FNA games)" "y"; then
        brew install mono || warn "Mono install failed"
    fi
fi

# ── Step 10: Rosetta 2 (for x86_64 mono / GPTK) ──
if [[ "$(uname -m)" == "arm64" ]]; then
    step "Checking Rosetta 2"
    if arch -x86_64 /usr/bin/true 2>/dev/null; then
        ok "Rosetta 2 available"
    else
        if confirm "Install Rosetta 2? (needed for x86_64 games like Celeste)" "y"; then
            softwareupdate --install-rosetta --agree-to-license || warn "Rosetta install failed"
        fi
    fi
fi

# ── Step 11: Game-specific deps ──
step "Game Setup"
echo ""
echo "  Supported games and setup commands:"
echo ""
echo "    Celeste (XNA/FNA, native Metal):"
echo "      ./scripts/setup-celeste-deps.sh"
echo "      ./scripts/launch-celeste.sh"
echo ""
echo "    Terraria (XNA/FNA, native Metal):"
echo "      ./scripts/setup-terraria-deps.sh"
echo "      ./scripts/launch-terraria.sh"
echo ""
echo "    Rain World (Unity D3D11, via GPTK):"
echo "      ./scripts/setup-rainworld-deps.sh"
echo "      ./scripts/launch-rainworld.sh"
echo ""

# ── Summary ──
echo ""
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}  MetalSharp is ready!${NC}"
echo ""
echo "  Built:"
echo "    $(ls build/*.dylib build/metalsharp_launcher 2>/dev/null | wc -l | tr -d ' ') targets in build/"
echo "    $TOTAL_PASS test checks passed"
echo ""
if [[ -z "${SKIP_WINE:-}" ]] || [[ -z "${SKIP_GPTK:-}" ]]; then
    echo "  Usage:"
    echo "    ./build/metalsharp_launcher game.exe"
    echo "    ./scripts/launch-celeste.sh     # XNA/FNA game (native Metal)"
    echo "    ./scripts/launch-terraria.sh    # XNA/FNA game (native Metal)"
    echo "    ./scripts/launch-rainworld.sh   # Unity D3D11 game (via GPTK)"
else
    echo "  Next steps:"
    echo "    Install GPTK:  brew install --cask gcenx/wine/game-porting-toolkit"
    echo "    Install Mono:  brew install mono"
    echo "    Then run:      ./scripts/setup-<game>-deps.sh"
fi
echo ""
echo "  Prefix:  $PREFIX_DIR"
echo "  Config:  ~/.metalsharp/metalsharp.toml"
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
