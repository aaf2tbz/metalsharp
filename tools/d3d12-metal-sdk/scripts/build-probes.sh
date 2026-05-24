#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
OUT_DIR="$SDK_DIR/out/bin"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
AGILITY_VERSION="${AGILITY_VERSION:-1.619.3}"
AGILITY_BIN="${AGILITY_BIN:-}"
DXC_BIN_DIR="${DXC_BIN_DIR:-}"

mkdir -p "$OUT_DIR"
mkdir -p "$OUT_DIR/D3D12"
mkdir -p "$OUT_DIR/D3D12/x64"

if [[ -z "$AGILITY_BIN" ]]; then
  AGILITY_BIN="$("$SDK_DIR/scripts/fetch-agility.sh" --version "$AGILITY_VERSION")"
fi

if [[ -z "$DXC_BIN_DIR" ]]; then
  DXC_BIN_DIR="$("$SDK_DIR/scripts/fetch-dxc.sh")"
fi

for dll in dxc.exe dxcompiler.dll dxil.dll; do
  if [[ ! -f "$DXC_BIN_DIR/$dll" ]]; then
    echo "Missing DXC file: $DXC_BIN_DIR/$dll" >&2
    exit 2
  fi
  cp "$DXC_BIN_DIR/$dll" "$OUT_DIR/$dll"
done

cp "$DXC_BIN_DIR/dxil.dll" "$OUT_DIR/D3D12/dxil.dll"
cp "$DXC_BIN_DIR/dxil.dll" "$OUT_DIR/D3D12/x64/dxil.dll"

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

for dll in D3D12Core.dll d3d12SDKLayers.dll; do
  if [[ ! -f "$AGILITY_BIN/$dll" ]]; then
    echo "Missing Agility SDK DLL: $AGILITY_BIN/$dll" >&2
    exit 2
  fi
  cp "$AGILITY_BIN/$dll" "$OUT_DIR/D3D12/$dll"
  cp "$AGILITY_BIN/$dll" "$OUT_DIR/D3D12/x64/$dll"
done

for optional in D3D12StateObjectCompiler.dll d3dconfig.exe; do
  if [[ -f "$AGILITY_BIN/$optional" ]]; then
    cp "$AGILITY_BIN/$optional" "$OUT_DIR/D3D12/$optional"
    cp "$AGILITY_BIN/$optional" "$OUT_DIR/D3D12/x64/$optional"
  else
    rm -f "$OUT_DIR/D3D12/$optional"
    rm -f "$OUT_DIR/D3D12/x64/$optional"
  fi
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
  "$SDK_DIR/probes/probe_queues/probe_queues.cpp" \
  -o "$OUT_DIR/probe_queues.exe"

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

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_shaders/probe_shaders.cpp" \
  -o "$OUT_DIR/probe_shaders.exe"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_render_headless/probe_render_headless.cpp" \
  -o "$OUT_DIR/probe_render_headless.exe"

"$CXX" \
  -std=c++17 \
  -O2 \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wall \
  -Wextra \
  -Werror \
  "$SDK_DIR/probes/probe_present_windowed/probe_present_windowed.cpp" \
  -lole32 \
  -luuid \
  -lgdi32 \
  -o "$OUT_DIR/probe_present_windowed.exe"

echo "$OUT_DIR/probe_loader.exe"
echo "$OUT_DIR/probe_agility_ue5.exe"
echo "$OUT_DIR/probe_device_caps.exe"
echo "$OUT_DIR/probe_dxgi_factory.exe"
echo "$OUT_DIR/probe_resources.exe"
echo "$OUT_DIR/probe_queues.exe"
echo "$OUT_DIR/probe_descriptors.exe"
echo "$OUT_DIR/probe_shaders.exe"
echo "$OUT_DIR/probe_render_headless.exe"
echo "$OUT_DIR/probe_present_windowed.exe"
