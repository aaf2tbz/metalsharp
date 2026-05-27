#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
PROFILE="standalone-wine"
WINE_BIN="${WINE_BIN:-wine}"
WINE_PREFIX="${WINEPREFIX:-}"
DXMT_RUNTIME="${DXMT_RUNTIME:-}"
RESULTS_DIR="$SDK_DIR/results"
SHADER_CACHE_DIR="${DXMT_SHADER_CACHE_PATH:-}"
METAL_SHADER_CONVERTER="${METAL_SHADER_CONVERTER:-}"
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
RUN_DXIL_SEMANTICS=0
RUN_GRAPHICS_PSO=1
RUN_COMPUTE_PSO=1
RUN_COMMAND_REPLAY=1
RUN_BARRIERS_RENDER_PASS=1
RUN_RESOURCE_VIEWS_FORMATS=1
RUN_RENDER_HEADLESS=1
RUN_MINI=1
RUN_PRESENT_WINDOWED=0
RUN_FULL_STRESS=0
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
  subnautica_geometry_dxil_replay
  dxil_texture_color_output
  compute_first_use_dispatch
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
  --dxil-semantics      Run the DXIL semantic opcode-group probe.
  --semantic-only       Run only the DXIL semantic opcode-group probe.
  --no-graphics-pso     Skip probe_graphics_pso.
  --graphics-pso-only   Run only the graphics PSO matrix probe.
  --no-compute-pso      Skip probe_compute_pso.
  --compute-pso-only    Run only the compute PSO matrix probe.
  --no-command-replay   Skip probe_command_replay.
  --command-replay-only Run only the command recording/replay probe.
  --no-barriers-render-pass
                        Skip probe_barriers_render_pass.
  --barriers-render-pass-only
                        Run only the resource barrier/render-pass probe.
  --no-resource-views-formats
                        Skip probe_resource_views_formats.
  --resource-views-formats-only
                        Run only the resource/view/format probe.
  --no-render-headless  Skip probe_render_headless.
  --no-mini             Skip one-purpose D3D12 mini-app probes.
  --mini-only           Run only one-purpose D3D12 mini-app probes.
  --windowed-present    Run the optional probe_present_windowed window/swapchain proof.
  --no-windowed-present Skip probe_present_windowed.
  --full-stress         Run the full Subnautica DXBC shader corpus stress probe.
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
    --dxil-semantics)
      RUN_DXIL_SEMANTICS=1
      shift
      ;;
    --semantic-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_DXIL_SEMANTICS=1
      RUN_RENDER_HEADLESS=0
      RUN_GRAPHICS_PSO=0
      RUN_COMPUTE_PSO=0
      RUN_COMMAND_REPLAY=0
      RUN_BARRIERS_RENDER_PASS=0
      RUN_RESOURCE_VIEWS_FORMATS=0
      RUN_MINI=0
      RUN_PRESENT_WINDOWED=0
      shift
      ;;
    --no-graphics-pso)
      RUN_GRAPHICS_PSO=0
      shift
      ;;
    --graphics-pso-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_DXIL_SEMANTICS=0
      RUN_GRAPHICS_PSO=1
      RUN_COMPUTE_PSO=0
      RUN_COMMAND_REPLAY=0
      RUN_BARRIERS_RENDER_PASS=0
      RUN_RESOURCE_VIEWS_FORMATS=0
      RUN_RENDER_HEADLESS=0
      RUN_MINI=0
      RUN_PRESENT_WINDOWED=0
      shift
      ;;
    --no-compute-pso)
      RUN_COMPUTE_PSO=0
      shift
      ;;
    --compute-pso-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_DXIL_SEMANTICS=0
      RUN_GRAPHICS_PSO=0
      RUN_COMPUTE_PSO=1
      RUN_COMMAND_REPLAY=0
      RUN_BARRIERS_RENDER_PASS=0
      RUN_RESOURCE_VIEWS_FORMATS=0
      RUN_RENDER_HEADLESS=0
      RUN_MINI=0
      RUN_PRESENT_WINDOWED=0
      shift
      ;;
    --no-command-replay)
      RUN_COMMAND_REPLAY=0
      shift
      ;;
    --command-replay-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_DXIL_SEMANTICS=0
      RUN_GRAPHICS_PSO=0
      RUN_COMPUTE_PSO=0
      RUN_COMMAND_REPLAY=1
      RUN_BARRIERS_RENDER_PASS=0
      RUN_RESOURCE_VIEWS_FORMATS=0
      RUN_RENDER_HEADLESS=0
      RUN_MINI=0
      RUN_PRESENT_WINDOWED=0
      shift
      ;;
    --no-barriers-render-pass)
      RUN_BARRIERS_RENDER_PASS=0
      shift
      ;;
    --barriers-render-pass-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_DXIL_SEMANTICS=0
      RUN_GRAPHICS_PSO=0
      RUN_COMPUTE_PSO=0
      RUN_COMMAND_REPLAY=0
      RUN_BARRIERS_RENDER_PASS=1
      RUN_RESOURCE_VIEWS_FORMATS=0
      RUN_RENDER_HEADLESS=0
      RUN_MINI=0
      RUN_PRESENT_WINDOWED=0
      shift
      ;;
    --no-resource-views-formats)
      RUN_RESOURCE_VIEWS_FORMATS=0
      shift
      ;;
    --resource-views-formats-only)
      RUN_LOADER=0
      RUN_AGILITY=0
      RUN_CAPS=0
      RUN_DXGI=0
      RUN_RESOURCES=0
      RUN_QUEUES=0
      RUN_DESCRIPTORS=0
      RUN_SHADERS=0
      RUN_DXIL_SEMANTICS=0
      RUN_GRAPHICS_PSO=0
      RUN_COMPUTE_PSO=0
      RUN_COMMAND_REPLAY=0
      RUN_BARRIERS_RENDER_PASS=0
      RUN_RESOURCE_VIEWS_FORMATS=1
      RUN_RENDER_HEADLESS=0
      RUN_MINI=0
      RUN_PRESENT_WINDOWED=0
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
      RUN_GRAPHICS_PSO=0
      RUN_COMPUTE_PSO=0
      RUN_COMMAND_REPLAY=0
      RUN_BARRIERS_RENDER_PASS=0
      RUN_RESOURCE_VIEWS_FORMATS=0
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
    --full-stress)
      RUN_FULL_STRESS=1
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
RUNTIME_LIB_DIR="$(dirname "$DXMT_RUNTIME")"
WINE_UNIX_DIR="$RUNTIME_LIB_DIR/wine/x86_64-unix"
DXMT_DYLD_LIBRARY_PATH="$WINE_UNIX_DIR:$UNIX_DIR:${DYLD_LIBRARY_PATH:-}"
DXMT_WINEMETAL_UNIXLIB_NAME="winemetal.so"
PROBE_EXE="$SDK_DIR/out/bin/probe_loader.exe"
AGILITY_PROBE_EXE="$SDK_DIR/out/bin/probe_agility_ue5.exe"
CAPS_PROBE_EXE="$SDK_DIR/out/bin/probe_device_caps.exe"
DXGI_PROBE_EXE="$SDK_DIR/out/bin/probe_dxgi_factory.exe"
RESOURCES_PROBE_EXE="$SDK_DIR/out/bin/probe_resources.exe"
QUEUES_PROBE_EXE="$SDK_DIR/out/bin/probe_queues.exe"
DESCRIPTORS_PROBE_EXE="$SDK_DIR/out/bin/probe_descriptors.exe"
SHADERS_PROBE_EXE="$SDK_DIR/out/bin/probe_shaders.exe"
DXIL_SEMANTICS_PROBE_EXE="$SDK_DIR/out/bin/probe_dxil_semantics.exe"
GRAPHICS_PSO_PROBE_EXE="$SDK_DIR/out/bin/probe_graphics_pso.exe"
COMPUTE_PSO_PROBE_EXE="$SDK_DIR/out/bin/probe_compute_pso.exe"
COMMAND_REPLAY_PROBE_EXE="$SDK_DIR/out/bin/probe_command_replay.exe"
BARRIERS_RENDER_PASS_PROBE_EXE="$SDK_DIR/out/bin/probe_barriers_render_pass.exe"
RESOURCE_VIEWS_FORMATS_PROBE_EXE="$SDK_DIR/out/bin/probe_resource_views_formats.exe"
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
  # Wine resolves native PE DLLs from the application directory before
  # WINEDLLPATH. Keep the probe bin dir pinned to the selected runtime so
  # stale local DLLs cannot make the SDK report false results.
  cp "$WINDOWS_DIR/$dll" "$SDK_DIR/out/bin/$dll"
