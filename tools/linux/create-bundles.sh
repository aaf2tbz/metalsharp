#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"
RELEASE_TAG="${METALSHARP_BUNDLE_TAG:-bundles}"
REPO="${METALSHARP_BUNDLE_REPO:-aaf2tbz/metalsharp}"
LINUX_RUNTIME_ASSET="${METALSHARP_LINUX_RUNTIME_ASSET:-metalsharp_linux_runtime.tar.zst}"
VERIFY_BUNDLES="$PROJECT_ROOT/tools/bundles/verify-bundles.sh"

mkdir -p "$BUNDLE_DIR"

download_asset() {
  local asset="$1"
  local dest="$2"

  if [ -f "$dest" ] && [ -s "$dest" ]; then
    if "$VERIFY_BUNDLES" --bundle-dir "$BUNDLE_DIR" "$(basename "$dest")" >/dev/null; then
      echo "SKIP: $(basename "$dest") already exists"
      return 0
    fi
    echo "Invalid existing $(basename "$dest") — removing and downloading fresh copy"
  fi

  rm -f "$dest"
  echo "Downloading $asset..."
  curl -fL -o "$dest" "https://github.com/$REPO/releases/download/$RELEASE_TAG/$asset"
  "$VERIFY_BUNDLES" --bundle-dir "$BUNDLE_DIR" "$(basename "$dest")"
}

create_system_wine_runtime_bundle() {
  local dest="$1"
  local tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/wine/bin" "$tmp/wine/share/metalsharp"
  echo "Creating system Wine fallback runtime bundle at $dest"
  for wrapper in wine metalsharp-wine; do
    printf '%s\n' \
      '#!/bin/sh' \
      'for candidate in /usr/bin/wine /usr/local/bin/wine /opt/wine/bin/wine /usr/bin/wine64 /usr/local/bin/wine64 /opt/wine/bin/wine64; do' \
      '  if [ -x "$candidate" ]; then' \
      '    exec "$candidate" "$@"' \
      '  fi' \
      'done' \
      'echo "MetalSharp Linux runtime requires system Wine. Install wine or wine64." >&2' \
      'exit 127' \
      > "$tmp/wine/bin/$wrapper"
    chmod 755 "$tmp/wine/bin/$wrapper"
  done
  printf '%s\n' \
    'MetalSharp Linux runtime bundle' \
    'This bundle installs wrapper scripts that dispatch to system Wine.' \
    'The Debian package declares wine | wine64 as a dependency.' \
    > "$tmp/wine/share/metalsharp/README-linux-runtime.txt"
  rm -f "$dest"
  COPYFILE_DISABLE=1 tar --no-xattrs -C "$tmp" -cf - wine | zstd -q -o "$dest"
  rm -rf "$tmp"
}

if [ "${METALSHARP_REUSE_LINUX_BUNDLE:-}" != "1" ]; then
  rm -f "$BUNDLE_DIR/metalsharp_bundle.tar.zst"
  rm -f "$BUNDLE_DIR/metalsharp_bundle2.tar.zst"
fi

if ! download_asset "$LINUX_RUNTIME_ASSET" "$BUNDLE_DIR/metalsharp_bundle.tar.zst"; then
  if [ "${METALSHARP_ALLOW_GENERIC_RUNTIME_FALLBACK:-}" = "1" ]; then
    echo "Linux runtime asset $LINUX_RUNTIME_ASSET unavailable; falling back to metalsharp_bundle.tar.zst"
    download_asset "metalsharp_bundle.tar.zst" "$BUNDLE_DIR/metalsharp_bundle.tar.zst"
  else
    create_system_wine_runtime_bundle "$BUNDLE_DIR/metalsharp_bundle.tar.zst"
  fi
fi

if ! download_asset "metalsharp_linux_runtime2.tar.zst" "$BUNDLE_DIR/metalsharp_bundle2.tar.zst"; then
  if [ "${METALSHARP_ALLOW_GENERIC_RUNTIME_FALLBACK:-}" = "1" ]; then
    download_asset "metalsharp_bundle2.tar.zst" "$BUNDLE_DIR/metalsharp_bundle2.tar.zst"
  else
    create_system_wine_runtime_bundle "$BUNDLE_DIR/metalsharp_bundle2.tar.zst"
  fi
fi

for asset in \
  dxmt.tar.zst \
  dxvk.tar.zst \
  mono-x86.tar.zst \
  goldberg.tar.zst \
  eac-toggle.tar.zst \
  SteamSetup.exe \
  steamwebhelper.exe \
  steamwebhelper-wrapper.c
do
  download_asset "$asset" "$BUNDLE_DIR/$asset"
done

echo ""
"$VERIFY_BUNDLES" --bundle-dir "$BUNDLE_DIR" --require linux dxmt.tar.zst steamwebhelper.exe steamwebhelper-wrapper.c
echo "Linux bundles saved to: $BUNDLE_DIR"
ls -lh "$BUNDLE_DIR"
