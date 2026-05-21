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

mapfile -t patches < <(find "$ROOT/tools/wine/patches" -maxdepth 1 -type f -name '*.patch' | sort)

if [[ "${#patches[@]}" -eq 0 ]]; then
  echo "error: no DXMT patches found under $ROOT/tools/wine/patches" >&2
  exit 1
fi

for patch in "${patches[@]}"; do
  echo "$MODE $(basename "$patch")"
  git -C "$DXMT_SRC" apply --check "$patch"
  if [[ "$MODE" == "apply" ]]; then
    git -C "$DXMT_SRC" apply "$patch"
  fi
done
