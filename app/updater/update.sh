#!/bin/bash
set -euo pipefail

DMG_PATH=""
BACKEND_PID=""
TARGET_VERSION=""
STATUS_FILE=""
METALSHARP_HOME_ARG=""
APP_PID="0"
APP_BUNDLE_ID="com.metalsharp.app"
MOUNT_POINT=""
APPLICATIONS_APP="/Applications/MetalSharp.app"

write_status() {
    local phase="$1" percent="$2" message="$3" error="${4:-}"
    local ver="$TARGET_VERSION"
    local safe_phase="${phase//\\/\\\\}"
    safe_phase="${safe_phase//\"/\\\"}"
    local safe_message="${message//\\/\\\\}"
    safe_message="${safe_message//\"/\\\"}"
    local safe_ver="${ver//\\/\\\\}"
    safe_ver="${safe_ver//\"/\\\"}"
    local error_json="null"
    if [ -n "$error" ]; then
        local safe_error="${error//\\/\\\\}"
        safe_error="${safe_error//\"/\\\"}"
        error_json="\"$safe_error\""
    fi
    mkdir -p "$(dirname "$STATUS_FILE")" 2>/dev/null || true
    printf '{"phase":"%s","percent":%d,"message":"%s","error":%s,"new_version":"%s","timestamp":%s}\n' \
        "$safe_phase" "$percent" "$safe_message" "$error_json" "$safe_ver" "$(date +%s)" > "$STATUS_FILE" 2>/dev/null || true
}

normalize_version() {
    local value="${1#v}"
    value="${value%%-*}"
    value="${value%%+*}"
    echo "$value" | sed -E 's/[^0-9.].*$//' | sed -E 's/^\.+|\.+$//g'
}

pid_alive() {
    local pid="$1"
    [ -n "$pid" ] && [ "$pid" -gt 0 ] 2>/dev/null && kill -0 "$pid" 2>/dev/null
}

wait_pid_exit() {
    local pid="$1" timeout="${2:-10}"
    local deadline=$((SECONDS + timeout))
    while [ $SECONDS -lt $deadline ]; do
        pid_alive "$pid" || return 0
        sleep 0.25
    done
    pid_alive "$pid" && return 1 || return 0
}

stop_pid() {
    local pid="$1" timeout="${2:-8}"
    pid_alive "$pid" || return 0
    kill "$pid" 2>/dev/null || true
    wait_pid_exit "$pid" "$timeout" || true
    if pid_alive "$pid"; then
        kill -9 "$pid" 2>/dev/null || true
        wait_pid_exit "$pid" 3 || true
    fi
}

wait_processes_gone() {
    local timeout="$1"
    shift
    local deadline=$((SECONDS + timeout))
    local found
    while [ $SECONDS -lt $deadline ]; do
        found=0
        for name in "$@"; do
            if pgrep -x "$name" >/dev/null 2>&1; then
                found=1
                break
            fi
        done
        [ "$found" -eq 0 ] && return 0
        sleep 0.3
    done
    return 1
}

close_app() {
    write_status "closing_app" 25 "Closing MetalSharp..."
    # Let the renderer poller show the closing message before this helper tells
    # Electron to quit.
    sleep 2

    osascript -e "tell application id \"$APP_BUNDLE_ID\" to quit" >/dev/null 2>&1 || true
    stop_pid "$APP_PID" 15

    for name in "MetalSharp" "MetalSharp Helper" "MetalSharp Helper (GPU)" "MetalSharp Helper (Renderer)"; do
        pkill -x "$name" 2>/dev/null || true
    done
    if ! wait_processes_gone 8 "MetalSharp" "MetalSharp Helper" "MetalSharp Helper (GPU)" "MetalSharp Helper (Renderer)"; then
        for name in "MetalSharp" "MetalSharp Helper" "MetalSharp Helper (GPU)" "MetalSharp Helper (Renderer)"; do
            pkill -9 -x "$name" 2>/dev/null || true
        done
        wait_processes_gone 3 "MetalSharp" "MetalSharp Helper" "MetalSharp Helper (GPU)" "MetalSharp Helper (Renderer)" || true
    fi
    write_status "app_closed" 30 "MetalSharp closed."
}

