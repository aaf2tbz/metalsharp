#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUNTIME="${1:-$HOME/.metalsharp/runtime/wine}"
WRAPPER="$RUNTIME/bin/metalsharp-wine"
WINESERVER="$RUNTIME/bin/wineserver"
PROBE_SOURCE="$SCRIPT_DIR/tests/writecopy_probe.c"

[ -x "$WRAPPER" ] || { echo "MetalSharp Wine wrapper missing: $WRAPPER" >&2; exit 1; }
[ -x "$WINESERVER" ] || { echo "MetalSharp wineserver missing: $WINESERVER" >&2; exit 1; }
[ -f "$PROBE_SOURCE" ] || { echo "probe source missing: $PROBE_SOURCE" >&2; exit 1; }
command -v x86_64-w64-mingw32-gcc >/dev/null || { echo "x86_64 MinGW compiler is required" >&2; exit 1; }

tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-writecopy-test.XXXXXX")"
prefix="$tmp/prefix"
cleanup() {
  WINEPREFIX="$prefix" "$WINESERVER" -k >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT

x86_64-w64-mingw32-gcc -O2 -s -o "$tmp/writecopy_probe.exe" "$PROBE_SOURCE"
cp "$tmp/writecopy_probe.exe" "$tmp/UplayWebCore.exe"

run_probe() {
  local log="$1" expected="$2"
  shift 2
  "$@" > "$log" 2>&1
  grep -q 'initial=0x8' "$log" && grep -q "old=$expected expected=$expected" "$log" || {
    cat "$log" >&2
    return 1
  }
  tail -1 "$log"
}

WINEPREFIX="$prefix" WINEDEBUG=-all WINEDEBUGGER=none "$WRAPPER" wineboot -u >/dev/null 2>&1
run_probe "$tmp/unset.log" 0x8 \
  env -u WINE_SIMULATE_WRITECOPY WINEPREFIX="$prefix" WINEDEBUG=-all WINEDEBUGGER=none \
  "$WRAPPER" "$tmp/writecopy_probe.exe" writecopy
run_probe "$tmp/explicit.log" 0x4 \
  env WINEPREFIX="$prefix" WINEDEBUG=-all WINEDEBUGGER=none WINE_SIMULATE_WRITECOPY=1 \
  "$WRAPPER" "$tmp/writecopy_probe.exe" readwrite
run_probe "$tmp/automatic.log" 0x4 \
  env -u WINE_SIMULATE_WRITECOPY WINEPREFIX="$prefix" WINEDEBUG=-all WINEDEBUGGER=none \
  "$WRAPPER" "$tmp/UplayWebCore.exe" readwrite

WINEPREFIX="$prefix" "$WINESERVER" -w
echo "Ubisoft write-copy functional tests passed"
