#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GRAPHICS_ARCHIVE="${1:-$ROOT/app/bundles/metalsharp-graphics-dll.tar.zst}"
RUNTIME_ARCHIVE="${2:-$ROOT/app/bundles/metalsharp-runtime.tar.zst}"
TEMP="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-production-dxmt.XXXXXX")"
trap '/bin/rm -rf -- "$TEMP"' EXIT

test -s "$GRAPHICS_ARCHIVE"
test -s "$RUNTIME_ARCHIVE"
python3 "$ROOT/tools/bundles/verify-dxmt-surfaces.py" --archive "$GRAPHICS_ARCHIVE"

tar --use-compress-program=unzstd -xf "$RUNTIME_ARCHIVE" -C "$TEMP"
tar --use-compress-program=unzstd -xf "$GRAPHICS_ARCHIVE" -C "$TEMP"

METALSHARP_HOME="$TEMP/metalsharp-home"
WINE_RUNTIME="$METALSHARP_HOME/runtime/wine"
mkdir -p "$WINE_RUNTIME/lib/wine/x86_64-unix" "$WINE_RUNTIME/lib/wine/x86_64-windows"
cp -R "$TEMP/runtime/." "$METALSHARP_HOME/runtime/"
rm -rf "$WINE_RUNTIME/lib/dxmt" "$WINE_RUNTIME/lib/dxmt_m12"
cp -R "$TEMP/Graphics/dll/dxmt" "$WINE_RUNTIME/lib/dxmt"
cp -R "$TEMP/Graphics/dll/dxmt-m12" "$WINE_RUNTIME/lib/dxmt_m12"
cp "$WINE_RUNTIME/lib/dxmt_m12/x86_64-unix/winemetal.so" \
  "$WINE_RUNTIME/lib/wine/x86_64-unix/winemetal.so"
cp "$WINE_RUNTIME/lib/dxmt_m12/x86_64-windows/winemetal.dll" \
  "$WINE_RUNTIME/lib/wine/x86_64-windows/winemetal.dll"

SURFACE_TEST="$ROOT/app/build/c-backend/metalsharp-dxmt-surface-tests"
BOTTLE_TEST="$ROOT/app/build/c-backend/metalsharp-bottle-deployment-tests"
test -x "$SURFACE_TEST"
test -x "$BOTTLE_TEST"
"$SURFACE_TEST" --repair "$WINE_RUNTIME/lib/dxmt_m12"
"$BOTTLE_TEST" "$METALSHARP_HOME" "$WINE_RUNTIME/lib/dxmt_m12"

echo "Production DXMT archives passed surface and bottle deployment conformance."
