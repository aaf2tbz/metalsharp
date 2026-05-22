#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/capture-steam-game-proof.sh <appid> <exe-match> [output-dir]

Examples:
  scripts/capture-steam-game-proof.sh 535520 Nidhogg_2 /tmp/metalsharp-proof-nidhogg2
  scripts/capture-steam-game-proof.sh 3164500 'Schedule I' /tmp/metalsharp-proof-schedule-i
  scripts/capture-steam-game-proof.sh 848450 SubnauticaZero /tmp/metalsharp-proof-subnautica-bz

Captures live and recent-launch proof for a Steam game: matching processes,
latest launch log, compatdata/bottle manifests, route cache listings, and lsof
for matching live PIDs. The script is read-only against METALSHARP_HOME, or
~/.metalsharp when METALSHARP_HOME is unset.

For Wine 11.9 parity proof, set METALSHARP_REQUIRE_PARITY_HOME=1. In that mode
METALSHARP_HOME must be explicit and must not be the caller's real ~/.metalsharp.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

appid="${1:-}"
exe_match="${2:-}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${3:-/tmp/metalsharp-steam-proof-${appid:-unknown}-$timestamp}"

if [[ -z "$appid" || -z "$exe_match" ]]; then
    usage >&2
    exit 1
fi

metalsharp_home="${METALSHARP_HOME:-$HOME/.metalsharp}"
if [[ -d "$metalsharp_home" ]]; then
    metalsharp_home="$(cd "$metalsharp_home" && pwd -P)"
fi

if [[ "${METALSHARP_REQUIRE_PARITY_HOME:-0}" == "1" ]]; then
    if [[ -z "${METALSHARP_HOME:-}" ]]; then
        echo "METALSHARP_REQUIRE_PARITY_HOME=1 requires explicit METALSHARP_HOME" >&2
        exit 1
    fi
    real_home_abs="$(cd "$HOME" && pwd -P)"
    real_ms_home="$real_home_abs/.metalsharp"
    if [[ "$metalsharp_home" == "$real_ms_home" ]]; then
        echo "refusing parity proof against real MetalSharp home: $metalsharp_home" >&2
        exit 1
    fi
fi
compat_dir="$metalsharp_home/compatdata/$appid"
bottle_dir="$metalsharp_home/bottles/steam_$appid"

mkdir -p "$out_dir"

copy_if_present() {
    local src="$1"
    local dst="$2"
    if [[ -e "$src" ]]; then
        cp "$src" "$dst"
    fi
}

ps -axo pid,ppid,etime,command > "$out_dir/all-processes.txt"
grep -iE "$exe_match|$appid|UnityCrash|Steam.exe|steam.exe|metalsharp-backend|wine" \
    "$out_dir/all-processes.txt" > "$out_dir/matching-processes.txt" || true

latest_log=""
if [[ -d "$compat_dir/logs" ]]; then
    latest_log="$(find "$compat_dir/logs" -name 'launch-*.log' -type f -print0 2>/dev/null \
        | xargs -0 ls -t 2>/dev/null \
        | head -1 || true)"
fi

if [[ -n "$latest_log" ]]; then
    copy_if_present "$latest_log" "$out_dir/$(basename "$latest_log")"
fi

copy_if_present "$compat_dir/metalsharp-compatdata.json" "$out_dir/metalsharp-compatdata.json"
copy_if_present "$bottle_dir/bottle.json" "$out_dir/bottle.json"

if [[ -d "$metalsharp_home/shader-cache" ]]; then
    find "$metalsharp_home/shader-cache" -path "*/$appid/*" -maxdepth 5 -print -exec ls -ld {} \; \
        > "$out_dir/shader-cache-files.txt" 2>/dev/null || true
fi

if [[ -d "$metalsharp_home/pipeline-cache" ]]; then
    find "$metalsharp_home/pipeline-cache" -path "*/$appid/*" -maxdepth 5 -print -exec ls -ld {} \; \
        > "$out_dir/pipeline-cache-files.txt" 2>/dev/null || true
fi

awk -v pat="$(printf '%s' "$exe_match" | tr '[:upper:]' '[:lower:]')" '
    {
        line = tolower($0)
        if (index(line, pat) > 0 &&
            index(line, "capture-steam-game-proof.sh") == 0 &&
            index(line, "/bin/zsh -lc") == 0 &&
            index(line, "/bin/bash") == 0 &&
            index(line, " rg ") == 0 &&
            index(line, " grep ") == 0) print $1
    }
' "$out_dir/all-processes.txt" > "$out_dir/game-pids.txt"

while read -r pid; do
    if [[ "$pid" =~ ^[0-9]+$ ]]; then
        lsof -p "$pid" > "$out_dir/lsof-$pid.txt" 2>&1 || true
    fi
done < "$out_dir/game-pids.txt"

{
    echo "appid=$appid"
    echo "exe_match=$exe_match"
    echo "captured_at_utc=$timestamp"
    echo "metalsharp_home=$metalsharp_home"
    echo "compat_dir=$compat_dir"
    echo "bottle_dir=$bottle_dir"
    echo "latest_launch_log=$latest_log"
    echo
    echo "[matching_processes]"
    cat "$out_dir/matching-processes.txt"
    echo
    echo "[game_pids]"
    if [[ -s "$out_dir/game-pids.txt" ]]; then
        cat "$out_dir/game-pids.txt"
    else
        echo "none; live lsof proof still required"
    fi
    echo
    echo "[launch_contract]"
    if [[ -n "$latest_log" ]]; then
        rg -n '^(appid|pipeline|prefix|metalsharp_home|host_runtime|wine_runtime|steam_identity_mode|cwd|exe|args|compatdata|compatdata_manifest)=' "$latest_log" || true
        rg -n 'DXMT|dxmt|winemetal|MoltenVK|feature level|DirectX|shader|swap|mscompatdb|KeService|Steam' "$latest_log" || true
    else
        echo "no launch log found"
    fi
    echo
    echo "[loaded_route_files]"
    for lsof_file in "$out_dir"/lsof-*.txt; do
        [[ -f "$lsof_file" ]] || continue
        echo "== $(basename "$lsof_file") =="
        rg -n 'd3d|dxgi|winemetal|MoltenVK|shader-cache|pipeline-cache|com\.apple\.metal|Player\.log|Steam|Subnautica|Nidhogg|Schedule' "$lsof_file" || true
    done
    echo
    echo "[shader_cache_files]"
    if [[ -f "$out_dir/shader-cache-files.txt" ]]; then
        cat "$out_dir/shader-cache-files.txt"
    else
        echo "no shader cache listing found"
    fi
    echo
    echo "[pipeline_cache_files]"
    if [[ -f "$out_dir/pipeline-cache-files.txt" ]]; then
        cat "$out_dir/pipeline-cache-files.txt"
    else
        echo "no pipeline cache listing found"
    fi
} > "$out_dir/summary.txt"

echo "steam game proof written: $out_dir"
echo "summary: $out_dir/summary.txt"
