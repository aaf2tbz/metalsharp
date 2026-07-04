#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE_ROOT="${METALSHARP_RUNTIME_SOURCE_DIR:-$ROOT/.cache/runtime-sources}"
BUILD_ROOT="${METALSHARP_RUNTIME_BUILD_DIR:-$ROOT/.cache/runtime-build}"
BUNDLE_DIR="${METALSHARP_BUNDLE_DIR:-$ROOT/app/bundles}"
METALSHARP_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
DXVK_TAG="${DXVK_TAG:-v3.0}"
VKD3D_PROTON_TAG="${VKD3D_PROTON_TAG:-v3.0.1}"
MOLTENVK_TAG="${MOLTENVK_TAG:-v1.4.1}"
REFRESH=0
STAGE_RUNTIME=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--refresh] [--stage-runtime]

Build and package the Vulkan-family runtime payload archives from pinned source
checkouts. This does not launch Wine, launch games, or replace the installed app.

Inputs:
  DXVK source:         $SOURCE_ROOT/dxvk-${DXVK_TAG}
  VKD3D-Proton source: $SOURCE_ROOT/vkd3d-proton-${VKD3D_PROTON_TAG}
  MoltenVK source:     $SOURCE_ROOT/MoltenVK-${MOLTENVK_TAG}

Outputs:
  $BUNDLE_DIR/dxvk.tar.zst
  $BUNDLE_DIR/vkd3d-proton.tar.zst

Options:
  --refresh        remove previous build work directories first
  --stage-runtime  also copy validated payloads into
                   $METALSHARP_HOME/runtime/wine/lib/{dxvk,vkd3d}
                   and refresh the local MoltenVK ICD JSON
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --refresh)
      REFRESH=1
      shift
      ;;
    --stage-runtime)
      STAGE_RUNTIME=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required tool: $1" >&2
    exit 2
  fi
}

require_file() {
  local path="$1"
  local description="$2"
  if [[ ! -s "$path" ]]; then
    echo "missing required $description: $path" >&2
    exit 1
  fi
}

copy_required() {
  local src="$1"
  local dst="$2"
  require_file "$src" "payload source"
  mkdir -p "$(dirname "$dst")"
  cp -p "$src" "$dst"
}

source_commit() {
  local dir="$1"
  git -C "$dir" rev-parse HEAD 2>/dev/null || echo unknown
}

write_provenance() {
  local path="$1"
  local dxvk_src="$2"
  local vkd3d_src="$3"
  local moltenvk_src="$4"
  cat > "$path" <<JSON
{
  "schema": "metalsharp.vulkan-lane-payload.v1",
  "generatedAtUnix": $(date +%s),
  "sources": {
    "dxvk": { "tag": "$DXVK_TAG", "commit": "$(source_commit "$dxvk_src")" },
    "vkd3d-proton": { "tag": "$VKD3D_PROTON_TAG", "commit": "$(source_commit "$vkd3d_src")" },
    "MoltenVK": { "tag": "$MOLTENVK_TAG", "commit": "$(source_commit "$moltenvk_src")" }
  }
}
JSON
}

archive_tree() {
  local root="$1"
  local dirname="$2"
  local output="$3"
  mkdir -p "$(dirname "$output")"
  rm -f "$output"
  (cd "$root" && tar -cf - "$dirname") | zstd -q -19 -T0 -o "$output"
  chmod 0644 "$output"
}

extract_archive() {
  local archive="$1"
  local dest="$2"
  mkdir -p "$dest"
  tar --use-compress-program=unzstd -xf "$archive" -C "$dest"
}

