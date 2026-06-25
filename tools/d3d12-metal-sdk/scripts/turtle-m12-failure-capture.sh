#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PID=""
APPID=""
PROFILE="manual"
LABEL="failure"
OUT_DIR=""
WAIT_SECONDS=45
CAPTURE_SECONDS=20
SAMPLE_SECONDS=4
LOG_LAST_MINUTES=10
XCTRACE_TEMPLATE="Metal System Trace"
DO_XCTRACE=1
DO_LLDB=1
DO_SAMPLE=1
DO_SPINDUMP=0
DO_SYSDIAGNOSE=0

usage() {
  cat <<'USAGE'
usage: turtle-m12-failure-capture.sh [--pid PID | --profile NAME | --appid APPID] [options]

Structured M12 failure turtle capture for Phase 15 launches. This script does
NOT launch or kill games. It waits for/attaches to an already-running target and
writes a self-contained capture bundle with Xcode/Instruments, LLDB, sample,
macOS logs, MetalSharp logs, process snapshots, and a JSON/Markdown summary.

Targets:
  --pid PID                 Attach to an exact running process.
  --profile NAME            subnautica2, elden-ring, armored-core-vi, schedule-1, peak.
  --appid APPID             Use appid for MetalSharp log collection and generic PID inference.

Options:
  --label NAME              Capture label. Default: failure.
  --out-dir DIR             Output directory. Default: AverySSD if mounted, otherwise /tmp.
  --wait-seconds N          Wait up to N seconds for inferred target. Default: 45.
  --capture-seconds N       xctrace/log capture window. Default: 20.
  --sample-seconds N        sample(1) duration. Default: 4.
  --log-last-minutes N      macOS unified log lookback. Default: 10.
  --xctrace-template NAME   Default: Metal System Trace. Good alternatives: System Trace, Game Performance.
  --no-xctrace              Skip xctrace Instruments capture.
  --no-lldb                 Skip LLDB interrupt/backtrace capture.
  --no-sample               Skip sample(1).
  --spindump                Add spindump; heavier, off by default.
  --sysdiagnose             Add sysdiagnose; very heavy, off by default.
  -h, --help                Show this help.

Recommended Phase 15 use:
  # In a second terminal before/during launch, let it wait for Subnautica 2:
  tools/d3d12-metal-sdk/scripts/turtle-m12-failure-capture.sh \
    --profile subnautica2 --label phase15-first-pixels --capture-seconds 30

  # If a hang is already visible and you know the PID:
  tools/d3d12-metal-sdk/scripts/turtle-m12-failure-capture.sh \
    --pid 12345 --appid 1962700 --label visible-hang --capture-seconds 12
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pid) PID="$2"; shift 2 ;;
    --appid) APPID="$2"; shift 2 ;;
    --profile) PROFILE="$2"; shift 2 ;;
    --label) LABEL="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --wait-seconds) WAIT_SECONDS="$2"; shift 2 ;;
    --capture-seconds) CAPTURE_SECONDS="$2"; shift 2 ;;
    --sample-seconds) SAMPLE_SECONDS="$2"; shift 2 ;;
    --log-last-minutes) LOG_LAST_MINUTES="$2"; shift 2 ;;
    --xctrace-template) XCTRACE_TEMPLATE="$2"; shift 2 ;;
    --no-xctrace) DO_XCTRACE=0; shift ;;
    --no-lldb) DO_LLDB=0; shift ;;
    --no-sample) DO_SAMPLE=0; shift ;;
    --spindump) DO_SPINDUMP=1; shift ;;
    --sysdiagnose) DO_SYSDIAGNOSE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
  esac
done

