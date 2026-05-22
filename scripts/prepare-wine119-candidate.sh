#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/prepare-wine119-candidate.sh <metalsharp_bundle.tar.zst> <candidate-runtime-root>

Example:
  scripts/prepare-wine119-candidate.sh \
    /tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst \
    /tmp/metalsharp-wine119-candidate/wine

Creates a disposable Wine 11.9 candidate runtime with the final MetalSharp
runtime shape expected by the app: <candidate-runtime-root>/bin, lib, etc, share.
It does not modify ~/.metalsharp.

The script normalizes the archive root (wine-11.9 -> runtime/wine), carries the
current metalsharp-wine wrapper when the bundle lacks one, binds x86_64
WineMetal into lib/wine fallback paths, localizes Vulkan ICD bindings so proof
does not load MoltenVK from the installed runtime, then writes reports beside
the runtime root. Set METALSHARP_I386_WINEMETAL_SOURCE=/path/to/winemetal.dll to bind an
explicit i386 WineMetal candidate. Set
METALSHARP_BORROW_BASELINE_I386_WINEMETAL=1 to intentionally borrow the 11.5
i386 winemetal.dll for an experiment; the default is to report it missing.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

bundle="${1:-}"
candidate_root="${2:-}"

if [[ -z "$bundle" || -z "$candidate_root" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -f "$bundle" ]]; then
    echo "bundle not found: $bundle" >&2
    exit 1
fi

if ! command -v zstd >/dev/null 2>&1; then
    echo "zstd is required to extract $bundle" >&2
    exit 1
fi

baseline_root="${METALSHARP_BASELINE_WINE:-$HOME/.metalsharp/runtime/wine}"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-wine119.XXXXXX")"
cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

mkdir -p "$(dirname "$candidate_root")"
rm -rf "$candidate_root"

zstd -dc "$bundle" | tar -xf - -C "$tmp_dir"

source_root=""
for candidate in "$tmp_dir/wine-11.9" "$tmp_dir/wine"; do
    if [[ -d "$candidate" ]]; then
        source_root="$candidate"
        break
    fi
done

if [[ -z "$source_root" ]]; then
    echo "bundle did not contain wine-11.9/ or wine/" >&2
    exit 1
fi

mkdir -p "$candidate_root"
if command -v ditto >/dev/null 2>&1; then
    ditto "$source_root" "$candidate_root"
else
    cp -R "$source_root"/. "$candidate_root"/
fi

mkdir -p "$candidate_root/bin" \
    "$candidate_root/lib/wine/x86_64-unix" \
    "$candidate_root/lib/wine/x86_64-windows" \
    "$candidate_root/lib/wine/i386-windows"

if [[ ! -f "$candidate_root/bin/metalsharp-wine" ]]; then
    wrapper_source="${METALSHARP_WINE_WRAPPER:-$baseline_root/bin/metalsharp-wine}"
    if [[ -f "$wrapper_source" ]]; then
        cp "$wrapper_source" "$candidate_root/bin/metalsharp-wine"
        chmod +x "$candidate_root/bin/metalsharp-wine"
    fi
fi

bind_if_missing() {
    local src="$1"
    local dst="$2"
    if [[ ! -f "$dst" && -f "$src" ]]; then
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
    fi
}

bind_if_missing \
    "$candidate_root/lib/dxmt/x86_64-unix/winemetal.so" \
    "$candidate_root/lib/wine/x86_64-unix/winemetal.so"

bind_if_missing \
    "$candidate_root/lib/dxmt/x86_64-windows/winemetal.dll" \
    "$candidate_root/lib/wine/x86_64-windows/winemetal.dll"

localize_vulkan_icd() {
    local molten_src="$candidate_root/lib/wine/x86_64-unix/libMoltenVK.1.dylib"
    local molten_dst="$candidate_root/lib/libMoltenVK.dylib"
    local icd

    if [[ -f "$molten_src" ]]; then
        mkdir -p "$candidate_root/lib"
        if [[ ! -e "$molten_dst" ]]; then
            ln -s "wine/x86_64-unix/libMoltenVK.1.dylib" "$molten_dst"
        fi
    fi

    for icd in "$candidate_root/etc/vulkan/icd.d/"*.json; do
        [[ -f "$icd" ]] || continue
        if grep -Fq '"library_path"' "$icd"; then
            tmp_icd="$icd.tmp.$$"
            sed -E 's#"library_path"[[:space:]]*:[[:space:]]*"[^"]*"#"library_path": "'"$molten_dst"'"#' "$icd" > "$tmp_icd"
            mv "$tmp_icd" "$icd"
        fi
    done
}

