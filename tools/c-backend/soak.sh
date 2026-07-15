#!/usr/bin/env bash
set -euo pipefail

backend=${1:?usage: soak.sh <metalsharp-backend>}
test -x "$backend"
tmp_home=$(mktemp -d)
log=$(mktemp)
HOME="$tmp_home" "$backend" >"$log" 2>&1 &
pid=$!
cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  rm -rf "$tmp_home" "$log"
}
trap cleanup EXIT

for _ in $(seq 1 40); do
  if curl --fail --silent --show-error http://127.0.0.1:9274/status >/dev/null; then
    break
  fi
  sleep 0.25
done
curl --fail --silent --show-error http://127.0.0.1:9274/status >/dev/null
# Keep the backend alive through a bounded soak period and recheck readiness.
for _ in $(seq 1 20); do
  kill -0 "$pid"
  sleep 0.25
done
curl --fail --silent --show-error http://127.0.0.1:9274/status >/dev/null
kill -TERM "$pid"
for _ in $(seq 1 40); do
  ! kill -0 "$pid" 2>/dev/null && exit 0
  sleep 0.25
done
cat "$log" >&2
exit 1
