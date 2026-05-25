#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="standalone-wine"
WINE_BIN="${WINE_BIN:-wine}"
WINE_PREFIX="${WINEPREFIX:-}"
DXMT_RUNTIME="${DXMT_RUNTIME:-}"
RESULTS_DIR="$SDK_DIR/results"
AGILITY_SDK_VERSION="${AGILITY_SDK_VERSION:-}"
AGILITY_SDK_PATH="${AGILITY_SDK_PATH:-}"
RUN_LOADER=1
RUN_AGILITY=1
RUN_CAPS=1
RUN_DXGI=1
RUN_RESOURCES=1
RUN_QUEUES=1
RUN_DESCRIPTORS=1
RUN_SHADERS=1
RUN_RENDER_HEADLESS=1
RUN_MINI=1
RUN_PRESENT_WINDOWED=0
MINI_PROBES=(
  create_device
  command_queue
  swapchain_present
  rtv_clear
  compute_dispatch
  root_signature
  descriptors
  graphics_pso
  geometry_shader_pso
  mesh_object_shader_pso
  texture_sample
)

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
  --agility-sdk-version N
                        Override exported D3D12SDKVersion for Agility-sensitive probes.
  --agility-sdk-path REL
                        Override exported D3D12SDKPath for Agility-sensitive probes.
  --no-loader           Skip probe_loader.
  --no-agility          Skip probe_agility_ue5.
  --no-caps             Skip probe_device_caps.
  --no-dxgi             Skip probe_dxgi_factory.
  --no-resources        Skip probe_resources.
  --no-queues           Skip probe_queues.
  --no-descriptors      Skip probe_descriptors.
  --no-shaders          Skip probe_shaders.
  --no-render-headless  Skip probe_render_headless.
  --no-mini             Skip one-purpose D3D12 mini-app probes.
  --mini-only           Run only one-purpose D3D12 mini-app probes.
  --windowed-present    Run the optional probe_present_windowed window/swapchain proof.
  --no-windowed-present Skip probe_present_windowed.
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
    --agility-sdk-version)
      AGILITY_SDK_VERSION="$2"
      shift 2
      ;;
    --agility-sdk-path)
      AGILITY_SDK_PATH="$2"
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
    --no-queues)
      RUN_QUEUES=0
      shift
      ;;
    --no-descriptors)
      RUN_DESCRIPTORS=0
      shift
      ;;
    --no-shaders)
      RUN_SHADERS=0
      shift
      ;;
    --no-render-headless)
      RUN_RENDER_HEADLESS=0
      shift
      ;;
    --no-mini)
      RUN_MINI=0
      shift
      ;;
    --mini-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_RENDER_HEADLESS=0
      RUN_MINI=1
      RUN_PRESENT_WINDOWED=0
      shift
      ;;
    --windowed-present)
      RUN_PRESENT_WINDOWED=1
      shift
      ;;
    --no-windowed-present)
      RUN_PRESENT_WINDOWED=0
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
QUEUES_PROBE_EXE="$SDK_DIR/out/bin/probe_queues.exe"
DESCRIPTORS_PROBE_EXE="$SDK_DIR/out/bin/probe_descriptors.exe"
SHADERS_PROBE_EXE="$SDK_DIR/out/bin/probe_shaders.exe"
RENDER_HEADLESS_PROBE_EXE="$SDK_DIR/out/bin/probe_render_headless.exe"
PRESENT_WINDOWED_PROBE_EXE="$SDK_DIR/out/bin/probe_present_windowed.exe"

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

NEED_BUILD=0
if [[ ! -f "$PROBE_EXE" || ! -f "$AGILITY_PROBE_EXE" || ! -f "$CAPS_PROBE_EXE" || ! -f "$DXGI_PROBE_EXE" || ! -f "$RESOURCES_PROBE_EXE" || ! -f "$QUEUES_PROBE_EXE" || ! -f "$DESCRIPTORS_PROBE_EXE" || ! -f "$SHADERS_PROBE_EXE" || ! -f "$RENDER_HEADLESS_PROBE_EXE" || ! -f "$PRESENT_WINDOWED_PROBE_EXE" || ! -f "$SDK_DIR/out/bin/D3D12/D3D12Core.dll" || ! -f "$SDK_DIR/out/bin/dxc.exe" || ! -f "$SDK_DIR/out/bin/dxcompiler.dll" || ! -f "$SDK_DIR/out/bin/dxil.dll" ]]; then
  NEED_BUILD=1
