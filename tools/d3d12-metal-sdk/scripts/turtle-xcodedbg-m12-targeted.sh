#!/usr/bin/env bash
set -euo pipefail

PROFILE="subnautica2"
APPID="1962700"
PID=""
LABEL="targeted"
OUT_DIR=""
TITLE_MATCH="Subnautica 2"
PROCESS_MATCH="Subnautica2-Win64-Shipping.exe"
LLDB_TIMEOUT_SECONDS=35
LOG_LINES=1400
FRAME_LIMIT=48
WINE_UNIX_BINARY="$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix/wine"

usage() {
  cat <<'USAGE'
usage: turtle-xcodedbg-m12-targeted.sh [options]

Targeted xcodedbg-style M12 snapshot for an already-running Subnautica 2
window. This script does NOT launch, relaunch, change env, or kill the game.
Run it only after the game is visibly in the failing state.

Options:
  --pid PID             Attach this exact PID after validating process args.
  --profile NAME        subnautica2 (default), or manual.
  --appid APPID         Appid for log tails. Default: 1962700.
  --title TEXT          Visible window title substring. Default: "Subnautica 2".
  --match TEXT          Process args substring. Default: "Subnautica2-Win64-Shipping.exe".
  --label NAME          Output label. Default: targeted.
  --out-dir DIR         Output dir. Default: /tmp/metalsharp-xcodedbg-targeted-<profile>-<label>-<stamp>
  --timeout N           Debugger timeout seconds. Default: 35.
  --frames N            Max frames per selected thread. Default: 48.
  --log-lines N         Tail lines for launch/Saved logs. Default: 1400.
  -h, --help            Show this help.

What it captures:
  - visible window PID/title before attach
  - validated process args
  - LLDB process status + thread list
  - targeted backtraces only for likely-relevant threads:
      main, GameThread, RHIThread, RenderThread, RHISubmissionThread,
      SlateLoadingThread, CVDisplayLink, dxmt encode/finish, PSO workers,
      async loading, and any thread whose current frame/module mentions
      D3D12/DXGI/DXMT/winemetal/Metal/AGX/present/fence/wait/drawable.
  - image/module list
  - symbol lookups for M12/D3D12/DXGI/winemetal/present/wait terms
  - launch and UE Saved log interesting tails
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pid) PID="$2"; shift 2 ;;
    --profile) PROFILE="$2"; shift 2 ;;
    --appid) APPID="$2"; shift 2 ;;
    --title) TITLE_MATCH="$2"; shift 2 ;;
    --match) PROCESS_MATCH="$2"; shift 2 ;;
    --label) LABEL="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --timeout|--lldb-timeout) LLDB_TIMEOUT_SECONDS="$2"; shift 2 ;;
    --frames) FRAME_LIMIT="$2"; shift 2 ;;
    --log-lines) LOG_LINES="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
  esac
done

STAMP="$(date +%Y%m%d-%H%M%S)"
if [[ -z "$OUT_DIR" ]]; then
  OUT_DIR="/tmp/metalsharp-xcodedbg-targeted-${PROFILE}-${LABEL}-${STAMP}"
fi
mkdir -p "$OUT_DIR/logs"

note() { printf '[xcodedbg-targeted] %s\n' "$*" | tee -a "$OUT_DIR/turtle.log" >&2; }

snapshot_windows() {
  osascript > "$OUT_DIR/window-snapshot.txt" 2>&1 <<'OSA' || true
tell application "System Events"
  repeat with p in (every process whose background only is false)
    try
      set pidText to unix id of p as string
      set pname to name of p as string
      set visibleText to visible of p as string
      repeat with w in windows of p
        try
          set wname to name of w as string
          set wrole to subrole of w as string
          set wpos to position of w as string
          set wsize to size of w as string
          log pidText & tab & pname & tab & visibleText & tab & wname & tab & wrole & tab & wpos & tab & wsize
        end try
      end repeat
    end try
  end repeat
end tell
OSA
}

pid_args() { ps -p "$1" -o args= 2>/dev/null || true; }

