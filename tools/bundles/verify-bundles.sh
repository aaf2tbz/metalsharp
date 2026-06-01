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
    if [ "$asset" = "metalsharp-runtime.tar.zst" ] && ! verify_runtime_host "$path"; then
      failed=1
      continue
    fi
    if [ "$asset" = "metalsharp-scripts-tools.tar.zst" ] && ! verify_scripts_tools_configs "$path"; then
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

verify_runtime_host() {
  local path="$1"
  local tmp
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-runtime-host.XXXXXX")"

  if ! tar --use-compress-program=unzstd -xf "$path" -C "$tmp" runtime/host && [ ! -d "$tmp/runtime/host" ]; then
    echo "HOST RUNTIME MISSING: $path does not contain runtime/host/" >&2
    rm -rf "$tmp"
    return 1
  fi

  local host="$tmp/runtime/host"
  local failed=0
  for required in manifest.json HostRuntimeABI.h; do
    if [ ! -s "$host/$required" ]; then
      echo "HOST RUNTIME INVALID: $path has missing or empty runtime/host/$required" >&2
      failed=1
    fi
  done

  if [ ! -s "$host/libmetalsharp_host_runtime.dylib" ] \
    && [ ! -s "$host/libmetalsharp_host_runtime.so" ] \
    && [ ! -s "$host/metalsharp_host_runtime.dll" ]; then
    echo "HOST RUNTIME INVALID: $path has no non-empty host runtime shared library" >&2
    failed=1
  fi

  rm -rf "$tmp"
  return "$failed"
}

verify_scripts_tools_configs() {
  local path="$1"
  local tmp
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-scripts-tools.XXXXXX")"

  if ! tar --use-compress-program=unzstd -xf "$path" -C "$tmp" scripts/tools/configs/mtsp-rules.toml \
    && [ ! -e "$tmp/scripts/tools/configs/mtsp-rules.toml" ]; then
    echo "SCRIPTS TOOLS INVALID: $path does not contain scripts/tools/configs/mtsp-rules.toml" >&2
    rm -rf "$tmp"
    return 1
  fi

  if [ ! -s "$tmp/scripts/tools/configs/mtsp-rules.toml" ]; then
    echo "SCRIPTS TOOLS INVALID: $path has empty scripts/tools/configs/mtsp-rules.toml" >&2
    rm -rf "$tmp"
    return 1
  fi

  rm -rf "$tmp"
  return 0
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
