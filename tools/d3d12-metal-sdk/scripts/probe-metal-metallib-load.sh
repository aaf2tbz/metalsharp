#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$ROOT/tools/d3d12-metal-sdk/probes/probe_metal_metallib_load/probe_metal_metallib_load.m"
OUT_DIR="${METAL_PROBE_OUT_DIR:-$ROOT/tools/d3d12-metal-sdk/results/probe-metal-metallib-load-$(date +%Y%m%d-%H%M%S)}"
BIN="$OUT_DIR/probe_metal_metallib_load"
ARGS=()
if [[ $# -eq 0 ]]; then echo "usage: $0 --cache-dir DIR (--hash HASH | --hash-file FILE)" >&2; exit 2; fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cache-dir|--hash|--hash-file) ARGS+=("$1" "$2"); shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done
mkdir -p "$OUT_DIR"
clang -arch x86_64 -mmacosx-version-min=12.0 "$SRC" -framework Foundation -framework Metal -o "$BIN"
{
  echo "root=$ROOT"
  echo "source=$SRC"
  echo "bin=$BIN"
  echo "args=${ARGS[*]}"
  file "$BIN"
} > "$OUT_DIR/build-info.txt"
"$BIN" "${ARGS[@]}" | tee "$OUT_DIR/probe-output.txt"
