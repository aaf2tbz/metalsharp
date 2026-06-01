#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"
OUT_DIR="$PROJECT_ROOT/dist/bundles"
RELEASE_TAG="${METALSHARP_BUNDLE_TAG:-bundles}"
REPO="${METALSHARP_BUNDLE_REPO:-aaf2tbz/metalsharp}"

mkdir -p "$BUNDLE_DIR" "$OUT_DIR"

download_asset() {
  local asset="$1"
  local dest="$BUNDLE_DIR/$asset"
  if [ -s "$dest" ]; then
    echo "SKIP source: $asset"
    return 0
  fi
  echo "Downloading source asset: $asset"
  curl -fL --retry 3 -o "$dest" "https://github.com/$REPO/releases/download/$RELEASE_TAG/$asset"
}

for asset in \
  metalsharp_bundle.tar.zst \
  metalsharp_bundle2.tar.zst \
  dxmt.tar.zst \
  gptk.tar.zst \
  dxvk.tar.zst \
  mono-x86.tar.zst \
  SteamSetup.exe
do
  download_asset "$asset"
done

python3 "$PROJECT_ROOT/tools/bundles/create-split-bundles.py" --out-dir "$OUT_DIR"
cp "$OUT_DIR"/metalsharp-*.tar.zst "$BUNDLE_DIR"/
"$PROJECT_ROOT/tools/bundles/verify-bundles.sh" --bundle-dir "$BUNDLE_DIR" --require mac

echo ""
echo "=== Split Bundle Summary ==="
ls -lh "$BUNDLE_DIR"/metalsharp-*.tar.zst
