#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  METALSHARP_RUN_LIVE_GAMES=1 scripts/run-wine119-live-control-suite.sh <parity-home> [output-dir]

Example:
  METALSHARP_RUN_LIVE_GAMES=1 scripts/run-wine119-live-control-suite.sh \
    /tmp/metalsharp-home-wine119-dxmt32 \
    /tmp/metalsharp-live-controls-dxmt32

Starts metalsharp-backend with HOME=<parity-home>, launches the three Wine 11.9
control games through /steam/launch-game, waits briefly, captures live process
proof, then stops the backend.

Hard safety guard:
  METALSHARP_RUN_LIVE_GAMES must be set to 1.

The parity home should be prepared with copied user state:
  METALSHARP_CLONE_USER_STATE=1 scripts/install-wine119-parity-home.sh ...
The runner refuses to continue if active cloned manifests still point at the
source ~/.metalsharp path instead of the parity home.

Controls:
  Nidhogg 2          appid 535520   launchMethod m9
  Schedule I         appid 3164500  launchMethod m11
  Subnautica BZ      appid 848450   launchMethod m11

Environment:
  METALSHARP_PORT       Port to use, default 9375
  METALSHARP_BACKEND    Backend binary override
  METALSHARP_WAIT_SECS  Seconds to wait after each launch before proof, default 20
  METALSHARP_REQUIRE_PREEXISTING_WINE_STEAM=1
                       Start Wine Steam before the controls and require every
                       launch to attach to that pre-existing Steam process.
  METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1
                       Allow a parity home outside /tmp or /private/tmp
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "${METALSHARP_RUN_LIVE_GAMES:-0}" != "1" ]]; then
    usage >&2
    echo >&2
    echo "refusing to launch games: set METALSHARP_RUN_LIVE_GAMES=1 to proceed" >&2
    exit 2
fi

parity_home="${1:-}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${2:-/tmp/metalsharp-live-controls-$timestamp}"

if [[ -z "$parity_home" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -d "$parity_home/.metalsharp/runtime/wine" ]]; then
    echo "parity runtime not found: $parity_home/.metalsharp/runtime/wine" >&2
    exit 1
fi

parity_home="$(cd "$parity_home" && pwd -P)"

for rel in prefix-steam compatdata bottles games; do
    if [[ ! -e "$parity_home/.metalsharp/$rel" ]]; then
        echo "parity home missing copied user state: $parity_home/.metalsharp/$rel" >&2
        echo "prepare it with METALSHARP_CLONE_USER_STATE=1 scripts/install-wine119-parity-home.sh ..." >&2
        exit 1
    fi
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
port="${METALSHARP_PORT:-9375}"
backend="${METALSHARP_BACKEND:-$repo_root/app/src-rust/target/release/metalsharp-backend}"
wait_secs="${METALSHARP_WAIT_SECS:-20}"
require_preexisting_steam="${METALSHARP_REQUIRE_PREEXISTING_WINE_STEAM:-0}"

if [[ ! -x "$backend" ]]; then
    backend="$repo_root/app/src-rust/target/debug/metalsharp-backend"
fi

if [[ ! -x "$backend" ]]; then
    echo "backend binary not found; run cargo build --release in app/src-rust first" >&2
    exit 1
fi

mkdir -p "$out_dir/launches" "$out_dir/proof"

source_ms_home="${METALSHARP_PARITY_SOURCE_HOME:-}"
if [[ -z "$source_ms_home" && -f "$parity_home/parity-install-report.txt" ]]; then
    source_ms_home="$(awk -F= '$1 == "source_metalsharp_home" { print substr($0, index($0, "=") + 1); exit }' "$parity_home/parity-install-report.txt")"
fi
if [[ -z "$source_ms_home" ]]; then
    echo "refusing to launch games: parity source MetalSharp home is unknown" >&2
    echo "prepare the parity home with scripts/install-wine119-parity-home.sh or set METALSHARP_PARITY_SOURCE_HOME" >&2
    exit 1
fi
if [[ -n "$source_ms_home" && -d "$source_ms_home" ]]; then
    source_ms_home="$(cd "$source_ms_home" && pwd -P)"
fi
source_home_parent="$(cd "$(dirname "$source_ms_home")" && pwd -P 2>/dev/null || dirname "$source_ms_home")"

if [[ "$parity_home" == "$source_home_parent" || "$parity_home/.metalsharp" == "$source_ms_home" ]]; then
    echo "refusing to launch games against real MetalSharp home: $parity_home" >&2
    exit 1
fi

