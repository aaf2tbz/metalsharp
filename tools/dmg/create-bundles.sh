#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"
OUT_DIR="$PROJECT_ROOT/dist/bundles"
RELEASE_TAG="${METALSHARP_BUNDLE_TAG:-bundles}"
REPO="${METALSHARP_BUNDLE_REPO:-aaf2tbz/metalsharp}"
MANIFEST="$PROJECT_ROOT/tools/bundles/asset-manifest.tsv"

mkdir -p "$BUNDLE_DIR" "$OUT_DIR"

download_asset() {
  local asset="$1"
  local dest="$2"
  if [ -s "$dest" ]; then
    echo "SKIP bundle: $asset"
    return 0
  fi
  echo "Downloading bundle: $asset"
  curl -fL --retry 3 -o "$dest" "https://github.com/$REPO/releases/download/$RELEASE_TAG/$asset"
}

while IFS=$'\t' read -r asset _root _platforms _notes; do
  case "$asset" in
    ""|\#*) continue ;;
  esac
  download_asset "$asset" "$BUNDLE_DIR/$asset"
  cp "$BUNDLE_DIR/$asset" "$OUT_DIR/$asset"
done < "$MANIFEST"

download_asset "metalsharp-bundle-manifest.tsv" "$OUT_DIR/metalsharp-bundle-manifest.tsv"
"$PROJECT_ROOT/tools/bundles/verify-bundles.sh" --bundle-dir "$BUNDLE_DIR" --require mac

echo ""
echo "=== Split Bundle Summary ==="
ls -lh "$BUNDLE_DIR"/metalsharp-*.tar.zst
