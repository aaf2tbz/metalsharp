#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"
RELEASE_TAG="bundles"
REPO="aaf2tbz/metalsharp"

echo "=== MetalSharp Bundle Downloader ==="
echo "Downloading pre-built bundles from GitHub Release $RELEASE_TAG"
echo ""

mkdir -p "$BUNDLE_DIR"

BUNDLES=(
    "metalsharp_bundle.tar.zst"
    "metalsharp_bundle2.tar.zst"
    "SteamSetup.exe"
    "steamwebhelper.exe"
    "steamwebhelper-wrapper.c"
)

for bundle in "${BUNDLES[@]}"; do
    dest="$BUNDLE_DIR/$bundle"
    if [ -f "$dest" ] && [ -s "$dest" ]; then
        echo "SKIP: $bundle — already exists"
        continue
    elif [ -e "$dest" ]; then
        echo "Invalid existing $bundle — removing and downloading fresh copy"
        rm -rf "$dest"
    fi
    echo "Downloading $bundle..."
    curl -fL -o "$dest" "https://github.com/$REPO/releases/download/$RELEASE_TAG/$bundle"
    if [ -s "$dest" ]; then
        size=$(du -sh "$dest" | cut -f1)
        echo "  -> $dest ($size)"
    else
        echo "  FAILED: $bundle"
        rm -f "$dest"
        exit 1
    fi
done

for bundle in "${BUNDLES[@]}"; do
    if [ ! -f "$BUNDLE_DIR/$bundle" ] || [ ! -s "$BUNDLE_DIR/$bundle" ]; then
        echo "Missing required bundle: $bundle"
        exit 1
    fi
done

echo ""
echo "=== Bundle Summary ==="
ls -lh "$BUNDLE_DIR/" 2>/dev/null
echo ""
echo "Total: $(du -sh "$BUNDLE_DIR" | cut -f1)"
echo ""
echo "Bundles saved to: $BUNDLE_DIR"
echo "Run 'npm run build && npx electron-builder --mac dmg' to build the DMG."
