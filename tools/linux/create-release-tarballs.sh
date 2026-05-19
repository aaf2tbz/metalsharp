#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="${METALSHARP_PACKAGE_OUT:-$PROJECT_ROOT/dist/packages}"

ensure_writable_dir() {
  local dir="$1"
  local parent
  parent="$(dirname "$dir")"

  if mkdir -p "$dir" 2>/dev/null; then
    return
  fi

  if command -v sudo >/dev/null 2>&1; then
    sudo mkdir -p "$dir"
    sudo chown -R "$(id -u):$(id -g)" "$parent"
    return
  fi

  mkdir -p "$dir"
}

ensure_writable_dir "$OUT_DIR"

create_runtime_archive() {
  local dest="$1"
  local tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/wine/bin" "$tmp/wine/share/metalsharp"

  for wrapper in wine metalsharp-wine; do
    printf '%s\n' \
      '#!/bin/sh' \
      'for candidate in /usr/bin/wine /usr/local/bin/wine /opt/wine/bin/wine /usr/bin/wine64 /usr/local/bin/wine64 /opt/wine/bin/wine64; do' \
      '  if [ -x "$candidate" ]; then' \
      '    exec "$candidate" "$@"' \
      '  fi' \
      'done' \
      'echo "MetalSharp Linux runtime requires system Wine. Install wine or wine64." >&2' \
      'exit 127' \
      > "$tmp/wine/bin/$wrapper"
    chmod 755 "$tmp/wine/bin/$wrapper"
  done

  printf '%s\n' \
    'MetalSharp Linux runtime bundle' \
    'This bundle installs wrapper scripts that dispatch to system Wine.' \
    'The Debian package declares wine | wine64 as a dependency.' \
    > "$tmp/wine/share/metalsharp/README-linux-runtime.txt"

  rm -f "$dest"
  COPYFILE_DISABLE=1 tar --no-xattrs -C "$tmp" -cf - wine | zstd -q -o "$dest"
  rm -rf "$tmp"
}

create_runtime_archive "$OUT_DIR/metalsharp_linux_runtime.tar.zst"
create_runtime_archive "$OUT_DIR/metalsharp_linux_runtime2.tar.zst"

sha256sum "$OUT_DIR"/metalsharp_linux_runtime*.tar.zst > "$OUT_DIR/metalsharp_linux_runtime.sha256"
ls -lh "$OUT_DIR"/metalsharp_linux_runtime*.tar.zst "$OUT_DIR/metalsharp_linux_runtime.sha256"
