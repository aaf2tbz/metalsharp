#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/runtime-manifest.sh [runtime-root] [output-dir]

Examples:
  scripts/runtime-manifest.sh ~/.metalsharp/runtime/wine /tmp/metalsharp-runtime-11.5
  scripts/runtime-manifest.sh /tmp/wine-11.9-candidate/runtime/wine /tmp/metalsharp-runtime-11.9

Captures a reproducible Wine/DXMT runtime manifest for MetalSharp route parity:
versions, normalized file list, route-critical hashes, file(1), otool -L,
Mach-O load commands/rpaths, critical-path type/linkage probes, and nm -gU hook
probes. The script is read-only against the runtime root.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

runtime_root="${1:-${METALSHARP_HOME:-$HOME/.metalsharp}/runtime/wine}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${2:-/tmp/metalsharp-runtime-manifest-$timestamp}"

if [[ ! -d "$runtime_root" ]]; then
    echo "runtime root not found: $runtime_root" >&2
    exit 1
fi

mkdir -p "$out_dir"

real_root="$(cd "$runtime_root" && pwd -P)"

critical_paths=(
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
    "lib/dxmt/i386-windows/d3d11.dll"
    "lib/dxmt/i386-windows/dxgi.dll"
    "lib/dxmt/i386-windows/winemetal.dll"
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
    "lib/wine/x86_64-unix/ntdll.so"
    "lib/wine/x86_64-unix/win32u.so"
)

probe_paths=(
    "bin/wine"
    "bin/wineserver"
    "bin/metalsharp-wine"
    "lib/dxmt/x86_64-unix/winemetal.so"
    "lib/wine/x86_64-unix/winemetal.so"
    "lib/wine/x86_64-unix/mscompatdb.so"
    "lib/wine/x86_64-unix/mscompatdb.dylib"
    "lib/libMoltenVK.dylib"
    "lib/wine/x86_64-unix/libMoltenVK.1.dylib"
    "lib/wine/x86_64-unix/ntdll.so"
    "lib/wine/x86_64-unix/win32u.so"
)

{
    echo "runtime_root=$real_root"
    echo "captured_at_utc=$timestamp"
    echo "host=$(uname -a)"
    echo
    if [[ -x "$real_root/bin/wine" ]]; then
        printf "wine_version="
        "$real_root/bin/wine" --version 2>&1 || true
    else
        echo "wine_version=missing bin/wine"
    fi
    if [[ -x "$real_root/bin/wineserver" ]]; then
        printf "wineserver_version="
        "$real_root/bin/wineserver" --version 2>&1 || true
    else
        echo "wineserver_version=missing bin/wineserver"
    fi
} > "$out_dir/versions.txt"

find "$real_root" -type f | sed "s#^$real_root/##" | LC_ALL=C sort > "$out_dir/files.txt"
find "$real_root" -type f | wc -l | tr -d ' ' > "$out_dir/file-count.txt"

{
    for rel in "${critical_paths[@]}"; do
        abs="$real_root/$rel"
        if [[ -f "$abs" ]]; then
            shasum -a 256 "$abs" | sed "s#  $real_root/#  #"
        else
            echo "MISSING  $rel"
        fi
    done
} > "$out_dir/critical-sha256.txt"

{
    for rel in "${probe_paths[@]}"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -e "$abs" ]]; then
            file "$abs" || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/file.txt"

{
    for rel in "${critical_paths[@]}"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -e "$abs" ]]; then
            file "$abs" || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/critical-file.txt"

{
    for rel in "${probe_paths[@]}"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -f "$abs" ]]; then
            otool -L "$abs" 2>&1 || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/otool-L.txt"

{
    for rel in "${critical_paths[@]}"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -f "$abs" ]]; then
            otool -L "$abs" 2>&1 || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/critical-otool-L.txt"

{
    for rel in "${probe_paths[@]}"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -f "$abs" ]]; then
            otool -l "$abs" 2>&1 || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/otool-l.txt"

