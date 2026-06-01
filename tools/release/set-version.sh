#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: tools/release/set-version.sh VERSION" >&2
  exit 2
fi

VERSION="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

case "$VERSION" in
  [0-9]*.[0-9]*.[0-9]*) ;;
  *)
    echo "invalid semver version: $VERSION" >&2
    exit 2
    ;;
esac

cd "$PROJECT_ROOT"

node - "$VERSION" <<'NODE'
const fs = require("fs");
const version = process.argv[2];
for (const file of ["app/package.json", "app/package-lock.json"]) {
  const data = JSON.parse(fs.readFileSync(file, "utf8"));
  data.version = version;
  if (data.packages && data.packages[""]) {
    data.packages[""].version = version;
  }
  fs.writeFileSync(file, `${JSON.stringify(data, null, 2)}\n`);
}
NODE

perl -0pi -e "s/project\\(metalsharp VERSION \\K[0-9]+\\.[0-9]+\\.[0-9]+/$VERSION/" CMakeLists.txt
perl -0pi -e "s/(\\[package\\]\\nname = \"metalsharp-backend\"\\nversion = \")\\d+\\.\\d+\\.\\d+(\")/\${1}$VERSION\${2}/" app/src-rust/Cargo.toml
perl -0pi -e "s/(\\[\\[package\\]\\]\\nname = \"metalsharp-backend\"\\nversion = \")\\d+\\.\\d+\\.\\d+(\")/\${1}$VERSION\${2}/" app/src-rust/Cargo.lock
