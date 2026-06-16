#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SDK_DIR="$ROOT_DIR/tools/d3d12-metal-sdk"
SDK_FETCH="$SDK_DIR/scripts/fetch-agility.sh"
USER_REDIST="${METALSHARP_AGILITY_REDIST_DIR:-$HOME/.metalsharp/runtime/redist/agility}"
RESULTS_DIR="${M12_AGILITY_MATRIX_RESULTS_DIR:-$SDK_DIR/results/m12-reference-payloads/agility-$(date +%Y%m%d-%H%M%S)}"
VERSIONS=()
COPY_TO_USER=1

usage() {
  cat <<'USAGE'
Usage:
  fetch-agility-matrix.sh [--version VERSION ...] [--all-retail] [--all-preview] [--no-user-copy]

Downloads Microsoft.Direct3D.D3D12 Agility SDK NuGet payloads with the existing
fetch-agility.sh helper, validates x64 D3D12Core.dll + d3d12SDKLayers.dll, and
optionally mirrors them into ~/.metalsharp/runtime/redist/agility/<version>/ so
MetalSharp backend staging can find them.

Default: fetch minimum currently needed version 1.611.2.
USAGE
}

retail_versions=(1.4.10 1.600.10 1.602.4 1.606.4 1.608.3 1.610.4 1.611.2 1.613.3 1.614.1 1.615.1 1.616.1 1.618.5 1.619.3)
preview_versions=(1.700.10-preview 1.706.4-preview 1.710.0-preview 1.711.3-preview 1.714.0-preview 1.715.0-preview 1.716.1-preview 1.717.1-preview 1.719.1-preview 1.720.0-preview 1.721.0-preview)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSIONS+=("$2"); shift 2 ;;
    --all-retail) VERSIONS+=("${retail_versions[@]}"); shift ;;
    --all-preview) VERSIONS+=("${preview_versions[@]}"); shift ;;
    --no-user-copy) COPY_TO_USER=0; shift ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ${#VERSIONS[@]} -eq 0 ]]; then
  VERSIONS=(1.611.2)
fi

mkdir -p "$RESULTS_DIR"
MANIFEST="$RESULTS_DIR/reference-payloads.jsonl"
: > "$MANIFEST"

for version in "${VERSIONS[@]}"; do
  echo "== Agility $version ==" >&2
  status="ok"
  error=""
  bin_dir=""
  if ! bin_dir="$($SDK_FETCH --version "$version" 2>"$RESULTS_DIR/fetch-$version.stderr")"; then
    status="fetch_failed"
    error="$(cat "$RESULTS_DIR/fetch-$version.stderr" 2>/dev/null | tail -20 | tr '\n' ' ')"
  fi
  if [[ "$status" == "ok" ]]; then
    if [[ ! -f "$bin_dir/D3D12Core.dll" || ! -f "$bin_dir/d3d12SDKLayers.dll" ]]; then
      status="missing_payload"
      error="missing D3D12Core.dll or d3d12SDKLayers.dll in $bin_dir"
    fi
  fi
  user_dir=""
  if [[ "$status" == "ok" && "$COPY_TO_USER" == "1" ]]; then
    user_dir="$USER_REDIST/$version"
    mkdir -p "$user_dir"
    rsync -a "$(dirname "$(dirname "$(dirname "$(dirname "$bin_dir")")")")/" "$user_dir/"
  fi
  python3 - <<PY >> "$MANIFEST"
import hashlib, json
from pathlib import Path
version = "$version"
bin_dir = Path("$bin_dir") if "$bin_dir" else None
user_dir = Path("$user_dir") if "$user_dir" else None
status = "$status"
error = "$error"
def sha(p):
    if not p or not p.exists(): return None
    h=hashlib.sha256(); h.update(p.read_bytes()); return h.hexdigest()
row = {
  'schema': 'metalsharp.m12.reference-payload.v1',
  'kind': 'agility',
  'version': version,
  'status': status,
  'error': error,
  'sdk_bin': str(bin_dir) if bin_dir else None,
  'user_redist': str(user_dir) if user_dir else None,
  'D3D12Core_sha256': sha(bin_dir/'D3D12Core.dll') if bin_dir else None,
  'd3d12SDKLayers_sha256': sha(bin_dir/'d3d12SDKLayers.dll') if bin_dir else None,
}
print(json.dumps(row))
PY
done

python3 - <<'PY' "$MANIFEST" "$RESULTS_DIR/reference-payloads.json" "$RESULTS_DIR/reference-payloads.md"
import json, sys
from pathlib import Path
rows=[json.loads(l) for l in Path(sys.argv[1]).read_text().splitlines() if l.strip()]
Path(sys.argv[2]).write_text(json.dumps(rows, indent=2)+'\n')
md=['# M12 reference payloads: Agility matrix','', '| version | status | sdk_bin | user_redist | D3D12Core |','|---|---|---|---|---|']
for r in rows:
    md.append(f"| `{r['version']}` | `{r['status']}` | `{r['sdk_bin']}` | `{r['user_redist']}` | `{r['D3D12Core_sha256']}` |")
Path(sys.argv[3]).write_text('\n'.join(md)+'\n')
if not all(r['status']=='ok' for r in rows):
    raise SystemExit(1)
PY

echo "$RESULTS_DIR/reference-payloads.md"
echo "$RESULTS_DIR/reference-payloads.json"
