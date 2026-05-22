#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/probe-wine119-parity-backend.sh <parity-home> [output-dir]

Example:
  scripts/probe-wine119-parity-backend.sh \
    /tmp/metalsharp-home-wine119-dxmt32 \
    /tmp/metalsharp-home-wine119-dxmt32/backend-probe

Starts metalsharp-backend with HOME=<parity-home> and a non-default port, probes
basic backend/runtime endpoints, records the parity Wine identity, then stops
the backend. This is a preflight for live game proof; it does not launch games.

Environment:
  METALSHARP_PORT       Port to use, default 9374
  METALSHARP_BACKEND    Backend binary override
  METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1
                       Allow a parity home outside /tmp or /private/tmp
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

parity_home="${1:-}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${2:-/tmp/metalsharp-parity-backend-probe-$timestamp}"

if [[ -z "$parity_home" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -d "$parity_home/.metalsharp/runtime/wine" ]]; then
    echo "parity runtime not found: $parity_home/.metalsharp/runtime/wine" >&2
    exit 1
fi

parity_home="$(cd "$parity_home" && pwd -P)"
source_ms_home="${METALSHARP_PARITY_SOURCE_HOME:-${METALSHARP_SOURCE_HOME:-}}"
if [[ -z "$source_ms_home" && -f "$parity_home/parity-install-report.txt" ]]; then
    source_ms_home="$(awk -F= '$1 == "source_metalsharp_home" { print substr($0, index($0, "=") + 1); exit }' "$parity_home/parity-install-report.txt")"
fi
source_ms_home="${source_ms_home:-$HOME/.metalsharp}"
if [[ -d "$source_ms_home" ]]; then
    source_ms_home="$(cd "$source_ms_home" && pwd -P)"
fi
source_home_parent="$(cd "$(dirname "$source_ms_home")" && pwd -P 2>/dev/null || dirname "$source_ms_home")"

if [[ "$parity_home" == "$source_home_parent" || "$parity_home/.metalsharp" == "$source_ms_home" ]]; then
    echo "refusing to probe real MetalSharp home as parity home: $parity_home" >&2
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

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
port="${METALSHARP_PORT:-9374}"
backend="${METALSHARP_BACKEND:-$repo_root/app/src-rust/target/release/metalsharp-backend}"

if [[ ! -x "$backend" ]]; then
    backend="$repo_root/app/src-rust/target/debug/metalsharp-backend"
fi

if [[ ! -x "$backend" ]]; then
    echo "backend binary not found; run npm run rust:build or npm run rust:dev in app first" >&2
    exit 1
fi

mkdir -p "$out_dir"

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
    echo "captured_at_utc=$timestamp"
    if [[ -x "$parity_home/.metalsharp/runtime/wine/bin/wine" ]]; then
        printf "wine_version="
        "$parity_home/.metalsharp/runtime/wine/bin/wine" --version 2>&1 || true
    fi
    if [[ -x "$parity_home/.metalsharp/runtime/wine/bin/wineserver" ]]; then
        printf "wineserver_version="
        "$parity_home/.metalsharp/runtime/wine/bin/wineserver" --version 2>&1 || true
    fi
} > "$out_dir/runtime-identity.txt"

HOME="$parity_home" \
METALSHARP_HOME="$parity_home/.metalsharp" \
METALSHARP_PORT="$port" \
    "$backend" > "$out_dir/backend.stdout.log" 2> "$out_dir/backend.stderr.log" &
backend_pid=$!
echo "$backend_pid" > "$out_dir/backend.pid"

base_url="http://127.0.0.1:$port"
ready=0
for _ in $(seq 1 50); do
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

curl -fsS "$base_url/setup/dependencies" > "$out_dir/setup-dependencies.json" 2> "$out_dir/setup-dependencies.curl.log" || true
curl -fsS "$base_url/runtime/host-abi" > "$out_dir/runtime-host-abi.json" 2> "$out_dir/runtime-host-abi.curl.log" || true
curl -fsS "$base_url/bottles/profiles" > "$out_dir/bottles-profiles.json" 2> "$out_dir/bottles-profiles.curl.log" || true

{
    echo "# MetalSharp Parity Backend Probe"
    echo
    cat "$out_dir/runtime-identity.txt"
    echo
    echo "status_json=$out_dir/status.json"
    echo "setup_dependencies_json=$out_dir/setup-dependencies.json"
    echo "runtime_host_abi_json=$out_dir/runtime-host-abi.json"
    echo "bottles_profiles_json=$out_dir/bottles-profiles.json"
    echo "backend_stdout=$out_dir/backend.stdout.log"
    echo "backend_stderr=$out_dir/backend.stderr.log"
} > "$out_dir/summary.txt"

echo "parity backend probe written: $out_dir"
echo "summary: $out_dir/summary.txt"
