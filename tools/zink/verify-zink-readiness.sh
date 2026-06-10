#!/usr/bin/env bash
set -euo pipefail
echo "=== MetalSharp Zink Readiness Check ==="
echo ""

MS_HOME="${METALSHARP_HOME:-$HOME/.metalsharp}"
WINE_LIB="$MS_HOME/runtime/wine/lib"
ZINK_DIR="$WINE_LIB/zink"
ICD_PATH="$MS_HOME/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json"

ok=true

echo "1. Zink PE DLLs"
for arch in x86_64-windows i386-windows; do
    echo "   [$arch]"
    for dll in opengl32.dll libgallium_wgl.dll libwinpthread-1.dll; do
        f="$ZINK_DIR/$arch/$dll"
        if [ -f "$f" ] && [ -s "$f" ]; then
            size="$(du -h "$f" | cut -f1)"
            echo "     OK: $dll ($size)"
        else
            echo "     MISSING: $dll"
            ok=false
        fi
    done
done

echo ""
echo "2. MoltenVK ICD"
if [ -f "$ICD_PATH" ] && [ -s "$ICD_PATH" ]; then
    lib_path="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['ICD']['library_path'])" "$ICD_PATH" 2>/dev/null || echo 'PARSE_ERROR')"
    if [ -f "$lib_path" ]; then
        echo "   OK: MoltenVK ICD -> $lib_path"
    else
        echo "   WARN: ICD exists but library_path broken: $lib_path"
        echo "   Fix: run install-all from MetalSharp to fix ICD paths"
    fi
else
    echo "   MISSING: $ICD_PATH"
    echo "   Fix: run MetalSharp install-all first"
    ok=false
fi

echo ""
echo "3. Vulkan Loader (Wine builtin)"
VULKAN_DLL="$WINE_LIB/wine/x86_64-windows/vulkan-1.dll"
if [ -f "$VULKAN_DLL" ] && [ -s "$VULKAN_DLL" ]; then
    echo "   OK: vulkan-1.dll present"
else
    echo "   MISSING: vulkan-1.dll (needed by Zink to call Vulkan)"
    ok=false
fi

echo ""
echo "4. Wine Runtime"
WINE_BIN="$MS_HOME/runtime/wine/bin/metalsharp-wine"
if [ -f "$WINE_BIN" ] && [ -x "$WINE_BIN" ]; then
    ver="$("$WINE_BIN" --version 2>/dev/null || echo 'unknown')"
    echo "   OK: $ver"
else
    echo "   MISSING: wine binary"
    ok=false
fi

echo ""
if $ok; then
    echo "=== All checks passed ==="
    echo ""
    echo "To test Zink OpenGL with a game:"
    echo "  MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592 \\"
    echo "  WINEDLLOVERRIDES=\"opengl32=n\" \\"
    echo "  VK_ICD_FILENAMES=\"$ICD_PATH\" \\"
    echo "  MESA_LOADER_DRIVER_OVERRIDE=zink \\"
    echo "  MESA_GL_VERSION_OVERRIDE=3.3COMPAT \\"
    echo "  MESA_SHADER_CACHE_DIR=\"$MS_HOME/shader-cache/zink\" \\"
    echo "  WINEDLLPATH=\"$ZINK_DIR/<arch>-windows\" \\"
    echo "  $WINE_BIN <game.exe>"
else
    echo "=== Some checks failed ==="
    exit 1
fi
