#!/bin/bash
set -euo pipefail

DMG_PATH=""
BACKEND_PID=""
TARGET_VERSION=""
STATUS_FILE=""

write_status() {
    local phase="$1" percent="$2" message="$3" error="${4:-}"
    printf '{"phase":"%s","percent":%d,"message":"%s","error":%s,"new_version":"%s","timestamp":%s}\n' \
        "$phase" "$percent" "$message" "$([ -n "$error" ] && echo "\"$error\"" || echo "null")" \
        "$TARGET_VERSION" "$(date +%s)" > "$STATUS_FILE" 2>/dev/null || true
}

kill_pid() {
    local pid="$1" timeout="${2:-10}"
    kill "$pid" 2>/dev/null || true
    local deadline=$((SECONDS + timeout))
    while [ $SECONDS -lt $deadline ]; do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.3
    done
    kill -9 "$pid" 2>/dev/null || true
    sleep 0.5
    kill -0 "$pid" 2>/dev/null && return 1 || return 0
}

for arg in "$@"; do
    case "$arg" in
        --dmg)         shift; DMG_PATH="$1"; shift ;;
        --backend-pid)  shift; BACKEND_PID="$1"; shift ;;
        --target-version) shift; TARGET_VERSION="$1"; shift ;;
        --status-file)  shift; STATUS_FILE="$1"; shift ;;
        --) shift; break ;;
    esac
done

STATUS_FILE="${STATUS_FILE:-$HOME/.metalsharp/update_install_status.json}"
DMG_PATH="${DMG_PATH:?--dmg required}"
BACKEND_PID="${BACKEND_PID:?--backend-pid required}"
TARGET_VERSION="${TARGET_VERSION:?--target-version required}"
APP_PATH="/Applications/MetalSharp.app"
MOUNT_POINT=""

cleanup() {
    if [ -n "$MOUNT_POINT" ] && [ -d "$MOUNT_POINT" ]; then
        hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
    fi
}
trap cleanup EXIT

write_status "starting" 0 "Starting update..." "new_version=$TARGET_VERSION"

write_status "killing_backend" 5 "Stopping backend (PID $BACKEND_PID)..."
kill_pid "$BACKEND_PID" 10
pkill -x metalsharp-backend 2>/dev/null || true
sleep 0.5

write_status "killing_steam" 15 "Stopping Steam and Wine processes..."
for pat in steam steam.exe steamwebhelper steamwebhelper.exe wine wine64 wineserver; do
    pkill -x "$pat" 2>/dev/null || true
    pkill -f "$pat" 2>/dev/null || true
done
sleep 1

write_status "closing_app" 25 "Closing MetalSharp..."
for pat in "MetalSharp" "MetalSharp Helper" "MetalSharp Helper (GPU)" "MetalSharp Helper (Renderer)"; do
    pkill -x "$pat" 2>/dev/null || true
done
deadline=$((SECONDS + 15))
while [ $SECONDS -lt $deadline ]; do
    pgrep -x "MetalSharp" >/dev/null 2>&1 || break
    sleep 0.5
done
killall -9 "MetalSharp" 2>/dev/null || true
sleep 2

write_status "verifying_dmg" 30 "Verifying DMG..."
if [ ! -f "$DMG_PATH" ]; then
    write_status "error" 30 "DMG not found: $DMG_PATH" "dmg_not_found"
    exit 1
fi

write_status "mounting" 35 "Mounting update disk image..."
MOUNT_OUTPUT=$(hdiutil attach -nobrowse -quiet "$DMG_PATH" 2>&1) || {
    MOUNT_OUTPUT=$(osascript -e "do shell script \"hdiutil attach -nobrowse -quiet \\\"$DMG_PATH\\\"\" with administrator privileges" 2>&1) || true
    sleep 2
}

MOUNT_POINT=$(echo "$MOUNT_OUTPUT" | awk '{print $NF}' | grep '/Volumes/')
if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT" ]; then
    MOUNT_POINT=$(hdiutil info 2>/dev/null | grep -A1 "$(basename "$DMG_PATH")" | grep '/Volumes/' | awk '{print $NF}' | head -1)
fi

if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT" ]; then
    write_status "error" 40 "Failed to mount DMG" "mount_failed"
    exit 1
fi

write_status "mounted" 45 "Mounted at $MOUNT_POINT"

APP_SOURCE=$(find "$MOUNT_POINT" -maxdepth 1 -name "*.app" -iname "*metalsharp*" 2>/dev/null | head -1)
if [ -z "$APP_SOURCE" ]; then
    write_status "error" 50 "MetalSharp.app not found in update" "app_not_found"
    exit 1
fi

write_status "installing" 50 "Removing old version..."
if [ -d "$APP_PATH" ]; then
    rm -rf "$APP_PATH" 2>/dev/null || {
        osascript -e "do shell script \"rm -rf \\\"$APP_PATH\\\"\" with administrator privileges" 2>/dev/null || true
        sleep 1
    }
    [ -d "$APP_PATH" ] && {
        write_status "error" 55 "Failed to remove old app" "remove_failed"
        exit 1
    }
fi

write_status "installing" 60 "Installing new version..."
cp -R "$APP_SOURCE" "$APP_PATH" 2>/dev/null || {
    osascript -e "do shell script \"cp -R \\\"$APP_SOURCE\\\" \\\"$APP_PATH\\\"\" with administrator privileges" 2>/dev/null || true
    sleep 2
}

if [ ! -d "$APP_PATH" ]; then
    write_status "error" 65 "Failed to install new version" "copy_failed"
    exit 1
fi

write_status "installed" 80 "New version installed"

write_status "unmounting" 82 "Unmounting update disk..."
hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
MOUNT_POINT=""

write_status "relaunching" 85 "Launching MetalSharp..."
sleep 1
open "$APP_PATH"

write_status "verifying" 90 "Verifying installation..."
sleep 5

BACKEND_VERSION=""
deadline=$((SECONDS + 45))
while [ $SECONDS -lt $deadline ]; do
    BACKEND_VERSION=$(curl -sf "http://127.0.0.1:9274/status" 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin).get('version',''))" 2>/dev/null || true)
    [ -n "$BACKEND_VERSION" ] && break
    sleep 1
done

if [ "$BACKEND_VERSION" != "$TARGET_VERSION" ] && [ -n "$BACKEND_VERSION" ]; then
    write_status "deploying_backend" 92 "Backend version mismatch, redeploying..."
    pkill -x metalsharp-backend 2>/dev/null || true
    sleep 1
    open -a "MetalSharp"
    sleep 5
    BACKEND_VERSION=$(curl -sf "http://127.0.0.1:9274/status" 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin).get('version',''))" 2>/dev/null || true)
fi

if [ "$BACKEND_VERSION" = "$TARGET_VERSION" ]; then
    write_status "complete" 100 "Successfully updated to v${TARGET_VERSION}!"
else
    write_status "complete" 100 "Update installed. Backend: v${BACKEND_VERSION:-?} (expected v${TARGET_VERSION})"
fi
