#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PROFILE="subnautica2"
APPID="1962700"
PID=""
LABEL="xcodedbg"
OUT_DIR=""
TITLE_MATCH="Subnautica 2"
PROCESS_MATCH="Subnautica2-Win64-Shipping.exe"
SAMPLE_SECONDS=3
LLDB_TIMEOUT_SECONDS=45
LOG_LINES=900
DO_SAMPLE=1

usage() {
  cat <<'USAGE'
usage: turtle-xcodedbg-m12.sh [options]

Fast M12 xcodedbg-style turtle snapshot for an already-running game window.
This script does not launch, relaunch, change env, or kill the game. Run it only after the game is already visible and you explicitly want the debugger attached.

Options:
  --pid PID              Attach to this exact PID after validating it.
  --profile NAME         subnautica2 (default), or manual.
  --appid APPID          Appid for MetalSharp log tails. Default: 1962700.
  --title TEXT           Visible window title substring. Default: "Subnautica 2".
  --match TEXT           Process args substring expected for target PID.
                         Default: "Subnautica2-Win64-Shipping.exe".
  --label NAME           Label for output dir. Default: xcodedbg.
  --out-dir DIR          Output dir. Default: /tmp/metalsharp-xcodedbg-<profile>-<label>-<stamp>
  --sample-seconds N     Run sample before LLDB. Default: 3.
  --no-sample            Skip sample(1); LLDB still runs.
  --lldb-timeout N       Kill debugger if it exceeds N seconds. Default: 45.
  --log-lines N          Tail this many MetalSharp/Saved log lines. Default: 900.
  -h, --help             Show help.

Targeting behavior:
  1. Snapshot visible macOS windows with AppleScript.
  2. Prefer the unix PID that owns a visible window whose title matches --title.
  3. Validate that PID's args contain --match.
  4. If no window PID validates, fall back to ps args matching --match.
  5. Attach via xcrun lldb, capture all thread backtraces/images from the
     stopped attach state, then detach. This is the repo's xcodedbg-style
     turtle snapshot.
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
    --sample-seconds) SAMPLE_SECONDS="$2"; shift 2 ;;
    --no-sample) DO_SAMPLE=0; shift ;;
    --lldb-timeout) LLDB_TIMEOUT_SECONDS="$2"; shift 2 ;;
    --log-lines) LOG_LINES="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$PROFILE" in
  subnautica2|subnautica-2)
    PROFILE="subnautica2"
    APPID="${APPID:-1962700}"
    TITLE_MATCH="${TITLE_MATCH:-Subnautica 2}"
    PROCESS_MATCH="${PROCESS_MATCH:-Subnautica2-Win64-Shipping.exe}"
    ;;
  manual) ;;
  *) ;;
esac

STAMP="$(date +%Y%m%d-%H%M%S)"
if [[ -z "$OUT_DIR" ]]; then
  OUT_DIR="/tmp/metalsharp-xcodedbg-${PROFILE}-${LABEL}-${STAMP}"
fi
mkdir -p "$OUT_DIR/logs"

note() { printf '[xcodedbg-turtle] %s\n' "$*" | tee -a "$OUT_DIR/turtle.log" >&2; }

snapshot_windows() {
  # osascript `log` writes to stderr, so capture both streams into the
  # primary window snapshot. The PID resolver reads this file.
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

snapshot_processes() {
  local suffix="$1"
  ps axww -o pid=,ppid=,stat=,etime=,pcpu=,pmem=,comm=,args= > "$OUT_DIR/processes-${suffix}.txt" || true
}

pid_args() {
  ps -p "$1" -o args= 2>/dev/null || true
}

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
  local candidates candidate count
  candidates="$(ps axww -o pid=,args= | awk -v pat="$PROCESS_MATCH" 'index($0, pat) && $0 !~ /turtle-xcodedbg-m12/ && $0 !~ /awk -v pat/ {print $1}' | sort -u || true)"
  count="$(printf '%s\n' "$candidates" | sed '/^$/d' | wc -l | tr -d ' ')"
  if [[ "$count" != "1" ]]; then
    note "ps fallback found $count candidates for '$PROCESS_MATCH'; refusing ambiguous fallback"
    printf '%s\n' "$candidates" > "$OUT_DIR/ps-fallback-candidates.txt"
    return 1
  fi
  candidate="$candidates"
  if validate_pid "$candidate"; then
    note "selected ps fallback pid=$PID process_match=$PROCESS_MATCH"
    return 0
  fi
  return 1
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
        grep -E -i 'M12|D3D12|DXGI|winemetal|m12core|mscompatdb|classification=|Failed|Fatal|Crash|Exception|not supported|black|present|RHI' "$latest" > "$OUT_DIR/logs/latest-launch.interesting" || true
      fi
    fi
  fi
  local saved="$HOME/.metalsharp/prefix-steam/drive_c/users/alexmondello/AppData/Local/Subnautica2/Saved/Logs/Subnautica2.log"
  if [[ -f "$saved" ]]; then
    printf '%s\n' "$saved" > "$OUT_DIR/latest-saved-log-path.txt"
    tail -n "$LOG_LINES" "$saved" > "$OUT_DIR/logs/Subnautica2.saved.tail" || true
    grep -E -i 'D3D12|DX12|SM6|Wave|RHI|adapter|Fatal|Crash|Exception|RequestExit|not supported|black|Slate|LoadingScreen|Present' "$saved" > "$OUT_DIR/logs/Subnautica2.saved.interesting" || true
  fi
}

