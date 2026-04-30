#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()  { echo -e "${CYAN}[check]${NC} $*"; }
ok()    { echo -e "${GREEN}[ok]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC} $*"; }
fail()  { echo -e "${RED}[fail]${NC} $*"; exit 1; }

ERRORS=0
WARNINGS=0

check_cmd() {
    local name="$1"
    local required="${2:-true}"
    if command -v "$name" &>/dev/null; then
        ok "$name found"
        return 0
    else
        if [[ "$required" == "true" ]]; then
            fail "$name not found (required)"
            ERRORS=$((ERRORS + 1))
            return 1
        else
            warn "$name not found (optional)"
            WARNINGS=$((WARNINGS + 1))
            return 0
        fi
    fi
}

echo -e "${BOLD}${CYAN}MetalSharp Dependency Check${NC}"
echo ""

info "Platform: $(uname -s) $(uname -r) ($(uname -m))"
echo ""

info "=== Required Dependencies ==="

check_cmd cmake true
check_cmd clang++ true || check_cmd g++ true

if xcode-select -p &>/dev/null; then
    ok "Xcode CLI tools: $(xcode-select -p)"
else
    fail "Xcode CLI tools not installed. Run: xcode-select --install"
    ERRORS=$((ERRORS + 1))
fi

echo ""
info "=== Optional Dependencies ==="

check_cmd wine false || check_cmd wine64 false
check_cmd steamcmd false || {
    if [[ -f "$HOME/steamcmd/steamcmd.sh" ]]; then
        ok "steamcmd found at ~/steamcmd/"
    fi
}

if [[ -f "/usr/local/lib/libmetalirconverter.dylib" ]]; then
    ok "libmetalirconverter found"
else
    warn "libmetalirconverter not found. Apple Metal Shader Converter recommended."
    WARNINGS=$((WARNINGS + 1))
    echo "  Install from: https://developer.apple.com/metal/shader-converter/"
fi

if [[ -d "/usr/local/include/metal_irconverter" ]]; then
    ok "metal_irconverter headers found"
else
    warn "metal_irconverter headers not found"
    WARNINGS=$((WARNINGS + 1))
fi

echo ""
info "=== Metal Support ==="

if system_profiler SPDisplaysDataType 2>/dev/null | grep -q "Metal"; then
    METAL_VER=$(system_profiler SPDisplaysDataType 2>/dev/null | grep "Metal" | head -1 | sed 's/.*Metal //')
    ok "Metal ${METAL_VER:-} supported"
else
    warn "Could not verify Metal support"
    WARNINGS=$((WARNINGS + 1))
fi

echo ""
info "=== MetalSharp Installation ==="

if [[ -d "$HOME/.metalsharp" ]]; then
    ok "~/.metalsharp/ directory exists"
else
    warn "~/.metalsharp/ not found — run install.sh first"
    WARNINGS=$((WARNINGS + 1))
fi

if [[ -f "$HOME/.metalsharp/settings.json" ]]; then
    ok "Settings file exists"
else
    warn "No settings file — will use defaults"
fi

echo ""
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
if [[ $ERRORS -eq 0 ]]; then
    echo -e "${GREEN}All required dependencies satisfied${NC}"
else
    echo -e "${RED}$ERRORS error(s) found${NC}"
fi
if [[ $WARNINGS -gt 0 ]]; then
    echo -e "${YELLOW}$WARNINGS warning(s) — some features may not work${NC}"
fi
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
