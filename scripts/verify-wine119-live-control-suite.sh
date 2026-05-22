#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/verify-wine119-live-control-suite.sh <live-control-output-dir>

Example:
  scripts/verify-wine119-live-control-suite.sh /tmp/metalsharp-live-controls-dxmt32

Verifies a directory produced by scripts/run-wine119-live-control-suite.sh.
This is intentionally strict: log-only captures, missing live PIDs, failed
launch JSON, wrong route, or missing loaded DXMT/WineMetal/cache evidence fail
the gate.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

suite_dir="${1:-}"

if [[ -z "$suite_dir" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -d "$suite_dir" ]]; then
    echo "live-control output dir not found: $suite_dir" >&2
    exit 1
fi

report="$suite_dir/verification.md"
failures=0

append() {
    printf '%s\n' "$*" >> "$report"
}

fail() {
    local label="$1"
    local message="$2"
    failures=$((failures + 1))
    append "- FAIL [$label] $message"
}

pass() {
    local label="$1"
    local message="$2"
    append "- PASS [$label] $message"
}

contains() {
    local file="$1"
    local pattern="$2"
    [[ -f "$file" ]] && rg -qi "$pattern" "$file"
}

require_contains() {
    local label="$1"
    local file="$2"
    local pattern="$3"
    local message="$4"
    if contains "$file" "$pattern"; then
        pass "$label" "$message"
    else
        fail "$label" "$message (pattern: $pattern)"
    fi
}

verify_steam_survives() {
    local label="$1"
    local before="$suite_dir/steam/before-$label-wine-steam-pids.txt"
    local after="$suite_dir/steam/after-$label-wine-steam-pids.txt"

    if [[ ! -f "$before" || ! -f "$after" ]]; then
        fail "$label" "missing Wine Steam PID snapshots for attach/relaunch proof"
        return
    fi

    if [[ ! -s "$before" ]]; then
        pass "$label" "no pre-existing Wine Steam PID before launch; attach survival gate not applicable"
        return
    fi

    local missing=()
    local pid
    while read -r pid; do
        [[ -n "$pid" ]] || continue
        if ! grep -qx "$pid" "$after"; then
            missing+=("$pid")
        fi
    done < "$before"

    if (( ${#missing[@]} == 0 )); then
        pass "$label" "pre-existing Wine Steam PID(s) survived launch: $(tr '\n' ' ' < "$before" | sed 's/[[:space:]]*$//')"
    else
        fail "$label" "pre-existing Wine Steam PID(s) disappeared after launch: ${missing[*]}"
    fi
}

verify_game() {
    local label="$1"
    local expected_pipeline="$2"
    local appid="$3"
    shift 3
    local patterns=("$@")
    local proof_dir="$suite_dir/proof/$label"
    local summary="$proof_dir/summary.txt"
    local pids="$proof_dir/game-pids.txt"
    local launch_json="$suite_dir/launches/$label-launch.json"

    append
    append "## $label"
    append

    if [[ -f "$launch_json" ]]; then
        require_contains "$label" "$launch_json" '"ok"[[:space:]]*:[[:space:]]*true' "launch JSON reports ok=true"
        require_contains "$label" "$launch_json" "\"pipeline\"[[:space:]]*:[[:space:]]*\"$expected_pipeline\"" "launch JSON reports pipeline=$expected_pipeline"
    else
        fail "$label" "missing launch JSON: $launch_json"
    fi

    if [[ -f "$summary" ]]; then
        pass "$label" "proof summary exists"
    else
        fail "$label" "missing proof summary: $summary"
        return
    fi

    if [[ -s "$pids" ]]; then
        pass "$label" "live game PID captured: $(tr '\n' ' ' < "$pids" | sed 's/[[:space:]]*$//')"
    else
        fail "$label" "missing live game PID; log-only proof is not a release gate"
    fi

    local expected_pipeline_upper
    expected_pipeline_upper="$(printf '%s' "$expected_pipeline" | tr '[:lower:]' '[:upper:]')"
    require_contains "$label" "$summary" "pipeline[=:]${expected_pipeline}|pipeline[=:]${expected_pipeline_upper}" "proof summary includes expected pipeline"
    require_contains "$label" "$summary" "$appid" "proof summary includes appid $appid"
    verify_steam_survives "$label"

    for pattern in "${patterns[@]}"; do
        require_contains "$label" "$summary" "$pattern" "proof summary includes $pattern"
    done
}

: > "$report"
append "# MetalSharp Wine 11.9 Live Control Verification"
append
append "- suite_dir: \`$suite_dir\`"
append

if [[ -f "$suite_dir/runtime-identity.txt" ]]; then
    require_contains "runtime" "$suite_dir/runtime-identity.txt" "wine_version=wine-11\\.9" "runtime identity is Wine 11.9"
    require_contains "runtime" "$suite_dir/runtime-identity.txt" "wineserver_version=Wine 11\\.9" "wineserver identity is Wine 11.9"
else
    fail "runtime" "missing runtime-identity.txt"
fi

if [[ -f "$suite_dir/status.json" ]]; then
    require_contains "backend" "$suite_dir/status.json" '"ok"[[:space:]]*:[[:space:]]*true' "backend status ok=true"
    require_contains "backend" "$suite_dir/status.json" '"version"[[:space:]]*:[[:space:]]*"0[.]33[.]27"' "backend status is current main version 0.33.27"
else
    fail "backend" "missing status.json"
fi

verify_game \
    "nidhogg2" \
    "m9" \
    "535520" \
    "i386-windows.*/winemetal\\.dll|i386-windows/winemetal\\.dll" \
    "i386-windows.*/dxgi\\.dll|i386-windows/dxgi\\.dll" \
    "i386-windows.*/d3d11\\.dll|i386-windows/d3d11\\.dll" \
    "x86_64-unix.*/winemetal\\.so|x86_64-unix/winemetal\\.so" \
    "shader-cache/m9/535520"

verify_game \
    "schedule-i" \
    "m11" \
    "3164500" \
    "x86_64-unix.*/winemetal\\.so|x86_64-unix/winemetal\\.so" \
    "libMoltenVK\\.1\\.dylib|MoltenVK" \
    "shader-cache/m11/3164500"

verify_game \
    "subnautica-bz" \
    "m11" \
    "848450" \
    "x86_64-windows.*/d3d11\\.dll|x86_64-windows/d3d11\\.dll" \
    "x86_64-windows.*/dxgi\\.dll|x86_64-windows/dxgi\\.dll" \
    "x86_64-windows.*/winemetal\\.dll|x86_64-windows/winemetal\\.dll" \
    "x86_64-unix.*/winemetal\\.so|x86_64-unix/winemetal\\.so" \
    "libMoltenVK\\.1\\.dylib|MoltenVK" \
    "shader-cache/m11/848450"

append
append "## Summary"
append
append "- failures: $failures"
if [[ "$failures" -eq 0 ]]; then
    append "- gate: pass"
    echo "live control verification passed: $report"
else
    append "- gate: fail"
    echo "live control verification failed: $report"
    exit 2
fi
