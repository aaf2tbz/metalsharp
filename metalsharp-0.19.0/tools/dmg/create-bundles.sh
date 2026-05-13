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
    "SteamSetup.exe"
)

for bundle in "${BUNDLES[@]}"; do
    dest="$BUNDLE_DIR/$bundle"
    if [ -f "$dest" ]; then
        echo "SKIP: $bundle — already exists"
        continue
    fi
    echo "Downloading $bundle..."
    curl -sL -o "$dest" "https://github.com/$REPO/releases/download/$RELEASE_TAG/$bundle"
    if [ -f "$dest" ]; then
        size=$(du -sh "$dest" | cut -f1)
        echo "  -> $dest ($size)"
    else
        echo "  FAILED: $bundle"
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
