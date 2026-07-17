#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCHIVE="${1:-$ROOT/app/bundles/metalsharp-graphics-dll.tar.zst}"
TEST_BIN="$ROOT/app/build/c-backend/metalsharp-dxmt-surface-tests"
TEMP="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-dxmt-promotion.XXXXXX")"
trap '/bin/rm -rf -- "$TEMP"' EXIT

/usr/bin/tar -xf "$ARCHIVE" -C "$TEMP" Graphics/dll/dxmt Graphics/dll/dxmt-m12
LIB="$TEMP/runtime/wine/lib"
/bin/mkdir -p "$LIB/wine/x86_64-unix" "$LIB/wine/x86_64-windows"
/bin/mv "$TEMP/Graphics/dll/dxmt" "$LIB/dxmt"
/bin/mv "$TEMP/Graphics/dll/dxmt-m12" "$LIB/dxmt_m12"
/usr/bin/ditto --noqtn "$LIB/dxmt_m12/x86_64-unix/winemetal.so" "$LIB/wine/x86_64-unix/winemetal.so"
/usr/bin/ditto --noqtn "$LIB/dxmt_m12/x86_64-windows/winemetal.dll" "$LIB/wine/x86_64-windows/winemetal.dll"

"$TEST_BIN" "$LIB/dxmt_m12"
test -d "$LIB/.metalsharp-dxmt-backups"

# The updater's combined bundle can temporarily leave both x64 and i386 lanes
# under dxmt. Reconciliation must recreate the isolated M12 root from that
# layout without discarding the i386 lane.
/bin/rm -rf -- "$LIB/dxmt_m12"
"$TEST_BIN" --repair "$LIB/dxmt_m12"
test -f "$LIB/dxmt_m12/x86_64-windows/d3d12.dll"
test -f "$LIB/dxmt/i386-windows/d3d11.dll"
grep -q 'metalsharp.dxmt-runtime.v2' "$LIB/dxmt_m12/metalsharp-dxmt-runtime.json"
METALSHARP_HOME="$TEMP" "$ROOT/app/build/c-backend/metalsharp-installer-tests" --check-home "$TEMP"
METALSHARP_HOME="$TEMP" "$ROOT/app/build/c-backend/metalsharp-installer-tests" --check-lane "$TEMP/runtime/dxmt"