validate_pid() {
  local candidate="$1"
  [[ -n "$candidate" && "$candidate" =~ ^[0-9]+$ ]] || return 1
  ps -p "$candidate" >/dev/null 2>&1 || return 1
  local args comm
  args="$(pid_args "$candidate")"
  comm="$(ps -p "$candidate" -o comm= 2>/dev/null || true)"
  [[ "$args" == *"$PROCESS_MATCH"* ]] || return 1
  PID="$candidate"
  {
    echo "selected_pid=$PID"
    echo "selected_comm=$comm"
    echo "selected_args=$args"
    echo "process_match=$PROCESS_MATCH"
  } > "$OUT_DIR/selected-target.txt"
  note "validated target pid=$PID comm=${comm:-unknown} match=$PROCESS_MATCH"
  return 0
}

infer_pid_from_window() {
  local candidates candidate
  candidates="$(awk -F '\t' -v title="$TITLE_MATCH" 'BEGIN{IGNORECASE=1} $4 ~ title {print $1}' "$OUT_DIR/window-snapshot.txt" | tr -d ' ' | sort -u || true)"
  if [[ -z "$candidates" ]]; then
    note "no visible window title matched '$TITLE_MATCH'"
    return 1
  fi
  while IFS= read -r candidate; do
    [[ -z "$candidate" ]] && continue
    if validate_pid "$candidate"; then
      note "selected visible window pid=$PID title_match=$TITLE_MATCH"
      return 0
    fi
    note "window pid=$candidate did not validate against process_match=$PROCESS_MATCH"
  done <<< "$candidates"
  return 1
}

infer_pid_from_ps() {
  local candidates count
  candidates="$(ps axww -o pid=,args= | awk -v pat="$PROCESS_MATCH" 'index($0, pat) && $0 !~ /turtle-xcodedbg-m12-targeted/ && $0 !~ /awk -v pat/ {print $1}' | sort -u || true)"
  count="$(printf '%s\n' "$candidates" | sed '/^$/d' | wc -l | tr -d ' ')"
  if [[ "$count" != "1" ]]; then
    note "ps fallback found $count candidates for '$PROCESS_MATCH'; refusing ambiguous fallback"
    printf '%s\n' "$candidates" > "$OUT_DIR/ps-fallback-candidates.txt"
    return 1
  fi
  validate_pid "$candidates" && note "selected ps fallback pid=$PID process_match=$PROCESS_MATCH"
}

copy_log_tails() {
  if [[ -n "${APPID:-}" ]]; then
    local compat="$HOME/.metalsharp/compatdata/$APPID/logs"
    if [[ -d "$compat" ]]; then
      local latest
      latest="$(ls -t "$compat"/launch-*.log 2>/dev/null | head -1 || true)"
      if [[ -n "$latest" ]]; then
        printf '%s\n' "$latest" > "$OUT_DIR/latest-launch-log-path.txt"
        tail -n "$LOG_LINES" "$latest" > "$OUT_DIR/logs/latest-launch.tail" || true
        rg -n -i 'M12|D3D12|DXGI|winemetal|m12core|mscompatdb|classification=|Failed|Fatal|Crash|Exception|not supported|Present|black|RHI|LoadingScreen|Slate|MoviePlayer|No Window' "$latest" > "$OUT_DIR/logs/latest-launch.interesting" || true
      fi
    fi
  fi
  local saved="$HOME/.metalsharp/prefix-steam/drive_c/users/alexmondello/AppData/Local/Subnautica2/Saved/Logs/Subnautica2.log"
  if [[ -f "$saved" ]]; then
    printf '%s\n' "$saved" > "$OUT_DIR/latest-saved-log-path.txt"
    tail -n "$LOG_LINES" "$saved" > "$OUT_DIR/logs/Subnautica2.saved.tail" || true
    rg -n -i 'D3D12|DX12|SM6|Wave|RHI|adapter|Fatal|Crash|Exception|RequestExit|not supported|black|Slate|LoadingScreen|MoviePlayer|No Window|GameState' "$saved" > "$OUT_DIR/logs/Subnautica2.saved.interesting" || true
  fi
}

