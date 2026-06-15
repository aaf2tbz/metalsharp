#!/usr/bin/env bash
set -euo pipefail

SDK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "$SDK_DIR/../.." && pwd)"
PROFILE="${M12_DEV_PROFILE:-metalsharp}"
RESULTS_DIR="${M12_DEV_RESULTS_DIR:-$SDK_DIR/results}"
STAGE_ROOT="${M12_DEV_STAGE_ROOT:-/Volumes/AverySSD/MetalSharp-M12-CorpusLab}"
M12_RUNTIME_DIR="${M12_DEV_RUNTIME_DIR:-$HOME/.metalsharp/runtime/wine/lib/dxmt-m12}"

usage() {
  cat <<'USAGE'
Usage:
  m12-dev.sh <command> [args...]

Developer-first M12 entrypoints:
  build-runtime     Rebuild the DXMT x86_64 D3D12 runtime used by M12.
  stage-runtime     Stage rebuilt DXMT DLLs/Unix sidecars into the SDK runtime layout.
  preflight         Validate contracts, runtime layout, and shader-engine structure.
  mini              Run focused one-purpose D3D12 mini probes.
  probes            Run the full required D3D12 SDK probe matrix.
  pipeline-contract Validate the M12 launch/logging contract against the tree.
  shader-engine     Validate the M12 shader-engine contract.
  shader-lab        Stage captured game shader corpora on AverySSD and Metal-compile them.
  stress-game       Build/stage/run the 15-second DX12-only M12 stress game.
  full-offline      Run build, stage, contracts, layout, probes, compare, and shader-engine gates.
  m12-check         Run the PR M12 cube/runtime CI gate from a full repository checkout.
  sdk-bundle        Build and verify the self-contained developer SDK archive.

Examples:
  tools/d3d12-metal-sdk/scripts/m12-dev.sh full-offline
  tools/d3d12-metal-sdk/scripts/m12-dev.sh mini
  tools/d3d12-metal-sdk/scripts/m12-dev.sh shader-lab -- \
    --corpus "$HOME/.metalsharp/tmp/subnautica2_m12_offline/sm6-20260612-225622/shader-cache" \
    --name subnautica2-sm6

Environment:
  M12_DEV_PROFILE       Result/runtime profile. Defaults to metalsharp.
  M12_DEV_RESULTS_DIR   Result directory. Defaults to tools/d3d12-metal-sdk/results.
  M12_DEV_STAGE_ROOT    Shader-lab stage root. Defaults to /Volumes/AverySSD/MetalSharp-M12-CorpusLab.
USAGE
}

need_repo_tool() {
  local path="$1"
  if [[ ! -x "$path" && ! -f "$path" ]]; then
    echo "Missing repository tool: $path" >&2
    echo "This command requires a full MetalSharp repository checkout, not only the packaged SDK." >&2
    exit 1
  fi
}

run_build_runtime() {
  "$SDK_DIR/scripts/prepare-dxmt-x86-llvm15.sh" "$@"
}

run_stage_runtime() {
  python3 "$SDK_DIR/scripts/stage-dxmt-runtime.py" \
    --profile "$PROFILE" \
    --results-dir "$RESULTS_DIR" \
    --runtime-dir "$M12_RUNTIME_DIR" \
    "$@"
}

run_contracts() {
  python3 "$SDK_DIR/scripts/validate-contracts.py" "$@"
}

run_layout() {
  python3 "$SDK_DIR/scripts/preflight-runtime-layout.py" \
    --profile "$PROFILE" \
    --results-dir "$RESULTS_DIR" \
    --dxmt-runtime "$M12_RUNTIME_DIR" \
    "$@"
}

run_shader_engine() {
  python3 "$SDK_DIR/scripts/validate-shader-engine.py" \
    --json "$RESULTS_DIR/shader-engine-audit-$PROFILE.json" \
    "$@"
}

run_pipeline_contract() {
  python3 "$SDK_DIR/scripts/validate-m12-pipeline-contract.py" \
    --json "$RESULTS_DIR/m12-pipeline-contract-audit-$PROFILE.json" \
    "$@"
}

run_compare() {
  python3 "$SDK_DIR/scripts/compare-contract.py" \
    --profile "$PROFILE" \
    --results-dir "$RESULTS_DIR" \
    "$@"
}

run_probe_matrix_validator() {
  python3 "$SDK_DIR/scripts/validate-probe-matrix.py" "$@"
}

run_probes() {
  "$SDK_DIR/scripts/run-probes.sh" \
    --profile "$PROFILE" \
    --results-dir "$RESULTS_DIR" \
    "$@"
}

run_shader_lab() {
  "$SDK_DIR/scripts/stage-game-metal-validation.py" \
    --stage-root "$STAGE_ROOT" \
    "$@"
}

run_sdk_bundle() {
  need_repo_tool "$PROJECT_ROOT/tools/bundles/create-developer-sdk.py"
  python3 "$PROJECT_ROOT/tools/bundles/create-developer-sdk.py" "$@"
  "$PROJECT_ROOT/tools/bundles/verify-developer-sdk.sh"
}

run_stress_game() {
  local build_dir="${DXMT_BUILD_DIR:-$PROJECT_ROOT/vendor/dxmt/build-metalsharp-x64}"
  need_repo_tool "$PROJECT_ROOT/vendor/dxmt/tests/d3d12_game/run_m12_game.sh"
  meson compile -C "$build_dir" m12_stress_game
  M12_GAME_TIMEOUT="${M12_GAME_TIMEOUT:-90}" \
    "$PROJECT_ROOT/vendor/dxmt/tests/d3d12_game/run_m12_game.sh" \
    "$build_dir" \
    --exe m12_stress_game \
    --seconds "${M12_STRESS_SECONDS:-15}" \
    "$@"
}

command="${1:-help}"
if [[ $# -gt 0 ]]; then
  shift
fi
if [[ "${1:-}" == "--" ]]; then
  shift
fi

case "$command" in
  help|-h|--help)
    usage
    ;;
  build-runtime)
    run_build_runtime "$@"
    ;;
  stage-runtime)
    run_stage_runtime "$@"
    ;;
  preflight)
    run_contracts
    run_pipeline_contract
    run_layout "$@"
    run_shader_engine
    ;;
  mini)
    run_probes --mini-only "$@"
    ;;
  probes)
    run_probes "$@"
    ;;
  shader-engine)
    run_shader_engine "$@"
    ;;
  pipeline-contract)
    run_pipeline_contract "$@"
    ;;
  shader-lab)
    run_shader_lab "$@"
    ;;
  stress-game)
    run_stress_game "$@"
    ;;
  full-offline)
    run_build_runtime
    run_stage_runtime
    run_contracts
    run_pipeline_contract
    run_layout
    run_probes "$@"
    run_compare
    run_probe_matrix_validator
    run_shader_engine
    ;;
  m12-check)
    need_repo_tool "$PROJECT_ROOT/tools/ci/m12-check.sh"
    "$PROJECT_ROOT/tools/ci/m12-check.sh" "$@"
    ;;
  sdk-bundle)
    run_sdk_bundle "$@"
    ;;
  *)
    echo "Unknown m12-dev command: $command" >&2
    usage >&2
    exit 2
    ;;
esac
