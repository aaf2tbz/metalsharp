#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUNDLE_DIR="${METALSHARP_BUNDLE_DIR:-$ROOT/app/bundles}"
TMP=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--bundle-dir PATH]

Validate Vulkan-family runtime payload archives without installing, launching Wine,
or mutating ~/.metalsharp.

Expected archives:
  dxvk.tar.zst
  vkd3d-proton.tar.zst

Default bundle dir:
  $BUNDLE_DIR
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bundle-dir)
      BUNDLE_DIR="${2:-}"
      if [[ -z "$BUNDLE_DIR" ]]; then
        echo "--bundle-dir requires a path" >&2
        exit 2
      fi
      shift 2
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

extract_archive() {
  local archive="$1"
  local dest="$2"
  mkdir -p "$dest"
  tar --use-compress-program=unzstd -xf "$archive" -C "$dest"
}

find_payload_root() {
  local extracted="$1"
  local expected="$2"
  if [[ -d "$extracted/$expected" ]]; then
    printf '%s\n' "$extracted/$expected"
  else
    printf '%s\n' "$extracted"
  fi
}

check_file() {
  local root="$1"
  local rel="$2"
  if [[ -s "$root/$rel" ]]; then
    echo "ok: $rel"
    return 0
  fi
  echo "missing: $rel" >&2
  return 1
}

check_archive() {
  local archive_name="$1"
  local expected_root="$2"
  shift 2
  local archive="$BUNDLE_DIR/$archive_name"
  local extracted="$TMP/${archive_name%.tar.zst}"

  echo "checking $archive_name"
  if [[ ! -s "$archive" ]]; then
    echo "missing archive: $archive" >&2
    return 1
  fi

  extract_archive "$archive" "$extracted"
  local root
  root="$(find_payload_root "$extracted" "$expected_root")"
  local ok=0
  for rel in "$@"; do
    check_file "$root" "$rel" || ok=1
  done
  return "$ok"
}

require_tool tar
require_tool unzstd

TMP="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-vulkan-payloads.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

failures=0
check_archive \
  "dxvk.tar.zst" \
  "dxvk" \
  "x86_64-windows/d3d9.dll" \
  "i386-windows/d3d9.dll" \
  "i386-windows/dxgi.dll" \
  "x86_64-windows/d3d10core.dll" \
  "x86_64-windows/d3d11.dll" \
  "x86_64-windows/dxgi.dll" \
  "x86_64-unix/libMoltenVK.dylib" || failures=1

check_archive \
  "vkd3d-proton.tar.zst" \
  "vkd3d-proton" \
  "x86_64-windows/d3d12.dll" \
  "x86_64-windows/dxgi.dll" \
  "x86_64-unix/libvkd3d-shader.dylib" \
  "x86_64-unix/libMoltenVK.dylib" || failures=1

if [[ "$failures" == "0" ]]; then
  echo "ok: Vulkan lane payload archives are present and structurally valid"
else
  echo "not ready: Vulkan lane payload archives are missing or structurally incomplete" >&2
fi

exit "$failures"
