#!/usr/bin/env bash
set -euo pipefail
echo "Installing Mesa Zink PE DLLs into MetalSharp runtime..."
echo ""

MS_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
ZINK_STAGING="$(cd "$(dirname "$0")/../.." && pwd)/app/bundles/zink-staging"

if [ ! -d "$ZINK_STAGING/zink" ]; then
    echo "ERROR: No zink build output at $ZINK_STAGING/zink"
    echo "Run tools/zink/build-mesa-zink.sh build first"
    exit 1
fi

WINE_LIB="$MS_HOME/runtime/wine/lib"
ZINK_DIR="$WINE_LIB/zink"

mkdir -p "$ZINK_DIR/x86_64-windows"
mkdir -p "$ZINK_DIR/i386-windows"

for arch_dir in "$ZINK_STAGING/zink/"*-windows; do
    arch="$(basename "$arch_dir")"
    target="$ZINK_DIR/$arch"
    mkdir -p "$target"
    for dll in "$arch_dir"/*.dll; do
        [ -f "$dll" ] || continue
        name="$(basename "$dll")"
        echo "  $arch/$name"
        cp "$dll" "$target/$name"
    done
done

echo ""
echo "Installed to $ZINK_DIR"
echo ""
echo "Layout:"
ls -lhR "$ZINK_DIR"
echo ""
echo "Verifying..."
ok=true
for dll in opengl32.dll libgallium_wgl.dll libwinpthread-1.dll; do
    if [ -f "$ZINK_DIR/x86_64-windows/$dll" ]; then
        echo "  OK: $dll"
    else
        echo "  MISSING: $dll (may be statically linked into opengl32.dll)"
    fi
done
echo ""
echo "Done. Zink DLLs are ready for MetalSharp runtime."
echo ""
echo "Required env vars for Zink OpenGL:"
echo "  WINEDLLOVERRIDES=opengl32=n"
echo "  WINEDLLPATH=<zink_arch_dir> (prepended, metalsharp-wine wrapper handles this)"
echo "  VK_ICD_FILENAMES=<MoltenVK_icd.json path>"
echo "  MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592  (Vulkan 1.3 — avoids winevulkan thunk bugs with 1.4)"
echo "  MESA_LOADER_DRIVER_OVERRIDE=zink"
echo "  MESA_GL_VERSION_OVERRIDE=3.3COMPAT"
echo "  MESA_SHADER_CACHE_DIR=~/.metalsharp/shader-cache/zink"
