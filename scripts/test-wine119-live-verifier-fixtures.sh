#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-live-verifier-test.XXXXXX")"
work_dir="$(cd "$work_dir" && pwd -P)"
trap 'rm -rf "$work_dir"' EXIT

suite="$work_dir/suite"
mkdir -p "$suite/launches" "$suite/proof" "$suite/steam"

cat > "$suite/runtime-identity.txt" <<'EOF'
wine_version=wine-11.9
wineserver_version=Wine 11.9
EOF

cat > "$suite/status.json" <<'EOF'
{"ok":true,"version":"0.33.27"}
EOF

cat > "$suite/summary.md" <<'EOF'
# MetalSharp Wine 11.9 Live Control Suite

require_preexisting_wine_steam=1
EOF

write_control() {
    local label="$1"
    local appid="$2"
    local pipeline="$3"
    local pid="$4"
    shift 4
    local patterns=("$@")
    local proof_dir="$suite/proof/$label"

    mkdir -p "$proof_dir"
    printf '{"ok":true,"pipeline":"%s"}\n' "$pipeline" > "$suite/launches/$label-launch.json"
    printf '%s\n' "$pid" > "$proof_dir/game-pids.txt"
    {
        echo "appid=$appid"
        echo "pipeline=$pipeline"
        echo
        for pattern in "${patterns[@]}"; do
            echo "$pattern"
        done
    } > "$proof_dir/summary.txt"
    printf '700\n701\n' > "$suite/steam/before-$label-wine-steam-pids.txt"
    printf '700\n701\n' > "$suite/steam/after-$label-wine-steam-pids.txt"
}

write_control \
    nidhogg2 \
    535520 \
    m9 \
    12345 \
    'lib/wine/i386-windows/winemetal.dll' \
    'lib/wine/i386-windows/dxgi.dll' \
    'lib/wine/i386-windows/d3d11.dll' \
    'lib/wine/x86_64-unix/winemetal.so' \
    'etc/dxmt.conf' \
    'shader-cache/m9/535520'

write_control \
    schedule-i \
    3164500 \
    m11 \
    12346 \
    'lib/wine/x86_64-windows/d3d11.dll' \
    'lib/wine/x86_64-windows/dxgi.dll' \
    'lib/wine/x86_64-windows/winemetal.dll' \
    'lib/wine/x86_64-unix/winemetal.so' \
    'libMoltenVK.1.dylib' \
    'etc/dxmt.conf' \
    'shader-cache/m11/3164500'

write_control \
    subnautica-bz \
    848450 \
    m11 \
    12347 \
    'lib/wine/x86_64-windows/d3d11.dll' \
    'lib/wine/x86_64-windows/dxgi.dll' \
    'lib/wine/x86_64-windows/winemetal.dll' \
    'lib/wine/x86_64-unix/winemetal.so' \
    'libMoltenVK.1.dylib' \
    'etc/dxmt.conf' \
    'shader-cache/m11/848450'

"$repo_root/scripts/verify-wine119-live-control-suite.sh" "$suite" >/dev/null
grep -q 'gate: pass' "$suite/verification.md"

cp -R "$suite" "$work_dir/suite-missing-steam"
printf '700\n' > "$work_dir/suite-missing-steam/steam/after-nidhogg2-wine-steam-pids.txt"
if "$repo_root/scripts/verify-wine119-live-control-suite.sh" "$work_dir/suite-missing-steam" >/dev/null 2>&1; then
    echo "expected verifier to fail when a pre-existing Wine Steam PID disappears" >&2
    exit 1
fi
grep -q 'pre-existing Wine Steam PID(s) disappeared after launch' "$work_dir/suite-missing-steam/verification.md"

cp -R "$suite" "$work_dir/suite-log-only"
: > "$work_dir/suite-log-only/proof/schedule-i/game-pids.txt"
if "$repo_root/scripts/verify-wine119-live-control-suite.sh" "$work_dir/suite-log-only" >/dev/null 2>&1; then
    echo "expected verifier to fail log-only Schedule I proof" >&2
    exit 1
fi
grep -q 'missing live game PID; log-only proof is not a release gate' "$work_dir/suite-log-only/verification.md"

echo "wine119 live verifier fixtures passed"
