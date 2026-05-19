#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

VERSION="${METALSHARP_VERSION:?METALSHARP_VERSION is required}"
TAGS="${METALSHARP_PACKAGE_TAGS:-$VERSION latest}"
OWNER="$(printf '%s' "${GITHUB_REPOSITORY_OWNER:?GITHUB_REPOSITORY_OWNER is required}" | tr '[:upper:]' '[:lower:]')"
SOURCE="${METALSHARP_SOURCE_URL:-https://github.com/${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}}"
REVISION="${GITHUB_SHA:-unknown}"
RUNTIME_IMAGE="${METALSHARP_RUNTIME_IMAGE:-ghcr.io/${OWNER}/metalsharp-linux-runtime}"
DEB_IMAGE="${METALSHARP_DEB_IMAGE:-ghcr.io/${OWNER}/metalsharp-linux-deb}"

DEB_PATH="${METALSHARP_DEB_PATH:-$PROJECT_ROOT/dist/electron/metalsharp_${VERSION}_amd64.deb}"
LATEST_LINUX_PATH="${METALSHARP_LATEST_LINUX_PATH:-$PROJECT_ROOT/dist/electron/latest-linux.yml}"
RUNTIME_TARBALL="${METALSHARP_RUNTIME_TARBALL:-$PROJECT_ROOT/dist/packages/metalsharp_linux_runtime.tar.zst}"
RUNTIME_TARBALL_2="${METALSHARP_RUNTIME_TARBALL_2:-$PROJECT_ROOT/dist/packages/metalsharp_linux_runtime2.tar.zst}"
RUNTIME_SHA="${METALSHARP_RUNTIME_SHA:-$PROJECT_ROOT/dist/packages/metalsharp_linux_runtime.sha256}"

for required in "$DEB_PATH" "$LATEST_LINUX_PATH" "$RUNTIME_TARBALL" "$RUNTIME_TARBALL_2" "$RUNTIME_SHA"; do
  test -s "$required"
done

relpath() {
  local path="$1"
  case "$path" in
    "$PROJECT_ROOT"/*) printf '%s\n' "${path#"$PROJECT_ROOT"/}" ;;
    *) printf '%s\n' "$path" ;;
  esac
}

DEB_REF="$(relpath "$DEB_PATH")"
LATEST_LINUX_REF="$(relpath "$LATEST_LINUX_PATH")"
RUNTIME_TARBALL_REF="$(relpath "$RUNTIME_TARBALL")"
RUNTIME_TARBALL_2_REF="$(relpath "$RUNTIME_TARBALL_2")"
RUNTIME_SHA_REF="$(relpath "$RUNTIME_SHA")"

if [ "${METALSHARP_OCI_DRY_RUN:-0}" = "1" ]; then
  printf '%s\n' \
    "$DEB_REF" \
    "$LATEST_LINUX_REF" \
    "$RUNTIME_TARBALL_REF" \
    "$RUNTIME_TARBALL_2_REF" \
    "$RUNTIME_SHA_REF"
  exit 0
fi

if [ -n "${GH_TOKEN:-}" ]; then
  echo "$GH_TOKEN" | oras login ghcr.io -u "${GITHUB_ACTOR:-metalsharp-ci}" --password-stdin
fi

cd "$PROJECT_ROOT"

for tag in $TAGS; do
  oras push "${RUNTIME_IMAGE}:${tag}" \
    --artifact-type application/vnd.metalsharp.linux-runtime.v1 \
    --annotation "org.opencontainers.image.source=${SOURCE}" \
    --annotation "org.opencontainers.image.description=MetalSharp Linux runtime tarballs" \
    --annotation "org.opencontainers.image.revision=${REVISION}" \
    "$RUNTIME_TARBALL_REF:application/vnd.metalsharp.runtime.tar+zstd" \
    "$RUNTIME_TARBALL_2_REF:application/vnd.metalsharp.runtime.tar+zstd" \
    "$RUNTIME_SHA_REF:text/plain"

  oras push "${DEB_IMAGE}:${tag}" \
    --artifact-type application/vnd.metalsharp.linux-deb.v1 \
    --annotation "org.opencontainers.image.source=${SOURCE}" \
    --annotation "org.opencontainers.image.description=MetalSharp Linux debian release installer" \
    --annotation "org.opencontainers.image.revision=${REVISION}" \
    "$DEB_REF:application/vnd.debian.binary-package" \
    "$LATEST_LINUX_REF:application/x-yaml"
done
