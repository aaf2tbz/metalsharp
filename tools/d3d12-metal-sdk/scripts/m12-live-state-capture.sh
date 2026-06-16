#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="elden-ring"
PID=""
LABEL="live-capture"
SECONDS_TO_SAMPLE="${M12_LIVE_CAPTURE_SECONDS:-10}"
SAMPLE_INTERVAL_MS="${M12_LIVE_CAPTURE_INTERVAL_MS:-250}"
RESULTS_DIR="${M12_LIVE_CAPTURE_RESULTS_DIR:-$SDK_DIR/results/live-captures}"

usage() {
  cat <<'USAGE'
Usage:
  m12-live-state-capture.sh --profile NAME [--pid PID] [--label LABEL] [--seconds N]

Captures the current state of a live or hung M12 game without killing it.
This is intended for hangs where the process remains open/rendered but input
or UI progression is stuck.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --pid) PID="$2"; shift 2 ;;
    --label) LABEL="$2"; shift 2 ;;
    --seconds) SECONDS_TO_SAMPLE="$2"; shift 2 ;;
    --sample-interval-ms) SAMPLE_INTERVAL_MS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$PROFILE" in
  elden-ring|eldenring) PROFILE="elden-ring"; APPID="1245620"; GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/ELDEN RING/Game" ;;
  subnautica2|subnautica-2) PROFILE="subnautica-2"; APPID="1962700"; GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2" ;;
  schedule-1|schedule1|schedule-i|schedulei) PROFILE="schedule-1"; APPID="3164500"; GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/Schedule I" ;;
  peak) PROFILE="peak"; APPID="3527290"; GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/PEAK" ;;
  *) echo "unknown profile: $PROFILE" >&2; exit 2 ;;
esac

if [[ -z "$PID" ]]; then
  PID="$(python3 - <<PY
import json
from pathlib import Path
manifest=Path.home()/'.metalsharp'/'compatdata'/'$APPID'/'metalsharp-compatdata.json'
try:
    data=json.loads(manifest.read_text())
    print(data.get('last_launch_pid') or '')
except Exception:
    print('')
PY
)"
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$RESULTS_DIR/$PROFILE-$LABEL-$STAMP"
mkdir -p "$RUN_DIR"
LOG_ROOT="$HOME/.metalsharp/compatdata/$APPID/logs"
PIPELINE_LOG_ROOT="$HOME/.metalsharp/logs/m12-pipeline/$APPID"
CACHE_DIR="$HOME/.metalsharp/shader-cache/m12/$APPID"
RUNTIME_DIR="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows"

{
  echo "profile=$PROFILE"
  echo "appid=$APPID"
  echo "pid=$PID"
  echo "label=$LABEL"
  echo "game_dir=$GAME_DIR"
  echo "capture_time=$STAMP"
} > "$RUN_DIR/capture.env"

if [[ -n "$PID" ]]; then
  ps -p "$PID" -o pid,ppid,stat,etime,pcpu,pmem,command > "$RUN_DIR/process-root.txt" 2>/dev/null || true
  ps -M "$PID" > "$RUN_DIR/process-threads.txt" 2>/dev/null || true
  pgrep -P "$PID" > "$RUN_DIR/child-pids.txt" 2>/dev/null || true
  "$SDK_DIR/scripts/sample-m12-process.py" --pid "$PID" --seconds "$SECONDS_TO_SAMPLE" --interval-ms "$SAMPLE_INTERVAL_MS" --out "$RUN_DIR/process-samples.csv" || true
fi

ps aux | rg -i "$APPID|elden|subnautica|schedule|peak|winedbg|wine64|start_protected" | rg -v rg > "$RUN_DIR/related-processes.txt" || true
find "$LOG_ROOT" -maxdepth 1 -type f -print0 2>/dev/null | xargs -0 ls -lt > "$RUN_DIR/compat-log-list.txt" 2>/dev/null || true
find "$PIPELINE_LOG_ROOT" -maxdepth 1 -type f -print0 2>/dev/null | xargs -0 ls -lt > "$RUN_DIR/pipeline-log-list.txt" 2>/dev/null || true
LATEST_LOG="$(ls -t "$LOG_ROOT"/launch-*.log 2>/dev/null | head -1 || true)"
if [[ -n "$LATEST_LOG" ]]; then
  cp "$LATEST_LOG" "$RUN_DIR/"
  tail -400 "$LATEST_LOG" > "$RUN_DIR/latest-launch-tail.txt" || true
  rg -n "M12|present|Graphics PSO|Compute PSO|DXIL MSL|MSL compile|unsafe|Failed|err:|warn:|exception|hang|fence|wait" "$LATEST_LOG" > "$RUN_DIR/latest-launch-interesting.txt" || true
fi

{
  echo "# Runtime hashes"
  for f in d3d12.dll d3d11.dll dxgi.dll dxgi_dxmt.dll d3d10core.dll winemetal.dll nvapi64.dll nvngx.dll; do
    [[ -f "$RUNTIME_DIR/$f" ]] && shasum -a 256 "$RUNTIME_DIR/$f"
  done
  echo
  echo "# Game-local hashes"
  for f in d3d12.dll d3d11.dll dxgi.dll dxgi_dxmt.dll d3d10core.dll winemetal.dll nvapi64.dll nvngx.dll; do
    [[ -f "$GAME_DIR/$f" ]] && shasum -a 256 "$GAME_DIR/$f"
  done
  echo
  echo "# Cache counts"
  find "$CACHE_DIR" -maxdepth 1 -type f 2>/dev/null | wc -l | awk '{print "cache_files=" $1}'
  find "$CACHE_DIR" -maxdepth 1 -type f -name '*.msl.err.txt' 2>/dev/null | wc -l | awk '{print "msl_errors=" $1}'
  find "$CACHE_DIR" -maxdepth 1 -type f -name '*.fail' 2>/dev/null | wc -l | awk '{print "fail_markers=" $1}'
} > "$RUN_DIR/hashes-and-cache.txt"

python3 - "$RUN_DIR" <<'PY'
import csv, json, sys
from pathlib import Path
run=Path(sys.argv[1])
proc={}
rows=[]
p=run/'process-samples.csv'
if p.exists() and p.stat().st_size:
    rows=list(csv.DictReader(p.open()))
    vals={k:[] for k in ['cpu_percent','rss_bytes','threads']}
    for r in rows:
        for k in vals:
            if r.get(k):
                try: vals[k].append(float(r[k]))
                except ValueError: pass
    proc={k:{'max':max(v) if v else None,'avg':sum(v)/len(v) if v else None} for k,v in vals.items()}
summary={'schema':'metalsharp.m12.live-capture.v1','run_dir':str(run),'samples':len(rows),'process':proc}
(run/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(run/'summary.md').write_text('\n'.join([
    '# M12 live state capture','',f'- run_dir: `{run}`',f'- samples: `{len(rows)}`',
    f'- max CPU %: `{proc.get("cpu_percent",{}).get("max")}`',
    f'- max RSS bytes: `{proc.get("rss_bytes",{}).get("max")}`',
    f'- max threads: `{proc.get("threads",{}).get("max")}`',''
]))
print(run/'summary.md')
print(run/'summary.json')
PY
