#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="apply"

case "${1:-}" in
  --check|--dry-run)
    MODE="check"
    shift
    ;;
  --help|-h)
    echo "usage: $0 [--check] /path/to/dxmt-src" >&2
    exit 0
    ;;
esac

DXMT_SRC="${1:-${DXMT_SRC:-}}"

if [[ -z "$DXMT_SRC" ]]; then
  echo "usage: $0 [--check] /path/to/dxmt-src" >&2
  exit 2
fi

if [[ ! -d "$DXMT_SRC/src/dxgi" ]]; then
  echo "error: '$DXMT_SRC' does not look like a DXMT checkout" >&2
  exit 1
fi

required_files=(
  "src/d3d12/d3d12_trace.hpp"
)

for required in "${required_files[@]}"; do
  if [[ ! -f "$DXMT_SRC/$required" ]]; then
    cat >&2 <<EOF
error: DXMT checkout is missing '$required'
MetalSharp's D3D12 trace-file patch is layered on the diagnostics-bearing DXMT
runtime source. Use a DXMT checkout that already contains the D3D12 trace helper
before applying tools/wine/patches.
EOF
    exit 1
  fi
done

patches=()
while IFS= read -r patch; do
  patches+=("$patch")
done < <(find "$ROOT/tools/wine/patches" -maxdepth 1 -type f -name '*.patch' | sort)

if [[ "${#patches[@]}" -eq 0 ]]; then
  echo "error: no DXMT patches found under $ROOT/tools/wine/patches" >&2
  exit 1
fi

for patch in "${patches[@]}"; do
  echo "$MODE $(basename "$patch")"
  if git -C "$DXMT_SRC" apply --check "$patch" 2>/dev/null; then
    if [[ "$MODE" == "apply" ]]; then
      git -C "$DXMT_SRC" apply "$patch"
    fi
  elif git -C "$DXMT_SRC" apply --reverse --check "$patch" 2>/dev/null; then
    echo "already applied $(basename "$patch")"
  else
    git -C "$DXMT_SRC" apply --check "$patch"
    echo "error: patch does not apply cleanly: $(basename "$patch")" >&2
    exit 1
  fi
done
