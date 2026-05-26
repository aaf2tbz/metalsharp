#!/usr/bin/env bash
set -euo pipefail

SDK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTERNAL_DIR="${D3D12_METAL_SDK_EXTERNAL_DIR:-"$SDK_ROOT/external"}"

mkdir -p "$EXTERNAL_DIR"

clone_or_update() {
  local name="$1"
  local url="$2"
  local dir="$EXTERNAL_DIR/$name"

  if [[ -d "$dir/.git" ]]; then
    echo "refreshing $name"
    git -C "$dir" fetch --depth 1 origin
    git -C "$dir" checkout -q FETCH_HEAD
    return
  fi

  if [[ -e "$dir" ]]; then
    echo "error: $dir exists but is not a git checkout" >&2
    return 1
  fi

  echo "cloning $name"
  git clone --depth 1 "$url" "$dir"
}

clone_or_update "smoldr" "https://github.com/GPUOpen-Tools/smoldr.git"
clone_or_update "gfxreconstruct" "https://github.com/LunarG/gfxreconstruct.git"
clone_or_update "renderdoc" "https://github.com/baldurk/renderdoc.git"
clone_or_update "DirectX-Headers" "https://github.com/microsoft/DirectX-Headers.git"
clone_or_update "DirectXTK12" "https://github.com/microsoft/DirectXTK12.git"

cat <<EOF

External D3D12 tooling is ready under:
  $EXTERNAL_DIR

These checkouts are ignored by git. Keep committed MetalSharp integration in:
  $SDK_ROOT
EOF
