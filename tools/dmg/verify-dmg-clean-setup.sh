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
compare_tree() {
  local source_root="$1"
  local target_root="$2"
  while IFS= read -r -d '' source; do
    relative="${source#"$source_root/"}"
    cmp "$source" "$target_root/$relative"
  done < <(find "$source_root" -type f -print0)
}

# Canonical runtime lanes must be byte-identical to the archive embedded in the DMG.
compare_tree "$EXTRACT_DIR/Graphics/dll/dxmt" "$HOME_DIR/runtime/wine/lib/dxmt"
compare_tree "$EXTRACT_DIR/Graphics/dll/dxmt_m12" "$HOME_DIR/runtime/wine/lib/dxmt_m12"

# Wine's active loader mirrors must use the promoted M12 bridge, not a stale runtime copy.
cmp "$EXTRACT_DIR/Graphics/dll/dxmt_m12/x86_64-unix/winemetal.so" \
  "$HOME_DIR/runtime/wine/lib/wine/x86_64-unix/winemetal.so"
cmp "$EXTRACT_DIR/Graphics/dll/dxmt_m12/x86_64-windows/winemetal.dll" \
  "$HOME_DIR/runtime/wine/lib/wine/x86_64-windows/winemetal.dll"

stage_test_bottle() {
  local appid="$1"
  local profile="$2"
  local executable="$3"
  local prefix="$HOME_DIR/clean-dmg-prefix-$profile"
  local game="$HOME_DIR/clean-dmg-game-$profile"
  local bottle="$HOME_DIR/bottles/steam_$appid"
  mkdir -p "$prefix" "$game" "$bottle"
  printf 'clean DMG direct launch probe\n' >"$game/$executable"
  python3 - "$bottle/bottle.json" "$appid" "$profile" "$prefix" "$game" <<'PY'
import json, sys
path, appid, profile, prefix, game = sys.argv[1:]
with open(path, "w") as output:
    json.dump({
        "id": f"steam_{appid}",
        "name": f"Clean DMG {profile.upper()} probe",
        "custom_name": None,
        "bottle_type": "steam",
        "steam_app_id": int(appid),
        "prefix_path": prefix,
        "arch": "wow64" if profile == "m12" else "win64",
        "runtime_profile": profile,
        "preferred_pipeline": profile,
        "installed_components": [],
        "source_installer_path": None,
        "installer_kind": None,
        "game_install_path": game,
        "runtime_assets": [],
        "installed_app_detections": [],
        "health": "ready",
        "last_launch_log": None,
        "last_launch_pid": None,
        "last_launch_status": None,
        "last_launch_finished_at": None,
        "created_at": "0",
        "updated_at": "0",
    }, output)
PY
  curl -fsS -H 'Content-Type: application/json' -X POST \
    -d "{\"id\":\"steam_$appid\",\"profile\":\"$profile\"}" \
    "http://127.0.0.1:$PORT/bottles/set-runtime-profile" >"$HOME_DIR/save-$profile.json"
  python3 - "$HOME_DIR/save-$profile.json" <<'PY'
import json, sys
response = json.load(open(sys.argv[1]))
assert response.get("ok") is True, response
PY
}

stage_test_bottle 1245620 m12 start_protected_game.exe
stage_test_bottle 312520 m11 RainWorld.exe

# Prefix and game-local routes must also be exact archive bytes. These two live
# acceptance shapes cover the promoted D3D12 and D3D11 x64 surfaces.
for entry in \
  d3d12.dll d3d11.dll d3d10core.dll dxgi.dll dxgi_dxmt.dll winemetal.dll nvapi64.dll nvngx.dll
do
  cmp "$EXTRACT_DIR/Graphics/dll/dxmt_m12/x86_64-windows/$entry" \
    "$HOME_DIR/clean-dmg-prefix-m12/drive_c/windows/system32/$entry"
  cmp "$EXTRACT_DIR/Graphics/dll/dxmt_m12/x86_64-windows/$entry" \
    "$HOME_DIR/clean-dmg-game-m12/$entry"
done
for entry in d3d11.dll d3d10core.dll dxgi.dll dxgi_dxmt.dll winemetal.dll; do
  cmp "$EXTRACT_DIR/Graphics/dll/dxmt/x86_64-windows/$entry" \
    "$HOME_DIR/clean-dmg-prefix-m11/drive_c/windows/system32/$entry"
  cmp "$EXTRACT_DIR/Graphics/dll/dxmt/x86_64-windows/$entry" \
    "$HOME_DIR/clean-dmg-game-m11/$entry"
done
test ! -e "$HOME_DIR/clean-dmg-prefix-m11/drive_c/windows/system32/d3d12.dll"
test ! -e "$HOME_DIR/clean-dmg-game-m11/d3d12.dll"

echo "DMG clean setup verified: 15/15 steps, no fallback downloads, exact canonical, Wine-mirror, prefix, and game payloads."
