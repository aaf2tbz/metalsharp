#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
build_dir="$repo_root/vendor/dxmt/build-metalsharp-x64-tests"
if [[ $# -gt 0 && "$1" != --* ]]; then
  build_dir="$1"
  shift
fi
wine_root="${WINE_ROOT:-$HOME/.metalsharp/runtime/wine}"
wine_bin="${WINE_BIN:-}"
if [[ -z "$wine_bin" ]]; then
  if [[ -x "$wine_root/bin/metalsharp-wine" ]]; then
    wine_bin="$wine_root/bin/metalsharp-wine"
  else
    wine_bin="$wine_root/bin/wine"
  fi
fi
run_root="${M12_GAME_RUN_ROOT:-$HOME/.metalsharp/tmp/m12_game_run}"
prefix="${M12_GAME_WINEPREFIX:-$HOME/.metalsharp/tmp/m12_game_prefix}"
loops="${M12_GAME_LOOPS:-1}"
timeout_seconds="${M12_GAME_TIMEOUT:-45}"

mkdir -p "$run_root" "$run_root/unix" "$(dirname "$prefix")"

cp "$build_dir/tests/d3d12_game/m12_game.exe" "$run_root/"
cp "$build_dir/src/d3d12/d3d12.dll" "$run_root/"
cp "$build_dir/src/dxgi/dxgi.dll" "$run_root/"
cp "$build_dir/src/dxgi/dxgi_dxmt.dll" "$run_root/"
if [[ -f "$build_dir/src/winemetal/winemetal.dll" ]]; then
  cp "$build_dir/src/winemetal/winemetal.dll" "$run_root/"
else
  cp "$wine_root/lib/dxmt/x86_64-windows/winemetal.dll" "$run_root/"
fi
cp "$wine_root/lib/wine/x86_64-windows/d3dcompiler_47.dll" "$run_root/"
if [[ -f "$wine_root/lib/metalsharp/x86_64-windows/metalsharp_ntdll_hook.dll" ]]; then
  cp "$wine_root/lib/metalsharp/x86_64-windows/metalsharp_ntdll_hook.dll" "$run_root/"
fi
if [[ -f "$build_dir/src/winemetal/unix/winemetal.so" ]]; then
  cp "$build_dir/src/winemetal/unix/winemetal.so" "$run_root/unix/"
else
  cp "$wine_root/lib/dxmt/x86_64-unix/winemetal.so" "$run_root/unix/"
fi

cd "$run_root"
rm -f m12_game.log /tmp/winemetal_debug.log /tmp/winemetal_pe_debug.log

export WINEPREFIX="$prefix"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d12,dxgi,dxgi_dxmt,winemetal=n,b}"
export DXMT_WINEMETAL_DEBUG="${DXMT_WINEMETAL_DEBUG:-1}"
export DXMT_WINEMETAL_UNIXLIB="${DXMT_WINEMETAL_UNIXLIB:-winemetal.so}"
export DXMT_D3D12_TRACE="${DXMT_D3D12_TRACE:-1}"
export DXMT_DXGI_TRACE="${DXMT_DXGI_TRACE:-1}"
export DXMT_SHADER_CACHE_PATH="${DXMT_SHADER_CACHE_PATH:-$run_root/shader-cache}"
export DYLD_LIBRARY_PATH="/Volumes/AverySSD/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0/lib:$wine_root/lib/wine/x86_64-unix:$run_root/unix:${DYLD_LIBRARY_PATH:-}"
export DYLD_FALLBACK_LIBRARY_PATH="$wine_root/lib/wine/x86_64-unix:$run_root/unix:${DYLD_FALLBACK_LIBRARY_PATH:-}"

/usr/bin/perl -e 'alarm shift; exec @ARGV' "$timeout_seconds" \
  "$wine_bin" ./m12_game.exe --loops "$loops" "$@" > "$run_root/m12_game.log" 2>&1
