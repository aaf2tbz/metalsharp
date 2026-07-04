#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUST_DIR="$ROOT/app/src-rust"
BACKEND="$RUST_DIR/target/debug/metalsharp-backend"
LOG_DIR="${TMPDIR:-/tmp}"
STDOUT_LOG="$LOG_DIR/metalsharp-wine20-local-backend.out"
STDERR_LOG="$LOG_DIR/metalsharp-wine20-local-backend.err"

PORT="${METALSHARP_PORT:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(python3 - <<'PY'
import socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
PY
)"
fi

cleanup() {
  if [[ -n "${BACKEND_PID:-}" ]]; then
    kill "$BACKEND_PID" 2>/dev/null || true
    wait "$BACKEND_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

echo "building debug backend..."
(cd "$RUST_DIR" && cargo build)

echo "starting temporary backend on 127.0.0.1:$PORT..."
(
  cd "$RUST_DIR"
  METALSHARP_PORT="$PORT" "$BACKEND" >"$STDOUT_LOG" 2>"$STDERR_LOG"
) &
BACKEND_PID=$!

for _ in $(seq 1 80); do
  if curl -fsS "http://127.0.0.1:$PORT/status" >/dev/null 2>&1; then
    "$ROOT/tools/runtime/check-wine20-runtime-readiness.sh" --url "http://127.0.0.1:$PORT"
    exit 0
  fi
  if ! kill -0 "$BACKEND_PID" 2>/dev/null; then
    echo "backend exited before readiness check" >&2
    echo "stderr: $STDERR_LOG" >&2
    tail -40 "$STDERR_LOG" >&2 || true
    exit 1
  fi
  sleep 0.25
done

echo "backend did not become ready on port $PORT" >&2
echo "stdout: $STDOUT_LOG" >&2
echo "stderr: $STDERR_LOG" >&2
tail -40 "$STDERR_LOG" >&2 || true
exit 1
