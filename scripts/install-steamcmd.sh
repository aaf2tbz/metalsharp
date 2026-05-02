#!/bin/bash
set -e

HOME_DIR="$HOME"
STEAMCMD_DIR="$HOME_DIR/steamcmd"

if [ -f "$STEAMCMD_DIR/steamcmd.sh" ]; then
    echo "SteamCMD already installed"
    exit 0
fi

mkdir -p "$STEAMCMD_DIR"

echo "Downloading SteamCMD..."
curl -sL -o "$STEAMCMD_DIR/steamcmd.tar.gz" \
    "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz"

echo "Extracting..."
tar -xzf "$STEAMCMD_DIR/steamcmd.tar.gz" -C "$STEAMCMD_DIR"

rm -f "$STEAMCMD_DIR/steamcmd.tar.gz"

echo "SteamCMD installed to $STEAMCMD_DIR"
