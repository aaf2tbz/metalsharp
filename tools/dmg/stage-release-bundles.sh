#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="${1:-$PROJECT_ROOT/app/bundles}"
STAGE_DIR="${2:-$PROJECT_ROOT/dist/staged-bundles}"
MANIFEST="$PROJECT_ROOT/tools/bundles/asset-manifest.tsv"
M12_HASH_MANIFEST="$PROJECT_ROOT/tools/bundles/m12-dxmt-runtime-hashes.tsv"

verify_staged_dxmt_m12() {
  local root="$STAGE_DIR/Graphics/dll/dxmt-m12"
  if [ ! -d "$root/x86_64-windows" ] || [ ! -d "$root/x86_64-unix" ]; then
    echo "Staged graphics bundle is missing Graphics/dll/dxmt-m12 runtime lanes" >&2
    exit 1
  fi

  local failed=0
  while IFS=$'\t' read -r rel expected; do
    case "$rel" in
      ""|"#"*|path) continue ;;
    esac

    local path="$root/$rel"
    if [ ! -s "$path" ]; then
      echo "Staged dxmt-m12 runtime missing: Graphics/dll/dxmt-m12/$rel" >&2
      failed=1
      continue
    fi

    local actual
    actual="$(shasum -a 256 "$path" | awk '{print $1}')"
    if [ "$actual" != "$expected" ]; then
      echo "Staged dxmt-m12 hash mismatch: Graphics/dll/dxmt-m12/$rel expected=$expected actual=$actual" >&2
      failed=1
    fi
  done < "$M12_HASH_MANIFEST"

  if [ "$failed" -ne 0 ]; then
    exit 1
  fi
  echo "VERIFIED: staged Graphics/dll/dxmt-m12 matches $M12_HASH_MANIFEST"
}

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

while IFS=$'\t' read -r asset root platforms _notes; do
  case "$asset" in
    ""|\#*) continue ;;
  esac

  case ",$platforms," in
    *,mac,*) ;;
    *) continue ;;
  esac

  archive="$BUNDLE_DIR/$asset"
  if [ ! -s "$archive" ]; then
    echo "Missing bundle archive: $archive" >&2
    exit 1
  fi

  tar --use-compress-program=unzstd -xf "$archive" -C "$STAGE_DIR"
  if [ ! -e "$STAGE_DIR/$root" ]; then
    echo "Bundle $asset did not stage expected root $root/" >&2
    exit 1
  fi
  echo "STAGED: $asset -> $root/"
done < "$MANIFEST"

verify_staged_dxmt_m12
