#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WINE_SRC="${1:-${WINE_SRC:-}}"

if [[ -z "$WINE_SRC" ]]; then
  echo "usage: $0 /path/to/wine-src" >&2
  exit 2
fi

if [[ ! -d "$WINE_SRC/dlls/winemac.drv" || ! -d "$WINE_SRC/dlls/ntdll" ]]; then
  echo "error: '$WINE_SRC' does not look like a Wine checkout" >&2
  exit 1
fi

for patch in "$ROOT"/tools/wine/wine-patches/*.patch; do
  echo "applying $(basename "$patch")"
  if git -C "$WINE_SRC" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$WINE_SRC" apply --check "$patch"
    git -C "$WINE_SRC" apply "$patch"
  else
    patch -d "$WINE_SRC" -p1 --dry-run < "$patch" >/dev/null
    patch -d "$WINE_SRC" -p1 < "$patch"
  fi
done