PROFILE_CANON="$PROFILE"
PATTERN=""
case "$PROFILE" in
  subnautica2|subnautica-2)
    PROFILE_CANON="subnautica2"
    APPID="${APPID:-1962700}"
    PATTERN='Subnautica2|Subnautica2-Win64-Shipping|CrashReportClient|start_protected_game'
    ;;
  elden-ring|eldenring)
    PROFILE_CANON="elden-ring"
    APPID="${APPID:-1245620}"
    PATTERN='eldenring|start_protected_game|EasyAntiCheat'
    ;;
  armored-core-vi|armoredcore6|ac6)
    PROFILE_CANON="armored-core-vi"
    APPID="${APPID:-1888160}"
    PATTERN='armoredcore|ARMORED CORE|start_protected_game|EasyAntiCheat'
    ;;
  schedule-1|schedule1|schedule-i|schedulei)
    PROFILE_CANON="schedule-1"
    APPID="${APPID:-3164500}"
    PATTERN='Schedule I|Schedule[[:space:]]*I|UnityCrashHandler|crashpad_handler'
    ;;
  peak)
    PROFILE_CANON="peak"
    APPID="${APPID:-3527290}"
    PATTERN='PEAK|UnityCrashHandler|crashpad_handler'
    ;;
  manual)
    PATTERN='start_protected_game|Shipping\.exe|UnityCrashHandler|crashpad_handler|eldenring|Subnautica|armoredcore|PEAK|Schedule'
    ;;
  *)
    # Treat unknown profile as a label with generic game inference.
    PATTERN='start_protected_game|Shipping\.exe|UnityCrashHandler|crashpad_handler|eldenring|Subnautica|armoredcore|PEAK|Schedule'
    ;;
esac
PROFILE="$PROFILE_CANON"

STAMP="$(date +%Y%m%d-%H%M%S)"
if [[ -z "$OUT_DIR" ]]; then
  if [[ -d /Volumes/AverySSD ]]; then
    OUT_DIR="/Volumes/AverySSD/metalsharp-turtle-captures/${PROFILE}-${LABEL}-${STAMP}"
  else
    OUT_DIR="/tmp/metalsharp-m12-turtle-${PROFILE}-${LABEL}-${STAMP}"
  fi
fi
mkdir -p "$OUT_DIR" "$OUT_DIR/logs" "$OUT_DIR/crashes"

log_note() { printf '[turtle] %s\n' "$*" | tee -a "$OUT_DIR/turtle.log" >&2; }

write_tool_inventory() {
  {
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "root=$ROOT_DIR"
    echo "sdk_dir=$SDK_DIR"
    echo "xcode_select=$(xcode-select -p 2>/dev/null || true)"
    echo "xcodebuild_version_begin"
    xcodebuild -version 2>/dev/null || true
    echo "xcodebuild_version_end"
    for tool in xctrace lldb metal metallib metal-dsymutil metal-source air-binary-perf sample spindump sysdiagnose log vmmap leaks atos; do
      printf '%s=' "$tool"
      if [[ "$tool" == sample || "$tool" == spindump || "$tool" == sysdiagnose || "$tool" == log || "$tool" == vmmap || "$tool" == leaks || "$tool" == atos ]]; then
        command -v "$tool" 2>/dev/null || true
      else
        xcrun --find "$tool" 2>/dev/null || true
      fi
    done
    echo "xctrace_version=$(xcrun xctrace version 2>/dev/null || true)"
    echo "lldb_version_begin"
    xcrun lldb --version 2>/dev/null || true
    echo "lldb_version_end"
    echo "xctrace_templates_begin"
    xcrun xctrace list templates 2>/dev/null || true
    echo "xctrace_templates_end"
    echo "xctrace_instruments_metal_begin"
    xcrun xctrace list instruments 2>/dev/null | egrep -i 'Metal|GPU|Hangs|Time Profiler|System|os_log|signpost|Activity|Frame' || true
    echo "xctrace_instruments_metal_end"
    echo "metal_version_begin"
    xcrun metal -v 2>/dev/null || true
    echo "metal_version_end"
  } > "$OUT_DIR/tool-inventory.txt"
}

snapshot_processes() {
  local name="$1"
  ps ax -o pid=,ppid=,stat=,etime=,pcpu=,pmem=,comm=,args= > "$OUT_DIR/process-${name}.txt" || true
}

