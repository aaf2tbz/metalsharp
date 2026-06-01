#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$PROJECT_ROOT/app/bundles"

"$PROJECT_ROOT/tools/dmg/create-bundles.sh"
"$PROJECT_ROOT/tools/bundles/verify-bundles.sh" --bundle-dir "$BUNDLE_DIR" --require linux

echo ""
echo "Linux split bundles saved to: $BUNDLE_DIR"
