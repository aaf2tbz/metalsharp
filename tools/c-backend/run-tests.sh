#!/usr/bin/env bash
set -euo pipefail

test_binary=${1:?usage: run-tests.sh <C test binary>}
backend_dir=${2:?usage: run-tests.sh <C test binary> <backend working directory>}
test -x "$test_binary"
log=$(mktemp)
trap 'rm -f "$log"' EXIT
(cd "$backend_dir" && "$test_binary" --test-threads=1) | tee "$log"
grep -q 'test result: ok. 629 passed; 0 failed' "$log"
