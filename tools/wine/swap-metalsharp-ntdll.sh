#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERIFY="$SCRIPT_DIR/verify-ubisoft-ntdll.sh"
DEFAULT_RUNTIME="$HOME/.metalsharp/runtime/wine"
ACTION="${1:-}"
shift || true
RUNTIME="$DEFAULT_RUNTIME"
CANDIDATE=""
BACKUP_DIR=""

usage() {
  cat <<'USAGE'
Usage:
  tools/wine/swap-metalsharp-ntdll.sh install --candidate PATH --backup-dir PATH [--runtime PATH]
  tools/wine/swap-metalsharp-ntdll.sh restore --backup-dir PATH [--runtime PATH]

Install backs up the current x86_64 Unix ntdll and atomically swaps a verified
candidate. Restore verifies the saved hash and atomically restores the backup.
The backup directory must be outside the Wine runtime.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --runtime) RUNTIME="$2"; shift 2 ;;
    --candidate) CANDIDATE="$2"; shift 2 ;;
    --backup-dir) BACKUP_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$ACTION" in install|restore) ;; *) usage >&2; exit 2 ;; esac
[ -n "$BACKUP_DIR" ] || { echo "--backup-dir is required" >&2; exit 2; }
command -v python3 >/dev/null || { echo "required tool missing: python3" >&2; exit 1; }
TARGET="$RUNTIME/lib/wine/x86_64-unix/ntdll.so"
[ -s "$TARGET" ] || { echo "installed ntdll missing: $TARGET" >&2; exit 1; }

runtime_real="$(cd "$RUNTIME" && pwd -P)"
backup_real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$BACKUP_DIR")"
case "$backup_real" in
  "$runtime_real"|"$runtime_real"/*) echo "backup directory must be outside the Wine runtime" >&2; exit 1 ;;
esac
if [ "$ACTION" = install ]; then
  [ -d "$(dirname "$BACKUP_DIR")" ] || { echo "backup parent directory not found: $(dirname "$BACKUP_DIR")" >&2; exit 1; }
  if [ -e "$BACKUP_DIR" ] || [ -L "$BACKUP_DIR" ]; then
    echo "install requires a new, non-existing backup directory: $BACKUP_DIR" >&2
    exit 1
  fi
else
  [ -d "$BACKUP_DIR" ] || { echo "backup directory not found: $BACKUP_DIR" >&2; exit 1; }
  [ "$(cd "$BACKUP_DIR" && pwd -P)" = "$backup_real" ] || { echo "backup directory does not resolve safely" >&2; exit 1; }
fi

wineserver_pattern="$(python3 -c 'import re,sys; print(re.escape(sys.argv[1]))' "$runtime_real/bin/wineserver")"
if pgrep -f "$wineserver_pattern" >/dev/null 2>&1; then
  echo "MetalSharp wineserver is running; stop its prefixes before swapping ntdll" >&2
  exit 1
else
  pgrep_status=$?
  [ "$pgrep_status" -eq 1 ] || { echo "could not inspect running wineserver processes" >&2; exit 1; }
fi

if [ "$ACTION" = install ]; then
  [ -n "$CANDIDATE" ] || { echo "--candidate is required for install" >&2; exit 2; }
  candidate_real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$CANDIDATE")"
  target_real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$TARGET")"
  [ "$candidate_real" != "$target_real" ] || { echo "candidate must not be the installed ntdll" >&2; exit 1; }
  candidate_hash_before="$(shasum -a 256 "$CANDIDATE" | awk '{print $1}')"
  "$VERIFY" "$CANDIDATE" "$TARGET"
  verified_candidate_hash="$(shasum -a 256 "$CANDIDATE" | awk '{print $1}')"
  [ "$verified_candidate_hash" = "$candidate_hash_before" ] || { echo "candidate changed during verification" >&2; exit 1; }
  mkdir -m 700 "$BACKUP_DIR"
  [ "$(cd "$BACKUP_DIR" && pwd -P)" = "$backup_real" ] || { echo "backup path changed while creating it" >&2; exit 1; }
fi

atomic_replace() {
  local source="$1" target="$2" expected_hash="$3" temp actual_hash
  temp="$target.swap.$$"
  trap 'rm -f "$temp"' EXIT
  cp -p "$source" "$temp"
  chmod 755 "$temp"
  actual_hash="$(shasum -a 256 "$temp" | awk '{print $1}')"
  [ "$actual_hash" = "$expected_hash" ] || { echo "replacement changed after verification" >&2; exit 1; }
  codesign --verify --strict "$temp"
  mv -f "$temp" "$target"
  trap - EXIT
}

if [ "$ACTION" = install ]; then
  cp -p "$TARGET" "$BACKUP_DIR/original-ntdll.so"
  shasum -a 256 "$BACKUP_DIR/original-ntdll.so" > "$BACKUP_DIR/original-ntdll.so.sha256"
  printf '%s  %s\n' "$verified_candidate_hash" "$TARGET" > "$BACKUP_DIR/installed-ntdll.so.sha256.pending"
  mv "$BACKUP_DIR/installed-ntdll.so.sha256.pending" "$BACKUP_DIR/installed-ntdll.so.sha256"
  atomic_replace "$CANDIDATE" "$TARGET" "$verified_candidate_hash"
  echo "Installed patched ntdll: $TARGET"
  echo "Rollback: $0 restore --runtime '$RUNTIME' --backup-dir '$BACKUP_DIR'"
else
  BACKUP="$BACKUP_DIR/original-ntdll.so"
  HASH_FILE="$BACKUP_DIR/original-ntdll.so.sha256"
  INSTALLED_HASH_FILE="$BACKUP_DIR/installed-ntdll.so.sha256"
  [ -s "$BACKUP" ] && [ -s "$HASH_FILE" ] && [ -s "$INSTALLED_HASH_FILE" ] || {
    echo "valid backup and installed-module record not found in $BACKUP_DIR" >&2
    exit 1
  }
  expected_current="$(awk '{print $1}' "$INSTALLED_HASH_FILE")"
  actual_current="$(shasum -a 256 "$TARGET" | awk '{print $1}')"
  [ "$actual_current" = "$expected_current" ] || {
    echo "installed ntdll changed since this backup was created; refusing stale rollback" >&2
    exit 1
  }
  expected="$(awk '{print $1}' "$HASH_FILE")"
  actual="$(shasum -a 256 "$BACKUP" | awk '{print $1}')"
  [ "$actual" = "$expected" ] || { echo "backup hash mismatch" >&2; exit 1; }
  codesign --verify --strict "$BACKUP"
  atomic_replace "$BACKUP" "$TARGET" "$expected"
  restored="$(shasum -a 256 "$TARGET" | awk '{print $1}')"
  [ "$restored" = "$expected" ] || { echo "restored ntdll hash mismatch" >&2; exit 1; }
  echo "Restored original ntdll: $TARGET ($restored)"
fi