done

if [[ ! -f "$UNIX_DIR/winemetal.so" ]]; then
  echo "Missing winemetal.so: $UNIX_DIR/winemetal.so" >&2
  exit 2
fi

for unix_dep in winemac.so ntdll.so; do
  if [[ ! -f "$WINE_UNIX_DIR/$unix_dep" ]]; then
    echo "Missing Wine Unix dependency for winemetal.so: $WINE_UNIX_DIR/$unix_dep" >&2
    exit 2
  fi
done

if [[ "$WINDOWS_DIR" == *"/gptk/"* || "$WINDOWS_DIR" == *"/lib/gptk/"* ]]; then
  echo "DXMT runtime points at GPTK/D3DMetal DLLs, not DXMT: $WINDOWS_DIR" >&2
  exit 2
fi

NEED_BUILD=0
if [[ ! -f "$PROBE_EXE" || ! -f "$AGILITY_PROBE_EXE" || ! -f "$CAPS_PROBE_EXE" || ! -f "$DXGI_PROBE_EXE" || ! -f "$RESOURCES_PROBE_EXE" || ! -f "$QUEUES_PROBE_EXE" || ! -f "$DESCRIPTORS_PROBE_EXE" || ! -f "$SHADERS_PROBE_EXE" || ! -f "$DXIL_SEMANTICS_PROBE_EXE" || ! -f "$GRAPHICS_PSO_PROBE_EXE" || ! -f "$COMPUTE_PSO_PROBE_EXE" || ! -f "$COMMAND_REPLAY_PROBE_EXE" || ! -f "$BARRIERS_RENDER_PASS_PROBE_EXE" || ! -f "$RESOURCE_VIEWS_FORMATS_PROBE_EXE" || ! -f "$RENDER_HEADLESS_PROBE_EXE" || ! -f "$PRESENT_WINDOWED_PROBE_EXE" || ! -f "$SDK_DIR/out/bin/D3D12/D3D12Core.dll" || ! -f "$SDK_DIR/out/bin/dxc.exe" || ! -f "$SDK_DIR/out/bin/dxcompiler.dll" || ! -f "$SDK_DIR/out/bin/dxil.dll" ]]; then
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
if [[ -z "$SHADER_CACHE_DIR" ]]; then
  SHADER_CACHE_DIR="$RESULTS_DIR/shader-cache-$PROFILE"
