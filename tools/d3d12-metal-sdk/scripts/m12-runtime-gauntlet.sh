#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
PROFILE="m12-phase4"
RESULTS_DIR="$SDK_DIR/results/m12-runtime-gauntlet/$TIMESTAMP"
WINE_BIN="${WINE_BIN:-$HOME/.metalsharp/runtime/wine/bin/metalsharp-wine}"
WINE_PREFIX="${WINEPREFIX:-$HOME/.metalsharp/prefix-steam}"
DXMT_RUNTIME="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12"
PROBE_SET="phase4-core"
ALLOW_FAILURES=0

EXPECT_D3D12_SHA="2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c"
EXPECT_DXGI_SHA="dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24"
EXPECT_DXGI_DXMT_SHA="659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d"
EXPECT_WINEMETAL_DLL_SHA="7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85"
EXPECT_WINEMETAL_SO_SHA="167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58"

usage() {
  cat <<'USAGE'
Usage:
  m12-runtime-gauntlet.sh [options]

Runs the Phase 4 no-game D3D12/DXGI probe gauntlet against an M12 runtime.
This script does not launch games and does not stage runtime DLLs.

Options:
  --results-dir PATH              Output directory.
  --wine PATH                     Wine/MetalSharp Wine binary.
  --prefix PATH                   WINEPREFIX.
  --dxmt-runtime PATH             Runtime root with x86_64-windows and x86_64-unix.
  --probe-set phase4-core|phase4-pso|all
                                  Default: phase4-core.
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
    --probe-set) PROBE_SET="$2"; shift 2 ;;
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

case "$PROBE_SET" in
  phase4-core|phase4-pso|all) ;;
  *) echo "Unknown --probe-set: $PROBE_SET" >&2; exit 2 ;;
esac

if [[ ! -x "$WINE_BIN" ]]; then
  echo "Wine binary is not executable: $WINE_BIN" >&2
  exit 2
fi
if [[ ! -d "$DXMT_RUNTIME/x86_64-windows" || ! -d "$DXMT_RUNTIME/x86_64-unix" ]]; then
  echo "Invalid M12 runtime root: $DXMT_RUNTIME" >&2
  exit 2
fi

mkdir -p "$RESULTS_DIR"

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

WINDOWS_DIR="$DXMT_RUNTIME/x86_64-windows"
UNIX_DIR="$DXMT_RUNTIME/x86_64-unix"
WINE_UNIX_DIR="$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix"

check_hash d3d12.dll "$WINDOWS_DIR/d3d12.dll" "$EXPECT_D3D12_SHA"
check_hash dxgi.dll "$WINDOWS_DIR/dxgi.dll" "$EXPECT_DXGI_SHA"
check_hash dxgi_dxmt.dll "$WINDOWS_DIR/dxgi_dxmt.dll" "$EXPECT_DXGI_DXMT_SHA"
check_hash winemetal.dll "$WINDOWS_DIR/winemetal.dll" "$EXPECT_WINEMETAL_DLL_SHA"
check_hash dxmt_m12/winemetal.so "$UNIX_DIR/winemetal.so" "$EXPECT_WINEMETAL_SO_SHA"
check_hash wine/winemetal.so "$WINE_UNIX_DIR/winemetal.so" "$EXPECT_WINEMETAL_SO_SHA"

