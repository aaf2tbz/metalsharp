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
