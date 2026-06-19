#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PID=""
APPID=""
PROFILE="manual"
OUT_DIR=""
DO_SAMPLE=1
DO_SPINDUMP=0
DO_LLDB=1
SAMPLE_SECONDS=2

usage() {
  cat <<'USAGE'
usage: capture-m12-hang-debugger.sh (--pid PID | --appid APPID) [options]

Low-overhead post-hang debugger capture for M12 game stalls. This does not
launch games and does not require pre-click D3D12/DXGI logging. Run it after the
hang/stall is visible.

Options:
  --pid PID             Target process ID, e.g. the start_protected_game.exe wine PID.
  --appid APPID         Infer newest registered/running process for appid when possible.
  --profile NAME        Label for output directory. Default: manual.
  --out-dir DIR         Output directory. Default under tools/d3d12-metal-sdk/results/.
  --no-lldb             Skip LLDB interrupt/backtrace capture.
  --no-sample           Skip sample(1) capture.
  --spindump            Also run spindump. Heavier; off by default.
  --sample-seconds N    sample(1) duration. Default: 2.
  -h, --help            Show this help.

Examples:
  capture-m12-hang-debugger.sh --pid 12345 --profile ac6-world-load
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pid) PID="$2"; shift 2 ;;
    --appid) APPID="$2"; shift 2 ;;
    --profile) PROFILE="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --no-lldb) DO_LLDB=0; shift ;;
    --no-sample) DO_SAMPLE=0; shift ;;
    --spindump) DO_SPINDUMP=1; shift ;;
    --sample-seconds) SAMPLE_SECONDS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$PID" && -n "$APPID" ]]; then
  # Prefer obvious launched game wrappers. This avoids requiring backend APIs.
  PID="$(ps ax -o pid=,ppid=,stat=,comm=,args= | awk 'BEGIN{IGNORECASE=1} /start_protected_game|armoredcore|eldenring|elden/ {print $1}' | tail -1 || true)"
fi
if [[ -z "$PID" ]]; then
  echo "--pid required unless --appid can infer a process" >&2
  exit 2
fi
if ! ps -p "$PID" >/dev/null 2>&1; then
  echo "target pid not running: $PID" >&2
  exit 1
fi

if [[ -z "$OUT_DIR" ]]; then
  TS="$(date +%Y%m%d-%H%M%S)"
  OUT_DIR="$ROOT/tools/d3d12-metal-sdk/results/m12-debugger-capture-${PROFILE}-${TS}"
fi
mkdir -p "$OUT_DIR"

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "root=$ROOT"
  echo "pid=$PID"
  echo "profile=$PROFILE"
  echo "sample_seconds=$SAMPLE_SECONDS"
  echo "do_lldb=$DO_LLDB"
  echo "do_sample=$DO_SAMPLE"
  echo "do_spindump=$DO_SPINDUMP"
  echo "xcode=$(xcode-select -p 2>/dev/null || true)"
  echo "lldb=$(xcrun -f lldb 2>/dev/null || true)"
  echo "xctrace=$(xcrun -f xctrace 2>/dev/null || true)"
  echo "process:"
  ps -p "$PID" -o pid=,ppid=,stat=,etime=,comm=,args= || true
} > "$OUT_DIR/context.txt"

ps ax -o pid=,ppid=,stat=,etime=,comm=,args= > "$OUT_DIR/process-list.txt" || true
vmmap "$PID" > "$OUT_DIR/vmmap.txt" 2> "$OUT_DIR/vmmap.err" || true

if [[ "$DO_LLDB" == "1" ]]; then
  LLDB_CMDS="$OUT_DIR/lldb-commands.txt"
  cat > "$LLDB_CMDS" <<'LLDB'
settings set target.process.thread.step-avoid-regexp ^$
settings set target.memory-module-load-level minimal
process interrupt
thread list
thread backtrace all
register read
image list
image lookup -rn "m12core|newLibrary|Create.*Pipeline|ExecuteCommandLists|Signal|Wait|Map|ApplyVertexBuffers|DrawIndexed|DrawInstanced|Present"
detach
quit
LLDB
  set +e
  xcrun lldb -p "$PID" -b -s "$LLDB_CMDS" > "$OUT_DIR/lldb.txt" 2> "$OUT_DIR/lldb.err"
  echo $? > "$OUT_DIR/lldb.exit"
  set -e
fi

if [[ "$DO_SAMPLE" == "1" ]]; then
  set +e
  sample "$PID" "$SAMPLE_SECONDS" 10 -file "$OUT_DIR/sample.txt" > "$OUT_DIR/sample.stdout" 2> "$OUT_DIR/sample.stderr"
  echo $? > "$OUT_DIR/sample.exit"
  set -e
fi

if [[ "$DO_SPINDUMP" == "1" ]]; then
  set +e
  spindump "$PID" 5 1 -file "$OUT_DIR/spindump.txt" > "$OUT_DIR/spindump.stdout" 2> "$OUT_DIR/spindump.stderr"
  echo $? > "$OUT_DIR/spindump.exit"
  set -e
fi

python3 - <<'PY' "$OUT_DIR"
from pathlib import Path
import sys,re
out=Path(sys.argv[1])
summary=[]
summary.append('# M12 debugger hang capture')
summary.append('')
summary.append((out/'context.txt').read_text(errors='replace'))
for name in ['lldb.txt','lldb.err','sample.txt','sample.stderr','spindump.txt','spindump.stderr']:
    p=out/name
    if not p.exists():
        continue
    text=p.read_text(errors='replace')
    summary.append(f'## {name}')
    summary.append(f'- bytes: `{p.stat().st_size}`')
    needles=['m12core','newLibrary','CreateRenderPipeline','CreateComputePipeline','ExecuteCommandLists','ApplyVertexBuffers','DrawIndexed','DrawInstanced','Fence','Signal','Wait','Map FAILED','AGX','Metal','mach_msg','semaphore','pthread']
    counts={n:text.count(n) for n in needles}
    summary.append('- counts: '+', '.join(f'`{k}`={v}' for k,v in counts.items() if v))
    interesting=[]
    for line in text.splitlines():
        if any(n in line for n in needles):
            interesting.append(line[:300])
            if len(interesting)>=80:
                break
    if interesting:
        summary.append('')
        summary.extend(interesting)
        summary.append('')
(out/'summary.md').write_text('\n'.join(summary)+'\n')
print(out)
print(out/'summary.md')
PY