infer_pid() {
  local deadline=$((SECONDS + WAIT_SECONDS))
  while [[ $SECONDS -le $deadline ]]; do
    local found=""
    found="$(ps ax -o pid=,ppid=,stat=,etime=,comm=,args= | awk -v pat="$PATTERN" 'BEGIN{IGNORECASE=1} $0 ~ pat && $0 !~ /turtle-m12-failure-capture/ {print $1}' | tail -1 || true)"
    if [[ -n "$found" ]] && ps -p "$found" >/dev/null 2>&1; then
      PID="$found"
      return 0
    fi
    sleep 1
  done
  return 1
}

write_tool_inventory
snapshot_processes before

if [[ -z "$PID" ]]; then
  log_note "waiting up to ${WAIT_SECONDS}s for profile=${PROFILE} appid=${APPID:-unknown} pattern=${PATTERN}"
  if ! infer_pid; then
    log_note "could not infer target pid"
    exit 2
  fi
fi

if ! ps -p "$PID" >/dev/null 2>&1; then
  log_note "target pid not running: $PID"
  exit 1
fi

TARGET_COMM="$(ps -p "$PID" -o comm= 2>/dev/null | sed 's#^.*/##' || true)"
TARGET_ARGS="$(ps -p "$PID" -o args= 2>/dev/null || true)"
log_note "target pid=$PID comm=${TARGET_COMM:-unknown} profile=$PROFILE appid=${APPID:-none} out=$OUT_DIR"

cat > "$OUT_DIR/context.json" <<EOF
{
  "schema": "metalsharp.m12.turtle.context.v1",
  "timestamp_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "repo": "$ROOT_DIR",
  "profile": "$PROFILE",
  "label": "$LABEL",
  "appid": "${APPID:-}",
  "pid": $PID,
  "target_comm": "${TARGET_COMM//"/\\"}",
  "target_args": "${TARGET_ARGS//"/\\"}",
  "capture_seconds": $CAPTURE_SECONDS,
  "sample_seconds": $SAMPLE_SECONDS,
  "log_last_minutes": $LOG_LAST_MINUTES,
  "xctrace_template": "${XCTRACE_TEMPLATE//"/\\"}",
  "do_xctrace": $DO_XCTRACE,
  "do_lldb": $DO_LLDB,
  "do_sample": $DO_SAMPLE,
  "do_spindump": $DO_SPINDUMP,
  "do_sysdiagnose": $DO_SYSDIAGNOSE
}
EOF

ps -p "$PID" -o pid,ppid,stat,etime,pcpu,pmem,command > "$OUT_DIR/target-process-before.txt" 2>/dev/null || true
vmmap "$PID" > "$OUT_DIR/vmmap-before.txt" 2> "$OUT_DIR/vmmap-before.err" || true

XCTRACE_PID=""
if [[ "$DO_XCTRACE" == "1" ]]; then
  TRACE_PATH="$OUT_DIR/xctrace-${XCTRACE_TEMPLATE// /-}.trace"
  log_note "starting xctrace template='$XCTRACE_TEMPLATE' time=${CAPTURE_SECONDS}s"
  set +e
  xcrun xctrace record \
    --template "$XCTRACE_TEMPLATE" \
    --attach "$PID" \
    --time-limit "${CAPTURE_SECONDS}s" \
    --window "${CAPTURE_SECONDS}s" \
    --output "$TRACE_PATH" \
    --no-prompt \
    > "$OUT_DIR/xctrace.stdout" 2> "$OUT_DIR/xctrace.stderr" &
  XCTRACE_PID=$!
  set -e
  # Give xctrace a moment to attach before low-intrusion sampling.
  sleep 2
fi

if [[ "$DO_SAMPLE" == "1" ]] && ps -p "$PID" >/dev/null 2>&1; then
  log_note "running sample(${SAMPLE_SECONDS}s)"
  set +e
  sample "$PID" "$SAMPLE_SECONDS" 10 -file "$OUT_DIR/sample.txt" > "$OUT_DIR/sample.stdout" 2> "$OUT_DIR/sample.stderr"
  echo $? > "$OUT_DIR/sample.exit"
  set -e
