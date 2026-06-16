#!/usr/bin/env bash
set -euo pipefail

SNAPSHOT="${1:-/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-loaded-20260615-181806/runtime-dxmt_m12}"
RUNTIME_DIR="${METALSHARP_M12_RUNTIME_DIR:-$HOME/.metalsharp/runtime/wine/lib/dxmt_m12}"

if [[ ! -d "$SNAPSHOT" ]]; then
  echo "known-good M12 runtime snapshot not found: $SNAPSHOT" >&2
  exit 1
fi

mkdir -p "$RUNTIME_DIR"
rsync -a --delete "$SNAPSHOT/" "$RUNTIME_DIR/"

printf 'restored_from=%s\n' "$SNAPSHOT"
printf 'runtime_dir=%s\n' "$RUNTIME_DIR"
find "$RUNTIME_DIR" -type f \( -name '*.dll' -o -name '*.so' \) -print0 \
  | sort -z \
  | xargs -0 shasum -a 256