stop_backend() {
    write_status "killing_backend" 35 "Stopping backend..."
    stop_pid "$BACKEND_PID" 8
    pkill -x metalsharp-backend 2>/dev/null || true
    if ! wait_processes_gone 8 metalsharp-backend; then
        pkill -9 -x metalsharp-backend 2>/dev/null || true
        wait_processes_gone 3 metalsharp-backend || true
    fi
    write_status "backend_stopped" 42 "Backend stopped."
}

read_app_version() {
    local app_path="$1"
    /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app_path/Contents/Info.plist" 2>/dev/null || \
        defaults read "$app_path/Contents/Info" CFBundleShortVersionString 2>/dev/null || true
}

verify_app_bundle() {
    local app_path="$1"
    local target_clean="$2"
    local actual
    for required in \
        "$app_path/Contents/Info.plist" \
        "$app_path/Contents/MacOS/MetalSharp" \
        "$app_path/Contents/Resources/runtime/metalsharp-backend"
    do
        if [ ! -s "$required" ]; then
            write_status "error" 78 "Update app is missing ${required#$app_path/}" "app_bundle_invalid"
            return 1
        fi
    done

    if [ ! -s "$app_path/Contents/Resources/scripts/tools/updater/update.sh" ] && \
       [ ! -s "$app_path/Contents/Resources/scripts/tools/updater/update.py" ]; then
        write_status "error" 78 "Update app is missing updater handoff tools" "app_bundle_invalid"
        return 1
    fi

    actual="$(normalize_version "$(read_app_version "$app_path")")"
    if [ -z "$actual" ]; then
        write_status "error" 78 "Update app version could not be read" "app_version_missing"
        return 1
    fi
    if [ "$actual" != "$target_clean" ]; then
        write_status "error" 78 "Update app version $actual does not match target $target_clean" "app_version_mismatch"
        return 1
    fi
}

find_mount_for_dmg() {
    local dmg_path="$1"
    hdiutil info 2>/dev/null | awk -v dmg="$dmg_path" '
        index($0, dmg) > 0 { found = 1 }
        found && /\/Volumes\// {
            idx = index($0, "/Volumes/")
            print substr($0, idx)
            exit
        }
    '
}

mount_dmg() {
    local output
    output="$(hdiutil attach -nobrowse "$DMG_PATH" 2>&1)" || true
    MOUNT_POINT="$(echo "$output" | awk '/\/Volumes\// { idx = index($0, "/Volumes/"); print substr($0, idx); exit }')"
    if [ -z "$MOUNT_POINT" ]; then
        local escaped="${DMG_PATH//\"/\\\"}"
        osascript -e "do shell script \"hdiutil attach -nobrowse \\\"$escaped\\\"\" with administrator privileges" >/dev/null 2>&1 || true
        sleep 2
        MOUNT_POINT="$(find_mount_for_dmg "$DMG_PATH")"
    fi
    [ -n "$MOUNT_POINT" ] && [ -d "$MOUNT_POINT" ]
}

detach_mount() {
    if [ -n "$MOUNT_POINT" ] && [ -d "$MOUNT_POINT" ]; then
        hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
    fi
}

applescript_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

copy_app_bundle() {
    local source="$1" destination="$2"
    mkdir -p "$(dirname "$destination")"

    if /bin/rm -rf "$destination" 2>/dev/null && \
       /usr/bin/ditto "$source" "$destination" 2>/dev/null; then
        /usr/bin/xattr -dr com.apple.quarantine "$destination" >/dev/null 2>&1 || true
        local direct_version
        direct_version="$(normalize_version "$(read_app_version "$source")")"
        [ -n "$direct_version" ] && verify_app_bundle "$destination" "$direct_version" && return 0
    fi

    local cmd escaped source_version
    cmd="/bin/rm -rf $(printf '%q' "$destination") && /usr/bin/ditto $(printf '%q' "$source") $(printf '%q' "$destination") && (/usr/bin/xattr -dr com.apple.quarantine $(printf '%q' "$destination") >/dev/null 2>&1 || true)"
    escaped="$(applescript_escape "$cmd")"
    osascript -e "do shell script \"$escaped\" with administrator privileges" >/dev/null 2>&1 || return 1
    source_version="$(normalize_version "$(read_app_version "$source")")"
    [ -n "$source_version" ] && verify_app_bundle "$destination" "$source_version"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            cat <<'EOF'
usage: update.sh --dmg DMG --backend-pid BACKEND_PID --target-version TARGET_VERSION
                 [--status-file STATUS_FILE] [--metalsharp-home METALSHARP_HOME]
                 [--app-pid APP_PID]

MetalSharp update handoff helper
EOF
            exit 0
            ;;
        --dmg) shift; DMG_PATH="${1:-}"; shift ;;
        --backend-pid) shift; BACKEND_PID="${1:-}"; shift ;;
        --target-version) shift; TARGET_VERSION="${1:-}"; shift ;;
        --status-file) shift; STATUS_FILE="${1:-}"; shift ;;
        --metalsharp-home) shift; METALSHARP_HOME_ARG="${1:-}"; shift ;;
        --app-pid) shift; APP_PID="${1:-0}"; shift ;;
        --) shift; break ;;
        *) shift ;;
    esac
