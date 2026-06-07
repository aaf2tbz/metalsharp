#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PE_DIR="$SCRIPT_DIR/pe"
WINE_ROOT="${WINE_ROOT:-$HOME/.metalsharp/runtime/wine}"
WINE_LIBS="$WINE_ROOT/lib/wine/x86_64-windows"
WINE_UNIX="$WINE_ROOT/lib/wine/x86_64-unix"
WINE_EXTERNAL="$WINE_ROOT/lib/external"
OUTPUT_DIR="$SCRIPT_DIR/output"

MINGW="x86_64-w64-mingw32-gcc"
MINGW_CFLAGS="-shared -static -Wall -O2 -Wl,--enable-stdcall-fixup -D_WIN32 -D_WIN64"

SIG='Wine builtin DLL'$'\x00'

patch_builtin() {
    local dll="$1"
    if [ ! -f "$dll" ]; then
        echo "WARNING: $dll not found, skipping patch"
        return
    fi
    python3 -c "
import sys
sig = b'Wine builtin DLL\x00' + b'\x00' * 15
with open(sys.argv[1], 'r+b') as f:
    f.seek(0x40)
    f.write(sig)
print(f'Patched {sys.argv[1]}')
" "$dll"
}

echo "=== D3DMetal PE Stub Builder ==="
echo "WINE_ROOT: $WINE_ROOT"
echo "Output:    $OUTPUT_DIR"
echo ""

mkdir -p "$OUTPUT_DIR"

echo "[1/4] Building d3d12.dll..."
$MINGW $MINGW_CFLAGS -o "$OUTPUT_DIR/d3d12.dll" "$PE_DIR/d3d12_d3dmetal.c"

echo "[2/4] Building dxgi.dll..."
$MINGW $MINGW_CFLAGS -o "$OUTPUT_DIR/dxgi.dll" "$PE_DIR/dxgi_d3dmetal.c"

echo "[3/4] Building d3d11.dll..."
$MINGW $MINGW_CFLAGS -o "$OUTPUT_DIR/d3d11.dll" "$PE_DIR/d3d11_d3dmetal.c"

echo "[4/4] Building d3d10.dll..."
$MINGW $MINGW_CFLAGS -o "$OUTPUT_DIR/d3d10.dll" "$PE_DIR/d3d10_d3dmetal.c"

echo ""
echo "Patching builtin signatures..."
for dll in d3d12.dll dxgi.dll d3d11.dll d3d10.dll; do
    patch_builtin "$OUTPUT_DIR/$dll"
done

echo ""
echo "Build complete. Output:"
ls -la "$OUTPUT_DIR/"

echo ""
echo "To install into Wine runtime:"
echo "  $SCRIPT_DIR/build-d3dmetal.sh install"
echo ""
echo "To test:"
echo "  WINEPREFIX=~/.metalsharp/prefix-steam WINEDLLOVERRIDES=d3d12,dxgi,d3d11,d3d10=b,n DYLD_LIBRARY_PATH=$WINE_EXTERNAL wine64 <exe>"

if [ "${1:-}" = "install" ]; then
    echo ""
    echo "=== Installing into Wine runtime ==="

    if [ ! -f "$WINE_EXTERNAL/libd3dshared.dylib" ]; then
        echo "ERROR: libd3dshared.dylib not found at $WINE_EXTERNAL/"
        echo "Install GPTK runtime first"
        exit 1
    fi

    echo "Installing PE DLLs to $WINE_LIBS/"
    for dll in d3d12.dll dxgi.dll d3d11.dll d3d10.dll; do
        cp "$OUTPUT_DIR/$dll" "$WINE_LIBS/$dll"
        echo "  $dll"
    done

    echo "Installing unix .so pairs to $WINE_UNIX/"
    for so_name in d3d12.so dxgi.so d3d11.so d3d10.so; do
        cp "$WINE_EXTERNAL/libd3dshared.dylib" "$WINE_UNIX/$so_name"
        echo "  $so_name (from libd3dshared.dylib)"
    done

    echo ""
    echo "Installation complete."
fi