localize_vulkan_icd

if [[ -n "${METALSHARP_I386_WINEMETAL_SOURCE:-}" ]]; then
    if [[ ! -f "$METALSHARP_I386_WINEMETAL_SOURCE" ]]; then
        echo "METALSHARP_I386_WINEMETAL_SOURCE not found: $METALSHARP_I386_WINEMETAL_SOURCE" >&2
        exit 1
    fi
    bind_if_missing \
        "$METALSHARP_I386_WINEMETAL_SOURCE" \
        "$candidate_root/lib/wine/i386-windows/winemetal.dll"
elif [[ "${METALSHARP_BORROW_BASELINE_I386_WINEMETAL:-0}" == "1" ]]; then
    bind_if_missing \
        "$baseline_root/lib/wine/i386-windows/winemetal.dll" \
        "$candidate_root/lib/wine/i386-windows/winemetal.dll"
fi

critical=(
    "bin/wine"
    "bin/wineserver"
    "bin/metalsharp-wine"
    "share/wine/wine.inf"
    "etc/dxmt.conf"
    "etc/mscompatdb_rules.toml"
    "etc/vulkan/icd.d/MoltenVK_icd.json"
    "etc/vulkan/icd.d/MoltenVK_x86_64_icd.json"
    "lib/libMoltenVK.dylib"
    "lib/dxmt/x86_64-windows/d3d10core.dll"
    "lib/dxmt/x86_64-windows/d3d11.dll"
    "lib/dxmt/x86_64-windows/d3d12.dll"
    "lib/dxmt/x86_64-windows/dxgi.dll"
    "lib/dxmt/x86_64-windows/winemetal.dll"
    "lib/dxmt/x86_64-unix/winemetal.so"
    "lib/wine/x86_64-unix/winemetal.so"
    "lib/wine/x86_64-unix/mscompatdb.so"
    "lib/wine/x86_64-unix/mscompatdb.dylib"
    "lib/wine/x86_64-unix/libMoltenVK.1.dylib"
    "lib/wine/x86_64-windows/d3d11.dll"
    "lib/wine/x86_64-windows/dxgi.dll"
    "lib/wine/x86_64-windows/winemetal.dll"
    "lib/wine/i386-windows/d3d11.dll"
    "lib/wine/i386-windows/dxgi.dll"
    "lib/wine/i386-windows/winemetal.dll"
)

report_dir="$(dirname "$candidate_root")"
missing_report="$report_dir/missing-critical.txt"
: > "$missing_report"
for rel in "${critical[@]}"; do
    if [[ ! -f "$candidate_root/$rel" ]]; then
        echo "$rel" >> "$missing_report"
    fi
done

{
    echo "bundle=$bundle"
    echo "source_root=$(basename "$source_root")"
    echo "candidate_root=$candidate_root"
    if [[ -x "$candidate_root/bin/wine" ]]; then
        printf "wine_version="
        "$candidate_root/bin/wine" --version 2>&1 || true
    fi
    if [[ -x "$candidate_root/bin/wineserver" ]]; then
        printf "wineserver_version="
        "$candidate_root/bin/wineserver" --version 2>&1 || true
    fi
    echo "i386_winemetal_source=${METALSHARP_I386_WINEMETAL_SOURCE:-}"
    echo "borrowed_baseline_i386_winemetal=${METALSHARP_BORROW_BASELINE_I386_WINEMETAL:-0}"
    echo "missing_count=$(wc -l < "$missing_report" | tr -d ' ')"
} > "$report_dir/prepare-report.txt"

echo "candidate runtime written: $candidate_root"
echo "prepare report: $report_dir/prepare-report.txt"
if [[ -s "$missing_report" ]]; then
    echo "missing critical files:"
    sed 's/^/  - /' "$missing_report"
fi
