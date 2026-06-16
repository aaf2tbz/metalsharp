#!/usr/bin/env bash
set -euo pipefail

SNAPSHOT="/Volumes/AverySSD/MetalSharp-M12-Preserved/working-current-source-runtime-elden-ring-20260615-220308"
RUNTIME_DIR="${METALSHARP_M12_RUNTIME_DIR:-$HOME/.metalsharp/runtime/wine/lib/dxmt_m12}"
RESTORE_GAMES=()

usage() {
  cat <<'USAGE'
Usage:
  restore-known-working-current-m12-runtime.sh [snapshot] [--game NAME ...]

Restores the frozen working current-source M12 runtime and optionally normalizes
game-local deployed DLLs from the restored runtime.

Games:
  elden-ring, subnautica-2, schedule-1, peak
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --game) RESTORE_GAMES+=("$2"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    /*) SNAPSHOT="$1"; shift ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ! -d "$SNAPSHOT/runtime-dxmt_m12" ]]; then
  echo "snapshot runtime missing: $SNAPSHOT/runtime-dxmt_m12" >&2
  exit 1
fi

mkdir -p "$RUNTIME_DIR"
rsync -a --delete "$SNAPSHOT/runtime-dxmt_m12/" "$RUNTIME_DIR/"

windows_dir="$RUNTIME_DIR/x86_64-windows"
restore_game() {
  local game="$1" dir=""
  case "$game" in
    elden-ring|eldenring) dir="/Volumes/AverySSD/SteamLibrary/steamapps/common/ELDEN RING/Game" ;;
    subnautica-2|subnautica2) dir="/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2" ;;
    schedule-1|schedule1|schedule-i|schedulei) dir="/Volumes/AverySSD/SteamLibrary/steamapps/common/Schedule I" ;;
    peak) dir="/Volumes/AverySSD/SteamLibrary/steamapps/common/PEAK" ;;
    *) echo "unknown game: $game" >&2; return 2 ;;
  esac
  if [[ ! -d "$dir" ]]; then
    echo "game dir missing for $game: $dir" >&2
    return 1
  fi
  for dll in d3d12.dll d3d11.dll dxgi.dll dxgi_dxmt.dll d3d10core.dll winemetal.dll nvapi64.dll nvngx.dll; do
    [[ -f "$windows_dir/$dll" ]] && cp "$windows_dir/$dll" "$dir/$dll"
  done
  echo "restored_game=$game"
  echo "game_dir=$dir"
}

for game in "${RESTORE_GAMES[@]}"; do
  restore_game "$game"
done

printf 'restored_snapshot=%s\n' "$SNAPSHOT"
printf 'runtime_dir=%s\n' "$RUNTIME_DIR"
verify_args=(--runtime-dir "$windows_dir" --strict)
for game in "${RESTORE_GAMES[@]}"; do
  verify_args+=(--game "$game")
done
"$(dirname "$0")/verify-m12-runtime-hashes.py" "${verify_args[@]}" >/dev/null
shasum -a 256 "$windows_dir/d3d12.dll"
