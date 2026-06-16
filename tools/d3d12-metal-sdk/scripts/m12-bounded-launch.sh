#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="elden-ring"
SECONDS_TO_RUN="${M12_BOUNDED_SECONDS:-45}"
BACKEND_URL="${METALSHARP_BACKEND_URL:-http://127.0.0.1:9277}"
RESULTS_DIR="${M12_BOUNDED_RESULTS_DIR:-$SDK_DIR/results/bounded-launches}"
LAUNCH_METHOD="${M12_BOUNDED_LAUNCH_METHOD:-dxmt_metal12}"
WORKERS="${METALSHARP_M12_PSO_WORKERS:-}"
ASYNC_COMPILE="${METALSHARP_M12_ASYNC_PIPELINE_COMPILE:-}"
TYPED_STAGE_IN="${METALSHARP_M12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR:-}"
FORCE_SOURCE_COMPILE="${METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE:-}"
RUN_REPLAY=0
RUN_OFFLINE_PSO=0
KILL_AFTER=1
SAMPLE_PROCESS=1
SAMPLE_DURATION=5
SAMPLE_INTERVAL_MS=10
PROCESS_SAMPLE_CSV="${M12_PROCESS_SAMPLE_CSV:-}"
PROCESS_SAMPLE_INTERVAL_MS="${M12_PROCESS_SAMPLE_INTERVAL_MS:-250}"

usage() {
  cat <<'USAGE'
Usage:
  m12-bounded-launch.sh [options]

Options:
  --profile NAME          elden-ring, subnautica2, schedule-1, or peak. Default: elden-ring.
  --seconds N             Bounded launch window. Default: $M12_BOUNDED_SECONDS or 45.
  --backend-url URL       Backend URL. Default: http://127.0.0.1:9277.
  --workers N             Override DXMT_D3D12_PSO_WORKERS through backend env hook.
  --async-compile 0|1     Override DXMT_ASYNC_PIPELINE_COMPILE through backend env hook.
  --typed-stage-in 0|1    Override DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR through backend env hook.
  --force-source-compile 0|1 Override DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE through backend env hook.
  --results-dir PATH      Output directory for bounded run artifacts.
  --replay                Replay newly available corpus through metal-shaderconverter after launch.
  --offline-pso           Run offline PSO factory after launch.
  --no-kill-after         Leave the launched game running.
  --no-sample             Skip macOS sample(1) capture.
  -h, --help              Show this help.

This is for short, repeatable M12 quality/perf probes. It launches a profile,
waits a bounded window, captures process/log/corpus evidence, optionally kills
only the launched PID, then writes a JSON + Markdown summary.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --seconds) SECONDS_TO_RUN="$2"; shift 2 ;;
    --backend-url) BACKEND_URL="$2"; shift 2 ;;
    --workers) WORKERS="$2"; shift 2 ;;
    --async-compile) ASYNC_COMPILE="$2"; shift 2 ;;
    --typed-stage-in) TYPED_STAGE_IN="$2"; shift 2 ;;
    --force-source-compile) FORCE_SOURCE_COMPILE="$2"; shift 2 ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --replay) RUN_REPLAY=1; shift ;;
    --offline-pso) RUN_OFFLINE_PSO=1; shift ;;
    --no-kill-after) KILL_AFTER=0; shift ;;
    --no-sample) SAMPLE_PROCESS=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$PROFILE" in
  elden-ring|eldenring)
    PROFILE="elden-ring"
    APPID="1245620"
    GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/ELDEN RING/Game"
    ;;
  subnautica2|subnautica-2)
    PROFILE="subnautica2"
    APPID="1962700"
    GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2"
    ;;
  schedule-1|schedule1|schedule-i|schedulei)
    PROFILE="schedule-1"
    APPID="3164500"
    GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/Schedule I"
    ;;
  peak)
    PROFILE="peak"
    APPID="3527290"
    GAME_DIR="/Volumes/AverySSD/SteamLibrary/steamapps/common/PEAK"
    ;;
  *) echo "unknown profile: $PROFILE" >&2; exit 2 ;;
esac

STAMP="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$RESULTS_DIR/$PROFILE-$STAMP"
mkdir -p "$RUN_DIR"
CORPUS_DIR="$HOME/.metalsharp/shader-cache/m12/$APPID"
LOG_ROOT="$HOME/.metalsharp/logs/m12-pipeline/$APPID"
COMPAT_LOG_ROOT="$HOME/.metalsharp/compatdata/$APPID/logs"

