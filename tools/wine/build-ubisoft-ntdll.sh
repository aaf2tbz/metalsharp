#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PATCH="$SCRIPT_DIR/patches/ubisoft-writecopy/0001-wine-11.5-ubisoft-writecopy.patch"
VERIFY="$SCRIPT_DIR/verify-ubisoft-ntdll.sh"
DEFAULT_RUNTIME="$HOME/.metalsharp/runtime/wine"
DEFAULT_SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"
SOURCE=""
OUTPUT=""
RUNTIME="$DEFAULT_RUNTIME"
SDK="${METALSHARP_WINE_SDK:-$DEFAULT_SDK}"
JOBS="${METALSHARP_WINE_JOBS:-$(sysctl -n hw.ncpu)}"

usage() {
  cat <<'USAGE'
Usage: tools/wine/build-ubisoft-ntdll.sh --source PATH --output PATH [options]

Incrementally configures a clean Wine 11.5 source tree and builds only the
x86_64 Unix ntdll target needed by MetalSharp's Ubisoft support.

Options:
  --source PATH   Clean, unconfigured Wine 11.5 source tree (required)
  --output PATH   Destination for the signed ntdll.so (required)
  --runtime PATH  Installed MetalSharp Wine used for tools/baseline
  --sdk PATH      macOS SDK (default: MacOSX26.5.sdk)
  --jobs N        Parallel make jobs
  -h, --help      Show this help

The source path must not contain whitespace because Wine's generated Makefile
embeds configured paths in preprocessor definitions without shell-safe quoting.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --source) SOURCE="$2"; shift 2 ;;
    --output) OUTPUT="$2"; shift 2 ;;
    --runtime) RUNTIME="$2"; shift 2 ;;
    --sdk) SDK="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[ -n "$SOURCE" ] && [ -n "$OUTPUT" ] || { usage >&2; exit 2; }
case "$SOURCE" in *[[:space:]]*) echo "Wine source path must not contain whitespace: $SOURCE" >&2; exit 2 ;; esac
for path_arg in "$SOURCE" "$OUTPUT" "$RUNTIME" "$SDK"; do
  case "$path_arg" in /*) ;; *) echo "build paths must be absolute: $path_arg" >&2; exit 2 ;; esac
done
[ "$(uname -s)" = "Darwin" ] || { echo "this incremental build is macOS-only" >&2; exit 1; }
[ -x "$SOURCE/configure" ] || { echo "Wine configure script missing: $SOURCE/configure" >&2; exit 1; }
[ -f "$PATCH" ] || { echo "patch missing: $PATCH" >&2; exit 1; }
[ -d "$SDK" ] || { echo "macOS SDK missing: $SDK" >&2; exit 1; }
[ -x "$RUNTIME/bin/winebuild" ] || { echo "MetalSharp winebuild missing: $RUNTIME/bin/winebuild" >&2; exit 1; }
[ -f "$RUNTIME/lib/wine/x86_64-unix/ntdll.so" ] || { echo "baseline ntdll missing in $RUNTIME" >&2; exit 1; }

for tool in git make clang codesign file lipo nm otool python3 strings; do
  command -v "$tool" >/dev/null || { echo "required tool missing: $tool" >&2; exit 1; }
done

runtime_real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$RUNTIME")"
output_real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$OUTPUT")"
case "$output_real" in
  "$runtime_real"|"$runtime_real"/*)
    echo "refusing to build directly into the installed runtime; use swap-metalsharp-ntdll.sh" >&2
    exit 1
    ;;
esac

[ "$("$RUNTIME/bin/metalsharp-wine" --version 2>&1)" = "wine-11.5" ] || {
  echo "installed MetalSharp runtime is not Wine 11.5" >&2
  exit 1
}
"$RUNTIME/bin/winebuild" --version 2>&1 | grep -q 'winebuild version 11\.5' || {
  echo "installed MetalSharp winebuild is not version 11.5" >&2
  exit 1
}
[ ! -e "$SOURCE/config.status" ] || { echo "Wine source is already configured; use a clean worktree" >&2; exit 1; }

if git -C "$SOURCE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  [ -z "$(git -C "$SOURCE" status --porcelain --untracked-files=no)" ] || {
    echo "Wine source has tracked changes; use a clean Wine 11.5 worktree" >&2
    exit 1
  }
fi

"$SOURCE/configure" --version 2>/dev/null | grep -q 'Wine configure 11.5' || {
  echo "source does not identify as Wine 11.5" >&2
  exit 1
}

git -C "$SOURCE" apply --check "$PATCH"
git -C "$SOURCE" apply "$PATCH"

BISON_BIN="${BISON:-}"
if [ -z "$BISON_BIN" ] && [ -x /opt/homebrew/opt/bison/bin/bison ]; then
  BISON_BIN=/opt/homebrew/opt/bison/bin/bison
fi
[ -n "$BISON_BIN" ] || BISON_BIN="$(command -v bison)"
"$BISON_BIN" --version | head -1

cd "$SOURCE"
SDKROOT="$SDK" ac_cv_func_pipe2=no ./configure \
  --host=x86_64-apple-darwin --enable-archs=i386,x86_64 --with-mingw \
  --without-alsa --without-capi --without-cups --without-dbus \
  --without-fontconfig --without-freetype --without-gettext --without-gphoto \
  --without-gssapi --without-gstreamer --without-hwloc --without-inotify \
  --without-krb5 --without-netapi --without-opencl --without-opengl \
  --without-oss --without-pcap --without-pcsclite --without-pulse \
  --without-sane --without-sdl --without-udev --without-usb \
  --without-v4l2 --without-vulkan --without-wayland --without-x \
  --prefix="$SOURCE/install" \
  CC=/usr/bin/clang CXX=/usr/bin/clang++ BISON="$BISON_BIN" PKG_CONFIG=/usr/bin/false \
  "CFLAGS=-O2 -g -arch x86_64 -isysroot $SDK -mmacosx-version-min=14.0" \
  "CXXFLAGS=-O2 -g -arch x86_64 -isysroot $SDK -mmacosx-version-min=14.0" \
  "LDFLAGS=-arch x86_64 -isysroot $SDK -mmacosx-version-min=26.0" \
  'CPPFLAGS='

# This target may build Wine's generated-header tools as prerequisites, but it
# does not build or install the rest of Wine.
make -j"$JOBS" WINEBUILD="$RUNTIME/bin/winebuild" dlls/ntdll/ntdll.so

mkdir -p "$(dirname "$OUTPUT")"
cp -p dlls/ntdll/ntdll.so "$OUTPUT"
chmod 755 "$OUTPUT"
codesign --force --sign - --timestamp=none "$OUTPUT"
"$VERIFY" "$OUTPUT" "$RUNTIME/lib/wine/x86_64-unix/ntdll.so"

echo "Built patched Unix ntdll: $OUTPUT"
