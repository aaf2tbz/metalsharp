#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
x86_64-w64-mingw32-gcc -shared -o "$SCRIPT_DIR/eac_identity_shim.dll" \
    "$SCRIPT_DIR/eac_identity_shim.c" \
    -lkernel32 \
    -static-libgcc \
    -s \
    -O2
echo "Built: $SCRIPT_DIR/eac_identity_shim.dll ($(wc -c < "$SCRIPT_DIR/eac_identity_shim.dll") bytes)"
