#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"
MANIFEST="$SCRIPT_DIR/asset-manifest.tsv"
REPO="${METALSHARP_BUNDLE_REPO:-aaf2tbz/metalsharp}"
TAG="${METALSHARP_BUNDLE_TAG:-bundles}"
CHECK_RELEASE=0
REQUIRE_PLATFORM=""
ASSETS=()

usage() {
  cat <<'USAGE'
Usage: tools/bundles/verify-bundles.sh [options] [asset...]

Options:
  --bundle-dir PATH    Local bundle directory to verify (default app/bundles)
  --manifest PATH      Manifest to verify (default tools/bundles/asset-manifest.tsv)
  --release            Verify that release assets exist
  --require PLATFORM   Require every manifest asset for PLATFORM (mac)
  -h, --help           Show this help
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --bundle-dir) BUNDLE_DIR="$2"; shift 2 ;;
    --manifest) MANIFEST="$2"; shift 2 ;;
    --release) CHECK_RELEASE=1; shift ;;
    --require) REQUIRE_PLATFORM="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) ASSETS+=("$1"); shift ;;
  esac
done

manifest_rows() {
  awk -F '\t' 'NF >= 4 && $1 !~ /^#/ { print $0 }' "$MANIFEST"
}

asset_requested() {
  local asset="$1"
  [ "${#ASSETS[@]}" -eq 0 ] && return 0
  local requested
  for requested in "${ASSETS[@]}"; do
    [ "$requested" = "$asset" ] && return 0
  done
  return 1
}

platform_required() {
  local platforms="$1"
  [ -z "$REQUIRE_PLATFORM" ] && return 1
  case ",$platforms," in
    *",$REQUIRE_PLATFORM,"*) return 0 ;;
    *) return 1 ;;
  esac
}

verify_local() {
  local failed=0
  while IFS=$'\t' read -r asset root platforms _notes; do
    asset_requested "$asset" || continue
    local path="$BUNDLE_DIR/$asset"
    if [ ! -s "$path" ]; then
      if platform_required "$platforms"; then
        echo "MISSING: $asset required for $REQUIRE_PLATFORM" >&2
        failed=1
      fi
      continue
    fi
    if ! archive_contains_root "$path" "$root"; then
      echo "ROOT MISMATCH: $asset does not contain $root/" >&2
      failed=1
      continue
    fi
    if [ "$asset" = "metalsharp-runtime.tar.zst" ] && ! verify_runtime_core "$path"; then
      failed=1
      continue
    fi
    if [ "$asset" = "metalsharp-graphics-dll.tar.zst" ] && ! verify_graphics_core "$path"; then
      failed=1
      continue
    fi
    if [ "$asset" = "metalsharp-assets.tar.zst" ] && ! verify_assets_core "$path"; then
      failed=1
      continue
    fi
    if [ "$asset" = "metalsharp-scripts-tools.tar.zst" ] && ! verify_scripts_tools_core "$path"; then
      failed=1
      continue
    fi
    if [ "$asset" = "metalsharp-steam.tar.zst" ] && ! verify_steam_core "$path"; then
      failed=1
      continue
    fi
    echo "OK: $asset root=$root"
  done < <(manifest_rows)
  return "$failed"
}

archive_contains_root() {
  local path="$1"
  local root="$2"
  local listing
  listing="$(tar --use-compress-program=unzstd -tf "$path" "$root" 2>/dev/null || true)"
  awk -v root="$root" 'index($0, root "/") == 1 || $0 == root { found=1 } END { exit found ? 0 : 1 }' \
    <<< "$listing"
}

verify_required_files() {
  local path="$1"
  local label="$2"
  shift 2
  local tmp
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-bundle-core.XXXXXX")"

  if ! tar --use-compress-program=unzstd -xf "$path" -C "$tmp" "$@"; then
    echo "$label INVALID: $path is missing one or more required files" >&2
    rm -rf "$tmp"
    return 1
  fi

  local failed=0
  local required
  for required in "$@"; do
    if [ ! -s "$tmp/$required" ]; then
      echo "$label INVALID: $path has missing or empty $required" >&2
      failed=1
    fi
  done

  rm -rf "$tmp"
  return "$failed"
}