write_lldb_python() {
  cat > "$OUT_DIR/lldb_targeted.py" <<'PY'
import lldb
import re

THREAD_NAME_RE = re.compile(
    r"GameThread|RHIThread|RenderThread|RHISubmissionThread|RHIInterruptThread|SlateLoadingThread|CVDisplayLink|dxmt-|PSOPrecompile|FAsyncLoadingThread|GameThread|main-thread|NSEventThread",
    re.I,
)
FRAME_RE = re.compile(
    r"D3D12|DXGI|DXMT|winemetal|m12core|Metal|MTL|AGX|Present|ExecuteCommandLists|ExecuteIndirect|Signal|Wait|Fence|drawable|CommandBuffer|RenderPipeline|ComputePipeline|Slate|LoadingScreen|MoviePlayer|semaphore|pthread|mach_msg|psynch|ulock",
    re.I,
)

def _thread_name(thread):
    return thread.GetName() or thread.GetQueueName() or ""

def _frame_text(frame):
    module = frame.GetModule().GetFileSpec().GetFilename() if frame.GetModule().IsValid() else ""
    function = frame.GetFunctionName() or frame.GetSymbol().GetName() or ""
    file_addr = frame.GetPCAddress().GetLoadAddress(frame.GetThread().GetProcess().GetTarget())
    return f"0x{file_addr:x} {module}`{function}"

def targeted_bt(debugger, command, exe_ctx, result, internal_dict):
    try:
        frame_limit = int(command.strip() or "48")
    except Exception:
        frame_limit = 48
    process = exe_ctx.GetProcess()
    target = exe_ctx.GetTarget()
    selected = []
    seen = set()
    for thread in process:
        name = _thread_name(thread)
        top = _frame_text(thread.GetFrameAtIndex(0)) if thread.GetNumFrames() else ""
        full_scan = "\n".join(_frame_text(thread.GetFrameAtIndex(i)) for i in range(min(thread.GetNumFrames(), 12)))
        if THREAD_NAME_RE.search(name) or FRAME_RE.search(top) or FRAME_RE.search(full_scan):
            tid = thread.GetThreadID()
            if tid not in seen:
                seen.add(tid)
                selected.append(thread)
    result.PutCString(f"TARGETED_THREAD_COUNT {len(selected)} / TOTAL {process.GetNumThreads()}")
    for thread in selected:
        name = _thread_name(thread)
        result.PutCString(f"\n===== THREAD index={thread.GetIndexID()} tid=0x{thread.GetThreadID():x} name={name!r} stop={thread.GetStopDescription(256)} =====")
        for i in range(min(thread.GetNumFrames(), frame_limit)):
            frame = thread.GetFrameAtIndex(i)
            module = frame.GetModule().GetFileSpec().GetFilename() if frame.GetModule().IsValid() else ""
            function = frame.GetFunctionName() or frame.GetSymbol().GetName() or ""
            line = frame.GetLineEntry()
            source = ""
            if line.IsValid():
                source = f" {line.GetFileSpec()}:{line.GetLine()}"
            pc = frame.GetPCAddress().GetLoadAddress(target)
            result.PutCString(f"frame #{i}: 0x{pc:x} {module}`{function}{source}")

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f lldb_targeted.targeted_bt targeted-bt")
PY
}

write_lldb_commands() {
  cat > "$OUT_DIR/lldb-commands.txt" <<LLDB
settings set target.process.thread.step-avoid-regexp ^$
settings set target.memory-module-load-level minimal
settings set symbols.enable-external-lookup false
settings set target.load-script-from-symbol-file false
target create "$WINE_UNIX_BINARY"
process attach --pid $PID
process status
thread list
command script import "$OUT_DIR/lldb_targeted.py"
targeted-bt $FRAME_LIMIT
image list -o -f
image lookup -rn "GXRenderThread|RenderThread|RHIThread|GameThread|Slate|LoadingScreen|MoviePlayer|D3D12|DXGI|DXMT|winemetal|m12core|Present|ExecuteCommandLists|ExecuteIndirect|Signal|Wait|Fence|WaitUntilCompleted|MTL|AGX|drawable|newCommandBuffer|commit|presentDrawable|nextDrawable|semaphore|pthread_cond|mach_msg"
detach
quit
LLDB
}