fi

if [[ -n "$XCTRACE_PID" ]]; then
  log_note "waiting for xctrace before intrusive debugger snapshot"
  set +e
  wait "$XCTRACE_PID"
  echo $? > "$OUT_DIR/xctrace.exit"
  set -e
  if [[ -d "${TRACE_PATH:-}" ]]; then
    xcrun xctrace export --input "$TRACE_PATH" --toc --output "$OUT_DIR/xctrace-toc.xml" > "$OUT_DIR/xctrace-export.stdout" 2> "$OUT_DIR/xctrace-export.stderr" || true
  fi
elif [[ "$DO_LLDB" == "1" && "$CAPTURE_SECONDS" -gt 0 ]]; then
  # If xctrace is disabled but LLDB is enabled, still wait for the requested
  # observation window before interrupting the process.
  sleep "$CAPTURE_SECONDS"
fi

if [[ "$DO_LLDB" == "1" ]] && ps -p "$PID" >/dev/null 2>&1; then
  log_note "running lldb interrupt/backtrace"
  LLDB_CMDS="$OUT_DIR/lldb-commands.txt"
  cat > "$LLDB_CMDS" <<'LLDB'
settings set target.process.thread.step-avoid-regexp ^$
settings set target.memory-module-load-level minimal
process status
process interrupt
thread list
thread backtrace all
register read
image list -o -f
image lookup -rn "m12core|winemetal|dxmt|D3D12|DXGI|ExecuteCommandLists|ExecuteIndirect|Create.*Pipeline|Present|Signal|Wait|Fence|MTL|AGX|newLibrary|newRenderPipeline|newComputePipeline"
detach
quit
LLDB
  set +e
  xcrun lldb -p "$PID" -b -s "$LLDB_CMDS" > "$OUT_DIR/lldb.txt" 2> "$OUT_DIR/lldb.err"
  echo $? > "$OUT_DIR/lldb.exit"
  set -e
fi

if [[ "$DO_SPINDUMP" == "1" ]] && ps -p "$PID" >/dev/null 2>&1; then
  log_note "running spindump"
  set +e
  /usr/sbin/spindump "$PID" 5 1 -file "$OUT_DIR/spindump.txt" > "$OUT_DIR/spindump.stdout" 2> "$OUT_DIR/spindump.stderr"
  echo $? > "$OUT_DIR/spindump.exit"
  set -e
fi

ps -p "$PID" -o pid,ppid,stat,etime,pcpu,pmem,command > "$OUT_DIR/target-process-after.txt" 2>/dev/null || true
vmmap "$PID" > "$OUT_DIR/vmmap-after.txt" 2> "$OUT_DIR/vmmap-after.err" || true
snapshot_processes after

log_note "collecting unified log and MetalSharp tails"
PREDICATE="processID == $PID OR eventMessage CONTAINS[c] \"AGX\" OR eventMessage CONTAINS[c] \"IOGPU\" OR eventMessage CONTAINS[c] \"Metal\" OR eventMessage CONTAINS[c] \"GPU\" OR eventMessage CONTAINS[c] \"D3D12\" OR eventMessage CONTAINS[c] \"DXMT\" OR eventMessage CONTAINS[c] \"winemetal\" OR process CONTAINS[c] \"WindowServer\""
set +e
/usr/bin/log show --last "${LOG_LAST_MINUTES}m" --style ndjson --info --debug --predicate "$PREDICATE" > "$OUT_DIR/oslog.ndjson" 2> "$OUT_DIR/oslog.err"
echo $? > "$OUT_DIR/oslog.exit"
set -e

