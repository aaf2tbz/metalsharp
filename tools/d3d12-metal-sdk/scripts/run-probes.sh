#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="standalone-wine"
WINE_BIN="${WINE_BIN:-wine}"
WINE_PREFIX="${WINEPREFIX:-}"
DXMT_RUNTIME="${DXMT_RUNTIME:-}"
RESULTS_DIR="$SDK_DIR/results"
RUN_LOADER=1
RUN_AGILITY=1
RUN_CAPS=1
RUN_DXGI=1
RUN_RESOURCES=1
RUN_DESCRIPTORS=1

usage() {
  cat <<'USAGE'
Usage:
  run-probes.sh [--profile metalsharp|standalone-wine] [options]

Options:
  --profile NAME        Runtime profile. Defaults to standalone-wine.
  --wine PATH           Wine binary path.
  --prefix PATH         WINEPREFIX path.
  --dxmt-runtime PATH   Runtime root containing x86_64-windows/ and x86_64-unix/.
  --results-dir PATH    Result output directory.
  --no-loader           Skip probe_loader.
  --no-agility          Skip probe_agility_ue5.
  --no-caps             Skip probe_device_caps.
  --no-dxgi             Skip probe_dxgi_factory.
  --no-resources        Skip probe_resources.
  --no-descriptors      Skip probe_descriptors.
  -h, --help            Show this help.

Examples:
  tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp

  tools/d3d12-metal-sdk/scripts/run-probes.sh \
    --wine /opt/wine/bin/wine \
    --prefix "$HOME/wine-d3d12-test" \
    --dxmt-runtime "$HOME/dxmt-build"
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      PROFILE="$2"
      shift 2
      ;;
    --wine)
      WINE_BIN="$2"
      shift 2
      ;;
    --prefix)
      WINE_PREFIX="$2"
      shift 2
      ;;
    --dxmt-runtime)
      DXMT_RUNTIME="$2"
      shift 2
      ;;
    --results-dir)
      RESULTS_DIR="$2"
      shift 2
      ;;
    --no-loader)
      RUN_LOADER=0
      shift
      ;;
    --no-agility)
      RUN_AGILITY=0
      shift
      ;;
    --no-caps)
      RUN_CAPS=0
      shift
      ;;
    --no-dxgi)
      RUN_DXGI=0
      shift
      ;;
    --no-resources)
      RUN_RESOURCES=0
      shift
      ;;
    --no-descriptors)
      RUN_DESCRIPTORS=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$PROFILE" == "metalsharp" ]]; then
  WINE_BIN="${WINE_BIN:-$HOME/.metalsharp/runtime/wine/bin/wine}"
  if [[ "$WINE_BIN" == "wine" && -x "$HOME/.metalsharp/runtime/wine/bin/wine" ]]; then
    WINE_BIN="$HOME/.metalsharp/runtime/wine/bin/wine"
  fi
  WINE_PREFIX="${WINE_PREFIX:-$HOME/.metalsharp/prefix-steam}"
  DXMT_RUNTIME="${DXMT_RUNTIME:-$HOME/.metalsharp/runtime/wine/lib/dxmt}"
fi

if [[ -z "$WINE_PREFIX" ]]; then
  echo "WINEPREFIX is required. Pass --prefix or use --profile metalsharp." >&2
  exit 2
fi

if [[ -z "$DXMT_RUNTIME" ]]; then
  echo "DXMT runtime root is required. Pass --dxmt-runtime or use --profile metalsharp." >&2
  exit 2
fi

WINDOWS_DIR="$DXMT_RUNTIME/x86_64-windows"
UNIX_DIR="$DXMT_RUNTIME/x86_64-unix"
PROBE_EXE="$SDK_DIR/out/bin/probe_loader.exe"
AGILITY_PROBE_EXE="$SDK_DIR/out/bin/probe_agility_ue5.exe"
CAPS_PROBE_EXE="$SDK_DIR/out/bin/probe_device_caps.exe"
DXGI_PROBE_EXE="$SDK_DIR/out/bin/probe_dxgi_factory.exe"
RESOURCES_PROBE_EXE="$SDK_DIR/out/bin/probe_resources.exe"
DESCRIPTORS_PROBE_EXE="$SDK_DIR/out/bin/probe_descriptors.exe"

if [[ ! -x "$WINE_BIN" ]]; then
  echo "Wine binary is not executable: $WINE_BIN" >&2
  exit 2
fi

if [[ ! -d "$WINDOWS_DIR" ]]; then
  echo "Missing DXMT Windows runtime directory: $WINDOWS_DIR" >&2
  exit 2
fi

