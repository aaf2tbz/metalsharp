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

# ── Step 4: Wine ──
step "Checking Wine"
if cmd_exists wine || cmd_exists wine64; then
    WINE_VER=$(wine --version 2>/dev/null || wine64 --version 2>/dev/null)
    ok "Wine found: $WINE_VER"
else
    echo ""
    info "Wine is required to run Windows executables."
    echo "  Options:"
    echo "    1) wine-stable via Homebrew (free, needs sudo for GStreamer dep)"
    echo "    2) CrossOver from crossover.com (paid, best compat, no sudo)"
    echo "    3) Skip Wine (build MetalSharp only, test later)"
    echo ""
    if confirm "Install wine-stable via Homebrew now? (requires sudo for GStreamer)" "y"; then
        info "Installing wine-stable..."
        info "This may prompt for your password (sudo) to install GStreamer."
        brew install --cask wine-stable || {
            warn "wine-stable install failed. You may need to run:"
            echo "  brew install --cask wine-stable"
            echo "  (requires sudo for GStreamer runtime)"
            echo ""
            if confirm "Continue without Wine? (MetalSharp will build but can't run .exe yet)" "y"; then
                SKIP_WINE=1
            else
                fail "Install Wine and re-run this script"
            fi
        }
        if [[ -z "${SKIP_WINE:-}" ]]; then
            if cmd_exists wine || cmd_exists wine64; then
                ok "Wine installed"
            else
                warn "Wine binary not found in PATH yet. You may need to restart your terminal."
                SKIP_WINE=1
            fi
        fi
    else
        info "Skipping Wine. MetalSharp will build but won't be able to run .exe files."
        info "Install Wine later with: brew install --cask wine-stable"
        SKIP_WINE=1
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

# ── Summary ──
echo ""
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}  MetalSharp is ready!${NC}"
echo ""
echo "  Built:"
echo "    $(ls build/*.dylib build/metalsharp_launcher 2>/dev/null | wc -l | tr -d ' ') targets in build/"
echo "    $TOTAL_PASS test checks passed"
echo ""
if [[ -z "${SKIP_WINE:-}" ]]; then
    echo "  Usage:"
    echo "    ./build/metalsharp_launcher game.exe"
    echo "    ./build/metalsharp_launcher --steam 730"
    echo "    ./build/metalsharp_launcher --list-games"
    echo "    ./build/metalsharp_launcher --help"
else
    echo "  Next steps:"
    echo "    Install Wine:  brew install --cask wine-stable"
    echo "    Then run:      ./build/metalsharp_launcher game.exe"
fi
echo ""
echo "  Prefix:  $PREFIX_DIR"
echo "  Config:  ~/.metalsharp/metalsharp.toml"
echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
