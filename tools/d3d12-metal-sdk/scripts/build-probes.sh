#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
OUT_DIR="$SDK_DIR/out/bin"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
AGILITY_BIN="${AGILITY_BIN:-/Volumes/AverySSD/metalsharp/metal-api-table/agility-sdk/extracted/build/native/bin/x64}"

mkdir -p "$OUT_DIR"
mkdir -p "$OUT_DIR/D3D12"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_loader/probe_loader.cpp" \
  -o "$OUT_DIR/probe_loader.exe"

for dll in D3D12Core.dll d3d12SDKLayers.dll D3D12StateObjectCompiler.dll; do
  if [[ ! -f "$AGILITY_BIN/$dll" ]]; then
    echo "Missing Agility SDK DLL: $AGILITY_BIN/$dll" >&2
    exit 2
  fi
  cp "$AGILITY_BIN/$dll" "$OUT_DIR/D3D12/$dll"
done

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_agility_ue5/probe_agility_ue5.cpp" \
  -o "$OUT_DIR/probe_agility_ue5.exe"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_device_caps/probe_device_caps.cpp" \
  -o "$OUT_DIR/probe_device_caps.exe"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_dxgi_factory/probe_dxgi_factory.cpp" \
  -o "$OUT_DIR/probe_dxgi_factory.exe"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_resources/probe_resources.cpp" \
  -o "$OUT_DIR/probe_resources.exe"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_descriptors/probe_descriptors.cpp" \
  -o "$OUT_DIR/probe_descriptors.exe"

echo "$OUT_DIR/probe_loader.exe"
echo "$OUT_DIR/probe_agility_ue5.exe"
echo "$OUT_DIR/probe_device_caps.exe"
echo "$OUT_DIR/probe_dxgi_factory.exe"
echo "$OUT_DIR/probe_resources.exe"
echo "$OUT_DIR/probe_descriptors.exe"
