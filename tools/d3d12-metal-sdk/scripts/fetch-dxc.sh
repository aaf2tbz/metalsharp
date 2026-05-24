#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
DXC_VERSION="${DXC_VERSION:-v1.9.2602}"
DXC_ARCHIVE="${DXC_ARCHIVE:-dxc_2026_02_20.zip}"
DXC_SHA256="${DXC_SHA256:-a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e}"
DXC_URL="${DXC_URL:-https://github.com/microsoft/DirectXShaderCompiler/releases/download/${DXC_VERSION}/${DXC_ARCHIVE}}"
DXC_CACHE_DIR="${DXC_CACHE_DIR:-$SDK_DIR/cache/dxc}"
DXC_DOWNLOAD_DIR="$DXC_CACHE_DIR/downloads"
DXC_EXTRACT_DIR="$DXC_CACHE_DIR/$DXC_VERSION"
DXC_BIN_DIR="$DXC_EXTRACT_DIR/bin/x64"
ARCHIVE_PATH="$DXC_DOWNLOAD_DIR/$DXC_ARCHIVE"

mkdir -p "$DXC_DOWNLOAD_DIR"

if [[ ! -f "$ARCHIVE_PATH" ]]; then
  curl -L --fail --retry 3 -o "$ARCHIVE_PATH" "$DXC_URL"
fi

actual_sha="$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')"
if [[ "$actual_sha" != "$DXC_SHA256" ]]; then
  echo "DXC archive checksum mismatch:" >&2
  echo "  expected: $DXC_SHA256" >&2
  echo "  actual:   $actual_sha" >&2
  exit 2
fi

if [[ ! -f "$DXC_BIN_DIR/dxc.exe" || ! -f "$DXC_BIN_DIR/dxcompiler.dll" || ! -f "$DXC_BIN_DIR/dxil.dll" ]]; then
  rm -rf "$DXC_EXTRACT_DIR"
  mkdir -p "$DXC_EXTRACT_DIR"
  unzip -q "$ARCHIVE_PATH" -d "$DXC_EXTRACT_DIR"
fi

for file in dxc.exe dxcompiler.dll dxil.dll; do
  if [[ ! -f "$DXC_BIN_DIR/$file" ]]; then
    echo "DXC archive did not contain expected x64 file: $file" >&2
    exit 2
  fi
done

printf '%s\n' "$DXC_BIN_DIR"
