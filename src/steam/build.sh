#!/bin/bash
set -euo pipefail

PROTON_SRC="$(cd "$(dirname "$0")" && pwd)/proton/lsteamclient"
WINE_INC="$(cd "$(dirname "$0")" && pwd)/wine-headers/include"
BUILD_DIR="$(cd "$(dirname "$0")" && pwd)/build"
RUNTIME="$HOME/.metalsharp/runtime/lsteamclient"

echo "=== Proton lsteamclient build for macOS ==="
echo "Source: $PROTON_SRC"
echo "Wine headers: $WINE_INC"
echo "Build: $BUILD_DIR"
echo "Install: $RUNTIME"

mkdir -p "$BUILD_DIR/pe" "$BUILD_DIR/unix"

# ============================================================
# Step 1: Build steamclient64.dll (Windows PE side)
# ============================================================
echo ""
echo "=== Building steamclient64.dll (PE) ==="

PE_CFLAGS="-DWINE_NO_LONG_TYPES -DSTEAM_API_EXPORTS -Dprivate=public -Dprotected=public"
PE_CFLAGS="$PE_CFLAGS -D__x86_64__ -D_WIN32 -D__WIN32__ -D__WIN64__"
PE_CFLAGS="$PE_CFLAGS -I$PROTON_SRC -I$WINE_INC"

PE_CXXFLAGS="$PE_CFLAGS -std=c++17 -fno-exceptions -fno-rtti -Wno-attributes"

PE_SRCS_C=$(ls "$PROTON_SRC"/*.c 2>/dev/null)
PE_SRCS_CPP=$(ls "$PROTON_SRC"/*.cpp 2>/dev/null)

# Compile C files
for f in $PE_SRCS_C; do
    base=$(basename "$f" .c)
    echo "  CC  $base.c"
    x86_64-w64-mingw32-gcc -c $PE_CFLAGS -o "$BUILD_DIR/pe/${base}.o" "$f" 2>/dev/null || \
        echo "  SKIP $base.c (errors)"
done

# Compile C++ files
for f in $PE_SRCS_CPP; do
    base=$(basename "$f" .cpp)
    echo "  CXX $base.cpp"
    x86_64-w64-mingw32-g++ -c $PE_CXXFLAGS -o "$BUILD_DIR/pe/${base}.o" "$f" 2>/dev/null || \
        echo "  SKIP $base.cpp (errors)"
done

echo "  Linking steamclient64.dll..."
x86_64-w64-mingw32-g++ -shared -o "$BUILD_DIR/steamclient64.dll" \
    "$BUILD_DIR/pe/"*.o \
    -luser32 -lws2_32 -lkernel32 -lntdll \
    -static-libgcc -static-libstdc++ \
    -Wl,--enable-stdcall-fixup 2>&1 || {
    echo "  PE link failed, trying with --undefined..."
    x86_64-w64-mingw32-g++ -shared -o "$BUILD_DIR/steamclient64.dll" \
        "$BUILD_DIR/pe/"*.o \
        -luser32 -lws2_32 -lkernel32 \
        -static-libgcc -static-libstdc++ \
        -Wl,--enable-stdcall-fixup,--unresolved-symbols=ignore-all 2>&1
}

if [ -f "$BUILD_DIR/steamclient64.dll" ]; then
    echo "  OK: $(ls -lh "$BUILD_DIR/steamclient64.dll" | awk '{print $5}')"
else
    echo "  FAILED"
fi

# ============================================================
# Step 2: Build lsteamclient.dylib (macOS Unix side)
# ============================================================
echo ""
echo "=== Building lsteamclient.dylib (Unix) ==="

# The Unix side needs: unixlib.cpp, unix_steam_*.cpp, unixlib_generated.cpp,
# steamclient_generated.c, and all cppISteam*.cpp files
# It also needs steamclient_structs_generated.h converted to the Unix view

UNIX_CFLAGS="-DWINE_UNIX_LIB -DWINE_NO_LONG_TYPES -DSTEAM_API_EXPORTS"
UNIX_CFLAGS="$UNIX_CFLAGS -Dprivate=public -Dprotected=public"
UNIX_CFLAGS="$UNIX_CFLAGS -I$PROTON_SRC -I$WINE_INC"
UNIX_CFLAGS="$UNIX_CFLAGS -target arm64-apple-macosx14.0"

UNIX_CXXFLAGS="$UNIX_CFLAGS -std=c++17 -fno-exceptions -fno-rtti -Wno-attributes -fPIC"

# Unix side sources (the ones with #pragma makedep unix)
UNIX_C_SRCS="steamclient_generated.c"
UNIX_CPP_SRCS="unixlib.cpp unixlib_generated.cpp unix_steam_input_manual.cpp unix_steam_networking_manual.cpp unix_steam_utils_manual.cpp"

# Also compile all cppISteam*.cpp for the Unix side (they contain both PE and Unix code)
UNIX_CPP_SRCS="$UNIX_CPP_SRCS $(ls "$PROTON_SRC"/cppISteam*.cpp 2>/dev/null)"

for src in $UNIX_C_SRCS; do
    f="$PROTON_SRC/$src"
    [ ! -f "$f" ] && continue
    base=$(basename "$src" .c)
    echo "  CC  $src"
    cc -c $UNIX_CFLAGS -o "$BUILD_DIR/unix/${base}.o" "$f" 2>/dev/null || \
        echo "  SKIP $src"
done

for src in $UNIX_CPP_SRCS; do
    f="$PROTON_SRC/$src"
    [ ! -f "$f" ] && continue
    base=$(basename "$src" .cpp)
    echo "  CXX $src"
    c++ -c $UNIX_CXXFLAGS -o "$BUILD_DIR/unix/${base}.o" "$f" 2>/dev/null || \
        echo "  SKIP $src"
done

echo "  Linking lsteamclient.dylib..."
c++ -dynamiclib -o "$BUILD_DIR/lsteamclient.dylib" \
    "$BUILD_DIR/unix/"*.o \
    -framework CoreFoundation \
    -lpthread 2>&1 || {
    echo "  dylib link failed, trying with -undefined dynamic_lookup..."
    c++ -dynamiclib -o "$BUILD_DIR/lsteamclient.dylib" \
        "$BUILD_DIR/unix/"*.o \
        -undefined dynamic_lookup \
        -framework CoreFoundation \
        -lpthread 2>&1
}

if [ -f "$BUILD_DIR/lsteamclient.dylib" ]; then
    echo "  OK: $(ls -lh "$BUILD_DIR/lsteamclient.dylib" | awk '{print $5}')"
else
    echo "  FAILED"
fi

# ============================================================
# Step 3: Install
# ============================================================
echo ""
echo "=== Installing to $RUNTIME ==="
mkdir -p "$RUNTIME"

[ -f "$BUILD_DIR/steamclient64.dll" ] && cp "$BUILD_DIR/steamclient64.dll" "$RUNTIME/"
[ -f "$BUILD_DIR/lsteamclient.dylib" ] && cp "$BUILD_DIR/lsteamclient.dylib" "$RUNTIME/"

ls -la "$RUNTIME/"
echo ""
echo "=== Done ==="