write_lldb_commands() {
  cat > "$OUT_DIR/lldb-commands.txt" <<'LLDB'
settings set target.process.thread.step-avoid-regexp ^$
settings set target.memory-module-load-level minimal
settings set symbols.enable-external-lookup false
process status
thread list
thread backtrace all
image list -o -f
image lookup -rn "GXRenderThread|RenderThread|RHIThread|GameThread|Slate|LoadingScreen|D3D12|DXGI|DXMT|winemetal|m12core|Present|ExecuteCommandLists|ExecuteIndirect|Signal|Wait|Fence|WaitUntilCompleted|MTL|AGX|newCommandBuffer|commit|presentDrawable|nextDrawable|newRenderPipeline|newComputePipeline"
detach
quit
LLDB
}

run_lldb_with_timeout() {
  note "attaching lldb/xcodedbg snapshot to validated pid=$PID timeout=${LLDB_TIMEOUT_SECONDS}s"
  set +e
  xcrun lldb -p "$PID" -b -s "$OUT_DIR/lldb-commands.txt" > "$OUT_DIR/lldb.txt" 2> "$OUT_DIR/lldb.err" &
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
    echo "# M12 xcodedbg turtle snapshot"
    echo
    echo "- timestamp: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "- repo: $ROOT_DIR"
    echo "- profile: $PROFILE"
    echo "- appid: ${APPID:-}"
    echo "- pid: $PID"
    echo "- title_match: $TITLE_MATCH"
    echo "- process_match: $PROCESS_MATCH"
    echo "- out_dir: $OUT_DIR"
    echo
    echo "## Target process"
    cat "$OUT_DIR/target-process.txt" 2>/dev/null || true
    echo
    echo "## Window match"
    grep -i "$TITLE_MATCH" "$OUT_DIR/window-snapshot.txt" || true
    echo
    echo "## LLDB exit"
    cat "$OUT_DIR/lldb.exit" 2>/dev/null || true
    echo
    echo "## Interesting thread/frame lines"
    grep -E -i 'thread #|frame #|GXRenderThread|RenderThread|RHIThread|GameThread|D3D12|DXGI|DXMT|winemetal|m12core|Present|ExecuteCommandLists|ExecuteIndirect|Signal|Wait|Fence|MTL|AGX|drawable|semaphore|pthread_cond|mach_msg|__psynch' "$OUT_DIR/lldb.txt" 2>/dev/null | head -240 || true
    echo
    echo "## Latest launch interesting"
    cat "$OUT_DIR/logs/latest-launch.interesting" 2>/dev/null | tail -160 || true
    echo
    echo "## Saved log interesting"
    cat "$OUT_DIR/logs/Subnautica2.saved.interesting" 2>/dev/null | tail -120 || true
  } > "$OUT_DIR/summary.md"
}

snapshot_windows
snapshot_processes before

if [[ -n "$PID" ]]; then
  validate_pid "$PID" || { echo "PID $PID does not validate against process match '$PROCESS_MATCH'" >&2; exit 2; }
else
  infer_pid_from_window || infer_pid_from_ps || { echo "Could not identify target PID for title '$TITLE_MATCH' / process '$PROCESS_MATCH'" >&2; exit 2; }
fi

ps -p "$PID" -o pid=,ppid=,stat=,etime=,pcpu=,pmem=,comm=,args= > "$OUT_DIR/target-process.txt" || true
copy_log_tails

if [[ "$DO_SAMPLE" == "1" ]]; then
  note "running quick sample pid=$PID seconds=$SAMPLE_SECONDS"
  set +e
  sample "$PID" "$SAMPLE_SECONDS" 10 -file "$OUT_DIR/sample.txt" > "$OUT_DIR/sample.stdout" 2> "$OUT_DIR/sample.stderr"
  echo $? > "$OUT_DIR/sample.exit"
  set -e
fi

write_lldb_commands
run_lldb_with_timeout || true
snapshot_processes after
copy_log_tails
write_summary

note "capture complete: $OUT_DIR"
printf '%s\n%s\n' "$OUT_DIR" "$OUT_DIR/summary.md"
