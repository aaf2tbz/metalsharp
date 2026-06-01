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
  --release            Verify GitHub release metadata against the manifest
  --require PLATFORM   Require every manifest asset for PLATFORM (mac or linux)
  -h, --help           Show this help

With no asset arguments, local verification checks manifest assets that already
exist in the bundle directory. Use --require to fail on missing required assets.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --bundle-dir)
      BUNDLE_DIR="$2"
      shift 2
      ;;
    --release)
      CHECK_RELEASE=1
      shift
      ;;
    --require)
      REQUIRE_PLATFORM="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      ASSETS+=("$1")
      shift
      ;;
  esac
done

sha256_file() {
  local file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  else
    sha256sum "$file" | awk '{print $1}'
  fi
}

manifest_rows() {
  awk -F '\t' 'NF >= 4 && $1 !~ /^#/ { print $0 }' "$MANIFEST"
}

asset_requested() {
  local asset="$1"
  if [ "${#ASSETS[@]}" -eq 0 ]; then
    return 0
  fi
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
  while IFS=$'\t' read -r asset expected_sha expected_size platforms _notes; do
    asset_requested "$asset" || continue
    local path="$BUNDLE_DIR/$asset"
    if [ ! -e "$path" ]; then
      if platform_required "$platforms"; then
        echo "MISSING: $asset required for $REQUIRE_PLATFORM" >&2
        failed=1
      fi
      continue
    fi
    if [ ! -s "$path" ]; then
      echo "INVALID: $asset exists but is empty" >&2
      failed=1
      continue
    fi
    local actual_sha actual_size
    actual_sha="$(sha256_file "$path")"
    actual_size="$(wc -c < "$path" | tr -d ' ')"
    if [ "$actual_sha" != "$expected_sha" ]; then
      echo "HASH MISMATCH: $asset" >&2
      echo "  expected $expected_sha" >&2
      echo "  actual   $actual_sha" >&2
      failed=1
    fi
    if [ "$actual_size" != "$expected_size" ]; then
      echo "SIZE MISMATCH: $asset expected $expected_size actual $actual_size" >&2
      failed=1
    fi
    if [ "$actual_sha" = "$expected_sha" ] && [ "$actual_size" = "$expected_size" ]; then
      echo "OK: $asset $actual_sha"
    fi
  done < <(manifest_rows)
  return "$failed"
}

verify_release() {
  if ! command -v gh >/dev/null 2>&1; then
    echo "ERROR: gh is required for --release verification" >&2
    return 1
  fi

  local release_tsv
  release_tsv="$(gh release view "$TAG" --repo "$REPO" --json assets --jq '.assets[] | [.name, .size, (.digest // "")] | @tsv')"

  local failed=0
  while IFS=$'\t' read -r asset expected_sha expected_size _platforms _notes; do
    asset_requested "$asset" || continue
    local row actual_size actual_digest actual_sha
    row="$(printf '%s\n' "$release_tsv" | awk -F '\t' -v asset="$asset" '$1 == asset { print; exit }')"
    if [ -z "$row" ]; then
      echo "RELEASE MISSING: $asset" >&2
      failed=1
      continue
    fi
    actual_size="$(printf '%s\n' "$row" | awk -F '\t' '{print $2}')"
    actual_digest="$(printf '%s\n' "$row" | awk -F '\t' '{print $3}')"
    actual_sha="${actual_digest#sha256:}"
    if [ "$actual_sha" != "$expected_sha" ]; then
      echo "RELEASE HASH MISMATCH: $asset" >&2
      echo "  expected $expected_sha" >&2
      echo "  actual   $actual_sha" >&2
      failed=1
    fi
    if [ "$actual_size" != "$expected_size" ]; then
      echo "RELEASE SIZE MISMATCH: $asset expected $expected_size actual $actual_size" >&2
      failed=1
    fi
    if [ "$actual_sha" = "$expected_sha" ] && [ "$actual_size" = "$expected_size" ]; then
      echo "RELEASE OK: $asset $actual_sha"
    fi
  done < <(manifest_rows)
  return "$failed"
}

verify_local
if [ "$CHECK_RELEASE" = "1" ]; then
  verify_release
fi
