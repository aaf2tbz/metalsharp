#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
OUT_DIR="$SDK_DIR/out/bin"
CXX="${CXX:-x86_64-w64-mingw32-g++}"

mkdir -p "$OUT_DIR"

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

echo "$OUT_DIR/probe_loader.exe"