if [[ -n "${APPID:-}" ]]; then
  for dir in "$HOME/.metalsharp/logs/m12-pipeline/$APPID" "$HOME/.metalsharp/compatdata/$APPID/logs"; do
    if [[ -d "$dir" ]]; then
      safe_name="$(echo "$dir" | sed 's#[/:]#_#g')"
      find "$dir" -maxdepth 2 -type f \( -name '*.log' -o -name '*.txt' -o -name '*.json' \) -print0 2>/dev/null \
        | while IFS= read -r -d '' file; do
            base="$(basename "$file")"
            tail -n 2500 "$file" > "$OUT_DIR/logs/${safe_name}_${base}.tail" 2>/dev/null || true
          done
    fi
  done
  SHADER_CACHE="$HOME/.metalsharp/shader-cache/m12/$APPID"
  if [[ -d "$SHADER_CACHE" ]]; then
    find "$SHADER_CACHE" -maxdepth 1 -type f 2>/dev/null | sed 's#^.*/##' | sort > "$OUT_DIR/shader-cache-files.txt" || true
    find "$SHADER_CACHE" -maxdepth 1 -type f 2>/dev/null | wc -l > "$OUT_DIR/shader-cache-file-count.txt" || true
  fi
fi

python3 - <<'PY' "$OUT_DIR" "$PID" "${TARGET_COMM:-}" "$LOG_LAST_MINUTES"
from pathlib import Path
import json, os, re, shutil, sys, time
out = Path(sys.argv[1])
pid = sys.argv[2]
comm = sys.argv[3]
log_last = int(sys.argv[4])
now = time.time()
roots = [Path.home()/"Library/Logs/DiagnosticReports", Path("/Library/Logs/DiagnosticReports")]
needles = [comm, "wine", "start_protected_game", "WindowServer", "AGX", "GPU", "Metal", "CrashReportClient"]
needles = [n.lower() for n in needles if n]
crashes = []
for root in roots:
    if not root.exists():
        continue
    for p in root.iterdir():
        if not p.is_file():
            continue
        try:
            st = p.stat()
        except OSError:
            continue
        if now - st.st_mtime > max(log_last * 60, 1800):
            continue
        lname = p.name.lower()
        if any(n in lname for n in needles):
            dst = out/"crashes"/p.name
            try:
                shutil.copy2(p, dst)
                crashes.append({"source": str(p), "dest": str(dst), "bytes": st.st_size, "mtime": st.st_mtime})
            except OSError:
                crashes.append({"source": str(p), "copy_error": True, "bytes": st.st_size, "mtime": st.st_mtime})
(out/"crash-reports.json").write_text(json.dumps(crashes, indent=2, sort_keys=True)+"\n")
PY

if [[ "$DO_SYSDIAGNOSE" == "1" ]]; then
  log_note "running sysdiagnose (heavy)"
  set +e
  /usr/bin/sysdiagnose -f "$OUT_DIR" > "$OUT_DIR/sysdiagnose.stdout" 2> "$OUT_DIR/sysdiagnose.stderr"
  echo $? > "$OUT_DIR/sysdiagnose.exit"
  set -e
fi

python3 - <<'PY' "$OUT_DIR"
from pathlib import Path
import json, re, sys
out = Path(sys.argv[1])
context = json.loads((out/'context.json').read_text())
terms = [
    'AGX', 'IOGPU', 'GPU Restarted', 'GPURestart', 'hang', 'timeout', 'fault', 'page fault',
    'EXC_', 'SIGSEGV', 'crash', 'Metal', 'MTL', 'DXMT', 'D3D12', 'DXGI', 'winemetal', 'm12core',
    'ExecuteCommandLists', 'ExecuteIndirect', 'Present', 'Signal', 'Wait', 'Fence', 'WaitUntilCompleted',
    'newLibrary', 'newRenderPipeline', 'newComputePipeline', 'Failed to create', 'MSL compile failed',
    'DXIL MSL compilation failed', 'descriptor', 'argument buffer', 'semaphore_wait', 'mach_msg_trap',
    'pthread_cond_wait', 'WindowServer'
]
files = ['lldb.txt','lldb.err','sample.txt','sample.stderr','spindump.txt','spindump.stderr','oslog.ndjson','xctrace.stderr']
counts = {t: 0 for t in terms}
interesting = []
for name in files:
    p = out/name
    if not p.exists():
        continue
    text = p.read_text(errors='replace')
    lower = text.lower()
    for t in terms:
        counts[t] += lower.count(t.lower())
    for line in text.splitlines():
        if any(t.lower() in line.lower() for t in terms):
            interesting.append({'file': name, 'line': line[:500]})
            if len(interesting) >= 240:
                break

