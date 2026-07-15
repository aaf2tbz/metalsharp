#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: tools/release/write-provenance.sh OUTPUT" >&2
  exit 2
fi

output="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
backend="$root/app/build/c-backend/metalsharp-backend"
lock="$root/tools/bundles/release-inputs.lock.tsv"

test -s "$backend"
test -s "$lock"
mkdir -p "$(dirname "$output")"

{
  printf 'field\tvalue\n'
  printf 'source_commit\t%s\n' "$(git -C "$root" rev-parse HEAD)"
  printf 'source_ref\t%s\n' "${GITHUB_REF:-local}"
  printf 'c_compiler\t%s\n' "$(xcrun --find clang)"
  printf 'c_compiler_version\t%s\n' "$(xcrun clang --version | head -n1)"
  printf 'c_backend_sha256\t%s\n' "$(shasum -a 256 "$backend" | awk '{print $1}')"
  printf 'c_backend_architectures\t%s\n' "$(lipo -archs "$backend")"
  printf 'release_input_lock_sha256\t%s\n' "$(shasum -a 256 "$lock" | awk '{print $1}')"
  printf 'bundle_release_id\t%s\n' "317125068"
} > "$output"
