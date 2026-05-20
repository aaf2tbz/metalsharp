#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/mscompatdb}"
MACOS_TARGET="${MACOS_TARGET:-x86_64-apple-macos11}"

mkdir -p "$OUT"

clang \
  -target "$MACOS_TARGET" \
  -dynamiclib \
  -O2 \
  -fvisibility=hidden \
  -I"$ROOT/include" \
  "$ROOT/src/wine/mscompatdb/mscompatdb_contract_shim.c" \
  -o "$OUT/mscompatdb.so" \
  -Wl,-install_name,mscompatdb.so

cp -p "$OUT/mscompatdb.so" "$OUT/mscompatdb.dylib"
install_name_tool -id @rpath/mscompatdb.dylib "$OUT/mscompatdb.dylib"

xattr -d com.apple.quarantine "$OUT/mscompatdb.so" "$OUT/mscompatdb.dylib" 2>/dev/null || true
codesign --force --sign - "$OUT/mscompatdb.so" >/dev/null
codesign --force --sign - "$OUT/mscompatdb.dylib" >/dev/null

file "$OUT/mscompatdb.so" "$OUT/mscompatdb.dylib"
