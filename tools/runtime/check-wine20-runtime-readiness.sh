#!/usr/bin/env bash
set -euo pipefail

PORT="${METALSHARP_PORT:-9274}"
BASE_URL="${METALSHARP_BACKEND_URL:-http://127.0.0.1:${PORT}}"
OUT="${TMPDIR:-/tmp}/metalsharp-wine20-runtime-diagnostics.json"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--url URL]

Read-only MetalSharp Wine 2.0 runtime readiness check.

Environment:
  METALSHARP_PORT         Backend port when --url is not supplied (default: 9274)
  METALSHARP_BACKEND_URL  Full backend URL override

This script only reads GET /runtime/diagnostics. It does not launch Wine,
repair assets, mutate prefixes, or authorize install replacement.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --url)
      BASE_URL="${2:?missing URL after --url}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

curl -fsS "${BASE_URL%/}/runtime/diagnostics" > "$OUT"

python3 - "$OUT" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as handle:
    data = json.load(handle)

ok = data.get("ok") is True
guard = data.get("installReplacementGuard", {})
lanes = data.get("lanes", {})
runtime = data.get("runtime", {})
prefixes = data.get("prefixes", {})
sources = data.get("sources", {})

print(f"schema: {data.get('schema')}")
print(f"ok: {ok}")
print(f"summary: {data.get('summary')}")
print(f"runtime.ready: {runtime.get('ready')}")
print(f"dxmt_m12 current: {runtime.get('dxmtM12Current')}")
print(f"manifest ok: {runtime.get('manifestOk')}")
print(f"prefix policy ok: {prefixes.get('ok')}")
print(f"GOGDL source ok: {sources.get('gog', {}).get('ok')}")
print(f"available lanes ready: {lanes.get('availableReady')}/{lanes.get('availableTotal')}")
print(f"all lanes ready: {lanes.get('ready')}/{lanes.get('total')} ({lanes.get('planned')} planned, {lanes.get('external')} external)")
print(f"install replacement allowed now: {guard.get('allowedNow')}")

blocked = [entry for entry in lanes.get("entries", []) if not entry.get("ready")]
if blocked:
    print("non-ready lanes:")
    for entry in blocked:
        print(f"  - {entry.get('id')}: {', '.join(entry.get('blockers', []))}")

if guard.get("allowedNow") is not False:
    print("ERROR: install replacement guard must remain false for this read-only check", file=sys.stderr)
    sys.exit(3)
if not ok:
    print("ERROR: runtime diagnostics are not green", file=sys.stderr)
    sys.exit(1)
PY
