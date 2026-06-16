#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="elden-ring"
SCENARIO="smoke"
SECONDS_TO_RUN=""
RESULTS_DIR="${M12_PERF_RESULTS_DIR:-$SDK_DIR/results/perf-runs}"
BACKEND_URL="${METALSHARP_BACKEND_URL:-http://127.0.0.1:9277}"
SAMPLE_INTERVAL_MS="${M12_PERF_SAMPLE_INTERVAL_MS:-250}"
BOUNDED_EXTRA=()

usage() {
  cat <<'USAGE'
Usage:
  m12-performance-run.sh [options]

Options:
  --profile NAME             elden-ring, subnautica-2, schedule-1, or peak.
  --scenario NAME            Scenario from the profile. Default: smoke.
  --seconds N                Override scenario duration.
  --backend-url URL          Backend URL. Default: http://127.0.0.1:9277.
  --results-dir PATH         Perf result root. Default: tools/d3d12-metal-sdk/results/perf-runs.
  --sample-interval-ms N     Process sampling interval. Default: 250.
  --no-kill-after            Forward to bounded launch.
  --no-sample                Disable bounded sample(1); perf sampler still runs unless no PID.
  --workers N                Forward worker override to bounded launch.
  --async-compile 0|1        Forward async override to bounded launch.
  --expect-d3d12-sha SHA     Forward strict d3d12.dll hash gate to bounded launch.
  --expect-winemetal-so-sha SHA Forward strict winemetal.so hash gate to bounded launch.
  -h, --help                 Show help.

Creates a perf run directory containing bounded launch results, process samples,
profile metadata, marker template, and analysis summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --scenario) SCENARIO="$2"; shift 2 ;;
    --seconds) SECONDS_TO_RUN="$2"; shift 2 ;;
    --backend-url) BACKEND_URL="$2"; shift 2 ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --sample-interval-ms) SAMPLE_INTERVAL_MS="$2"; shift 2 ;;
    --no-kill-after|--no-sample) BOUNDED_EXTRA+=("$1"); shift ;;
    --workers|--async-compile|--typed-stage-in|--force-source-compile|--expect-d3d12-sha|--expect-winemetal-so-sha) BOUNDED_EXTRA+=("$1" "$2"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

PROFILE_PATH="$SDK_DIR/profiles/m12-perf/$PROFILE.json"
if [[ ! -f "$PROFILE_PATH" ]]; then
  echo "unknown perf profile: $PROFILE ($PROFILE_PATH not found)" >&2
  exit 2
fi

PROFILE_INFO_FILE="/tmp/m12-perf-profile.$$"
PROFILE_JSON="$PROFILE_PATH" SCENARIO="$SCENARIO" python3 - <<'PY' > "$PROFILE_INFO_FILE"
import json, os, sys
from pathlib import Path
profile=json.loads(Path(os.environ['PROFILE_JSON']).read_text())
scenario=os.environ['SCENARIO']
if profile.get('appid') is None:
    raise SystemExit(f"profile {profile.get('profile')} is a placeholder; fill appid/bounded_profile before running")
scenarios=profile.get('scenarios', {})
if scenario not in scenarios:
    raise SystemExit(f"unknown scenario {scenario}; available: {', '.join(sorted(scenarios))}")
print(profile.get('bounded_profile') or profile['profile'])
print(scenarios[scenario].get('seconds') or profile.get('default_seconds') or 60)
print(json.dumps(scenarios[scenario].get('markers', [])))
PY
BOUNDED_PROFILE="$(sed -n '1p' "$PROFILE_INFO_FILE")"
DEFAULT_SECONDS="$(sed -n '2p' "$PROFILE_INFO_FILE")"
MARKERS_JSON="$(sed -n '3p' "$PROFILE_INFO_FILE")"
rm -f "$PROFILE_INFO_FILE"
if [[ -z "$SECONDS_TO_RUN" ]]; then SECONDS_TO_RUN="$DEFAULT_SECONDS"; fi

STAMP="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$RESULTS_DIR/$PROFILE-$SCENARIO-$STAMP"
BOUNDED_DIR="$RUN_DIR/bounded"
mkdir -p "$RUN_DIR" "$BOUNDED_DIR"
cp "$PROFILE_PATH" "$RUN_DIR/profile.json"
MARKERS_JSON="$MARKERS_JSON" RUN_DIR="$RUN_DIR" PROFILE="$PROFILE" SCENARIO="$SCENARIO" SECONDS_TO_RUN="$SECONDS_TO_RUN" python3 - <<'PY'
import json, os, time
from pathlib import Path
run=Path(os.environ['RUN_DIR'])
markers=json.loads(os.environ['MARKERS_JSON'])
(run/'marker-template.json').write_text(json.dumps({'markers': markers}, indent=2)+'\n')
(run/'markers.jsonl').write_text(json.dumps({'event':'launch_started','time_unix':time.time(),'profile':os.environ['PROFILE'],'scenario':os.environ['SCENARIO']})+'\n')
(run/'perf-run.json').write_text(json.dumps({'schema':'metalsharp.m12.perf-run.v1','profile':os.environ['PROFILE'],'scenario':os.environ['SCENARIO'],'seconds':int(os.environ['SECONDS_TO_RUN']),'markers':markers}, indent=2)+'\n')
PY

# Run bounded launch with sample(1) disabled by default; this wrapper adds concurrent CSV process sampling.
BOUNDED_OUT=$(M12_PROCESS_SAMPLE_CSV="$RUN_DIR/process-samples.csv" M12_PROCESS_SAMPLE_INTERVAL_MS="$SAMPLE_INTERVAL_MS" "$SDK_DIR/scripts/m12-bounded-launch.sh" --profile "$BOUNDED_PROFILE" --seconds "$SECONDS_TO_RUN" --backend-url "$BACKEND_URL" --results-dir "$BOUNDED_DIR" --no-sample ${BOUNDED_EXTRA[@]+"${BOUNDED_EXTRA[@]}"})
printf '%s\n' "$BOUNDED_OUT" | tee "$RUN_DIR/bounded-launch.stdout"
SUMMARY_JSON="$(printf '%s\n' "$BOUNDED_OUT" | tail -1)"
SUMMARY_MD="${SUMMARY_JSON%.json}.md"
cp "$SUMMARY_JSON" "$RUN_DIR/bounded-summary.json"
cp "$SUMMARY_MD" "$RUN_DIR/bounded-summary.md"
if [[ ! -f "$RUN_DIR/process-samples.csv" ]]; then
  : > "$RUN_DIR/process-samples.csv"
fi
"$SDK_DIR/scripts/analyze-m12-perf-run.py" "$RUN_DIR" | tee "$RUN_DIR/analyze.stdout"
printf '%s\n' "$RUN_DIR/perf-analysis.md"
printf '%s\n' "$RUN_DIR/perf-analysis.json"
