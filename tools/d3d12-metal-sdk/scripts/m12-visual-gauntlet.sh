#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
RESULTS_DIR="$SDK_DIR/results/m12-visual-gauntlet/$TIMESTAMP"
WINE_BIN="${WINE_BIN:-$HOME/.metalsharp/runtime/wine/bin/metalsharp-wine}"
WINE_PREFIX="${WINEPREFIX:-$HOME/.metalsharp/prefix-steam}"
DXMT_RUNTIME="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12"
RUN_WINDOWED=0
ALLOW_FAILURES=0

EXPECT_D3D12_SHA="2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c"
EXPECT_DXGI_SHA="dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24"
EXPECT_DXGI_DXMT_SHA="659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d"
EXPECT_WINEMETAL_DLL_SHA="7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85"
EXPECT_WINEMETAL_SO_SHA="167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58"

usage() {
  cat <<'USAGE'
Usage:
  m12-visual-gauntlet.sh [options]

Runs the Phase 5 deterministic visual/headless correctness gauntlet.
This does not launch games and does not stage runtime DLLs.

Options:
  --results-dir PATH              Output directory.
  --wine PATH                     Wine/MetalSharp Wine binary.
  --prefix PATH                   WINEPREFIX.
  --dxmt-runtime PATH             Runtime root with x86_64-windows and x86_64-unix.
  --windowed-present              Also run the optional windowed swapchain/present probe.
  --allow-failures                Generate reports but exit 0 even if probes fail.
  --expect-d3d12-sha SHA
  --expect-dxgi-sha SHA
  --expect-dxgi-dxmt-sha SHA
  --expect-winemetal-dll-sha SHA
  --expect-winemetal-so-sha SHA
  -h, --help                      Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --wine) WINE_BIN="$2"; shift 2 ;;
    --prefix) WINE_PREFIX="$2"; shift 2 ;;
    --dxmt-runtime) DXMT_RUNTIME="$2"; shift 2 ;;
    --windowed-present) RUN_WINDOWED=1; shift ;;
    --allow-failures) ALLOW_FAILURES=1; shift ;;
    --expect-d3d12-sha) EXPECT_D3D12_SHA="$2"; shift 2 ;;
    --expect-dxgi-sha) EXPECT_DXGI_SHA="$2"; shift 2 ;;
    --expect-dxgi-dxmt-sha) EXPECT_DXGI_DXMT_SHA="$2"; shift 2 ;;
    --expect-winemetal-dll-sha) EXPECT_WINEMETAL_DLL_SHA="$2"; shift 2 ;;
    --expect-winemetal-so-sha) EXPECT_WINEMETAL_SO_SHA="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

sha256_file() {
  shasum -a 256 "$1" | awk '{print $1}'
}

check_hash() {
  local label="$1"
  local path="$2"
  local expected="$3"
  if [[ ! -f "$path" ]]; then
    echo "Missing $label: $path" >&2
    exit 2
  fi
  local actual
  actual="$(sha256_file "$path")"
  if [[ -n "$expected" && "$actual" != "$expected" ]]; then
    echo "M12 runtime $label hash mismatch: expected $expected got $actual path=$path" >&2
    exit 3
  fi
}

if [[ ! -x "$WINE_BIN" ]]; then
  echo "Wine binary is not executable: $WINE_BIN" >&2
  exit 2
fi
if [[ ! -d "$DXMT_RUNTIME/x86_64-windows" || ! -d "$DXMT_RUNTIME/x86_64-unix" ]]; then
  echo "Invalid M12 runtime root: $DXMT_RUNTIME" >&2
  exit 2
fi

WINDOWS_DIR="$DXMT_RUNTIME/x86_64-windows"
UNIX_DIR="$DXMT_RUNTIME/x86_64-unix"
WINE_UNIX_DIR="$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix"

check_hash d3d12.dll "$WINDOWS_DIR/d3d12.dll" "$EXPECT_D3D12_SHA"
check_hash dxgi.dll "$WINDOWS_DIR/dxgi.dll" "$EXPECT_DXGI_SHA"
check_hash dxgi_dxmt.dll "$WINDOWS_DIR/dxgi_dxmt.dll" "$EXPECT_DXGI_DXMT_SHA"
check_hash winemetal.dll "$WINDOWS_DIR/winemetal.dll" "$EXPECT_WINEMETAL_DLL_SHA"
check_hash dxmt_m12/winemetal.so "$UNIX_DIR/winemetal.so" "$EXPECT_WINEMETAL_SO_SHA"
check_hash wine/winemetal.so "$WINE_UNIX_DIR/winemetal.so" "$EXPECT_WINEMETAL_SO_SHA"

