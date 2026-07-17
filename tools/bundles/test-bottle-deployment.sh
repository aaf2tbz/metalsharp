#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCHIVE="${1:-$ROOT/app/bundles/metalsharp-graphics-dll.tar.zst}"
SURFACE_TEST="$ROOT/app/build/c-backend/metalsharp-dxmt-surface-tests"
BOTTLE_TEST="$ROOT/app/build/c-backend/metalsharp-bottle-deployment-tests"
TEMP="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-bottle-deployment.XXXXXX")"
trap '/bin/rm -rf -- "$TEMP"' EXIT

/usr/bin/tar -xf "$ARCHIVE" -C "$TEMP" Graphics/dll/dxmt Graphics/dll/dxmt_m12
LIB="$TEMP/runtime/wine/lib"
/bin/mkdir -p "$LIB"
/bin/mv "$TEMP/Graphics/dll/dxmt" "$LIB/dxmt"
/bin/mv "$TEMP/Graphics/dll/dxmt_m12" "$LIB/dxmt_m12"

"$SURFACE_TEST" --repair "$LIB/dxmt_m12"
"$BOTTLE_TEST" "$TEMP" "$LIB/dxmt_m12"
