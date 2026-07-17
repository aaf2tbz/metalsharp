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
cp -R "$TEMP/Graphics/dll/dxmt_m12" "$WINE_RUNTIME/lib/dxmt_m12"
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

WINE="$WINE_RUNTIME/bin/wine"
if [ ! -x "$WINE" ]; then
  WINE="$WINE_RUNTIME/bin/metalsharp-wine"
fi
test -x "$WINE"

PROBE="$ROOT/tools/d3d12-metal-sdk/scripts/run-probes.sh"
RESULTS="$TEMP/results"
PREFIX="$METALSHARP_HOME/prefix-m12"
GAME="$METALSHARP_HOME/game-m12"
mkdir -p "$PREFIX/drive_c/windows/syswow64"
cp "$WINE_RUNTIME/lib/dxmt/i386-windows/winemetal.dll" \
  "$PREFIX/drive_c/windows/syswow64/winemetal.dll"

# Static/dynamic bridge ABI plus real loader, device, command queue, swapchain
# presenter, and D3D12 mini probes all execute against the two release archives.
"$PROBE" --profile production-bundle --wine "$WINE" --prefix "$PREFIX" \
  --dxmt-runtime "$WINE_RUNTIME/lib/dxmt_m12" --results-dir "$RESULTS" \
  --game-dir "$GAME" --winemetal-abi-only
"$PROBE" --profile production-bundle --wine "$WINE" --prefix "$PREFIX" \
  --dxmt-runtime "$WINE_RUNTIME/lib/dxmt_m12" --results-dir "$RESULTS" --loader-only
"$PROBE" --profile production-bundle --wine "$WINE" --prefix "$PREFIX" \
  --dxmt-runtime "$WINE_RUNTIME/lib/dxmt_m12" --results-dir "$RESULTS" --mini-only

echo "Production DXMT archives passed ABI, D3D11/D3D12 loader, device, queue, swapchain, presenter, and bottle conformance."
