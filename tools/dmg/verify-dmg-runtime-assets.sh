#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DMG="${1:?usage: tools/dmg/verify-dmg-runtime-assets.sh path/to/MetalSharp.dmg}"

if [ ! -s "$DMG" ]; then
  echo "Missing DMG: $DMG" >&2
  exit 1
fi

MOUNT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-dmg-mount.XXXXXX")"
LIST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-dmg-bundles.XXXXXX")"

cleanup() {
  hdiutil detach "$MOUNT_DIR" -quiet 2>/dev/null || true
  rm -rf "$MOUNT_DIR" "$LIST_DIR"
}
trap cleanup EXIT

hdiutil attach "$DMG" -mountpoint "$MOUNT_DIR" -nobrowse -quiet

APP_DIR="$(find "$MOUNT_DIR" -maxdepth 1 -name '*.app' -type d | head -n 1)"
if [ -z "$APP_DIR" ]; then
  echo "DMG does not contain a top-level .app" >&2
  exit 1
fi

RESOURCES="$APP_DIR/Contents/Resources"
BACKEND="$RESOURCES/runtime/metalsharp-backend"
HOST="$RESOURCES/runtime/host"
BUNDLES="$RESOURCES/bundles"

for required in \
  "$BACKEND" \
  "$HOST/manifest.json" \
  "$HOST/HostRuntimeABI.h" \
  "$RESOURCES/scripts/tools/updater/update.py" \
  "$RESOURCES/scripts/tools/updater/update.sh" \
  "$BUNDLES/metalsharp-electron.tar.zst" \
  "$BUNDLES/metalsharp-graphics-dll.tar.zst" \
  "$BUNDLES/metalsharp-runtime.tar.zst" \
  "$BUNDLES/metalsharp-assets.tar.zst" \
  "$BUNDLES/fnalibs.tar.zst" \
  "$BUNDLES/dxvk.tar.zst" \
  "$BUNDLES/vkd3d-proton.tar.zst" \
  "$BUNDLES/metalsharp-scripts-tools.tar.zst" \
  "$BUNDLES/metalsharp-steam.tar.zst" \
  "$BUNDLES/metalsharp-d3d12-developer-sdk.tar.zst" \
  "$BUNDLES/metalsharp-d3dmetal-native-contract.tar.zst"
do
  if [ ! -s "$required" ]; then
    echo "DMG missing required runtime asset: ${required#$APP_DIR/}" >&2
    exit 1
  fi
done

# Phase 5: d3dmetal_native contract + verifier scripts must ship in the DMG's
# scripts-tools bundle path. The contract bundle MUST be binary-free (Apple
# D3DMetal payloads are never redistributed).
if [ -s "$BUNDLES/metalsharp-d3dmetal-native-contract.tar.zst" ]; then
  if zstd -dc "$BUNDLES/metalsharp-d3dmetal-native-contract.tar.zst" 2>/dev/null | tar -tf - 2>/dev/null | grep -Eq '\.(dll|dylib)$|D3DMetal$'; then
    echo "d3dmetal-native contract bundle must NOT contain Apple binaries" >&2
    exit 1
  fi
fi

if [ ! -s "$HOST/libmetalsharp_host_runtime.dylib" ] \
  && [ ! -s "$HOST/libmetalsharp_host_runtime.so" ] \
  && [ ! -s "$HOST/metalsharp_host_runtime.dll" ]; then
  echo "DMG host runtime has no non-empty shared library" >&2
  exit 1
fi

cp "$BUNDLES"/*.tar.zst "$LIST_DIR"/
"$PROJECT_ROOT/tools/bundles/verify-bundles.sh" --bundle-dir "$LIST_DIR" --require mac

echo "DMG runtime assets verified: $DMG"
