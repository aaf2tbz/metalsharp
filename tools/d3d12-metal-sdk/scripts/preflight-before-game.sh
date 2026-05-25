#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="metalsharp"
RESULTS_DIR="$SDK_DIR/results"
RUN_PROBES=1
ALLOW_EMPTY_CORPUS=0
GAME_DIR=""
CORPUS_ARGS=()

usage() {
  cat <<'USAGE'
Usage:
  preflight-before-game.sh [options]

Options:
  --profile NAME          Result profile name. Defaults to metalsharp.
  --results-dir PATH      Result output directory.
  --game-dir PATH         Optional game Win64 directory containing staged DLLs.
  --corpus PATH           Shader corpus directory. Can be repeated.
  --allow-empty-corpus    Do not fail when no .dxbc corpus exists.
  --no-probes             Skip existing Wine/D3D12 SDK probes.
  -h, --help              Show this help.

This command does not launch Steam or a game. It gates game launches on:
  1. Winemetal route/export safety.
  2. Existing D3D12 Wine probes.
  3. Offline MetalShaderConverter replay of dumped .dxbc shaders.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      PROFILE="$2"
      shift 2
      ;;
    --results-dir)
      RESULTS_DIR="$2"
      shift 2
      ;;
    --game-dir)
      GAME_DIR="$2"
      shift 2
      ;;
    --corpus)
      CORPUS_ARGS+=(--corpus "$2")
      shift 2
      ;;
    --allow-empty-corpus)
      ALLOW_EMPTY_CORPUS=1
      shift
      ;;
    --no-probes)
      RUN_PROBES=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

mkdir -p "$RESULTS_DIR"

RUNTIME_ARGS=(--profile "$PROFILE" --results-dir "$RESULTS_DIR")
if [[ -n "$GAME_DIR" ]]; then
  RUNTIME_ARGS+=(--game-dir "$GAME_DIR")
fi
python3 "$SDK_DIR/scripts/preflight-runtime-layout.py" "${RUNTIME_ARGS[@]}"

if [[ "$RUN_PROBES" == "1" ]]; then
  "$SDK_DIR/scripts/run-probes.sh" \
    --profile metalsharp \
    --results-dir "$RESULTS_DIR" \
    --no-windowed-present
fi

REPLAY_ARGS=(--profile "$PROFILE" --results-dir "$RESULTS_DIR")
if [[ "$ALLOW_EMPTY_CORPUS" == "1" ]]; then
  REPLAY_ARGS+=(--allow-empty)
fi
if [[ "${#CORPUS_ARGS[@]}" -gt 0 ]]; then
  python3 "$SDK_DIR/scripts/replay-shader-corpus.py" "${REPLAY_ARGS[@]}" "${CORPUS_ARGS[@]}"
else
  python3 "$SDK_DIR/scripts/replay-shader-corpus.py" "${REPLAY_ARGS[@]}"
fi

echo "D3D12 Metal SDK preflight passed for profile: $PROFILE"