snapshot_files() {
  local out="$1"
  find "$CORPUS_DIR" "$LOG_ROOT" "$COMPAT_LOG_ROOT" -maxdepth 2 -type f 2>/dev/null | sort > "$out" || true
}

snapshot_sizes() {
  local out="$1"
  find "$CORPUS_DIR" "$LOG_ROOT" "$COMPAT_LOG_ROOT" -maxdepth 2 -type f 2>/dev/null \
    | while IFS= read -r file; do stat -f '%z\t%N' "$file"; done \
    | sort -k2 > "$out" || true
}

snapshot_files "$RUN_DIR/before-files.txt"
snapshot_sizes "$RUN_DIR/before-sizes.tsv"
python3 "$SDK_DIR/scripts/preflight-runtime-layout.py" --profile "$PROFILE" --game-dir "$GAME_DIR" --dxmt-runtime "$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" --results-dir "$RUN_DIR" >/dev/null

launch_env=()
if [[ -n "$WORKERS" ]]; then launch_env+=("METALSHARP_M12_PSO_WORKERS=$WORKERS"); fi
if [[ -n "$ASYNC_COMPILE" ]]; then launch_env+=("METALSHARP_M12_ASYNC_PIPELINE_COMPILE=$ASYNC_COMPILE"); fi
if [[ -n "$TYPED_STAGE_IN" ]]; then launch_env+=("METALSHARP_M12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=$TYPED_STAGE_IN"); fi
if [[ -n "$FORCE_SOURCE_COMPILE" ]]; then launch_env+=("METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE=$FORCE_SOURCE_COMPILE"); fi