verify_runtime_core() {
  verify_required_files "$1" "RUNTIME" \
    runtime/wine/bin/metalsharp-wine \
    runtime/metalsharp-backend \
    runtime/host/manifest.json \
    runtime/host/HostRuntimeABI.h \
    runtime/host/libmetalsharp_host_runtime.dylib \
    runtime/wine/lib/metalsharp/x86_64-windows/metalsharp_ntdll_hook.dll
}

verify_graphics_core() {
  verify_required_files "$1" "GRAPHICS" \
    Graphics/dll/dxmt/x86_64-unix/winemetal.so \
    Graphics/dll/dxmt/x86_64-windows/d3d10core.dll \
    Graphics/dll/dxmt/x86_64-windows/d3d11.dll \
    Graphics/dll/dxmt/x86_64-windows/d3d12.dll \
    Graphics/dll/dxmt/x86_64-windows/dxgi.dll \
    Graphics/dll/dxmt/x86_64-windows/dxgi_dxmt.dll \
    Graphics/dll/dxmt/x86_64-windows/nvapi64.dll \
    Graphics/dll/dxmt/x86_64-windows/nvngx.dll \
    Graphics/dll/dxmt/x86_64-windows/winemetal.dll
}

verify_assets_core() {
  verify_required_files "$1" "ASSETS" \
    assets/eac-toggle/x86_64-windows/_winhttp.dll \
    assets/fna-kickstart/kick.bin.osx \
    assets/fna-kickstart/FNA.dll \
    assets/fna-kickstart/mscorlib.dll \
    assets/fna-kickstart/osx/libmonosgen-2.0.1.dylib \
    assets/fna-kickstart/osx/libSDL3.0.dylib \
    assets/fna-kickstart/osx/libFNA3D.0.dylib \
    assets/fna-kickstart/osx/libFAudio.0.dylib \
    assets/fna-kickstart/osx/libMonoPosixHelper.dylib \
    assets/fnalibs/libFNA3D.0.dylib \
    assets/fnalibs/libSDL3.0.dylib \
    assets/fnalibs/libFAudio.0.dylib \
    assets/fnalibs/libtheorafile.dylib \
    assets/goldberg/x64/steam_api64.dll \
    assets/goldberg/x86/steam_api.dll \
    assets/goldberg/steamclient/steamclient64.dll \
    assets/goldberg/steamclient/steamclient.dll \
    assets/gptk/external/D3DMetal.framework/Versions/A/D3DMetal \
    assets/gptk/external/D3DMetal.framework/Versions/A/Resources/libmetalirconverter.dylib \
    assets/gptk/x86_64-windows/atidxx64.dll \
    assets/gptk/x86_64-windows/d3d10.dll \
    assets/gptk/x86_64-windows/d3d11.dll \
    assets/gptk/x86_64-windows/d3d12.dll \
    assets/gptk/x86_64-windows/dxgi.dll \
    assets/gptk/x86_64-windows/nvapi64.dll \
    assets/gptk/x86_64-windows/nvngx-on-metalfx.dll \
    assets/mono-arm64/bin/mono-sgen \
    assets/shims/libsteam_api.dylib
}

verify_scripts_tools_core() {
  verify_required_files "$1" "SCRIPTS TOOLS" \
    scripts/tools/configs/mtsp-rules.toml \
    scripts/tools/updater/update.sh
}

verify_steam_core() {
  verify_required_files "$1" "STEAM" \
    steam/SteamSetup.exe \
    steam/steamwebhelper.exe \
    steam/steamwebhelper-wrapper.c
}

verify_release() {
  command -v gh >/dev/null 2>&1 || {
    echo "ERROR: gh is required for --release verification" >&2
    return 1
  }
  local assets
  assets="$(gh release view "$TAG" --repo "$REPO" --json assets --jq '.assets[].name')"
  local failed=0
  while IFS=$'\t' read -r asset _root platforms _notes; do
    asset_requested "$asset" || continue
    if ! printf '%s\n' "$assets" | grep -Fx "$asset" >/dev/null; then
      if [ -z "$REQUIRE_PLATFORM" ] || platform_required "$platforms"; then
        echo "RELEASE MISSING: $asset" >&2
        failed=1
      fi
    else
      echo "RELEASE OK: $asset"
    fi
  done < <(manifest_rows)
  return "$failed"
}

verify_local
if [ "$CHECK_RELEASE" = "1" ]; then
  verify_release
fi
