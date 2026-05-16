#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE="${METALSHARP_DEB_BUILDER_IMAGE:-metalsharp-deb-builder}"
PLATFORM="${METALSHARP_DEB_PLATFORM:-linux/amd64}"

docker build --platform "$PLATFORM" -f "$SCRIPT_DIR/Dockerfile.deb" -t "$IMAGE" "$PROJECT_ROOT"

docker run --rm \
  --platform "$PLATFORM" \
  -e HOME=/tmp \
  -e METALSHARP_TARGET=linux \
  -v "$PROJECT_ROOT:/work" \
  -v metalsharp-deb-node-modules:/work/app/node_modules \
  -v metalsharp-deb-cargo-registry:/usr/local/cargo/registry \
  -v metalsharp-deb-cargo-git:/usr/local/cargo/git \
  -w /work/app \
  "$IMAGE" \
  bash -lc 'source /usr/local/cargo/env && npm ci && npm run deb'
