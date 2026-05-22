#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/preflight-i386-winemetal-build.sh <dxmt-source-root> <wine-11.9-runtime-root> [output-dir]

Example:
  scripts/preflight-i386-winemetal-build.sh \
    /Volumes/AverySSD/metalsharp/dxmt-src \
    /tmp/metalsharp-wine119-parity/candidates/dxmt32/wine \
    /tmp/metalsharp-i386-winemetal-build-preflight

Checks whether this machine has the inputs required to build a release-grade
i386 winemetal.dll from DXMT source against the shaped Wine 11.9 runtime. This
script is read-only. It does not run Meson setup, build, or mutate the DXMT tree.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

src_root="${1:-}"
wine_root="${2:-}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${3:-/tmp/metalsharp-i386-winemetal-build-preflight-$timestamp}"

if [[ -z "$src_root" || -z "$wine_root" ]]; then
    usage >&2
    exit 1
fi

mkdir -p "$out_dir"
report="$out_dir/preflight.md"
failures=0
warnings=0

append() {
    printf '%s\n' "$*" >> "$report"
}

pass() {
    append "- PASS [$1] $2"
}

fail() {
    failures=$((failures + 1))
    append "- FAIL [$1] $2"
}

warn() {
    warnings=$((warnings + 1))
    append "- WARN [$1] $2"
}

check_tool() {
    local tool="$1"
    if command -v "$tool" >/dev/null 2>&1; then
        pass "tool-$tool" "$(command -v "$tool")"
    else
        fail "tool-$tool" "$tool is not on PATH"
    fi
}

check_file() {
    local label="$1"
    local path="$2"
    if [[ -f "$path" ]]; then
        pass "$label" "$path exists; sha256=$(shasum -a 256 "$path" | awk '{ print $1 }')"
    else
        fail "$label" "missing $path"
    fi
}

check_executable() {
    local label="$1"
    local path="$2"
    if [[ -x "$path" ]]; then
        pass "$label" "$path is executable; sha256=$(shasum -a 256 "$path" | awk '{ print $1 }')"
    else
        fail "$label" "missing executable $path"
    fi
}

: > "$report"
append "# i386 WineMetal 11.9 Build Preflight"
append
append "- captured_at_utc: \`$timestamp\`"
append "- dxmt_source_root: \`$src_root\`"
append "- wine_119_runtime_root: \`$wine_root\`"
append

if ! git -C "$src_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    fail "source" "$src_root is not a git checkout"
else
    src_abs="$(cd "$src_root" && pwd -P)"
    append "- source_abs: \`$src_abs\`"
    append "- source_head: \`$(git -C "$src_abs" rev-parse HEAD)\`"
    append "- source_branch: \`$(git -C "$src_abs" branch --show-current 2>/dev/null || true)\`"
    git -C "$src_abs" status --short --branch > "$out_dir/git-status.txt"
    if [[ -n "$(git -C "$src_abs" status --porcelain)" ]]; then
        fail "source-clean" "DXMT source has uncommitted changes; see $out_dir/git-status.txt"
    else
        pass "source-clean" "DXMT source tree is clean"
    fi
    if [[ -f "$src_abs/build-win32.txt" ]]; then
        pass "cross-file" "$src_abs/build-win32.txt exists"
    else
        fail "cross-file" "missing $src_abs/build-win32.txt"
    fi
fi

if [[ ! -d "$wine_root" ]]; then
    fail "wine-root" "$wine_root does not exist"
else
    wine_abs="$(cd "$wine_root" && pwd -P)"
    append "- wine_abs: \`$wine_abs\`"
    if [[ -x "$wine_abs/bin/wine" ]]; then
        version="$("$wine_abs/bin/wine" --version 2>&1 || true)"
        if [[ "$version" == "wine-11.9" ]]; then
            pass "wine-version" "wine reports wine-11.9"
        else
            fail "wine-version" "wine reports '$version', expected wine-11.9"
        fi
    else
        fail "wine-version" "missing executable $wine_abs/bin/wine"
    fi
    check_executable "winebuild" "$wine_abs/bin/winebuild"
    check_file "libwinecrt0-i386" "$wine_abs/lib/wine/i386-windows/libwinecrt0.a"
    check_file "libntdll-i386" "$wine_abs/lib/wine/i386-windows/libntdll.a"
    check_file "dbghelp-i386" "$wine_abs/lib/wine/i386-windows/dbghelp.dll"
fi

check_tool "meson"
check_tool "ninja"
check_tool "i686-w64-mingw32-gcc"
check_tool "i686-w64-mingw32-g++"
check_tool "i686-w64-mingw32-ar"
check_tool "i686-w64-mingw32-strip"
check_tool "i686-w64-mingw32-windres"

if [[ -d "$src_root" && -d "$wine_root" ]]; then
    build_dir="$out_dir/build32-wine119"
    append
    append "## Proposed Build Commands"
    append
    append '```bash'
    append "meson setup '$build_dir' '$src_root' \\"
    append "  --cross-file '$src_root/build-win32.txt' \\"
    append "  --buildtype release \\"
    append "  -Dwine_install_path='$wine_root' \\"
    append "  -Dwine_builtin_dll=true \\"
    append "  -Denable_tests=false"
    append "ninja -C '$build_dir' src/winemetal/winemetal.dll"
    append '```'
fi

append
append "## Summary"
append
append "- failures: $failures"
append "- warnings: $warnings"
if [[ "$failures" -eq 0 ]]; then
    append "- gate: pass"
    echo "i386 WineMetal build preflight passed: $report"
else
    append "- gate: fail"
    echo "i386 WineMetal build preflight failed: $report"
    exit 2
fi