mkdir -p "$RESULTS_DIR/image-diffs"
cat > "$RESULTS_DIR/visual-preflight.json" <<EOF_JSON
{
  "schema": "metalsharp.m12.visual-gauntlet.preflight.v1",
  "wine": "$WINE_BIN",
  "prefix": "$WINE_PREFIX",
  "dxmt_runtime": "$DXMT_RUNTIME",
  "windowed_present": $([[ "$RUN_WINDOWED" == "1" ]] && echo true || echo false),
  "runtime_hashes": {
    "d3d12.dll": "$(sha256_file "$WINDOWS_DIR/d3d12.dll")",
    "dxgi.dll": "$(sha256_file "$WINDOWS_DIR/dxgi.dll")",
    "dxgi_dxmt.dll": "$(sha256_file "$WINDOWS_DIR/dxgi_dxmt.dll")",
    "winemetal.dll": "$(sha256_file "$WINDOWS_DIR/winemetal.dll")",
    "dxmt_m12/winemetal.so": "$(sha256_file "$UNIX_DIR/winemetal.so")",
    "wine/winemetal.so": "$(sha256_file "$WINE_UNIX_DIR/winemetal.so")"
  }
}
EOF_JSON

rm -f /tmp/dxmt_render_headless_rgba.bin
RUN_ARGS=(
  --profile metalsharp
  --wine "$WINE_BIN"
  --prefix "$WINE_PREFIX"
  --dxmt-runtime "$DXMT_RUNTIME"
  --results-dir "$RESULTS_DIR"
  --render-headless
  --no-loader
  --no-agility
  --no-caps
  --no-dxgi
  --no-resources
  --no-queues
  --no-descriptors
  --no-shaders
  --no-dxil-semantics
  --no-shader-corpus
  --no-sm66-capabilities
  --no-wave-ops
  --no-reflection-abi
  --no-graphics-pso
  --no-compute-pso
  --no-command-replay
  --no-barriers-render-pass
  --no-resource-views-formats
  --no-heap-aliasing
  --no-mini
)
if [[ "$RUN_WINDOWED" == "1" ]]; then
  RUN_ARGS+=(--windowed-present)
else
  RUN_ARGS+=(--no-windowed-present)
fi

set +e
"$SDK_DIR/scripts/run-probes.sh" "${RUN_ARGS[@]}" \
  > "$RESULTS_DIR/run-probes.stdout" \
  2> "$RESULTS_DIR/run-probes.stderr"
RUN_PROBES_EXIT=$?
set -e

if [[ -f /tmp/dxmt_render_headless_rgba.bin ]]; then
  cp /tmp/dxmt_render_headless_rgba.bin "$RESULTS_DIR/image-diffs/render-headless-rgba.bin"
fi

python3 - "$RESULTS_DIR" "$RUN_PROBES_EXIT" <<'PY'
from __future__ import annotations
import json
import sys
from pathlib import Path

results = Path(sys.argv[1])
run_exit = int(sys.argv[2])
preflight = json.loads((results / "visual-preflight.json").read_text())
headless_path = results / "probe-render-headless-metalsharp.json"
windowed_path = results / "probe-present-windowed-metalsharp.json"
headless = json.loads(headless_path.read_text()) if headless_path.exists() else {"pass": False, "missing": True}
windowed = json.loads(windowed_path.read_text()) if windowed_path.exists() else None

checks = []
def add(name, ok, actual=None, expected=None, artifact=None):
    checks.append({"name": name, "ok": bool(ok), "actual": actual, "expected": expected, "artifact": artifact})

add("headless_probe_pass", headless.get("pass") is True, headless.get("pass"), True, str(headless_path))
add("actual_pixels_compared", bool(headless.get("readback")), list(headless.get("readback", {}).keys()), "readback pixel fields", str(headless_path))
for key in ["background_preserved", "triangle_changed_pixels", "indexed_geometry_verified", "compute_buffer_draw_verified", "indexed_texture_verified", "depth_verified", "render_changed_from_clear"]:
    add(key, headless.get("draw_checks", {}).get(key) is True, headless.get("draw_checks", {}).get(key), True, str(headless_path))
