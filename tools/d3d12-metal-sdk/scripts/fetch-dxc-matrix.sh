#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
RESULTS_DIR="${M12_DXC_MATRIX_RESULTS_DIR:-$SDK_DIR/results/m12-reference-payloads/dxc-$(date +%Y%m%d-%H%M%S)}"
VERSIONS=("v1.9.2602:dxc_2026_02_20.zip:a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e")

usage() {
  cat <<'USAGE'
Usage:
  fetch-dxc-matrix.sh [--results-dir PATH]

Fetches/validates the known-good DXC package matrix used by the M12 SDK probes.
Currently this wraps the pinned repository DXC payload. Add new versions here
only with explicit archive name and SHA256.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

mkdir -p "$RESULTS_DIR"
MANIFEST="$RESULTS_DIR/reference-payloads.jsonl"
: > "$MANIFEST"

for entry in "${VERSIONS[@]}"; do
  IFS=: read -r version archive sha <<< "$entry"
  echo "== DXC $version ==" >&2
  status="ok"; error=""; bin_dir=""
  if ! bin_dir="$(DXC_VERSION="$version" DXC_ARCHIVE="$archive" DXC_SHA256="$sha" "$SDK_DIR/scripts/fetch-dxc.sh" 2>"$RESULTS_DIR/fetch-$version.stderr")"; then
    status="fetch_failed"
    error="$(cat "$RESULTS_DIR/fetch-$version.stderr" 2>/dev/null | tail -20 | tr '\n' ' ')"
  fi
  python3 - <<PY >> "$MANIFEST"
import hashlib, json
from pathlib import Path
version="$version"; archive="$archive"; expected_sha="$sha"; status="$status"; error="$error"; bin_dir=Path("$bin_dir") if "$bin_dir" else None
def h(p):
    if not p or not p.exists(): return None
    x=hashlib.sha256(); x.update(p.read_bytes()); return x.hexdigest()
row={'schema':'metalsharp.m12.reference-payload.v1','kind':'dxc','version':version,'archive':archive,'archive_expected_sha256':expected_sha,'status':status,'error':error,'bin_dir':str(bin_dir) if bin_dir else None,'dxc_sha256':h(bin_dir/'dxc.exe') if bin_dir else None,'dxcompiler_sha256':h(bin_dir/'dxcompiler.dll') if bin_dir else None,'dxil_sha256':h(bin_dir/'dxil.dll') if bin_dir else None}
print(json.dumps(row))
PY
done

python3 - <<'PY' "$MANIFEST" "$RESULTS_DIR/reference-payloads.json" "$RESULTS_DIR/reference-payloads.md"
import json, sys
from pathlib import Path
rows=[json.loads(l) for l in Path(sys.argv[1]).read_text().splitlines() if l.strip()]
Path(sys.argv[2]).write_text(json.dumps(rows, indent=2)+'\n')
md=['# M12 reference payloads: DXC matrix','', '| version | status | bin_dir | dxc | dxcompiler | dxil |','|---|---|---|---|---|---|']
for r in rows:
    md.append(f"| `{r['version']}` | `{r['status']}` | `{r['bin_dir']}` | `{r['dxc_sha256']}` | `{r['dxcompiler_sha256']}` | `{r['dxil_sha256']}` |")
Path(sys.argv[3]).write_text('\n'.join(md)+'\n')
if not all(r['status']=='ok' for r in rows): raise SystemExit(1)
PY

echo "$RESULTS_DIR/reference-payloads.md"
echo "$RESULTS_DIR/reference-payloads.json"
