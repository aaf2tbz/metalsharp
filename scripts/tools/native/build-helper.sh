#!/bin/bash
# Builds the MetalSharp Process Manager telemetry helper into app/native/ so
# electron-builder ships it at Resources/scripts/tools/native/, and also into
# scripts/tools/native/ so the scripts-tools bundle carries it for the runtime
# install path. Produces a thin arm64 Mach-O (adhoc-signed at packaging time).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$SCRIPT_DIR/metalsharp-process-manager-helper.cpp"
APP_NATIVE="$PROJECT_ROOT/app/native"
BUNDLE_NATIVE="$SCRIPT_DIR"

mkdir -p "$APP_NATIVE"

CXX="${CXX:-clang++}"
CXXFLAGS="${CXXFLAGS:--O2 -std=c++17 -Wall -Wextra -mmacosx-version-min=11.0}"
LDFLAGS="${LDFLAGS:--framework IOKit -framework CoreFoundation}"

echo "Compiling metalsharp-process-manager-helper with $CXX ..."
"$CXX" $CXXFLAGS "$SRC" -o "$APP_NATIVE/metalsharp-process-manager-helper" $LDFLAGS

# Mirror into the scripts-tools source tree so create-split-bundles.py picks
# it up when building metalsharp-scripts-tools.tar.zst.
cp "$APP_NATIVE/metalsharp-process-manager-helper" "$BUNDLE_NATIVE/metalsharp-process-manager-helper"

echo "Built:"
echo "  $APP_NATIVE/metalsharp-process-manager-helper   (electron-builder extraResources)"
echo "  $BUNDLE_NATIVE/metalsharp-process-manager-helper (scripts-tools bundle)"