{
    for rel in "${critical_paths[@]}"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -f "$abs" ]]; then
            otool -l "$abs" 2>/dev/null |
                awk '
                    /cmd LC_ID_DYLIB/ { want=1; next }
                    /cmd LC_LOAD_DYLIB/ { want=1; next }
                    /cmd LC_LOAD_WEAK_DYLIB/ { want=1; next }
                    /cmd LC_RPATH/ { want=1; next }
                    want && /^[[:space:]]+(name|path) / {
                        sub(/^[[:space:]]+/, "")
                        print
                        want=0
                    }
                    /^[[:space:]]*cmd / && !/LC_(ID_DYLIB|LOAD_DYLIB|LOAD_WEAK_DYLIB|RPATH)/ {
                        want=0
                    }
                ' || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/critical-linkage-summary.txt"

{
    wrapper="$real_root/bin/metalsharp-wine"
    echo "path=$wrapper"
    if [[ -e "$wrapper" ]]; then
        stat -f 'mode=%Sp' "$wrapper" 2>/dev/null || stat -c 'mode=%A' "$wrapper" 2>/dev/null || true
        shasum -a 256 "$wrapper" | sed "s#  $real_root/#  #"
        echo "first_line=$(sed -n '1p' "$wrapper")"
        echo "exports:"
        rg -n '^export ' "$wrapper" || true
        echo "version_probe:"
        "$wrapper" --version 2>&1 || true
    else
        echo "missing"
    fi
} > "$out_dir/metalsharp-wine-wrapper.txt"

{
    for rel in "${critical_paths[@]}"; do
        case "$rel" in
            *-windows/*.dll)
                abs="$real_root/$rel"
                echo "== $rel =="
                if [[ -f "$abs" ]]; then
                    objdump -p "$abs" 2>&1 | awk '
                        /DLL Name:/ { print }
                        /^The Export Tables / { in_exports=1; print; next }
                        /^The Import Tables / { in_imports=1; print; next }
                        in_exports && /Ordinal Base|Number in:|Name Pointer|Export Address Table|Export Flags|Time\/Date|Major\/Minor/ { print }
                        in_imports && /DLL Name:/ { print }
                    ' || true
                else
                    echo "missing"
                fi
                ;;
        esac
    done
} > "$out_dir/critical-pe-objdump.txt"

{
    for rel in \
        "lib/wine/x86_64-unix/ntdll.so" \
        "lib/wine/x86_64-unix/mscompatdb.so" \
        "lib/wine/x86_64-unix/mscompatdb.dylib"; do
        abs="$real_root/$rel"
        echo "== $rel =="
        if [[ -f "$abs" ]]; then
            nm -gU "$abs" 2>&1 || true
        else
            echo "missing"
        fi
    done
} > "$out_dir/nm-gU.txt"

{
    echo "{"
    printf '  "runtime_root": "%s",\n' "$real_root"
    printf '  "captured_at_utc": "%s",\n' "$timestamp"
    printf '  "file_count": %s,\n' "$(cat "$out_dir/file-count.txt")"
    echo '  "artifacts": {'
    echo '    "versions": "versions.txt",'
    echo '    "files": "files.txt",'
    echo '    "critical_sha256": "critical-sha256.txt",'
    echo '    "file": "file.txt",'
    echo '    "critical_file": "critical-file.txt",'
    echo '    "otool_L": "otool-L.txt",'
    echo '    "critical_otool_L": "critical-otool-L.txt",'
    echo '    "otool_l": "otool-l.txt",'
    echo '    "critical_linkage_summary": "critical-linkage-summary.txt",'
    echo '    "metalsharp_wine_wrapper": "metalsharp-wine-wrapper.txt",'
    echo '    "critical_pe_objdump": "critical-pe-objdump.txt",'
    echo '    "nm_gU": "nm-gU.txt"'
    echo "  }"
    echo "}"
} > "$out_dir/manifest.json"

echo "runtime manifest written: $out_dir"
