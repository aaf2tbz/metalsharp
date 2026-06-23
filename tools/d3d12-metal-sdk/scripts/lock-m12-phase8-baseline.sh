#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
STAMP="$(date +%Y%m%d-%H%M%S)"
PROFILE="${M12_PHASE8_PROFILE:-metalsharp}"
BUILD_DIR="${M12_PHASE8_BUILD_DIR:-$ROOT_DIR/vendor/dxmt/build-metalsharp-x64}"
RUNTIME_DIR="${M12_PHASE8_RUNTIME_DIR:-$HOME/.metalsharp/runtime/wine/lib/dxmt_m12}"
RESULTS_DIR="${M12_PHASE8_RESULTS_DIR:-$SDK_DIR/results/m12-phase8-baseline-lock-$STAMP}"
MSCOMPATDB_PATH="${M12_PHASE8_MSCOMPATDB_PATH:-$HOME/.metalsharp/mscompatdb}"

usage() {
  cat <<'USAGE'
Usage:
  lock-m12-phase8-baseline.sh [options]

Offline-only Phase A guard for the AC6/M12 known-good baseline.
This script does not launch Steam, Wine games, or AC6.

Options:
  --profile NAME       Profile label. Default: metalsharp.
  --build-dir PATH     DXMT build dir to compare against staged runtime.
  --runtime-dir PATH   M12 staged runtime root. Default: ~/.metalsharp/runtime/wine/lib/dxmt_m12.
  --results-dir PATH   Evidence output directory. Default: results/m12-phase8-baseline-lock-<stamp>.
  -h, --help           Show this help.

Hard gates:
  1. mscompatdb must be absent.
  2. Full atomic M12 DLL set must exist in the build and staged runtime.
  3. Build and staged hashes must match for:
     d3d12.dll, d3d11.dll, d3d10core.dll, dxgi.dll, dxgi_dxmt.dll,
     winemetal.dll, winemetal.so.
  4. winemetal.so must contain internal m12core symbols and must not require
     a libm12core.dylib sidecar.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      PROFILE="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --runtime-dir)
      RUNTIME_DIR="$2"
      shift 2
      ;;
    --results-dir)
      RESULTS_DIR="$2"
      shift 2
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

mkdir -p "$RESULTS_DIR"

SUMMARY_MD="$RESULTS_DIR/phase8-baseline-lock-summary.md"
SUMMARY_JSON="$RESULTS_DIR/phase8-baseline-lock.json"
PREFLIGHT_JSON="$RESULTS_DIR/runtime-preflight-$PROFILE.json"
ABI_JSON="$RESULTS_DIR/winemetal-abi-$PROFILE.json"

{
  echo "# M12 Phase 8 baseline lock"
  echo
  echo "- timestamp: \`$STAMP\`"
  echo "- profile: \`$PROFILE\`"
  echo "- root: \`$ROOT_DIR\`"
  echo "- build_dir: \`$BUILD_DIR\`"
  echo "- runtime_dir: \`$RUNTIME_DIR\`"
  echo "- mscompatdb_path: \`$MSCOMPATDB_PATH\`"
  echo "- live_launch: \`false\`"
  echo
} > "$SUMMARY_MD"

if [[ -e "$MSCOMPATDB_PATH" ]]; then
  echo "Phase 8 baseline lock failed: mscompatdb exists at $MSCOMPATDB_PATH" >&2
  {
    echo "## Result"
    echo
    echo "FAIL — mscompatdb exists. Remove/quarantine it before any AC6/M12 evidence run."
  } >> "$SUMMARY_MD"
  exit 1
fi

python3 "$SDK_DIR/scripts/preflight-runtime-layout.py" \
  --profile "$PROFILE" \
  --dxmt-runtime "$RUNTIME_DIR" \
  --build-dir "$BUILD_DIR" \
  --results-dir "$RESULTS_DIR"

