#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DXMT_DIR="${ROOT_DIR}/vendor/dxmt"
BUILD_DIR="${DXMT_DIR}/build-metalsharp-x64"
TOOLCHAIN_ROOT="${METALSHARP_X86_LLVM_ROOT:-/Volumes/AverySSD/toolchains}"
LLVM_NAME="clang+llvm-15.0.7-x86_64-apple-darwin21.0"
LLVM_DIR="${TOOLCHAIN_ROOT}/${LLVM_NAME}"
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-15.0.7/${LLVM_NAME}.tar.xz"

mkdir -p "${TOOLCHAIN_ROOT}"

if [[ ! -x "${LLVM_DIR}/bin/llvm-config" ]]; then
  archive="${TOOLCHAIN_ROOT}/${LLVM_NAME}.tar.xz"
  if [[ ! -f "${archive}" ]]; then
    curl -L --fail --progress-bar -o "${archive}" "${LLVM_URL}"
  fi
  tar -C "${TOOLCHAIN_ROOT}" -xf "${archive}"
fi

if ! file "${LLVM_DIR}/bin/llvm-config" | grep -q 'x86_64'; then
  echo "error: ${LLVM_DIR}/bin/llvm-config is not x86_64" >&2
  exit 1
fi

meson setup "${BUILD_DIR}" "${DXMT_DIR}" --reconfigure \
  -Dnative_llvm_path="${LLVM_DIR}"

ninja -C "${BUILD_DIR}" \
  src/dxgi/dxgi.dll \
  src/dxgi/dxgi_dxmt.dll \
  src/winemetal/unix/winemetal.so \
  src/winemetal/winemetal.dll \
  src/d3d12/d3d12.dll

echo "x86_64 LLVM ready: ${LLVM_DIR}"
echo "rebuilt: ${BUILD_DIR}/src/dxgi/dxgi.dll"
echo "rebuilt: ${BUILD_DIR}/src/dxgi/dxgi_dxmt.dll"
echo "rebuilt: ${BUILD_DIR}/src/d3d12/d3d12.dll"
echo "rebuilt: ${BUILD_DIR}/src/winemetal/winemetal.dll"
echo "rebuilt: ${BUILD_DIR}/src/winemetal/unix/winemetal.so"
