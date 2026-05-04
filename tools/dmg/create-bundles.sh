#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"

echo "=== MetalSharp Bundle Creator ==="
echo "Creates compressed .tar.zst archives for bundling into the DMG"
echo ""

mkdir -p "$BUNDLE_DIR"

bundle_app() {
    local name="$1"
    local src_path="$2"
    local dest="$BUNDLE_DIR/${name}.tar.zst"

    if [ ! -e "$src_path" ]; then
        echo "SKIP: $name — $src_path not found"
        return 0
    fi

    if [ -f "$dest" ]; then
        local src_size
        src_size=$(du -sk "$src_path" 2>/dev/null | cut -f1)
        local dest_size
        dest_size=$(du -sk "$dest" 2>/dev/null | cut -f1)
        if [ "$dest_size" -gt 0 ] && [ "$((src_size / 10))" -lt "$dest_size" ]; then
            echo "SKIP: $name — bundle already exists ($((dest_size / 1024))MB)"
            return 0
        fi
    fi

    echo "Bundling $name from $src_path..."
    local parent
    parent="$(dirname "$src_path")"
    local base
    base="$(basename "$src_path")"

    tar -cf - -C "$parent" "$base" | zstd -19 -T0 -o "$dest" -f
    local compressed_size
    compressed_size=$(du -sh "$dest" | cut -f1)
    echo "  -> $dest ($compressed_size)"
}

bundle_dir() {
    local name="$1"
    local src_path="$2"
    local dest="$BUNDLE_DIR/${name}.tar.zst"

    if [ ! -d "$src_path" ]; then
        echo "SKIP: $name — $src_path not found"
        return 0
    fi

    echo "Bundling $name from $src_path..."
    local parent
    parent="$(dirname "$src_path")"
    local base
    base="$(basename "$src_path")"

    tar -cf - -C "$parent" "$base" | zstd -19 -T0 -o "$dest" -f
    local compressed_size
    compressed_size=$(du -sh "$dest" | cut -f1)
    echo "  -> $dest ($compressed_size)"
}

echo "--- external runtime ---"
bundle_app "external runtime" "/Applications/external runtime.app"

echo ""
echo "--- Game Porting Toolkit ---"
bundle_app "gptk" "/Applications/Game Porting Toolkit.app"

echo ""
echo "--- Mono x86 ---"
bundle_dir "mono-x86" "$HOME/.metalsharp/runtime/mono-x86"

echo ""
echo "--- DXVK 1.10.3 ---"
bundle_dir "dxvk" "$HOME/.metalsharp/runtime/dxvk-1.10.3"

echo ""
echo "--- SteamCMD ---"
if [ -d "$HOME/steamcmd" ]; then
    bundle_dir "steamcmd" "$HOME/steamcmd"
else
    echo "SKIP: steamcmd — ~/steamcmd not found (will be downloaded at install time)"
fi

echo ""
echo "--- SteamSetup.exe ---"
if [ -f "$HOME/.metalsharp/SteamSetup.exe" ]; then
    cp "$HOME/.metalsharp/SteamSetup.exe" "$BUNDLE_DIR/SteamSetup.exe"
    echo "  -> $BUNDLE_DIR/SteamSetup.exe (copied)"
else
    echo "Downloading SteamSetup.exe..."
    curl -sL -o "$BUNDLE_DIR/SteamSetup.exe" "https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe"
    echo "  -> $BUNDLE_DIR/SteamSetup.exe (downloaded)"
fi

echo ""
echo "=== Bundle Summary ==="
ls -lh "$BUNDLE_DIR/"*.tar.zst 2>/dev/null || echo "No bundles created"
echo ""
echo "Bundles saved to: $BUNDLE_DIR"
echo "Add 'bundles/' to your extraResources in electron-builder config to include in DMG."