fi
RESULT_FILE="$RESULTS_DIR/probe-loader-${PROFILE}.json"
AGILITY_RESULT_FILE="$RESULTS_DIR/probe-agility-ue5-${PROFILE}.json"
CAPS_RESULT_FILE="$RESULTS_DIR/probe-device-caps-${PROFILE}.json"
DXGI_RESULT_FILE="$RESULTS_DIR/probe-dxgi-factory-${PROFILE}.json"
RESOURCES_RESULT_FILE="$RESULTS_DIR/probe-resources-${PROFILE}.json"
QUEUES_RESULT_FILE="$RESULTS_DIR/probe-queues-${PROFILE}.json"
DESCRIPTORS_RESULT_FILE="$RESULTS_DIR/probe-descriptors-${PROFILE}.json"
SHADERS_RESULT_FILE="$RESULTS_DIR/probe-shaders-${PROFILE}.json"
DXIL_SEMANTICS_WARMUP_RESULT_FILE="$RESULTS_DIR/probe-dxil-semantics-warmup-${PROFILE}.json"
DXIL_SEMANTICS_RESULT_FILE="$RESULTS_DIR/probe-dxil-semantics-${PROFILE}.json"
GRAPHICS_PSO_RESULT_FILE="$RESULTS_DIR/probe-graphics-pso-${PROFILE}.json"
COMPUTE_PSO_RESULT_FILE="$RESULTS_DIR/probe-compute-pso-${PROFILE}.json"
COMMAND_REPLAY_RESULT_FILE="$RESULTS_DIR/probe-command-replay-${PROFILE}.json"
BARRIERS_RENDER_PASS_RESULT_FILE="$RESULTS_DIR/probe-barriers-render-pass-${PROFILE}.json"
RESOURCE_VIEWS_FORMATS_RESULT_FILE="$RESULTS_DIR/probe-resource-views-formats-${PROFILE}.json"
RENDER_HEADLESS_RESULT_FILE="$RESULTS_DIR/probe-render-headless-${PROFILE}.json"
PRESENT_WINDOWED_RESULT_FILE="$RESULTS_DIR/probe-present-windowed-${PROFILE}.json"

