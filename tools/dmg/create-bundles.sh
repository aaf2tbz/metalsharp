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
    "dxmt.tar.zst"
    "gptk.tar.zst"
    "SteamSetup.exe"
    "steamwebhelper.exe"
    "steamwebhelper-wrapper.c"
)

validate_bundle_runtime() {
    local main_bundle="$BUNDLE_DIR/metalsharp_bundle.tar.zst"
    local support_bundle="$BUNDLE_DIR/metalsharp_bundle2.tar.zst"
    local main_listing
    local support_listing

    if ! command -v unzstd >/dev/null 2>&1; then
        echo "ERROR: unzstd not found; cannot validate bundled Wine runtime version"
        exit 1
    fi

    main_listing="$(tar --use-compress-program=unzstd -tf "$main_bundle")"
    support_listing="$(tar --use-compress-program=unzstd -tf "$support_bundle")"

    if ! grep -q '^wine-11\.5/' <<<"$main_listing"; then
        echo "ERROR: metalsharp_bundle.tar.zst does not contain the expected wine-11.5 runtime"
        exit 1
    fi

    if grep -q '^wine-11\.9/' <<<"$main_listing"; then
        echo "ERROR: metalsharp_bundle.tar.zst contains Wine 11.9; expected 0.33.27 Wine 11.5 assets"
        exit 1
    fi

    if grep -Eq 'wine-11\.9|metalsharp-wine-11\.9-candidate' <<<"$support_listing"; then
        echo "ERROR: metalsharp_bundle2.tar.zst contains Wine 11.9 candidate assets; expected 0.33.27 support bundle"
        exit 1
    fi
}

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

validate_bundle_runtime

echo ""
echo "=== Bundle Summary ==="
ls -lh "$BUNDLE_DIR/" 2>/dev/null
echo ""
echo "Total: $(du -sh "$BUNDLE_DIR" | cut -f1)"
echo ""
echo "Bundles saved to: $BUNDLE_DIR"
echo "Run 'npm run build && npx electron-builder --mac dmg' to build the DMG."
