#!/bin/bash
set -euo pipefail

if [ "$(uname -s)" != "Linux" ] && [ "${METALSHARP_ALLOW_CROSS_DEB:-}" != "1" ]; then
  echo "The .deb package must be built on Linux so metalsharp-backend is a Linux binary."
  echo "Set METALSHARP_ALLOW_CROSS_DEB=1 only if you already provide a Linux backend binary."
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$PROJECT_ROOT/app"

RESTORE_BUNDLES=()
for bundle in metalsharp_bundle.tar.zst metalsharp_bundle2.tar.zst; do
  if [ -f "$PROJECT_ROOT/app/bundles/$bundle" ]; then
    backup="$PROJECT_ROOT/app/bundles/${bundle%.tar.zst}.host-backup.$$.tar.zst"
    mv "$PROJECT_ROOT/app/bundles/$bundle" "$backup"
    RESTORE_BUNDLES+=("$backup:$PROJECT_ROOT/app/bundles/$bundle")
  fi
done

restore_bundle() {
  for entry in "${RESTORE_BUNDLES[@]}"; do
    backup="${entry%%:*}"
    dest="${entry#*:}"
    if [ -f "$backup" ]; then
      mv "$backup" "$dest"
    fi
  done
}
trap restore_bundle EXIT

export METALSHARP_TARGET=linux
npm run prepare:native
npm run build:all
npm run fetch:bundles:linux
npx electron-builder --linux deb --x64