run_probe_exe() {
  local exe="$1"
  local result_file="$2"
  local strict_deferred_pso=0
  local enable_geometry_mesh="${DXMT_D3D12_ENABLE_GEOMETRY_MESH:-0}"
  if [[ "$(basename "$exe")" == "probe_mini_subnautica_geometry_dxil_replay.exe" ]]; then
    strict_deferred_pso=1
    enable_geometry_mesh=1
  fi
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    DXMT_SHADER_CACHE_PATH="$SHADER_CACHE_DIR" \
    DXMT_D3D12_ENABLE_GEOMETRY_MESH="$enable_geometry_mesh" \
    DXMT_D3D12_FAIL_DEFERRED_PSO="$strict_deferred_pso" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$exe" > "$result_file"
  )
  echo "$result_file"
}

convert_dxil_shader_cache() {
  local cache_dir="$1"
  local converter="$METAL_SHADER_CONVERTER"
  if [[ -z "$converter" ]]; then
    converter="$(command -v metal-shaderconverter || true)"
  fi
  if [[ -z "$converter" || ! -x "$converter" ]]; then
    echo "metal-shaderconverter not found; DXIL probes may fail until shader cache is prebuilt" >&2
    return 0
  fi

  local dxbc
  shopt -s nullglob
  for dxbc in "$cache_dir"/*.dxbc; do
    local base="${dxbc%.dxbc}"
    local metallib="$base.metallib"
    local reflection="$base.json"
    local layout="$base.vertex-layout.json"
    if [[ -s "$metallib" && -s "$reflection" ]]; then
      continue
    fi
    if [[ -s "$layout" ]]; then
      "$converter" -o "$metallib" "$dxbc" \
        --output-reflection-file="$reflection" \
        --deployment-os=macOS \
        --minimum-os-build-version=15.0.0 \
        --vertex-input-layout-file="$layout" >/dev/null
    else
      "$converter" -o "$metallib" "$dxbc" \
        --output-reflection-file="$reflection" \
        --deployment-os=macOS \
        --minimum-os-build-version=15.0.0 >/dev/null
    fi
  done
  shopt -u nullglob
}

prepare_dxil_color_probe() {
  local hlsl="$SDK_DIR/out/bin/probe_dxil_color.hlsl"
  local vs="$SDK_DIR/out/bin/probe_dxil_color_vs.cso"
  local ps="$SDK_DIR/out/bin/probe_dxil_color_ps.cso"

  cat > "$hlsl" <<'HLSL'
struct VSIn {
  float3 pos : POSITION;
  float2 uv : TEXCOORD0;
};

struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
};

Texture2D tx : register(t0);
SamplerState smp : register(s0);

VSOut vs_main(VSIn input) {
  VSOut output;
  output.pos = float4(input.pos, 1.0);
  output.uv = input.uv;
  return output;
}

float4 ps_main(VSOut input) : SV_Target0 {
  return tx.Sample(smp, input.uv);
}
HLSL

  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLOVERRIDES="dxcompiler,dxil=n,b" \
    "$WINE_BIN" dxc.exe -nologo -E vs_main -T vs_6_0 -Fo probe_dxil_color_vs.cso probe_dxil_color.hlsl >/dev/null
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLOVERRIDES="dxcompiler,dxil=n,b" \
    "$WINE_BIN" dxc.exe -nologo -E ps_main -T ps_6_0 -Fo probe_dxil_color_ps.cso probe_dxil_color.hlsl >/dev/null
  )

  mkdir -p "$SHADER_CACHE_DIR"
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    DXMT_SHADER_CACHE_PATH="$SHADER_CACHE_DIR" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" probe_mini_dxil_texture_color_output.exe >/dev/null || true
  )
  convert_dxil_shader_cache "$SHADER_CACHE_DIR"
}

prepare_dxil_semantic_probes() {
  local hlsl="$SDK_DIR/out/bin/probe_dxil_semantics.hlsl"

  cat > "$hlsl" <<'HLSL'
RWByteAddressBuffer outbuf : register(u0);
ByteAddressBuffer inbuf : register(t0);

[numthreads(4, 1, 1)]
void cs_math_bits(uint3 id : SV_DispatchThreadID) {
  if (id.x == 0) {
    float f = sqrt(144.0) + abs(-5.0) + floor(2.9) + ceil(2.1);
    uint bits = (1u << 5) | (0xf0u & 0x0fu) | (0x12u ^ 0x02u);
    outbuf.Store(0, (uint)f);
    outbuf.Store(4, bits);
    outbuf.Store(8, asuint(asfloat(0x3f800000u)));
    outbuf.Store(12, countbits(0xf0f0u));
  }
}

[numthreads(4, 1, 1)]
void cs_buffer(uint3 id : SV_DispatchThreadID) {
  uint v = inbuf.Load(id.x * 4);
  outbuf.Store(id.x * 4, v * 3 + 1);
}

groupshared uint g_counter;

[numthreads(4, 1, 1)]
void cs_atomics_ids(uint3 gid : SV_GroupID,
                    uint3 tid : SV_GroupThreadID,
                    uint gi : SV_GroupIndex,
                    uint3 did : SV_DispatchThreadID) {
  if (gi == 0)
    g_counter = 0;
  GroupMemoryBarrierWithGroupSync();
  InterlockedAdd(g_counter, 1);
  GroupMemoryBarrierWithGroupSync();
  outbuf.Store(gi * 4, g_counter + did.x + tid.x + gid.x);
}

[numthreads(4, 1, 1)]
void cs_wave_quad(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint first = WaveReadLaneFirst(id.x + 1);
  uint sum = WaveActiveSum(1);
  uint across = QuadReadAcrossX(id.x);
  uint valid = (sum >= 4 ? 0x100u : 0u) |
               (first == 1 ? 0x10u : 0u) |
               (across == (id.x ^ 1u) ? 0x1u : 0u);
  outbuf.Store(gi * 4, valid);
}
HLSL

  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLOVERRIDES="dxcompiler,dxil=n,b" \
    "$WINE_BIN" dxc.exe -nologo -E cs_math_bits -T cs_6_0 -Fo probe_dxil_semantic_math_bits.cso probe_dxil_semantics.hlsl >/dev/null
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLOVERRIDES="dxcompiler,dxil=n,b" \
    "$WINE_BIN" dxc.exe -nologo -E cs_buffer -T cs_6_0 -Fo probe_dxil_semantic_buffer.cso probe_dxil_semantics.hlsl >/dev/null
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLOVERRIDES="dxcompiler,dxil=n,b" \
    "$WINE_BIN" dxc.exe -nologo -E cs_atomics_ids -T cs_6_0 -Fo probe_dxil_semantic_atomics_ids.cso probe_dxil_semantics.hlsl >/dev/null
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLOVERRIDES="dxcompiler,dxil=n,b" \
    "$WINE_BIN" dxc.exe -nologo -E cs_wave_quad -T cs_6_6 -Fo probe_dxil_semantic_wave_quad.cso probe_dxil_semantics.hlsl >/dev/null
  )

  mkdir -p "$SHADER_CACHE_DIR"
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    DXMT_SHADER_CACHE_PATH="$SHADER_CACHE_DIR" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_DXIL_SEMANTICS_MODE="warmup" \
    "$WINE_BIN" "$DXIL_SEMANTICS_PROBE_EXE" > "$DXIL_SEMANTICS_WARMUP_RESULT_FILE"
  )
  convert_dxil_shader_cache "$SHADER_CACHE_DIR"
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
  "wine_unix_runtime": "$WINE_UNIX_DIR",
  "dyld_runtime_path": "$DXMT_DYLD_LIBRARY_PATH",
  "shader_cache": "$SHADER_CACHE_DIR",
  "winemetal_unixlib": "$DXMT_WINEMETAL_UNIXLIB_NAME",
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

if [[ "$RUN_MINI" == "1" ]]; then
  prepare_dxil_color_probe
fi

if [[ "$RUN_LOADER" == "1" ]]; then
  # DXMT is shipped as PE DLLs; native-first avoids Wine resolving stale builtin shims.
  WINEPREFIX="$WINE_PREFIX" \
  WINEDLLPATH="$WINDOWS_DIR" \
  WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
  DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
  DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    D3D12_METAL_SDK_EXPECT_DXC="1" \
    "$WINE_BIN" "$SHADERS_PROBE_EXE" > "$SHADERS_RESULT_FILE"
  )
  echo "$SHADERS_RESULT_FILE"
fi

if [[ "$RUN_DXIL_SEMANTICS" == "1" ]]; then
  prepare_dxil_semantic_probes
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    DXMT_SHADER_CACHE_PATH="$SHADER_CACHE_DIR" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$DXIL_SEMANTICS_PROBE_EXE" > "$DXIL_SEMANTICS_RESULT_FILE"
  )
  echo "$DXIL_SEMANTICS_WARMUP_RESULT_FILE"
  echo "$DXIL_SEMANTICS_RESULT_FILE"
fi

if [[ "$RUN_GRAPHICS_PSO" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    DXMT_SHADER_CACHE_PATH="$SHADER_CACHE_DIR" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$GRAPHICS_PSO_PROBE_EXE" > "$GRAPHICS_PSO_RESULT_FILE"
  )
  echo "$GRAPHICS_PSO_RESULT_FILE"
fi

if [[ "$RUN_COMPUTE_PSO" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    DXMT_SHADER_CACHE_PATH="$SHADER_CACHE_DIR" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$COMPUTE_PSO_PROBE_EXE" > "$COMPUTE_PSO_RESULT_FILE"
  )
  echo "$COMPUTE_PSO_RESULT_FILE"
fi

if [[ "$RUN_COMMAND_REPLAY" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$COMMAND_REPLAY_PROBE_EXE" > "$COMMAND_REPLAY_RESULT_FILE"
  )
  echo "$COMMAND_REPLAY_RESULT_FILE"
fi

if [[ "$RUN_BARRIERS_RENDER_PASS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$BARRIERS_RENDER_PASS_PROBE_EXE" > "$BARRIERS_RENDER_PASS_RESULT_FILE"
  )
  echo "$BARRIERS_RENDER_PASS_RESULT_FILE"
fi

if [[ "$RUN_RESOURCE_VIEWS_FORMATS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$RESOURCE_VIEWS_FORMATS_PROBE_EXE" > "$RESOURCE_VIEWS_FORMATS_RESULT_FILE"
  )
  echo "$RESOURCE_VIEWS_FORMATS_RESULT_FILE"
fi

if [[ "$RUN_RENDER_HEADLESS" == "1" ]]; then
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
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
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    "$WINE_BIN" "$PRESENT_WINDOWED_PROBE_EXE" > "$PRESENT_WINDOWED_RESULT_FILE"
  )
  echo "$PRESENT_WINDOWED_RESULT_FILE"
fi

FULL_STRESS_RESULT_DIR="$RESULTS_DIR/full-stress"
FULL_STRESS_PROBE_EXE="$SDK_DIR/out/bin/probe_subnautica_full_stress.exe"

if [[ "$RUN_FULL_STRESS" == "1" ]]; then
  mkdir -p "$FULL_STRESS_RESULT_DIR"
  (
    cd "$SDK_DIR/out/bin"
    WINEPREFIX="$WINE_PREFIX" \
    WINEDLLPATH="$WINDOWS_DIR" \
    WINEDLLOVERRIDES="d3d12,dxgi,d3d11,d3d10core,winemetal=n,b" \
    DYLD_LIBRARY_PATH="$DXMT_DYLD_LIBRARY_PATH" \
    DXMT_WINEMETAL_UNIXLIB="$DXMT_WINEMETAL_UNIXLIB_NAME" \
    D3D12_METAL_SDK_PROFILE="$PROFILE" \
    DXMT_SHADER_CACHE="/tmp/dxmt_shader_cache" \
    "$WINE_BIN" "$FULL_STRESS_PROBE_EXE" \
      > "$FULL_STRESS_RESULT_DIR/probe_full_stress.stdout.jsonl" \
      2> "$FULL_STRESS_RESULT_DIR/probe_full_stress.stderr.log"
  )
  echo "$FULL_STRESS_RESULT_DIR"
fi
