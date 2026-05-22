#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/audit-mscompatdb-hook-surface.sh <wine-root> [output-dir]

Example:
  scripts/audit-mscompatdb-hook-surface.sh \
    /tmp/metalsharp-home-wine119-dxmt32-state/.metalsharp/runtime/wine \
    /tmp/metalsharp-mscompatdb-hook-audit

Audits the Wine runtime mscompatdb hook surface without mutating the runtime.
This is deliberately conservative: exported symbols prove only symbol presence,
not that the hook is loaded or ready inside a protected game process.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

wine_root="${1:-}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${2:-/tmp/metalsharp-mscompatdb-hook-audit-$timestamp}"

if [[ -z "$wine_root" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -d "$wine_root" ]]; then
    echo "wine root not found: $wine_root" >&2
    exit 1
fi

wine_root="$(cd "$wine_root" && pwd -P)"
mkdir -p "$out_dir"

ntdll="$wine_root/lib/wine/x86_64-unix/ntdll.so"
mscompatdb_so="$wine_root/lib/wine/x86_64-unix/mscompatdb.so"
mscompatdb_dylib="$wine_root/lib/wine/x86_64-unix/mscompatdb.dylib"

json_bool() {
    if [[ "$1" == "1" ]]; then
        printf 'true'
    else
        printf 'false'
    fi
}

path_exists() {
    [[ -f "$1" ]] && printf '1' || printf '0'
}

has_symbol() {
    local file="$1"
    local symbol="$2"
    [[ -f "$file" ]] && nm -gU "$file" 2>/dev/null | grep -Fq "$symbol"
}

capture_probe() {
    local rel="$1"
    local file="$wine_root/$rel"
    {
        echo "== $rel =="
        if [[ -f "$file" ]]; then
            file "$file" || true
            echo
            echo "-- nm -gU --"
            nm -gU "$file" 2>&1 || true
            echo
            echo "-- otool -L --"
            otool -L "$file" 2>&1 || true
            echo
            echo "-- otool -l --"
            otool -l "$file" 2>&1 || true
        else
            echo "missing"
        fi
    } >> "$out_dir/probes.txt"
}

: > "$out_dir/probes.txt"
capture_probe "lib/wine/x86_64-unix/ntdll.so"
capture_probe "lib/wine/x86_64-unix/mscompatdb.so"
capture_probe "lib/wine/x86_64-unix/mscompatdb.dylib"

ntdll_exists="$(path_exists "$ntdll")"
mscompatdb_so_exists="$(path_exists "$mscompatdb_so")"
mscompatdb_dylib_exists="$(path_exists "$mscompatdb_dylib")"

contract_symbol=0
contract_version_symbol=0
if has_symbol "$ntdll" "_MetalSharpGetMscompatdbHookContract"; then
    contract_symbol=1
fi
if has_symbol "$ntdll" "_MetalSharpGetMscompatdbHookContractVersion"; then
    contract_version_symbol=1
fi

mscompatdb_so_public_symbols=0
if [[ -f "$mscompatdb_so" ]] && nm -gU "$mscompatdb_so" 2>/dev/null | grep -q ' _'; then
    mscompatdb_so_public_symbols=1
fi

mscompatdb_dylib_public_symbols=0
if [[ -f "$mscompatdb_dylib" ]] && nm -gU "$mscompatdb_dylib" 2>/dev/null | grep -q ' _'; then
    mscompatdb_dylib_public_symbols=1
fi

symbol_surface_ready=0
if [[ "$ntdll_exists" == "1" && "$contract_symbol" == "1" && "$contract_version_symbol" == "1" ]]; then
    symbol_surface_ready=1
fi

runtime_loaded=0
hook_ready=0
status="missing_hook_symbols"
if [[ "$ntdll_exists" != "1" ]]; then
    status="missing_ntdll_unix"
elif [[ "$symbol_surface_ready" != "1" ]]; then
    status="missing_hook_symbols"
elif [[ "$mscompatdb_so_exists" != "1" && "$mscompatdb_dylib_exists" != "1" ]]; then
    status="missing_mscompatdb_runtime"
else
    status="symbol_surface_present_runtime_unproven"
fi

cat > "$out_dir/hook-surface.json" <<EOF
{
  "ok": true,
  "wineRoot": "$wine_root",
  "status": "$status",
  "symbolSurfaceReady": $(json_bool "$symbol_surface_ready"),
  "runtimeLoaded": $(json_bool "$runtime_loaded"),
  "hookReady": $(json_bool "$hook_ready"),
  "files": {
    "ntdllUnix": {"path": "$ntdll", "exists": $(json_bool "$ntdll_exists")},
    "mscompatdbSo": {"path": "$mscompatdb_so", "exists": $(json_bool "$mscompatdb_so_exists")},
    "mscompatdbDylib": {"path": "$mscompatdb_dylib", "exists": $(json_bool "$mscompatdb_dylib_exists")}
  },
  "symbols": {
    "MetalSharpGetMscompatdbHookContract": $(json_bool "$contract_symbol"),
    "MetalSharpGetMscompatdbHookContractVersion": $(json_bool "$contract_version_symbol"),
    "mscompatdbSoPublicSymbols": $(json_bool "$mscompatdb_so_public_symbols"),
    "mscompatdbDylibPublicSymbols": $(json_bool "$mscompatdb_dylib_public_symbols")
  },
  "evidence": {
    "probes": "$out_dir/probes.txt"
  }
}
EOF

{
    echo "# MetalSharp mscompatdb Hook Surface Audit"
    echo
    echo "- wine_root: \`$wine_root\`"
    echo "- status: \`$status\`"
    echo "- symbol_surface_ready: \`$(json_bool "$symbol_surface_ready")\`"
    echo "- runtime_loaded: \`false\`"
    echo "- hook_ready: \`false\`"
    echo
    echo "| Check | Result |"
    echo "| --- | --- |"
    echo "| ntdll.so exists | $(json_bool "$ntdll_exists") |"
    echo "| mscompatdb.so exists | $(json_bool "$mscompatdb_so_exists") |"
    echo "| mscompatdb.dylib exists | $(json_bool "$mscompatdb_dylib_exists") |"
    echo "| _MetalSharpGetMscompatdbHookContract | $(json_bool "$contract_symbol") |"
    echo "| _MetalSharpGetMscompatdbHookContractVersion | $(json_bool "$contract_version_symbol") |"
    echo "| mscompatdb.so public symbols | $(json_bool "$mscompatdb_so_public_symbols") |"
    echo "| mscompatdb.dylib public symbols | $(json_bool "$mscompatdb_dylib_public_symbols") |"
    echo
    echo "Symbols alone do not prove hook readiness. This audit stays false for \`hook_ready\` until a protected runtime launch proves load and behavior."
} > "$out_dir/hook-surface.md"

echo "mscompatdb hook surface audit written: $out_dir"
if [[ "$symbol_surface_ready" != "1" ]]; then
    exit 2
fi
