#!/usr/bin/env bash
set -euo pipefail

DMG="${1:?usage: tools/dmg/verify-dmg-clean-setup.sh path/to/MetalSharp.dmg}"
MOUNT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-setup-mount.XXXXXX")"
HOME_DIR="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-setup-home.XXXXXX")"
EXTRACT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-setup-archive.XXXXXX")"
PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
BACKEND_PID=""

cleanup() {
  if [ -n "$BACKEND_PID" ]; then
    kill "$BACKEND_PID" 2>/dev/null || true
    wait "$BACKEND_PID" 2>/dev/null || true
  fi
  hdiutil detach "$MOUNT_DIR" -quiet 2>/dev/null || true
  rm -rf "$MOUNT_DIR" "$HOME_DIR" "$EXTRACT_DIR"
}
trap cleanup EXIT

hdiutil attach "$DMG" -mountpoint "$MOUNT_DIR" -nobrowse -quiet
APP_DIR="$(find "$MOUNT_DIR" -maxdepth 1 -name '*.app' -type d | head -n 1)"
BACKEND="$APP_DIR/Contents/Resources/runtime/metalsharp-backend"
BUNDLE="$APP_DIR/Contents/Resources/bundles/metalsharp-graphics-dll.tar.zst"
test -x "$BACKEND"
test -s "$BUNDLE"

METALSHARP_HOME="$HOME_DIR" METALSHARP_PORT="$PORT" "$BACKEND" >"$HOME_DIR/backend.log" 2>&1 &
BACKEND_PID=$!
for _ in $(seq 1 80); do
  curl -fsS "http://127.0.0.1:$PORT/status" >"$HOME_DIR/status.json" 2>/dev/null && break
  sleep 0.25
done
test -s "$HOME_DIR/status.json"
curl -fsS -X POST "http://127.0.0.1:$PORT/setup/install-all" >/dev/null

for _ in $(seq 1 300); do
  status="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status", ""))' "$HOME_DIR/install_progress.json" 2>/dev/null || true)"
  [ "$status" = complete ] && break
  if [ "$status" = failed ]; then
    cat "$HOME_DIR/install_progress.json" >&2
    exit 1
  fi
  sleep 1
done
python3 - "$HOME_DIR/install_progress.json" <<'PY'
import json, sys
progress = json.load(open(sys.argv[1]))
assert progress["status"] == "complete", progress
assert progress["step"] == progress["total"] == 15, progress
PY

curl -fsS "http://127.0.0.1:$PORT/setup/dependencies" >"$HOME_DIR/dependencies.json"
python3 - "$HOME_DIR/dependencies.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert data.get("allInstalled") is True, data
deps = {item["id"]: item for item in data["dependencies"]}
for key in ("dxmt_runtime", "dxmt_m12_runtime"):
    assert deps[key]["installed"] is True, deps[key]
    assert deps[key]["status"]["current"] is True, deps[key]
    assert deps[key]["status"]["filesReady"] is True, deps[key]
PY

if find "$HOME_DIR/cache/bundles" -type f 2>/dev/null | grep -q .; then
  echo "clean DMG setup unexpectedly downloaded a fallback bundle" >&2
  exit 1
fi

tar --use-compress-program=unzstd -xf "$BUNDLE" -C "$EXTRACT_DIR"
while IFS= read -r -d '' source; do
  relative="${source#"$EXTRACT_DIR/Graphics/dll/dxmt-m12/"}"
  cmp "$source" "$HOME_DIR/runtime/wine/lib/dxmt_m12/$relative"
done < <(find "$EXTRACT_DIR/Graphics/dll/dxmt-m12" -type f -print0)

echo "DMG clean setup verified: 15/15 steps, no fallback downloads, exact M12 payload."
