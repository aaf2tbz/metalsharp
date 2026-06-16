#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
RESULTS_DIR="${M12_GAME_LAB_RESULTS_DIR:-$SDK_DIR/results}"
BACKEND_URL="${METALSHARP_BACKEND_URL:-http://127.0.0.1:9277}"
SECONDS_TO_RUN="${M12_GAME_LAB_SECONDS:-45}"
LAUNCH_METHOD="${M12_GAME_LAB_LAUNCH_METHOD:-dxmt_metal12}"
PROFILE="${1:-}"
COMMAND="${2:-info}"

usage() {
  cat <<'USAGE'
Usage:
  m12-game-lab.sh PROFILE [command]

Profiles:
  subnautica2     Steam appid 1962700, /Volumes/AverySSD/.../Subnautica2
  elden-ring      Steam appid 1245620, /Volumes/AverySSD/.../ELDEN RING/Game

Commands:
  info            Print resolved paths and latest local artifacts.
  index           Index local M12 logs/shader sidecars.
  replay          Replay captured .dxbc corpus through metal-shaderconverter.
  stage           Stage captured corpus into the offline Metal validation lab.
  preflight       Runtime layout preflight for the profile game dir.
  capture         Bounded backend launch/capture using dxmt_metal12.
  offline         Run index + replay + stage without launching anything.

Environment:
  METALSHARP_BACKEND_URL       Backend URL. Defaults to http://127.0.0.1:9277.
  M12_GAME_LAB_RESULTS_DIR     Results dir. Defaults to tools/d3d12-metal-sdk/results.
  M12_GAME_LAB_SECONDS         Capture seconds. Defaults to 45.
  M12_GAME_LAB_LAUNCH_METHOD   Launch method. Defaults to dxmt_metal12.
USAGE
}

if [[ -z "$PROFILE" || "$PROFILE" == "-h" || "$PROFILE" == "--help" ]]; then
  usage
  exit 0
fi

case "$PROFILE" in
  subnautica2|subnautica-2)
    PROFILE="subnautica2"
    APPID="1962700"
    GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2"
    KILL_PATTERN='[S]ubnautica2-Win64-Shipping.exe|[S]ubnautica2.exe|[S]ubnautica2|[C]rashReportClient.exe|[c]rashpad_handler.exe'
    ;;
  elden-ring|eldenring)
    PROFILE="elden-ring"
    APPID="1245620"
    GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/ELDEN RING/Game"
    KILL_PATTERN='[e]ldenring.exe|[s]tart_protected_game.exe|[E]asyAntiCheat|[c]rashpad_handler.exe'
    ;;
  *)
    echo "unknown profile: $PROFILE" >&2
    usage >&2
    exit 2
    ;;
esac

CORPUS_DIR="$HOME/.metalsharp/shader-cache/m12/$APPID"
LOG_ROOT="$HOME/.metalsharp/logs/m12-pipeline/$APPID"
COMPAT_LOG_ROOT="$HOME/.metalsharp/compatdata/$APPID/logs"
mkdir -p "$RESULTS_DIR"

print_info() {
  cat <<INFO
profile=$PROFILE
appid=$APPID
game_dir=$GAME_DIR
corpus_dir=$CORPUS_DIR
log_root=$LOG_ROOT
compat_log_root=$COMPAT_LOG_ROOT
backend_url=$BACKEND_URL
launch_method=$LAUNCH_METHOD
seconds=$SECONDS_TO_RUN
kill_pattern=$KILL_PATTERN
INFO
  echo
  echo "latest artifacts:"
  find "$LOG_ROOT" "$COMPAT_LOG_ROOT" "$CORPUS_DIR" -maxdepth 2 -type f 2>/dev/null \
    | xargs -I{} stat -f '%Sm %z %N' -t '%Y-%m-%d %H:%M:%S' {} 2>/dev/null \
    | sort -r \
    | sed -n '1,40p' || true
}

run_index() {
  python3 "$SDK_DIR/scripts/index-m12-failures.py" \
    --appid "$APPID" \
    --profile "$PROFILE" \
    --results-dir "$RESULTS_DIR"
}

run_replay() {
  python3 "$SDK_DIR/scripts/replay-shader-corpus.py" \
    --corpus "$CORPUS_DIR" \
    --profile "$PROFILE" \
    --results-dir "$RESULTS_DIR" \
    --force \
    --allow-empty
}

run_stage() {
  python3 "$SDK_DIR/scripts/stage-game-metal-validation.py" \
    --corpus "$CORPUS_DIR" \
    --name "$PROFILE" \
    --allow-empty
}

run_preflight() {
  python3 "$SDK_DIR/scripts/preflight-runtime-layout.py" \
    --profile "$PROFILE" \
    --game-dir "$GAME_DIR" \
    --results-dir "$RESULTS_DIR"
}

run_capture() {
  "$SDK_DIR/scripts/capture-game-shader-corpus.sh" \
    --profile "$PROFILE" \
    --appid "$APPID" \
    --launch-method "$LAUNCH_METHOD" \
    --seconds "$SECONDS_TO_RUN" \
    --backend-url "$BACKEND_URL" \
    --game-dir "$GAME_DIR" \
    --corpus-dir "$CORPUS_DIR" \
    --results-dir "$RESULTS_DIR" \
    --kill-pattern "$KILL_PATTERN"
}

case "$COMMAND" in
  info) print_info ;;
  index) run_index ;;
  replay) run_replay ;;
  stage) run_stage ;;
  preflight) run_preflight ;;
  capture) run_capture ;;
  offline)
    run_index
    run_replay
    run_stage
    ;;
  *)
    echo "unknown command: $COMMAND" >&2
    usage >&2
    exit 2
    ;;
esac