cat > "$RESULTS_DIR/runtime-gauntlet-preflight.json" <<EOF_JSON
{
  "schema": "metalsharp.m12.runtime-gauntlet.preflight.v1",
  "probe_set": "$PROBE_SET",
  "wine": "$WINE_BIN",
  "prefix": "$WINE_PREFIX",
  "dxmt_runtime": "$DXMT_RUNTIME",
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

RUN_ARGS=(
  --profile metalsharp
  --wine "$WINE_BIN"
  --prefix "$WINE_PREFIX"
  --dxmt-runtime "$DXMT_RUNTIME"
  --results-dir "$RESULTS_DIR"
  --no-shaders
  --no-dxil-semantics
  --no-shader-corpus
  --no-sm66-capabilities
  --no-wave-ops
  --no-reflection-abi
  --no-render-headless
  --no-windowed-present
  --no-mini
)

if [[ "$PROBE_SET" == "phase4-core" ]]; then
  RUN_ARGS+=(--no-graphics-pso --no-compute-pso)
elif [[ "$PROBE_SET" == "phase4-pso" ]]; then
  :
elif [[ "$PROBE_SET" == "all" ]]; then
  RUN_ARGS=(
    --profile metalsharp
    --wine "$WINE_BIN"
    --prefix "$WINE_PREFIX"
    --dxmt-runtime "$DXMT_RUNTIME"
    --results-dir "$RESULTS_DIR"
    --render-headless
    --windowed-present
  )
fi

set +e
"$SDK_DIR/scripts/run-probes.sh" "${RUN_ARGS[@]}" \
  > "$RESULTS_DIR/run-probes.stdout" \
  2> "$RESULTS_DIR/run-probes.stderr"
RUN_PROBES_EXIT=$?
set -e

python3 - "$RESULTS_DIR" "$PROBE_SET" "$RUN_PROBES_EXIT" <<'PY'
from __future__ import annotations
import json
import sys
from pathlib import Path

results = Path(sys.argv[1])
probe_set = sys.argv[2]
run_exit = int(sys.argv[3])
preflight = json.loads((results / "runtime-gauntlet-preflight.json").read_text())

probe_files = sorted(p for p in results.glob("*.json") if p.name != "runtime-gauntlet-preflight.json")
entries = []
failures = []
for path in probe_files:
    entry = {"file": path.name, "path": str(path), "json_valid": True, "ok": None, "category": "probe"}
    try:
        data = json.loads(path.read_text())
    except Exception as exc:
        entry.update({"json_valid": False, "error": str(exc), "ok": False})
        failures.append(entry)
        entries.append(entry)
        continue
    if "schema" in data and "winemetal" in str(data.get("schema", "")).lower():
        entry["category"] = "winemetal_abi"
    elif path.name.startswith("host-runtime"):
        entry["category"] = "host_runtime"
    if isinstance(data, dict):
        if "ok" in data:
            entry["ok"] = bool(data["ok"])
        elif "passed" in data:
            entry["ok"] = bool(data["passed"])
        elif "pass" in data:
            entry["ok"] = bool(data["pass"])
        elif "failure_count" in data:
            entry["ok"] = int(data.get("failure_count") or 0) == 0
        elif "failures" in data and isinstance(data["failures"], list):
            entry["ok"] = len(data["failures"]) == 0
        elif "errors" in data and isinstance(data["errors"], list):
            entry["ok"] = len(data["errors"]) == 0
        else:
            entry["ok"] = None
        for key in ("failure_count", "error_count", "warning_count", "unsupported_count"):
            if key in data:
                entry[key] = data[key]
    if entry["ok"] is False:
        failures.append(entry)
    entries.append(entry)

summary = {
    "schema": "metalsharp.m12.runtime-gauntlet.summary.v1",
    "ok": run_exit == 0 and not failures,
    "probe_set": probe_set,
    "run_probes_exit": run_exit,
    "results_dir": str(results),
    "runtime": preflight,
    "probe_json_count": len(entries),
    "failure_count": len(failures),
    "probes": entries,
    "failures": failures,
}
(results / "runtime-gauntlet-summary.json").write_text(json.dumps(summary, indent=2) + "\n")

lines = [
    f"# M12 Phase 4 runtime gauntlet: {probe_set}",
    "",
    f"- ok: `{summary['ok']}`",
    f"- run_probes_exit: `{run_exit}`",
    f"- probe_json_count: `{len(entries)}`",
    f"- failure_count: `{len(failures)}`",
    f"- results_dir: `{results}`",
    "",
    "## Runtime hashes",
    "",
]
for name, value in preflight["runtime_hashes"].items():
    lines.append(f"- `{name}` `{value}`")
lines += ["", "## Probe results", "", "| file | category | ok |", "|---|---|---:|"]
for entry in entries:
    lines.append(f"| `{entry['file']}` | `{entry['category']}` | `{entry['ok']}` |")
lines.append("")
if failures:
    lines += ["## Failures", ""]
    for failure in failures:
        lines.append(f"- `{failure['file']}` category=`{failure['category']}` ok=`{failure['ok']}`")
else:
    lines += ["## Failures", "", "None."]
lines.append("")
lines += [
    "## Notes",
    "",
    "This gauntlet does not launch games and does not stage runtime DLLs.",
    "It validates the selected M12 runtime in place using full runtime hash gates before probes run.",
]
(results / "runtime-gauntlet-summary.md").write_text("\n".join(lines) + "\n")

fail_lines = ["# M12 Phase 4 probe failures", ""]
if failures:
    for failure in failures:
        fail_lines.append(f"- `{failure['file']}` category=`{failure['category']}` path=`{failure['path']}`")
else:
    fail_lines.append("None.")
(results / "probe-failures.md").write_text("\n".join(fail_lines) + "\n")

print(results / "runtime-gauntlet-summary.md")
print(results / "runtime-gauntlet-summary.json")
print(results / "probe-failures.md")
PY

SUMMARY_JSON="$RESULTS_DIR/runtime-gauntlet-summary.json"
OK="$(python3 - <<'PY' "$SUMMARY_JSON"
import json,sys
print('1' if json.load(open(sys.argv[1])).get('ok') else '0')
PY
)"

if [[ "$ALLOW_FAILURES" == "1" || "$OK" == "1" ]]; then
  exit 0
fi
exit 1