python3 - "$PREFLIGHT_JSON" "$ABI_JSON" "$SUMMARY_JSON" "$SUMMARY_MD" <<'PY'
from __future__ import annotations

import json
import sys
from pathlib import Path

preflight_path = Path(sys.argv[1])
abi_path = Path(sys.argv[2])
summary_json_path = Path(sys.argv[3])
summary_md_path = Path(sys.argv[4])

preflight = json.loads(preflight_path.read_text())
abi = json.loads(abi_path.read_text()) if abi_path.exists() else None
atomic = [
    "d3d12.dll",
    "d3d11.dll",
    "d3d10core.dll",
    "dxgi.dll",
    "dxgi_dxmt.dll",
    "winemetal.dll",
    "winemetal.so",
]
by_name = {}
for item in preflight.get("build_comparisons", []):
    role = item.get("role", "")
    name = role.removeprefix("build_match_")
    by_name[name] = item
missing = [name for name in atomic if name not in by_name]
failed = [name for name in atomic if name in by_name and not by_name[name].get("ok")]
ok = bool(preflight.get("ok")) and not missing and not failed
report = {
    "schema": "metalsharp.m12.phase8-baseline-lock.v1",
    "ok": ok,
    "preflight_ok": preflight.get("ok"),
    "failure_count": preflight.get("failure_count"),
    "winemetal_abi_ok": bool(abi.get("ok")) if isinstance(abi, dict) else preflight.get("winemetal_abi", {}).get("ok"),
    "atomic_runtime_set": atomic,
    "missing_build_comparisons": missing,
    "failed_build_comparisons": failed,
    "build_comparisons": [by_name[name] for name in atomic if name in by_name],
    "safe_phase8_launch_shape": {
        "route": "POST /steam/launch-game",
        "port_env": "METALSHARP_PORT=9277",
        "binary_archive": "enabled",
        "binary_archive_lookup": "bypassed",
        "binary_archive_population": "omitted/off",
        "logging_tracing": "none",
    },
    "notes": [
        "This is an offline Phase A guard; it does not launch AC6.",
        "Future AC6 Continue evidence is invalid if this guard is red.",
        "Do not enable binary archive population unless explicitly testing a redesigned/proven population path.",
    ],
}
summary_json_path.write_text(json.dumps(report, indent=2) + "\n")

lines = [
    "## Result",
    "",
    f"- ok: `{ok}`",
    f"- preflight_ok: `{preflight.get('ok')}`",
    f"- failure_count: `{preflight.get('failure_count')}`",
    f"- winemetal_abi_ok: `{report['winemetal_abi_ok']}`",
    "- mscompatdb_absent: `true`",
    "",
    "## Atomic runtime parity",
    "",
    "| artifact | ok | build sha256 | staged sha256 |",
    "|---|---:|---|---|",
]
for name in atomic:
    item = by_name.get(name)
    if item is None:
        lines.append(f"| `{name}` | `false` | missing comparison | missing comparison |")
    else:
        lines.append(
            f"| `{name}` | `{item.get('ok')}` | `{item.get('build_sha256')}` | `{item.get('staged_sha256')}` |"
        )
lines += [
    "",
    "## Safe Phase 8 launch shape",
    "",
    "```text",
    "POST /steam/launch-game",
    "METALSHARP_PORT=9277",
    "METALSHARP_M12_BINARY_ARCHIVE=1",
    "METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP=1",
    "# omit METALSHARP_M12_BINARY_ARCHIVE_POPULATE",
    "METALSHARP_M12_LOG_LEVEL=none",
    "METALSHARP_M12_LOG_PATH=none",
    "METALSHARP_M12_TRACE_CAPTURE=0",
    "```",
    "",
]
with summary_md_path.open("a") as fh:
    fh.write("\n".join(lines) + "\n")
if not ok:
    raise SystemExit(1)
PY

cat "$SUMMARY_MD"
echo
echo "Phase 8 baseline lock evidence: $RESULTS_DIR"
