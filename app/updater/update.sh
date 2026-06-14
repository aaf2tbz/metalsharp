#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON_BIN="${METALSHARP_UPDATER_PYTHON:-}"

if [ -z "$PYTHON_BIN" ]; then
    if command -v python3 >/dev/null 2>&1; then
        PYTHON_BIN="$(command -v python3)"
    elif [ -x /usr/bin/python3 ]; then
        PYTHON_BIN="/usr/bin/python3"
    fi
fi

if [ -z "$PYTHON_BIN" ]; then
    STATUS_FILE=""
    TARGET_VERSION=""
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --status-file) shift; STATUS_FILE="${1:-}"; shift ;;
            --target-version) shift; TARGET_VERSION="${1:-}"; shift ;;
            *) shift ;;
        esac
    done
    if [ -n "$STATUS_FILE" ]; then
        mkdir -p "$(dirname "$STATUS_FILE")" 2>/dev/null || true
        printf '{"phase":"error","percent":0,"message":"python3 is required for update handoff","error":"python3_missing","new_version":"%s","timestamp":%s}\n' \
            "$TARGET_VERSION" "$(date +%s)" > "$STATUS_FILE" 2>/dev/null || true
    fi
    exit 1
fi

exec "$PYTHON_BIN" "$SCRIPT_DIR/update.py" "$@"
