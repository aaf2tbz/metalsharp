#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DXMT_SRC="${1:-${DXMT_SRC:-}}"

if [[ -z "$DXMT_SRC" ]]; then
  echo "usage: $0 /path/to/dxmt-src" >&2
  exit 2
fi

if [[ ! -d "$DXMT_SRC/src/dxgi" ]]; then
  echo "error: '$DXMT_SRC' does not look like a DXMT checkout" >&2
  exit 1
fi

for patch in "$ROOT"/tools/wine/patches/*.patch; do
  echo "applying $(basename "$patch")"
  git -C "$DXMT_SRC" apply --check "$patch"
  git -C "$DXMT_SRC" apply "$patch"
done
