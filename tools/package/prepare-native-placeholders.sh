#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NATIVE_DIR="$PROJECT_ROOT/app/native"

mkdir -p "$NATIVE_DIR"

for file in \
  metalsharp \
  metalsharp_launcher \
  d3d11.dylib \
  d3d12.dylib \
  dxgi.dylib \
  xaudio2_9.dylib \
  xinput1_4.dylib \
  d3d11.so \
  d3d12.so \
  dxgi.so \
  xaudio2_9.so \
  xinput1_4.so
do
  if [ ! -f "$NATIVE_DIR/$file" ]; then
    : > "$NATIVE_DIR/$file"
  fi
done