fi

for mini_probe in "${MINI_PROBES[@]}"; do
  if [[ ! -f "$SDK_DIR/out/bin/probe_mini_${mini_probe}.exe" ]]; then
    NEED_BUILD=1
  fi
done

if [[ "$NEED_BUILD" == "1" ]]; then
  "$SDK_DIR/scripts/build-probes.sh" >/dev/null
fi

mkdir -p "$RESULTS_DIR"
RESULT_FILE="$RESULTS_DIR/probe-loader-${PROFILE}.json"
AGILITY_RESULT_FILE="$RESULTS_DIR/probe-agility-ue5-${PROFILE}.json"
CAPS_RESULT_FILE="$RESULTS_DIR/probe-device-caps-${PROFILE}.json"
DXGI_RESULT_FILE="$RESULTS_DIR/probe-dxgi-factory-${PROFILE}.json"
RESOURCES_RESULT_FILE="$RESULTS_DIR/probe-resources-${PROFILE}.json"
QUEUES_RESULT_FILE="$RESULTS_DIR/probe-queues-${PROFILE}.json"
DESCRIPTORS_RESULT_FILE="$RESULTS_DIR/probe-descriptors-${PROFILE}.json"
SHADERS_RESULT_FILE="$RESULTS_DIR/probe-shaders-${PROFILE}.json"
RENDER_HEADLESS_RESULT_FILE="$RESULTS_DIR/probe-render-headless-${PROFILE}.json"
PRESENT_WINDOWED_RESULT_FILE="$RESULTS_DIR/probe-present-windowed-${PROFILE}.json"

run_probe_exe() {
  local exe="$1"
  local result_file="$2"
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$exe" > "$result_file"
  )
  echo "$result_file"
}

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
  DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
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
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR="system32" \
    D3D12_METAL_SDK_AGILITY_VERSION="$AGILITY_SDK_VERSION" \
    D3D12_METAL_SDK_AGILITY_PATH="$AGILITY_SDK_PATH" \
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
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR="system32" \
    D3D12_METAL_SDK_AGILITY_VERSION="$AGILITY_SDK_VERSION" \
    D3D12_METAL_SDK_AGILITY_PATH="$AGILITY_SDK_PATH" \
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
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
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
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$RESOURCES_PROBE_EXE" > "$RESOURCES_RESULT_FILE"
  )
  echo "$RESOURCES_RESULT_FILE"
fi

if [[ "$RUN_QUEUES" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$QUEUES_PROBE_EXE" > "$QUEUES_RESULT_FILE"
  )
  echo "$QUEUES_RESULT_FILE"
fi

if [[ "$RUN_DESCRIPTORS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$DESCRIPTORS_PROBE_EXE" > "$DESCRIPTORS_RESULT_FILE"
  )
  echo "$DESCRIPTORS_RESULT_FILE"
fi

if [[ "$RUN_SHADERS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_EXPECT_DXC="1" \
    "$WINE_BIN" "$SHADERS_PROBE_EXE" > "$SHADERS_RESULT_FILE"
  )
  echo "$SHADERS_RESULT_FILE"
fi

if [[ "$RUN_RENDER_HEADLESS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$RENDER_HEADLESS_PROBE_EXE" > "$RENDER_HEADLESS_RESULT_FILE"
  )
  echo "$RENDER_HEADLESS_RESULT_FILE"
fi

if [[ "$RUN_MINI" == "1" ]]; then
  for mini_probe in "${MINI_PROBES[@]}"; do
    run_probe_exe \
      "$SDK_DIR/out/bin/probe_mini_${mini_probe}.exe" \
      "$RESULTS_DIR/probe-mini-${mini_probe}-${PROFILE}.json"
  done
fi

if [[ "$RUN_PRESENT_WINDOWED" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$UNIX_DIR:${DYLD_LIBRARY_PATH:-}" \
    DXMT_WINEMETAL_UNIXLIB="$UNIX_DIR/winemetal.so" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$PRESENT_WINDOWED_PROBE_EXE" > "$PRESENT_WINDOWED_RESULT_FILE"
  )
  echo "$PRESENT_WINDOWED_RESULT_FILE"
fi
