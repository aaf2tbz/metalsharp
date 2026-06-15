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
    if "$PROJECT_ROOT/tools/bundles/verify-bundles.sh" --bundle-dir "$BUNDLE_DIR" "$asset" >/dev/null 2>&1; then
      echo "SKIP bundle: $asset"
      return 0
    fi
    echo "Refreshing stale bundle: $asset"
    rm -f "$dest"
  fi
  echo "Downloading bundle: $asset"
  curl -fL --retry 3 -o "$dest" "https://github.com/$REPO/releases/download/$RELEASE_TAG/$asset"
}

repair_graphics_m12_bundle() {
  local archive="$BUNDLE_DIR/metalsharp-graphics-dll.tar.zst"
  local m12_root="${METALSHARP_DXMT_M12_ROOT:-$HOME/.metalsharp/runtime/wine/lib/dxmt_m12}"
  if [ ! -s "$archive" ] || [ ! -d "$m12_root/x86_64-windows" ] || [ ! -d "$m12_root/x86_64-unix" ]; then
    return 0
  fi

  local tmp root
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-graphics-m12.XXXXXX")"
  root="$tmp/root"
  mkdir -p "$root"
  tar --use-compress-program=unzstd -xf "$archive" -C "$root"
  mkdir -p "$root/Graphics/dll"
  rm -rf "$root/Graphics/dll/dxmt-m12"
  mkdir -p "$root/Graphics/dll/dxmt-m12"
  cp -R -p "$m12_root/x86_64-unix" "$root/Graphics/dll/dxmt-m12/x86_64-unix"
  cp -R -p "$m12_root/x86_64-windows" "$root/Graphics/dll/dxmt-m12/x86_64-windows"
  (
    cd "$root"
    tar -cf "$tmp/metalsharp-graphics-dll.tar" Graphics
  )
  zstd -q -19 -T0 -f "$tmp/metalsharp-graphics-dll.tar" -o "$archive"
  chmod 0644 "$archive"
  rm -rf "$tmp"
  echo "repaired graphics M12 payload: $archive from $m12_root"
}

repair_assets_fnalibs_bundle() {
  local assets_archive="$BUNDLE_DIR/metalsharp-assets.tar.zst"
  local fnalibs_archive="$BUNDLE_DIR/fnalibs.tar.zst"
  if [ ! -s "$assets_archive" ] || [ ! -s "$fnalibs_archive" ]; then
    return 0
  fi

  local tmp assets_root fnalibs_root
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-assets-fnalibs.XXXXXX")"
  assets_root="$tmp/assets"
  fnalibs_root="$tmp/fnalibs"
  mkdir -p "$assets_root" "$fnalibs_root"

  tar --use-compress-program=unzstd -xf "$assets_archive" -C "$assets_root"
  tar --use-compress-program=unzstd -xf "$fnalibs_archive" -C "$fnalibs_root"

  if [ ! -d "$assets_root/assets" ] || [ ! -d "$fnalibs_root/fnalibs" ]; then
    echo "Unable to repair assets fnalibs payload: unexpected bundle layout" >&2
    rm -rf "$tmp"
    return 1
  fi

  rm -rf "$assets_root/assets/fnalibs"
  mkdir -p "$assets_root/assets"
  cp -R -p "$fnalibs_root/fnalibs" "$assets_root/assets/fnalibs"
  mkdir -p "$assets_root/assets/fna-kickstart/osx"
  for dylib in libFNA3D.0.dylib libSDL2-2.0.0.dylib libFAudio.0.dylib libtheorafile.dylib; do
    cp -p "$fnalibs_root/fnalibs/$dylib" "$assets_root/assets/fna-kickstart/osx/$dylib"
  done

  (
    cd "$assets_root"
    tar -cf "$tmp/metalsharp-assets.tar" assets
  )
  zstd -q -19 -T0 -f "$tmp/metalsharp-assets.tar" -o "$assets_archive"
  chmod 0644 "$assets_archive"
  rm -rf "$tmp"
  echo "repaired assets fnalibs payload: $assets_archive"
}

while IFS=$'\t' read -r asset _root _platforms _notes; do
  case "$asset" in
    ""|\#*) continue ;;
  esac
  download_asset "$asset" "$BUNDLE_DIR/$asset"
done < "$MANIFEST"

repair_graphics_m12_bundle
repair_assets_fnalibs_bundle

"$PROJECT_ROOT/tools/dmg/repair-runtime-bundle.py" \
  --archive "$BUNDLE_DIR/metalsharp-runtime.tar.zst" \
  --host-dir "$PROJECT_ROOT/app/native/host" \
  --backend "$PROJECT_ROOT/app/src-rust/target/release/metalsharp-backend"

"$PROJECT_ROOT/tools/bundles/verify-bundles.sh" --bundle-dir "$BUNDLE_DIR" --require mac

rm -f "$OUT_DIR"/metalsharp-*.tar.zst
while IFS=$'\t' read -r asset _root _platforms _notes; do
  case "$asset" in
    ""|\#*) continue ;;
  esac
  cp "$BUNDLE_DIR/$asset" "$OUT_DIR/$asset"
done < "$MANIFEST"

{
  printf 'asset\troot\tsha256\tsize\tnotes\n'
  while IFS=$'\t' read -r asset root _platforms notes; do
    case "$asset" in
      ""|\#*) continue ;;
    esac
    archive="$BUNDLE_DIR/$asset"
    hash="$(shasum -a 256 "$archive" | awk '{print $1}')"
    size="$(wc -c < "$archive" | tr -d ' ')"
    printf '%s\t%s\t%s\t%s\t%s\n' "$asset" "$root" "$hash" "$size" "$notes"
  done < "$MANIFEST"
} > "$OUT_DIR/metalsharp-bundle-manifest.tsv"

echo ""
echo "=== Split Bundle Summary ==="
ls -lh "$BUNDLE_DIR"/metalsharp-*.tar.zst