log_dir = out/'logs'
for p in sorted(log_dir.glob('*.tail')) if log_dir.exists() else []:
    text = p.read_text(errors='replace')
    lower = text.lower()
    for t in terms:
        counts[t] += lower.count(t.lower())
    for line in text.splitlines():
        if any(t.lower() in line.lower() for t in terms):
            interesting.append({'file': str(p.relative_to(out)), 'line': line[:500]})
            if len(interesting) >= 360:
                break

exit_files = {p.name: p.read_text(errors='replace').strip() for p in out.glob('*.exit')}
artifacts = {}
for p in out.iterdir():
    if p.is_file():
        artifacts[p.name] = p.stat().st_size
    elif p.is_dir() and p.suffix == '.trace':
        artifacts[p.name] = sum(f.stat().st_size for f in p.rglob('*') if f.is_file())

classification = []
if counts.get('GPU Restarted', 0) or counts.get('GPURestart', 0) or counts.get('IOGPU', 0) > 5:
    classification.append('gpu-driver-or-windowserver-signal')
if counts.get('MSL compile failed', 0) or counts.get('DXIL MSL compilation failed', 0) or counts.get('Failed to create', 0):
    classification.append('shader-or-pso-creation-signal')
m12_context_count = counts.get('DXMT', 0) + counts.get('D3D12', 0) + counts.get('winemetal', 0) + counts.get('m12core', 0)
if (counts.get('descriptor', 0) + counts.get('argument buffer', 0)) > 2 and m12_context_count:
    classification.append('descriptor-argbuf-signal')
if counts.get('WaitUntilCompleted', 0) or counts.get('semaphore_wait', 0) > 5 or counts.get('pthread_cond_wait', 0) > 5:
    classification.append('sync-wait-or-hang-signal')
if counts.get('EXC_', 0) or counts.get('SIGSEGV', 0) or counts.get('page fault', 0):
    classification.append('crash-or-access-fault-signal')
if not classification:
    classification.append('no-obvious-signature-yet')

summary = {
    'schema': 'metalsharp.m12.turtle.summary.v1',
    'context': context,
    'classification_hints': classification,
    'term_counts': {k:v for k,v in counts.items() if v},
    'exit_codes': exit_files,
    'artifact_sizes': artifacts,
    'interesting_examples': interesting[:120],
}
(out/'summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True)+'\n')
md = [
    '# M12 turtle failure capture', '',
    f"- profile: `{context.get('profile')}`",
    f"- appid: `{context.get('appid')}`",
    f"- pid: `{context.get('pid')}`",
    f"- target: `{context.get('target_comm')}`",
    f"- label: `{context.get('label')}`",
    f"- xctrace_template: `{context.get('xctrace_template')}`",
    f"- classification_hints: `{', '.join(classification)}`",
    '', '## Exit codes'
]
for k,v in exit_files.items():
    md.append(f'- `{k}`: `{v}`')
md += ['', '## Term counts']
for k,v in sorted(summary['term_counts'].items(), key=lambda kv: (-kv[1], kv[0])):
    md.append(f'- `{k}`: {v}')
md += ['', '## Key artifacts']
for k,v in sorted(artifacts.items()):
    if k in {'summary.json','summary.md'}:
        continue
    md.append(f'- `{k}`: {v} bytes')
if interesting:
    md += ['', '## Interesting examples']
    for ex in interesting[:80]:
        line = ex['line'].replace('`', '\\`')
        md.append(f"- `{ex['file']}`: `{line}`")
(out/'summary.md').write_text('\n'.join(md)+'\n')
PY

log_note "capture complete: $OUT_DIR"
printf '%s\n' "$OUT_DIR" "$OUT_DIR/summary.md" "$OUT_DIR/summary.json"