for dll in d3d12.dll dxgi.dll d3d11.dll d3d10core.dll winemetal.dll; do
  if [[ ! -f "$WINDOWS_DIR/$dll" ]]; then
    echo "Missing DXMT Windows runtime DLL: $WINDOWS_DIR/$dll" >&2
    exit 2
  fi
done

if [[ ! -f "$UNIX_DIR/winemetal.so" ]]; then
  echo "Missing winemetal.so: $UNIX_DIR/winemetal.so" >&2
  exit 2
fi

if [[ "$WINDOWS_DIR" == *"/gptk/"* || "$WINDOWS_DIR" == *"/lib/gptk/"* ]]; then
  echo "DXMT runtime points at GPTK/D3DMetal DLLs, not DXMT: $WINDOWS_DIR" >&2
  exit 2
fi

if [[ ! -f "$PROBE_EXE" || ! -f "$AGILITY_PROBE_EXE" || ! -f "$CAPS_PROBE_EXE" || ! -f "$DXGI_PROBE_EXE" || ! -f "$RESOURCES_PROBE_EXE" || ! -f "$DESCRIPTORS_PROBE_EXE" || ! -f "$SDK_DIR/out/bin/D3D12/D3D12Core.dll" ]]; then
  "$SDK_DIR/scripts/build-probes.sh" >/dev/null
fi

mkdir -p "$RESULTS_DIR"
RESULT_FILE="$RESULTS_DIR/probe-loader-${PROFILE}.json"
AGILITY_RESULT_FILE="$RESULTS_DIR/probe-agility-ue5-${PROFILE}.json"
CAPS_RESULT_FILE="$RESULTS_DIR/probe-device-caps-${PROFILE}.json"
DXGI_RESULT_FILE="$RESULTS_DIR/probe-dxgi-factory-${PROFILE}.json"
RESOURCES_RESULT_FILE="$RESULTS_DIR/probe-resources-${PROFILE}.json"
DESCRIPTORS_RESULT_FILE="$RESULTS_DIR/probe-descriptors-${PROFILE}.json"

cat > "$RESULTS_DIR/host-runtime-${PROFILE}.json" <<EOF
{
  "schema": "metalsharp.d3d12-metal.host-runtime.v1",
  "profile": "$PROFILE",
  "wine": "$WINE_BIN",
  "prefix": "$WINE_PREFIX",
  "dxmt_runtime": "$DXMT_RUNTIME",
  "windows_runtime": "$WINDOWS_DIR",
  "unix_runtime": "$UNIX_DIR",
  "winemetal_so": "$UNIX_DIR/winemetal.so",
  "required_windows_dlls": [
    "$WINDOWS_DIR/d3d12.dll",
    "$WINDOWS_DIR/dxgi.dll",
    "$WINDOWS_DIR/d3d11.dll",
    "$WINDOWS_DIR/d3d10core.dll",
    "$WINDOWS_DIR/winemetal.dll"
  ]
}
EOF

if [[ "$RUN_LOADER" == "1" ]]; then
  # DXMT is shipped as PE DLLs; native-first avoids Wine resolving stale builtin shims.
  WINEPREFIX="$WINE_PREFIX" \
  WINEDLLPATH="$WINDOWS_DIR" \
  WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
  DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
  D3D12_METAL_SDK_PROFILE="$PROFILE" \
  D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR="system32" \
  "$WINE_BIN" "$PROBE_EXE" > "$RESULT_FILE"
  echo "$RESULT_FILE"
fi

if [[ "$RUN_AGILITY" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR="system32" \
    "$WINE_BIN" "$AGILITY_PROBE_EXE" > "$AGILITY_RESULT_FILE"
  )
  echo "$AGILITY_RESULT_FILE"
fi

if [[ "$RUN_CAPS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR="system32" \
    "$WINE_BIN" "$CAPS_PROBE_EXE" > "$CAPS_RESULT_FILE"
  )
  echo "$CAPS_RESULT_FILE"
fi

if [[ "$RUN_DXGI" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$DXGI_PROBE_EXE" > "$DXGI_RESULT_FILE"
  )
  echo "$DXGI_RESULT_FILE"
fi

if [[ "$RUN_RESOURCES" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$RESOURCES_PROBE_EXE" > "$RESOURCES_RESULT_FILE"
  )
  echo "$RESOURCES_RESULT_FILE"
fi

if [[ "$RUN_DESCRIPTORS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$DESCRIPTORS_PROBE_EXE" > "$DESCRIPTORS_RESULT_FILE"
  )
  echo "$DESCRIPTORS_RESULT_FILE"
fi