done

MS_DIR="${METALSHARP_HOME_ARG:-${METALSHARP_HOME:-$HOME/.metalsharp}}"
STATUS_FILE="${STATUS_FILE:-$MS_DIR/update_install_status.json}"
DMG_PATH="${DMG_PATH:?--dmg required}"
BACKEND_PID="${BACKEND_PID:?--backend-pid required}"
TARGET_VERSION="${TARGET_VERSION:?--target-version required}"
TARGET_VERSION_CLEAN="$(normalize_version "$TARGET_VERSION")"

case "$DMG_PATH" in
    /*) ;;
    *) DMG_PATH="$(cd "$(dirname "$DMG_PATH")" && pwd)/$(basename "$DMG_PATH")" ;;
esac

if [ -z "$TARGET_VERSION_CLEAN" ]; then
    write_status "error" 0 "Update target version is invalid: $TARGET_VERSION" "target_version_invalid"
    exit 1
fi

write_status "starting" 0 "Starting update handoff..."

close_app
stop_backend

write_status "verifying_dmg" 50 "Verifying update disk image..."
if [ ! -f "$DMG_PATH" ]; then
    write_status "error" 50 "DMG not found: $DMG_PATH" "dmg_not_found"
    exit 1
fi
if ! hdiutil verify "$DMG_PATH" >/dev/null 2>&1; then
    write_status "error" 50 "DMG failed verification: $DMG_PATH" "dmg_verify_failed"
    exit 1
fi

write_status "mounting" 62 "Mounting update disk image..."
if ! mount_dmg; then
    write_status "error" 62 "Failed to mount update DMG." "mount_failed"
    exit 1
fi

write_status "mounted" 75 "Mounted update at $MOUNT_POINT"
APP_SOURCE="$(find "$MOUNT_POINT" -maxdepth 1 -name "*.app" -iname "*metalsharp*" 2>/dev/null | head -1)"
if [ -z "$APP_SOURCE" ]; then
    detach_mount
    write_status "error" 75 "MetalSharp.app not found in update DMG." "app_not_found"
    exit 1
fi

if ! verify_app_bundle "$APP_SOURCE" "$TARGET_VERSION_CLEAN"; then
    detach_mount
    exit 1
fi

mkdir -p "$MS_DIR"
printf '{"needed":true,"target_version":"%s","timestamp":%s}\n' "$TARGET_VERSION" "$(date +%s)" > "$MS_DIR/.post-update-migration" 2>/dev/null || true

write_status "installing_app" 86 "Installing MetalSharp v$TARGET_VERSION_CLEAN to /Applications..."
if ! copy_app_bundle "$APP_SOURCE" "$APPLICATIONS_APP"; then
    detach_mount
    write_status "error" 86 "Failed to replace MetalSharp in /Applications." "app_install_failed"
    exit 1
fi

if ! verify_app_bundle "$APPLICATIONS_APP" "$TARGET_VERSION_CLEAN"; then
    detach_mount
    exit 1
fi

detach_mount

write_status "launching_installed_app" 94 "Opening MetalSharp v$TARGET_VERSION_CLEAN from /Applications..."
if ! open -n "$APPLICATIONS_APP"; then
    write_status "error" 94 "Failed to open MetalSharp from /Applications." "open_failed"
    exit 1
fi

write_status "complete" 100 "Update handoff complete. Opening migration wizard..."
