#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NATIVE_DIR="$PROJECT_ROOT/app/native"
HOST_DIR="$NATIVE_DIR/host"

mkdir -p "$NATIVE_DIR"
mkdir -p "$HOST_DIR"

for file in \
  metalsharp \
  metalsharp_launcher \
  metalsharp-process-manager-helper \
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

for file in \
  libmetalsharp_host_runtime.dylib \
  libmetalsharp_host_runtime.so
do
  if [ ! -f "$HOST_DIR/$file" ]; then
    : > "$HOST_DIR/$file"
  fi
done

if [ ! -f "$HOST_DIR/HostRuntimeABI.h" ]; then
  cp "$PROJECT_ROOT/include/metalsharp/HostRuntimeABI.h" "$HOST_DIR/HostRuntimeABI.h"
fi

if [ ! -f "$HOST_DIR/manifest.json" ]; then
  cat > "$HOST_DIR/manifest.json" <<'JSON'
{
  "abi": "metalsharp-host-runtime",
  "version": {
    "major": 1,
    "minor": 0
  },
  "services": [
    "process",
    "paths",
    "logging",
    "steam",
    "graphics",
    "audio",
    "input",
    "managed_runtime"
  ]
}
JSON
fi
