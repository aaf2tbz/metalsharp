#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-live-guard-test.XXXXXX")"
work_dir="$(cd "$work_dir" && pwd -P)"
trap 'rm -rf "$work_dir"' EXIT

sha="12b9343459d28dfe1d7668a31a58a0c3506948d7c533507fdaec65d59c6697ab"
candidate_work="$work_dir/candidates-work"
candidate_root="$candidate_work/candidates/dxmt32/wine"
parity_home="$work_dir/parity-home"
parity_ms_home="$parity_home/.metalsharp"

mkdir -p "$candidate_root" \
    "$candidate_work/candidates/dxmt32" \
    "$parity_ms_home/runtime" \
    "$parity_ms_home/prefix-steam" \
    "$parity_ms_home/compatdata" \
    "$parity_ms_home/bottles" \
    "$parity_ms_home/games" \
    "$parity_ms_home/configs" \
    "$work_dir/source-home/.metalsharp"

ln -s "$candidate_root" "$parity_ms_home/runtime/wine"

cat > "$candidate_work/summary.md" <<EOF
# MetalSharp Wine 11.9 Parity Candidates

- dxmt32_source_kind: \`clean-11.9-linked-default\`

## clean

- gate: fail

## dxmt32

- gate: fail

## borrowed

- gate: pass

## Required Live Gates
EOF

cat > "$candidate_work/candidates/dxmt32/prepare-report.txt" <<'EOF'
i386_winemetal_source=/tmp/metalsharp-dxmt-clean-build32-wine119/src/winemetal/winemetal.dll
EOF

cat > "$candidate_work/candidates/dxmt32/provenance-report.txt" <<EOF
[lib/wine/i386-windows/winemetal.dll]
source_sha256=$sha
destination_sha256=$sha
EOF

cat > "$parity_home/parity-install-report.txt" <<EOF
candidate_root=$candidate_root
source_metalsharp_home=$work_dir/source-home/.metalsharp
EOF

for appid in 535520 3164500 848450; do
    mkdir -p "$parity_ms_home/bottles/steam_$appid" "$parity_ms_home/compatdata/$appid"
    printf '{"metalsharp_home":"%s"}\n' "$parity_ms_home" > "$parity_ms_home/bottles/steam_$appid/bottle.json"
    printf '{"metalsharp_home":"%s"}\n' "$parity_ms_home" > "$parity_ms_home/compatdata/$appid/metalsharp-compatdata.json"
done

run_preflight() {
    local out="$1"
    METALSHARP_RUN_LIVE_GAMES=1 \
    METALSHARP_LIVE_PREFLIGHT_ONLY=1 \
    METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1 \
    METALSHARP_WINE119_CANDIDATE_WORK_DIR="$candidate_work" \
        "$repo_root/scripts/run-wine119-live-control-suite.sh" "$parity_home" "$out"
}

run_preflight "$work_dir/pass"
test -f "$work_dir/pass/preflight-only.txt"

cp "$candidate_work/summary.md" "$candidate_work/summary.md.good"
perl -0pi -e 's/dxmt32_source_kind: `clean-11\.9-linked-default`/dxmt32_source_kind: `legacy-dirty-dxmt-build32-fallback`/' "$candidate_work/summary.md"
if run_preflight "$work_dir/fail-dirty-summary" >/dev/null 2>"$work_dir/fail-dirty-summary.err"; then
    echo "expected dirty dxmt32 summary to fail live preflight" >&2
    exit 1
fi
grep -q 'dxmt32 summary does not prove clean 11.9-linked i386 source' "$work_dir/fail-dirty-summary.err"
mv "$candidate_work/summary.md.good" "$candidate_work/summary.md"

perl -0pi -e 's#^candidate_root=.*#candidate_root=/tmp/wrong-dxmt32#m' "$parity_home/parity-install-report.txt"
if run_preflight "$work_dir/fail-wrong-root" >/dev/null 2>"$work_dir/fail-wrong-root.err"; then
    echo "expected mismatched candidate_root to fail live preflight" >&2
    exit 1
fi
grep -q 'parity home was installed from' "$work_dir/fail-wrong-root.err"

echo "wine119 live-suite guard fixtures passed"
