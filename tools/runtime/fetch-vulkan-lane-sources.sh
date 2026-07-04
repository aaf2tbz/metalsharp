#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEST_ROOT="${METALSHARP_RUNTIME_SOURCE_DIR:-$ROOT/.cache/runtime-sources}"
MANIFEST="$DEST_ROOT/vulkan-lane-sources.json"
DXVK_TAG="${DXVK_TAG:-v3.0}"
VKD3D_PROTON_TAG="${VKD3D_PROTON_TAG:-v3.0.1}"
MOLTENVK_TAG="${MOLTENVK_TAG:-v1.4.1}"
REFRESH=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--refresh]

Fetch pinned source checkouts for the planned Vulkan-family runtime lanes.

Defaults:
  DXVK_TAG=${DXVK_TAG}
  VKD3D_PROTON_TAG=${VKD3D_PROTON_TAG}
  MOLTENVK_TAG=${MOLTENVK_TAG}
  METALSHARP_RUNTIME_SOURCE_DIR=${DEST_ROOT}

The script performs shallow source clones with submodules where needed and
writes a manifest with exact commits. It does not build, install, launch Wine,
or mutate ~/.metalsharp.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --refresh)
      REFRESH=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

mkdir -p "$DEST_ROOT"

clone_source() {
  local name="$1"
  local repo="$2"
  local tag="$3"
  local dir="$DEST_ROOT/${name}-${tag}"

  if [[ "$REFRESH" == "1" ]]; then
    rm -rf "$dir"
  fi

  if [[ ! -d "$dir/.git" ]]; then
    echo "fetching $name $tag..." >&2
    git clone \
      --depth 1 \
      --branch "$tag" \
      --recurse-submodules \
      --shallow-submodules \
      "$repo" \
      "$dir" >&2
  else
    echo "using existing $name $tag checkout..." >&2
    git -C "$dir" fetch --depth 1 origin "refs/tags/$tag:refs/tags/$tag" >/dev/null 2>&1 || true
    git -C "$dir" checkout --detach "$tag" >/dev/null
    git -C "$dir" submodule update --init --recursive --depth 1 >&2
  fi

  local commit
  commit="$(git -C "$dir" rev-parse HEAD)"
  local submodules
  submodules="$(git -C "$dir" submodule status --recursive 2>/dev/null | wc -l | tr -d ' ')"
  printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$repo" "$tag" "$commit" "$submodules"
}

TMP_MANIFEST="$MANIFEST.tmp"
{
  echo "{"
  echo "  \"schema\": \"metalsharp.vulkan-lane-sources.v1\","
  echo "  \"generatedAtUnix\": $(date +%s),"
  echo "  \"sourceRoot\": \"$DEST_ROOT\","
  echo "  \"sources\": ["
} > "$TMP_MANIFEST"

rows=()
while IFS=$'\t' read -r name repo tag commit submodules; do
  rows+=("$name|$repo|$tag|$commit|$submodules")
done < <(
  clone_source "dxvk" "https://github.com/doitsujin/dxvk.git" "$DXVK_TAG"
  clone_source "vkd3d-proton" "https://github.com/HansKristian-Work/vkd3d-proton.git" "$VKD3D_PROTON_TAG"
  clone_source "MoltenVK" "https://github.com/KhronosGroup/MoltenVK.git" "$MOLTENVK_TAG"
)

for index in "${!rows[@]}"; do
  IFS='|' read -r name repo tag commit submodules <<< "${rows[$index]}"
  comma=","; [[ "$index" == "$((${#rows[@]} - 1))" ]] && comma=""
  cat >> "$TMP_MANIFEST" <<JSON
    {
      "name": "$name",
      "repo": "$repo",
      "tag": "$tag",
      "commit": "$commit",
      "path": "$DEST_ROOT/${name}-${tag}",
      "submoduleCount": $submodules
    }$comma
JSON
done

{
  echo "  ]"
  echo "}"
} >> "$TMP_MANIFEST"

mv "$TMP_MANIFEST" "$MANIFEST"
echo "wrote $MANIFEST"