stage_runtime_payloads() {
  local tmp="$1"
  local runtime_lib="$METALSHARP_HOME/runtime/wine/lib"
  local wine_unix="$runtime_lib/wine/x86_64-unix"
  local icd_dir="$METALSHARP_HOME/runtime/wine/etc/vulkan/icd.d"
  local moltenvk_runtime="$wine_unix/libMoltenVK.dylib"

  rm -rf "$tmp/stage-runtime"
  mkdir -p "$tmp/stage-runtime"
  extract_archive "$BUNDLE_DIR/dxvk.tar.zst" "$tmp/stage-runtime/dxvk"
  extract_archive "$BUNDLE_DIR/vkd3d-proton.tar.zst" "$tmp/stage-runtime/vkd3d"

  rm -rf "$runtime_lib/dxvk" "$runtime_lib/vkd3d"
  mkdir -p "$runtime_lib"
  cp -R "$tmp/stage-runtime/dxvk/dxvk" "$runtime_lib/dxvk"
  cp -R "$tmp/stage-runtime/vkd3d/vkd3d-proton" "$runtime_lib/vkd3d"

  mkdir -p "$wine_unix" "$icd_dir"
  cp -p "$runtime_lib/dxvk/x86_64-unix/libMoltenVK.dylib" "$moltenvk_runtime"
  cat > "$icd_dir/MoltenVK_icd.json" <<JSON
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "$moltenvk_runtime",
    "api_version": "1.4.0"
  }
}
JSON
  echo "staged Vulkan runtime payloads into $runtime_lib"
}

require_tool git
require_tool meson
require_tool ninja
require_tool zstd
require_tool unzstd
require_tool tar
require_tool xcodebuild
require_tool x86_64-w64-mingw32-gcc
require_tool i686-w64-mingw32-gcc

DXVK_SRC="$SOURCE_ROOT/dxvk-${DXVK_TAG}"
VKD3D_SRC="$SOURCE_ROOT/vkd3d-proton-${VKD3D_PROTON_TAG}"
MOLTENVK_SRC="$SOURCE_ROOT/MoltenVK-${MOLTENVK_TAG}"
for dir in "$DXVK_SRC" "$VKD3D_SRC" "$MOLTENVK_SRC"; do
  if [[ ! -d "$dir/.git" ]]; then
    echo "missing source checkout: $dir" >&2
    echo "run tools/runtime/fetch-vulkan-lane-sources.sh first" >&2
    exit 1
  fi
done

if [[ "$REFRESH" == "1" ]]; then
  rm -rf "$BUILD_ROOT/dxvk" "$BUILD_ROOT/vkd3d-proton" "$BUILD_ROOT/package"
fi
mkdir -p "$BUILD_ROOT" "$BUNDLE_DIR"

DXVK_BUILD_PARENT="$BUILD_ROOT/dxvk"
DXVK_BUILD="$DXVK_BUILD_PARENT/dxvk-${DXVK_TAG}"
if [[ ! -s "$DXVK_BUILD/x64/d3d11.dll" || ! -s "$DXVK_BUILD/x32/d3d9.dll" ]]; then
  rm -rf "$DXVK_BUILD_PARENT"
  mkdir -p "$DXVK_BUILD_PARENT"
  echo "building DXVK $DXVK_TAG..."
  "$DXVK_SRC/package-release.sh" "$DXVK_TAG" "$DXVK_BUILD_PARENT" --no-package
else
  echo "using existing DXVK build at $DXVK_BUILD"
fi

VKD3D_BUILD_PARENT="$BUILD_ROOT/vkd3d-proton"
VKD3D_BUILD="$VKD3D_BUILD_PARENT/vkd3d-proton-${VKD3D_PROTON_TAG}"
if [[ ! -s "$VKD3D_BUILD/x64/d3d12.dll" || ! -s "$VKD3D_BUILD/x64/d3d12core.dll" ]]; then
  rm -rf "$VKD3D_BUILD_PARENT"
  mkdir -p "$VKD3D_BUILD_PARENT"
  echo "building VKD3D-Proton $VKD3D_PROTON_TAG..."
  "$VKD3D_SRC/package-release.sh" "$VKD3D_PROTON_TAG" "$VKD3D_BUILD_PARENT" --no-package
