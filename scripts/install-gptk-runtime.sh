#!/bin/bash
set -euo pipefail

SCRIPT_NAME="$(basename "$0")"
MS_HOME="${HOME}/.metalsharp"
GPTK_DIR="${MS_HOME}/runtime/wine/lib/gptk"
EXTERNAL_DIR="${MS_HOME}/runtime/wine/lib/external"

usage() {
    echo "Usage: ${SCRIPT_NAME} <gptk_dmg_or_redist_dir>"
    echo ""
    echo "Installs Apple Game Porting Toolkit 3.0 runtime files into MetalSharp."
    echo ""
    echo "  <gptk_dmg_or_redist_dir>  Path to mounted GPTK DMG or extracted redist/ directory"
    echo ""
    echo "Expected layout from DMG:"
    echo "  redist/lib/external/D3DMetal.framework/"
    echo "  redist/lib/external/libd3dshared.dylib"
    echo "  redist/lib/wine/x86_64-windows/*.dll"
    echo "  redist/lib/wine/x86_64-unix/*.so"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

SOURCE="$1"

if [ -d "${SOURCE}/redist/lib" ]; then
    REDIST="${SOURCE}/redist"
elif [ -d "${SOURCE}/lib/wine" ] && [ -d "${SOURCE}/lib/external" ]; then
    REDIST="${SOURCE}"
else
    echo "ERROR: Cannot find GPTK redist layout in ${SOURCE}"
    echo "Expected redist/lib/wine/ and redist/lib/external/"
    exit 1
fi

echo "=== MetalSharp GPTK Runtime Installer ==="
echo "Source: ${SOURCE}"
echo "Target: ${MS_HOME}/runtime/wine/"
echo ""

mkdir -p "${GPTK_DIR}/x86_64-windows"
mkdir -p "${GPTK_DIR}/x86_64-unix"
mkdir -p "${EXTERNAL_DIR}"

PE_COUNT=$(find "${REDIST}/lib/wine/x86_64-windows" -name "*.dll" 2>/dev/null | wc -l | tr -d ' ')
SO_COUNT=$(find "${REDIST}/lib/wine/x86_64-unix" -name "*.so" 2>/dev/null | wc -l | tr -d ' ')

if [ "${PE_COUNT}" -eq 0 ] || [ "${SO_COUNT}" -eq 0 ]; then
    echo "ERROR: No GPTK PE/SO DLLs found. Is this the correct GPTK 3.0 DMG?"
    exit 1
fi

echo "Copying ${PE_COUNT} PE DLLs..."
cp -a "${REDIST}/lib/wine/x86_64-windows/"*.dll "${GPTK_DIR}/x86_64-windows/"

echo "Copying ${SO_COUNT} Wine unix modules..."
cp -a "${REDIST}/lib/wine/x86_64-unix/"*.so "${GPTK_DIR}/x86_64-unix/"

echo "Installing D3DMetal.framework..."
cp -a "${REDIST}/lib/external/D3DMetal.framework" "${EXTERNAL_DIR}/"

if [ -f "${REDIST}/lib/external/libd3dshared.dylib" ]; then
    cp -a "${REDIST}/lib/external/libd3dshared.dylib" "${EXTERNAL_DIR}/"
    echo "Installed libd3dshared.dylib"
fi

echo ""
echo "=== Installed GPTK Runtime ==="
echo "PE DLLs:    $(ls "${GPTK_DIR}/x86_64-windows/"*.dll 2>/dev/null | wc -l | tr -d ' ')"
echo "Unix SOs:   $(ls "${GPTK_DIR}/x86_64-unix/"*.so 2>/dev/null | wc -l | tr -d ' ')"
echo "Framework:  $(test -d "${EXTERNAL_DIR}/D3DMetal.framework" && echo "D3DMetal.framework" || echo "missing")"
echo "Size:       $(du -sh "${GPTK_DIR}" "${EXTERNAL_DIR}/D3DMetal.framework" 2>/dev/null | tail -1 | cut -f1)"
echo ""
echo "M13 (GPTK D3DMetal) pipeline is now available."
