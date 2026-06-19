#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$ROOT/tools/d3d12-metal-sdk/probes/probe_m12_metallib_load/probe_m12_metallib_load.m"
STAGED_RUNTIME_DIR="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix"
DEFAULT_LIB="$STAGED_RUNTIME_DIR/libm12core.dylib"
if [[ ! -f "$DEFAULT_LIB" ]]; then
  DEFAULT_LIB="$ROOT/vendor/dxmt/build-metalsharp-x64/src/m12core/libm12core.dylib"
fi
LIB="${M12CORE_DYLIB:-$DEFAULT_LIB}"
INCLUDE="$ROOT/vendor/dxmt/src/m12core"
TOOLCHAIN_LIB="${M12_X86_64_TOOLCHAIN_LIB:-$HOME/.metalsharp/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0/lib}"
OUT_DIR="${M12_PROBE_OUT_DIR:-$ROOT/tools/d3d12-metal-sdk/results/probe-m12-metallib-load-$(date +%Y%m%d-%H%M%S)}"
BIN="$OUT_DIR/probe_m12_metallib_load"

usage() {
  cat >&2 <<'USAGE'
usage: probe-m12-metallib-load.sh --cache-dir DIR (--hash HASH | --hash-file FILE)

Builds and runs an x86_64 native libm12core metallib-load probe. Does not launch games.
USAGE
}

ARGS=()
if [[ $# -eq 0 ]]; then usage; exit 2; fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cache-dir|--hash|--hash-file) ARGS+=("$1" "$2"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

mkdir -p "$OUT_DIR"
if [[ ! -f "$SRC" ]]; then echo "missing source: $SRC" >&2; exit 1; fi
if [[ ! -f "$LIB" ]]; then echo "missing libm12core: $LIB" >&2; exit 1; fi

LIB_DIR="$(dirname "$LIB")"
clang -arch x86_64 -mmacosx-version-min=12.0 \
  -I"$INCLUDE" \
  "$SRC" "$LIB" \
  -framework Foundation -framework Metal \
  -Wl,-rpath,"$LIB_DIR" \
  -o "$BIN"

{
  echo "root=$ROOT"
  echo "source=$SRC"
  echo "lib=$LIB"
  echo "bin=$BIN"
  echo "toolchain_lib=$TOOLCHAIN_LIB"
  echo "toolchain_lib_runtime_used=0"
  echo "args=${ARGS[*]}"
  file "$BIN" "$LIB"
  otool -L "$BIN"
  otool -L "$LIB"
} > "$OUT_DIR/build-info.txt"

# Do not add the raw LLVM toolchain lib directory to DYLD_LIBRARY_PATH here.
# Apple Metal's x86_64 framework path can load C++ support internally, and the
# LLVM 15 toolchain libc++/libunwind interposes badly enough to crash
# MTLCreateSystemDefaultDevice() before libm12core is called.  The staged M12
# runtime directory is safe because it carries the exact dylibs the Wine-side
# runtime uses, colocated with libm12core via @loader_path/@rpath.
DYLD_LIBRARY_PATH="$LIB_DIR:${DYLD_LIBRARY_PATH:-}" "$BIN" "${ARGS[@]}" | tee "$OUT_DIR/probe-output.txt"