else
  echo "using existing VKD3D-Proton build at $VKD3D_BUILD"
fi

MOLTENVK_DYLIB="$MOLTENVK_SRC/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
if [[ ! -s "$MOLTENVK_DYLIB" ]]; then
  echo "building MoltenVK dependencies for macOS..."
  (cd "$MOLTENVK_SRC" && ./fetchDependencies --macos)
  echo "building MoltenVK $MOLTENVK_TAG for macOS..."
  (cd "$MOLTENVK_SRC" && make macos)
else
  echo "using existing MoltenVK dylib at $MOLTENVK_DYLIB"
fi
require_file "$MOLTENVK_DYLIB" "MoltenVK dylib"

PKG_ROOT="$BUILD_ROOT/package"
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/dxvk" "$PKG_ROOT/vkd3d-proton"

copy_required "$DXVK_BUILD/x64/d3d9.dll" "$PKG_ROOT/dxvk/x86_64-windows/d3d9.dll"
copy_required "$DXVK_BUILD/x64/d3d10core.dll" "$PKG_ROOT/dxvk/x86_64-windows/d3d10core.dll"
copy_required "$DXVK_BUILD/x64/d3d11.dll" "$PKG_ROOT/dxvk/x86_64-windows/d3d11.dll"
copy_required "$DXVK_BUILD/x64/dxgi.dll" "$PKG_ROOT/dxvk/x86_64-windows/dxgi.dll"
copy_required "$DXVK_BUILD/x32/d3d9.dll" "$PKG_ROOT/dxvk/i386-windows/d3d9.dll"
copy_required "$DXVK_BUILD/x32/d3d10core.dll" "$PKG_ROOT/dxvk/i386-windows/d3d10core.dll"
copy_required "$DXVK_BUILD/x32/d3d11.dll" "$PKG_ROOT/dxvk/i386-windows/d3d11.dll"
copy_required "$DXVK_BUILD/x32/dxgi.dll" "$PKG_ROOT/dxvk/i386-windows/dxgi.dll"
copy_required "$MOLTENVK_DYLIB" "$PKG_ROOT/dxvk/x86_64-unix/libMoltenVK.dylib"
write_provenance "$PKG_ROOT/dxvk/provenance.json" "$DXVK_SRC" "$VKD3D_SRC" "$MOLTENVK_SRC"

copy_required "$VKD3D_BUILD/x64/d3d12.dll" "$PKG_ROOT/vkd3d-proton/x86_64-windows/d3d12.dll"
copy_required "$VKD3D_BUILD/x64/d3d12core.dll" "$PKG_ROOT/vkd3d-proton/x86_64-windows/d3d12core.dll"
copy_required "$DXVK_BUILD/x64/dxgi.dll" "$PKG_ROOT/vkd3d-proton/x86_64-windows/dxgi.dll"
copy_required "$MOLTENVK_DYLIB" "$PKG_ROOT/vkd3d-proton/x86_64-unix/libMoltenVK.dylib"
write_provenance "$PKG_ROOT/vkd3d-proton/provenance.json" "$DXVK_SRC" "$VKD3D_SRC" "$MOLTENVK_SRC"

archive_tree "$PKG_ROOT" "dxvk" "$BUNDLE_DIR/dxvk.tar.zst"
archive_tree "$PKG_ROOT" "vkd3d-proton" "$BUNDLE_DIR/vkd3d-proton.tar.zst"

"$ROOT/tools/runtime/check-vulkan-lane-payloads.sh" --bundle-dir "$BUNDLE_DIR"

if [[ "$STAGE_RUNTIME" == "1" ]]; then
  stage_runtime_payloads "$BUILD_ROOT"
fi

echo "wrote $BUNDLE_DIR/dxvk.tar.zst"
echo "wrote $BUNDLE_DIR/vkd3d-proton.tar.zst"
