#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SEVERITY="${SHELLCHECK_SEVERITY:-error}"

if ! command -v shellcheck >/dev/null 2>&1; then
  echo "shellcheck is required. Install it locally or through CI before running this script." >&2
  exit 127
fi

scripts=()
while IFS= read -r -d '' script; do
  scripts+=("$script")
done < <(
  cd "$ROOT_DIR"
  find . \
    -path './.git' -prune -o \
    -path './app/node_modules' -prune -o \
    -path './.cache' -prune -o \
    -path './vendor' -prune -o \
    -type f -name '*.sh' -print0
)

if [ "${#scripts[@]}" -eq 0 ]; then
  echo "No maintained shell scripts found."
  exit 0
fi

cd "$ROOT_DIR"
printf 'ShellCheck severity=%s scripts=%s\n' "$SEVERITY" "${#scripts[@]}"
shellcheck --external-sources --severity="$SEVERITY" "${scripts[@]}"