run_lldb_with_timeout() {
  note "attaching targeted lldb/xcodedbg snapshot to pid=$PID timeout=${LLDB_TIMEOUT_SECONDS}s frames=${FRAME_LIMIT}"
  set +e
  xcrun lldb -b -s "$OUT_DIR/lldb-commands.txt" > "$OUT_DIR/lldb.txt" 2> "$OUT_DIR/lldb.err" &
  local dbg_pid=$!
  local deadline=$((SECONDS + LLDB_TIMEOUT_SECONDS))
  while kill -0 "$dbg_pid" 2>/dev/null; do
    if [[ $SECONDS -ge $deadline ]]; then
      note "lldb timeout; terminating debugger pid=$dbg_pid"
      kill "$dbg_pid" 2>/dev/null || true
      sleep 2
      kill -9 "$dbg_pid" 2>/dev/null || true
      echo timeout > "$OUT_DIR/lldb.exit"
      set -e
      return 124
    fi
    sleep 1
  done
  wait "$dbg_pid"
  local rc=$?
  echo "$rc" > "$OUT_DIR/lldb.exit"
  set -e
  return 0
}

write_summary() {
  {
    echo "# Targeted M12 xcodedbg turtle snapshot"
    echo
    echo "- timestamp: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "- profile: $PROFILE"
    echo "- appid: ${APPID:-}"
    echo "- pid: $PID"
    echo "- title_match: $TITLE_MATCH"
    echo "- process_match: $PROCESS_MATCH"
    echo "- out_dir: $OUT_DIR"
    echo "- lldb_exit: $(cat "$OUT_DIR/lldb.exit" 2>/dev/null || true)"
    echo
    echo "## Target process"
    cat "$OUT_DIR/target-process.txt" 2>/dev/null || true
    echo
    echo "## Window match"
    grep -i "$TITLE_MATCH" "$OUT_DIR/window-snapshot.txt" || true
    echo
    echo "## Targeted thread count"
    rg 'TARGETED_THREAD_COUNT' "$OUT_DIR/lldb.txt" || true
    echo
    echo "## Targeted thread headers"
    rg '^===== THREAD' "$OUT_DIR/lldb.txt" || true
    echo
    echo "## Most relevant frames"
    rg -n -i '===== THREAD|GameThread|RHIThread|RenderThread|RHISubmissionThread|SlateLoadingThread|CVDisplayLink|dxmt-|D3D12|DXGI|DXMT|winemetal|Present|ExecuteCommandLists|Signal|Wait|Fence|MTL|AGX|drawable|LoadingScreen|MoviePlayer|semaphore|pthread|mach_msg|psynch|ulock' "$OUT_DIR/lldb.txt" | head -320 || true
    echo
    echo "## Latest launch interesting tail"
    tail -180 "$OUT_DIR/logs/latest-launch.interesting" 2>/dev/null || true
    echo
    echo "## Saved log interesting tail"
    tail -140 "$OUT_DIR/logs/Subnautica2.saved.interesting" 2>/dev/null || true
  } > "$OUT_DIR/summary.md"
}

snapshot_windows
ps axww -o pid=,ppid=,stat=,etime=,pcpu=,pmem=,comm=,args= > "$OUT_DIR/processes-before.txt" || true
if [[ -n "$PID" ]]; then
  validate_pid "$PID" || { echo "PID $PID does not validate against '$PROCESS_MATCH'" >&2; exit 2; }
else
  infer_pid_from_window || infer_pid_from_ps || { echo "Could not identify target PID for title '$TITLE_MATCH' / process '$PROCESS_MATCH'" >&2; exit 2; }
fi
ps -p "$PID" -o pid=,ppid=,stat=,etime=,pcpu=,pmem=,comm=,args= > "$OUT_DIR/target-process.txt" || true
copy_log_tails
write_lldb_python
write_lldb_commands
run_lldb_with_timeout || true
ps axww -o pid=,ppid=,stat=,etime=,pcpu=,pmem=,comm=,args= > "$OUT_DIR/processes-after.txt" || true
copy_log_tails
write_summary
note "capture complete: $OUT_DIR"
printf '%s\n%s\n' "$OUT_DIR" "$OUT_DIR/summary.md"