if [[ "${METALSHARP_ALLOW_NON_TMP_PARITY_HOME:-0}" != "1" ]]; then
    case "$parity_home" in
        /tmp/*|/private/tmp/*) ;;
        *)
            echo "refusing non-tmp parity home: $parity_home" >&2
            echo "set METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1 for an intentional scratch location" >&2
            exit 1
            ;;
    esac
fi

if [[ "$source_ms_home" != "$parity_home/.metalsharp" ]]; then
    stale_refs="$out_dir/stale-active-state-refs.txt"
    : > "$stale_refs"
    if find "$parity_home/.metalsharp/bottles" "$parity_home/.metalsharp/compatdata" "$parity_home/.metalsharp/configs" \
        -type f \( -name '*.json' -o -name '*.toml' -o -name '*.conf' -o -name '*.env' \) -print0 2>/dev/null \
        | xargs -0 rg -n --fixed-strings "$source_ms_home" -S > "$stale_refs" 2>/dev/null; then
        echo "refusing to launch games: active parity manifests still reference source MetalSharp home: $source_ms_home" >&2
        echo "stale refs: $stale_refs" >&2
        exit 1
    fi
fi

validate_control_state_paths() {
    local label="$1"
    local appid="$2"
    local bottle="$parity_home/.metalsharp/bottles/steam_$appid/bottle.json"
    local compat="$parity_home/.metalsharp/compatdata/$appid/metalsharp-compatdata.json"

    if [[ ! -f "$bottle" ]]; then
        echo "refusing to launch games: missing bottle manifest for $label: $bottle" >&2
        exit 1
    fi
    if [[ ! -f "$compat" ]]; then
        echo "refusing to launch games: missing compatdata manifest for $label: $compat" >&2
        exit 1
    fi
    if ! grep -Fq -- "$parity_home/.metalsharp" "$bottle"; then
        echo "refusing to launch games: bottle manifest for $label does not reference parity MetalSharp home" >&2
        exit 1
    fi
    if ! grep -Fq -- "$parity_home/.metalsharp" "$compat"; then
        echo "refusing to launch games: compatdata manifest for $label does not reference parity MetalSharp home" >&2
        exit 1
    fi
}

validate_control_state_paths "Nidhogg 2" 535520
validate_control_state_paths "Schedule I" 3164500
validate_control_state_paths "Subnautica Below Zero" 848450

if find "$parity_home/.metalsharp/compatdata" -path '*/logs/*' -type f -print -quit 2>/dev/null | grep -q .; then
    echo "refusing to launch games: copied compatdata log files are present and could contaminate proof" >&2
    echo "reinstall parity state with scripts/install-wine119-parity-home.sh to prune historical logs" >&2
    exit 1
fi

mkdir -p "$out_dir/steam"

capture_steam_snapshot() {
    local label="$1"
    local all_processes="$out_dir/steam/$label-processes.txt"
    local matching="$out_dir/steam/$label-matching-steam.txt"
    local pids="$out_dir/steam/$label-wine-steam-pids.txt"

    ps -axo pid,ppid,etime,command > "$all_processes"
    grep -iE 'Steam\.exe|steam\.exe|steamwebhelper|steam://|metalsharp-backend|wine' \
        "$all_processes" > "$matching" || true
    awk '
        {
            line = tolower($0)
            if (index(line, "steam.exe") > 0 &&
                index(line, "run-wine119-live-control-suite.sh") == 0 &&
                index(line, "/bin/zsh -lc") == 0 &&
                index(line, "/bin/bash") == 0 &&
                index(line, " grep ") == 0 &&
                index(line, " rg ") == 0) print $1
        }
    ' "$all_processes" | sort -n > "$pids"
}

capture_steam_snapshot "before-backend"

backend_pid=""
cleanup() {
    if [[ -n "$backend_pid" ]] && kill -0 "$backend_pid" 2>/dev/null; then
        kill "$backend_pid" 2>/dev/null || true
        wait "$backend_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

{
    echo "parity_home=$parity_home"
    echo "metalsharp_home=$parity_home/.metalsharp"
    echo "runtime=$parity_home/.metalsharp/runtime/wine"
    echo "backend=$backend"
    echo "port=$port"
    echo "wait_secs=$wait_secs"
    echo "captured_at_utc=$timestamp"
    printf "wine_version="
    "$parity_home/.metalsharp/runtime/wine/bin/wine" --version 2>&1 || true
    printf "wineserver_version="
    "$parity_home/.metalsharp/runtime/wine/bin/wineserver" --version 2>&1 || true
} > "$out_dir/runtime-identity.txt"

HOME="$parity_home" \
METALSHARP_HOME="$parity_home/.metalsharp" \
METALSHARP_PORT="$port" \
    "$backend" > "$out_dir/backend.stdout.log" 2> "$out_dir/backend.stderr.log" &
backend_pid=$!
echo "$backend_pid" > "$out_dir/backend.pid"

base_url="http://127.0.0.1:$port"
ready=0
for _ in $(seq 1 80); do
    if curl -fsS "$base_url/status" > "$out_dir/status.json" 2> "$out_dir/status.curl.log"; then
        ready=1
        break
    fi
    sleep 0.1
done

if [[ "$ready" != "1" ]]; then
    echo "backend did not become ready on $base_url" >&2
    exit 1
fi

capture_steam_snapshot "backend-ready"

if [[ "$require_preexisting_steam" == "1" ]]; then
    curl -fsS -X POST "$base_url/steam/launch" \
        -H 'Content-Type: application/json' \
        --data-binary '{}' \
        > "$out_dir/steam/prestart-launch.json" \
        2> "$out_dir/steam/prestart-launch.curl.log" || true

    prestarted=0
    for _ in $(seq 1 120); do
        capture_steam_snapshot "prestarted"
        if [[ -s "$out_dir/steam/prestarted-wine-steam-pids.txt" ]]; then
            prestarted=1
            break
        fi
        sleep 0.5
    done
    if [[ "$prestarted" != "1" ]]; then
        echo "refusing to launch games: Wine Steam did not become visible before controls" >&2
        exit 1
    fi
fi

launch_game() {
    local label="$1"
    local appid="$2"
    local method="$3"
    local exe_match="$4"
    local launch_json="$out_dir/launches/$label-launch.json"
    local launch_curl="$out_dir/launches/$label-launch.curl.log"
    local proof_dir="$out_dir/proof/$label"

    capture_steam_snapshot "before-$label"

    printf '{"appid":%s,"launchMethod":"%s"}' "$appid" "$method" \
        | curl -fsS -X POST "$base_url/steam/launch-game" \
            -H 'Content-Type: application/json' \
            --data-binary @- \
            > "$launch_json" 2> "$launch_curl" || true

    sleep "$wait_secs"

    capture_steam_snapshot "after-$label"

    HOME="$parity_home" \
    METALSHARP_HOME="$parity_home/.metalsharp" \
    METALSHARP_REQUIRE_PARITY_HOME=1 \
        "$repo_root/scripts/capture-steam-game-proof.sh" "$appid" "$exe_match" "$proof_dir"
}

launch_game "nidhogg2" 535520 "m9" "Nidhogg_2"
launch_game "schedule-i" 3164500 "m11" "Schedule I"
launch_game "subnautica-bz" 848450 "m11" "SubnauticaZero"

{
    echo "# MetalSharp Wine 11.9 Live Control Suite"
    echo
    cat "$out_dir/runtime-identity.txt"
    echo
    echo "status_json=$out_dir/status.json"
    echo "backend_stdout=$out_dir/backend.stdout.log"
    echo "backend_stderr=$out_dir/backend.stderr.log"
    echo "steam_snapshots=$out_dir/steam"
    echo "require_preexisting_wine_steam=$require_preexisting_steam"
    if [[ -f "$out_dir/steam/prestarted-wine-steam-pids.txt" ]]; then
        echo "prestarted_wine_steam_pids=$(tr '\n' ' ' < "$out_dir/steam/prestarted-wine-steam-pids.txt" | sed 's/[[:space:]]*$//')"
    fi
    echo
    for label in nidhogg2 schedule-i subnautica-bz; do
        echo "## $label"
        echo
        echo "- launch_json: $out_dir/launches/$label-launch.json"
        echo "- proof_summary: $out_dir/proof/$label/summary.txt"
        if [[ -f "$out_dir/proof/$label/game-pids.txt" && -s "$out_dir/proof/$label/game-pids.txt" ]]; then
            echo "- live_game_pids: $(tr '\n' ' ' < "$out_dir/proof/$label/game-pids.txt" | sed 's/[[:space:]]*$//')"
        else
            echo "- live_game_pids: none"
        fi
        if [[ -f "$out_dir/steam/before-$label-wine-steam-pids.txt" ]]; then
            echo "- wine_steam_pids_before: $(tr '\n' ' ' < "$out_dir/steam/before-$label-wine-steam-pids.txt" | sed 's/[[:space:]]*$//')"
        fi
        if [[ -f "$out_dir/steam/after-$label-wine-steam-pids.txt" ]]; then
            echo "- wine_steam_pids_after: $(tr '\n' ' ' < "$out_dir/steam/after-$label-wine-steam-pids.txt" | sed 's/[[:space:]]*$//')"
        fi
        echo
    done
} > "$out_dir/summary.md"

echo "live control suite written: $out_dir"
echo "summary: $out_dir/summary.md"