add("compute_draw_pixel", headless.get("readback", {}).get("compute_draw") == [136, 119, 102, 255], headless.get("readback", {}).get("compute_draw"), [136, 119, 102, 255], str(headless_path))
add("compute_uav_verified", headless.get("uav_checks", {}).get("compute_verified") is True, headless.get("uav_checks", {}).get("compute_verified"), True, str(headless_path))
add("readback_differs_from_clear", headless.get("readback", {}).get("checksum") != headless.get("readback", {}).get("clear_checksum"), {"checksum": headless.get("readback", {}).get("checksum"), "clear_checksum": headless.get("readback", {}).get("clear_checksum")}, "different", str(headless_path))
pixels_path = results / "image-diffs" / "readback-pixels.json"
pixels_path.write_text(json.dumps({
    "schema": "metalsharp.m12.visual-gauntlet.readback-pixels.v1",
    "source": str(headless_path),
    "readback": headless.get("readback", {}),
    "draw_checks": headless.get("draw_checks", {}),
    "uav_checks": headless.get("uav_checks", {}),
}, indent=2) + "\n")
raw_path = results / "image-diffs" / "render-headless-rgba.bin"
add("pixel_artifact", pixels_path.exists(), str(pixels_path), "exists", str(pixels_path))
add("raw_readback_artifact_optional", True, str(raw_path) if raw_path.exists() else "not emitted", "optional", str(raw_path))
if windowed is not None:
    windowed_pixels_path = results / "image-diffs" / "windowed-present-pixels.json"
    windowed_pixels_path.write_text(json.dumps({
        "schema": "metalsharp.m12.visual-gauntlet.windowed-present-pixels.v1",
        "source": str(windowed_path),
        "sampled_pixels": windowed.get("sampled_pixels", {}),
        "present_counts": windowed.get("present_counts", {}),
        "backbuffer_indices": windowed.get("backbuffer_indices", {}),
        "checks": windowed.get("checks", {}),
    }, indent=2) + "\n")
    sampled = windowed.get("sampled_pixels", {})
    add("windowed_present_probe_pass", windowed.get("pass") is True, windowed.get("pass"), True, str(windowed_path))
    add("windowed_buffer0_pixel", sampled.get("buffer0_center") == [255, 0, 0, 255], sampled.get("buffer0_center"), [255, 0, 0, 255], str(windowed_path))
    add("windowed_buffer1_pixel", sampled.get("buffer1_center") == [0, 255, 0, 255], sampled.get("buffer1_center"), [0, 255, 0, 255], str(windowed_path))
    add("windowed_resized_buffer_pixel", sampled.get("resized_buffer_center") == [0, 0, 255, 255], sampled.get("resized_buffer_center"), [0, 0, 255, 255], str(windowed_path))
    add("windowed_pixel_artifact", windowed_pixels_path.exists(), str(windowed_pixels_path), "exists", str(windowed_pixels_path))

ok = run_exit == 0 and all(item["ok"] for item in checks)
summary = {
    "schema": "metalsharp.m12.visual-gauntlet.summary.v1",
    "ok": ok,
    "run_probes_exit": run_exit,
    "results_dir": str(results),
    "preflight": preflight,
    "headless_probe": str(headless_path),
    "windowed_probe": str(windowed_path) if windowed is not None else None,
    "checks": checks,
    "residual_risks": [
        "This is a no-game deterministic visual probe baseline, not Subnautica 2 visual correctness.",
        "Compute writes are verified through UAV readback and a compute-buffer-then-draw pixel path; compute-to-texture remains future coverage.",
        "Windowed present has explicit pixel evidence when --windowed-present is used, but remains optional because it can interact with local desktop/window server state.",
    ],
}
(results / "visual-gauntlet-summary.json").write_text(json.dumps(summary, indent=2) + "\n")

lines = [
    "# M12 Phase 5 visual gauntlet",
    "",
    f"- ok: `{ok}`",
    f"- run_probes_exit: `{run_exit}`",
    f"- results_dir: `{results}`",
    f"- headless_probe: `{headless_path}`",
    "",
    "## Runtime hashes",
    "",
]
for name, value in preflight["runtime_hashes"].items():
    lines.append(f"- `{name}` `{value}`")
lines += ["", "## Checks", "", "| check | expected | actual | ok |", "|---|---|---|---:|"]
for item in checks:
    lines.append(f"| `{item['name']}` | `{item.get('expected')}` | `{item.get('actual')}` | `{item['ok']}` |")
lines += ["", "## Residual risks", ""]
for risk in summary["residual_risks"]:
    lines.append(f"- {risk}")
(results / "visual-gauntlet-summary.md").write_text("\n".join(lines) + "\n")
print(results / "visual-gauntlet-summary.md")
print(results / "visual-gauntlet-summary.json")
PY

SUMMARY_JSON="$RESULTS_DIR/visual-gauntlet-summary.json"
OK="$(python3 - <<'PY' "$SUMMARY_JSON"
import json,sys
print('1' if json.load(open(sys.argv[1])).get('ok') else '0')
PY
)"

if [[ "$ALLOW_FAILURES" == "1" || "$OK" == "1" ]]; then
  exit 0
fi
exit 1
