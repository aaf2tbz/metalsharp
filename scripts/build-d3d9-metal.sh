#!/bin/bash
set -e

WINE_ROOT="$HOME/.metalsharp/runtime/wine"
WINE_LIBS32="$WINE_ROOT/lib/wine/i386-windows"
WINE_LIBS64="$WINE_ROOT/lib/wine/x86_64-windows"
WINE_UNIX64="$WINE_ROOT/lib/wine/x86_64-unix"

MINGW_INC="/opt/homebrew/Cellar/mingw-w64/14.0.0/toolchain-x86_64/x86_64-w64-mingw32/include"
MOJO_DIR="../build/metalsharp-v0.18.0/repo/src/wine/mojoshader"

SRC=$(dirname "$0")

echo "=== Building read() stub ==="
i686-w64-mingw32-gcc -c -O2 "$SRC/read_stub.c" -o /tmp/d3d9_read_stub.o

echo "=== Building custom operator new/delete ==="
i686-w64-mingw32-g++ -c -O2 -fno-exceptions -fno-rtti "$SRC/ms_newdel.cpp" -o /tmp/d3d9_ms_newdel.o

echo "=== Building 32-bit PE d3d9.dll ==="
i686-w64-mingw32-g++ -shared -o /tmp/d3d9.dll \
  -Wall -O2 -static-libgcc -static-libstdc++ \
  -fno-exceptions -fno-rtti \
  -Wl,--enable-stdcall-fixup \
  -D_WIN32 -D__USE_MINGW_ANSI_STDIO=1 \
  -I"$MINGW_INC" \
  "$SRC/d3d9_pe.cpp" \
  /tmp/d3d9_ms_newdel.o /tmp/d3d9_read_stub.o \
  -L"$WINE_LIBS32" -lwinecrt0 -lntdll

echo "=== Applying Wine builtin marker ==="
"$WINE_ROOT/bin/winebuild" --builtin /tmp/d3d9.dll

echo "=== Building MojoShader objects (x86_64) ==="
MOJO_CFLAGS="-arch x86_64 -O2 -DMOJOSHADER_NO_VERSION_INCLUDE -I$MOJO_DIR -I$MOJO_DIR/profiles"
MOJO_OBJS=""
for src in mojoshader.c mojoshader_common.c mojoshader_effects.c \
           profiles/mojoshader_profile_common.c profiles/mojoshader_profile_bytecode.c \
           profiles/mojoshader_profile_metal.c profiles/mojoshader_profile_glsl.c \
           profiles/mojoshader_profile_hlsl.c profiles/mojoshader_profile_arb1.c \
           profiles/mojoshader_profile_d3d.c profiles/mojoshader_profile_spirv.c; do
    base=$(basename "$src" .c)
    obj="/tmp/d3d9_mojo_${base}_x64.o"
    clang -c $MOJO_CFLAGS "$MOJO_DIR/$src" -o "$obj" 2>/dev/null
    MOJO_OBJS="$MOJO_OBJS $obj"
done

echo "=== Building 64-bit unix d3d9.so ==="
clang++ -bundle -o /tmp/d3d9.so \
  -target x86_64-apple-macosx11.0 \
  -Wall -O2 -fobjc-arc \
  -framework Metal -framework Foundation -framework QuartzCore -framework AppKit \
  -DMOJOSHADER_NO_VERSION_INCLUDE \
  -I"$MOJO_DIR" -I"$MOJO_DIR/profiles" \
  "$SRC/d3d9_unix.mm" \
  $MOJO_OBJS

echo "=== Deploying ==="
cp /tmp/d3d9.dll "$WINE_LIBS32/d3d9.dll"
cp /tmp/d3d9.so "$WINE_UNIX64/d3d9.so"

echo "=== Verifying PE deps ==="
i686-w64-mingw32-objdump -p "$WINE_LIBS32/d3d9.dll" 2>&1 | grep "DLL Name"

echo ""
echo "Done. Test with:"
echo "  WINEPREFIX=/tmp/d3d9test WINEDEBUG=fixme-all,warn-all WINEDLLOVERRIDES=d3d9=b wine /tmp/d3d9test/test_d3d9.exe"
