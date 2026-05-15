#!/usr/bin/env bash
# bundle_updater.sh — creates a minimal Python runtime bundle for the MetalSharp updater.
#
# Output: bundles/updater.tar.gz containing:
#   updater/
#     bin/python3         — standalone Python interpreter
#     lib/python3X/       — minimal stdlib (json, subprocess, os, urllib, signal, time, argparse)
#     update.py           — the updater script
#
# The CI workflow should run this before electron-builder packs the DMG.
# At runtime, the Electron app looks for Resources/updater/bin/python3,
# falling back to system /usr/bin/python3.

set -euo pipefail
cd "$(dirname "$0")/.."

BUNDLE_DIR="app/bundles/updater"
TAR_OUT="app/bundles/updater.tar.gz"
SCRIPT_SRC="app/updater/update.py"

rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/bin" "$BUNDLE_DIR/lib"

SYS_PYTHON=""
for candidate in /opt/homebrew/bin/python3 /usr/local/bin/python3 /usr/bin/python3; do
    if command -v "$candidate" &>/dev/null; then
        SYS_PYTHON="$candidate"
        break
    fi
done

if [ -z "$SYS_PYTHON" ]; then
    echo "ERROR: no python3 found on system" >&2
    exit 1
fi

echo "Using system python3: $SYS_PYTHON"

# Copy the interpreter binary
cp "$SYS_PYTHON" "$BUNDLE_DIR/bin/python3"
chmod +x "$BUNDLE_DIR/bin/python3"

# Copy the minimal stdlib modules the updater needs
PYTHON_VERSION=$("$SYS_PYTHON" -c "import sys; print(f'python{sys.version_info.major}.{sys.version_info.minor}')")
STDLIB_SRC=$("$SYS_PYTHON" -c "import sysconfig; print(sysconfig.get_path('stdlib'))")
LIB_DEST="$BUNDLE_DIR/lib/$PYTHON_VERSION"

mkdir -p "$LIB_DEST"

# Core modules needed by update.py
NEEDED_MODULES=(
    "json/__init__.py" "json/decoder.py" "json/encoder.py" "json/scanner.py"
    "argparse.py" "subprocess.py" "signal.py" "shutil.py"
    "urllib/__init__.py" "urllib/request.py" "urllib/response.py" "urllib/error.py" "urllib/parse.py"
    "http/__init__.py" "http/client.py" "http/cookiejar.py"
    "email/__init__.py" "email/parser.py" "email/message.py" "email/policy.py" "email/feedparser.py"
    "email/header.py" "email/charset.py" "email/utils.py" "email/errors.py" "email/_policybase.py"
    "collections/__init__.py" "collections/abc.py"
    "importlib/__init__.py" "importlib/util.py" "importlib/machinery.py" "importlib/abc.py"
    "encodings/__init__.py" "encodings/utf_8.py" "encodings/ascii.py" "encodings/aliases.py"
    "logging/__init__.py"
    "base64.py" "bisect.py" "calendar.py" "contextlib.py" "copy.py" "copyreg.py"
    "datetime.py" "enum.py" "functools.py" "genericpath.py" "getopt.py"
    "gettext.py" "glob.py" "heapq.py" "io.py" "keyword.py" "linecache.py"
    "locale.py" "operator.py" "os.py" "pathlib.py" "posixpath.py" "pprint.py"
    "re.py" "reprlib.py" "sre_compile.py" "sre_constants.py" "sre_parse.py"
    "stat.py" "string.py" "struct.py" "sysconfig.py" "tempfile.py" "textwrap.py"
    "threading.py" "token.py" "tokenize.py" "traceback.py" "types.py"
    "typing.py" "warnings.py" "weakref.py" "zipimport.py"
)

for mod in "${NEEDED_MODULES[@]}"; do
    src="$STDLIB_SRC/$mod"
    if [ -f "$src" ]; then
        mkdir -p "$(dirname "$LIB_DEST/$mod")"
        cp "$src" "$LIB_DEST/$mod"
    fi
done

# Copy the update script
cp "$SCRIPT_SRC" "$BUNDLE_DIR/update.py"

# Create the tarball
rm -f "$TAR_OUT"
tar -czf "$TAR_OUT" -C app/bundles updater/

echo "Created $TAR_OUT ($(du -sh "$TAR_OUT" | cut -f1))"

# Clean up
rm -rf "$BUNDLE_DIR"