(
  if [[ ${#launch_env[@]} -gt 0 ]]; then
    env "${launch_env[@]}" curl -m 45 -fsS -X POST "$BACKEND_URL/steam/launch-game" \
      -H 'Content-Type: application/json' \
      -d "{\"appid\":$APPID,\"launchMethod\":\"$LAUNCH_METHOD\"}"
  else
    curl -m 45 -fsS -X POST "$BACKEND_URL/steam/launch-game" \
      -H 'Content-Type: application/json' \
      -d "{\"appid\":$APPID,\"launchMethod\":\"$LAUNCH_METHOD\"}"
  fi
) > "$RUN_DIR/launch.json"

PID="$(python3 - <<PY
import json
from pathlib import Path
try:
    print(json.loads(Path('$RUN_DIR/launch.json').read_text()).get('pid') or '')
except Exception:
    print('')
PY
)"

if [[ -n "$PID" && "$SAMPLE_PROCESS" == "1" ]]; then
  (sleep 3; sample "$PID" "$SAMPLE_DURATION" "$SAMPLE_INTERVAL_MS" -file "$RUN_DIR/sample-$PID.txt" >/dev/null 2>&1 || true) &
fi
if [[ -n "$PID" && -n "$PROCESS_SAMPLE_CSV" ]]; then
  python3 "$SDK_DIR/scripts/sample-m12-process.py" --pid "$PID" --seconds "$SECONDS_TO_RUN" --interval-ms "$PROCESS_SAMPLE_INTERVAL_MS" --out "$PROCESS_SAMPLE_CSV" &
  PROCESS_SAMPLE_PID=$!
fi

sleep "$SECONDS_TO_RUN"
if [[ -n "${PROCESS_SAMPLE_PID:-}" ]]; then
  wait "$PROCESS_SAMPLE_PID" 2>/dev/null || true
fi

if [[ -n "$PID" ]]; then
  ps -p "$PID" -o pid,stat,etime,pcpu,pmem,command > "$RUN_DIR/process.txt" 2>/dev/null || true
fi
snapshot_files "$RUN_DIR/after-files.txt"
snapshot_sizes "$RUN_DIR/after-sizes.tsv"
comm -13 "$RUN_DIR/before-files.txt" "$RUN_DIR/after-files.txt" > "$RUN_DIR/new-files.txt" || true

python3 "$SDK_DIR/scripts/index-m12-failures.py" --appid "$APPID" --profile "$PROFILE-$STAMP" --results-dir "$RUN_DIR" >/dev/null || true

if [[ "$RUN_REPLAY" == "1" ]]; then
  python3 "$SDK_DIR/scripts/replay-shader-corpus.py" --corpus "$CORPUS_DIR" --profile "$PROFILE-$STAMP" --results-dir "$RUN_DIR" --allow-empty > "$RUN_DIR/replay.stdout" 2> "$RUN_DIR/replay.stderr" || true
fi
if [[ "$RUN_OFFLINE_PSO" == "1" ]]; then
  python3 "$SDK_DIR/scripts/offline-pso-factory.py" --corpus "$CORPUS_DIR" --profile "$PROFILE-$STAMP" --results-dir "$RUN_DIR" --allow-empty > "$RUN_DIR/offline-pso.stdout" 2> "$RUN_DIR/offline-pso.stderr" || true
fi

if [[ "$KILL_AFTER" == "1" && -n "$PID" ]]; then
  curl -fsS -X POST "$BACKEND_URL/kill" -H 'Content-Type: application/json' -d "{\"appid\":$APPID,\"pid\":$PID}" > "$RUN_DIR/kill.json" 2>/dev/null || true
fi

RUN_DIR="$RUN_DIR" PROFILE="$PROFILE" APPID="$APPID" PID="$PID" SECONDS_TO_RUN="$SECONDS_TO_RUN" WORKERS="$WORKERS" ASYNC_COMPILE="$ASYNC_COMPILE" TYPED_STAGE_IN="$TYPED_STAGE_IN" FORCE_SOURCE_COMPILE="$FORCE_SOURCE_COMPILE" CORPUS_DIR="$CORPUS_DIR" python3 - <<'PY'
import json, os, re
from pathlib import Path
run = Path(os.environ['RUN_DIR'])
new_files = [Path(x) for x in (run/'new-files.txt').read_text(errors='replace').splitlines() if x]
counts = {
  'new_files': len(new_files),
  'new_dxbc': sum(p.suffix == '.dxbc' for p in new_files),
  'new_msl': sum(p.suffix == '.msl' for p in new_files),
  'new_metallib': sum(p.suffix == '.metallib' for p in new_files),
  'new_pso_render': sum(p.name.startswith('pso-render-') and p.suffix == '.json' for p in new_files),
  'new_pso_compute': sum(p.name.startswith('pso-compute-') and p.suffix == '.json' for p in new_files),
  'new_dxil_reports': sum(p.name.endswith('.dxil_report.txt') for p in new_files),
  'new_msl_errors': sum(p.name.endswith('.msl.err.txt') for p in new_files),
  'new_fail_markers': sum(p.name.endswith('.fail') for p in new_files),
}
launch = {}
try: launch = json.loads((run/'launch.json').read_text())
except Exception: pass

def read_size_map(path):
    sizes = {}
    if not path.exists():
        return sizes
    for line in path.read_text(errors='replace').splitlines():
        size, _, name = line.partition('\t')
        try:
            sizes[name] = int(size)
        except ValueError:
            continue
    return sizes

before_sizes = read_size_map(run/'before-sizes.tsv')
after_sizes = read_size_map(run/'after-sizes.tsv')
log_text = ''
log_sources = []
launch_log = launch.get('launch_log')
if launch_log and Path(launch_log).exists():
    text = Path(launch_log).read_text(errors='replace')
    log_text += '\n' + text
    log_sources.append({'path': launch_log, 'bytes': len(text.encode(errors='replace')), 'mode': 'launch_log_full'})
for name, after_size in after_sizes.items():
    p = Path(name)
    if p.suffix not in {'.log', '.txt'}:
        continue
    # Use exact byte deltas for append-only shared M12 logs, avoiding cumulative/noisy metrics.
    before_size = before_sizes.get(name, 0)
    if after_size <= before_size:
        continue
    try:
        with p.open('rb') as fh:
            fh.seek(before_size)
            data = fh.read(after_size - before_size)
        text = data.decode(errors='replace')
    except OSError:
        continue
    log_text += '\n' + text
    log_sources.append({'path': name, 'bytes': len(data), 'mode': 'byte_delta', 'from': before_size, 'to': after_size})

pso_error_lines = []
translation_issue_lines = []
for line in log_text.splitlines():
    if 'Failed to create render PSO' in line or 'Failed to create compute PSO' in line:
        pso_error_lines.append(line.strip())
    if any(token in line for token in ['SM50Compile failed', 'DXIL MSL compilation failed', 'MSL compile failed', 'mismatching vertex shader output', 'no vertex descriptor was set', 'skipping unsafe Draw']):
        translation_issue_lines.append(line.strip())
metrics = {
  'present_count': len(re.findall(r'M12 present entry', log_text)),
  'drawn_present_count': len(re.findall(r'classification=drawn', log_text)),
  'graphics_pso_compiled': len(re.findall(r'Graphics PSO compiled', log_text)),
  'compute_pso_compiled': len(re.findall(r'Compute PSO compiled', log_text)),
  'render_pso_failed': len(re.findall(r'Failed to create render PSO', log_text)),
  'compute_pso_failed': len(re.findall(r'Failed to create compute PSO', log_text)),
  'sm50_compile_failed': len(re.findall(r'SM50Compile failed', log_text)),
  'dxil_msl_compile_failed': len(re.findall(r'DXIL MSL compilation failed|MSL compile failed', log_text)),
  'vertex_descriptor_missing': len(re.findall(r'no vertex descriptor was set', log_text)),
  'vs_ps_varying_mismatch': len(re.findall(r'mismatching vertex shader output', log_text)),
  'tessellation_fallback': len(re.findall(r'D3D12 tessellation fallback', log_text)),
  'unix_call_failed': len(re.findall(r'unix_call_failed', log_text)),
  'unsafe_draw_skips': len(re.findall(r'skipping unsafe Draw', log_text)),
}
(run/'log-sources.json').write_text(json.dumps(log_sources, indent=2) + '\n')
(run/'pso-error-lines.txt').write_text('\n'.join(pso_error_lines[:200]) + ('\n' if pso_error_lines else ''))
(run/'translation-issue-lines.txt').write_text('\n'.join(translation_issue_lines[:300]) + ('\n' if translation_issue_lines else ''))
summary = {
  'schema': 'metalsharp.m12.bounded-launch.v1',
  'profile': os.environ['PROFILE'],
  'appid': int(os.environ['APPID']),
  'pid': int(os.environ['PID']) if os.environ['PID'] else None,
  'seconds': int(os.environ['SECONDS_TO_RUN']),
  'workers_override': os.environ['WORKERS'],
  'async_compile_override': os.environ['ASYNC_COMPILE'],
  'typed_stage_in_override': os.environ['TYPED_STAGE_IN'],
  'force_source_compile_override': os.environ['FORCE_SOURCE_COMPILE'],
  'corpus_dir': os.environ['CORPUS_DIR'],
  'launch_ok': bool(launch.get('ok')),
  'launch_log': launch.get('launch_log'),
  'counts': counts,
  'metrics': metrics,
  'log_sources': log_sources,
  'pso_error_examples': pso_error_lines[:20],
  'translation_issue_examples': translation_issue_lines[:20],
}
(run/'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
md = [f"# M12 bounded launch: {summary['profile']}", '', f"- appid: `{summary['appid']}`", f"- pid: `{summary['pid']}`", f"- seconds: `{summary['seconds']}`", f"- workers_override: `{summary['workers_override']}`", f"- async_compile_override: `{summary['async_compile_override']}`", f"- typed_stage_in_override: `{summary['typed_stage_in_override']}`", f"- force_source_compile_override: `{summary['force_source_compile_override']}`", f"- launch_ok: `{summary['launch_ok']}`", f"- launch_log: `{summary['launch_log']}`", '', '## New artifacts']
for k,v in counts.items(): md.append(f"- `{k}`: {v}")
md += ['', '## Per-run runtime metrics']
for k,v in metrics.items(): md.append(f"- `{k}`: {v}")
md += ['', '## Log sources']
for src in log_sources: md.append(f"- `{src['mode']}` `{src['path']}` bytes={src['bytes']}")
if pso_error_lines:
    md += ['', '## PSO error examples']
    for line in pso_error_lines[:10]: md.append(f"- `{line[:500]}`")
if translation_issue_lines:
    md += ['', '## Translation issue examples']
    for line in translation_issue_lines[:10]: md.append(f"- `{line[:500]}`")
(run/'summary.md').write_text('\n'.join(md)+'\n')
print(run/'summary.md')
print(run/'summary.json')
PY
