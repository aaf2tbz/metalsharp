#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-objective-audit-test.XXXXXX")"
work_dir="$(cd "$work_dir" && pwd -P)"
trap 'rm -rf "$work_dir"' EXIT

sha="12b9343459d28dfe1d7668a31a58a0c3506948d7c533507fdaec65d59c6697ab"
candidate_summary="$work_dir/summary.md"
readiness="$work_dir/readiness.md"
fetch_report="$work_dir/fetch-report.txt"
probe_dir="$work_dir/probe"
clean_provenance="$work_dir/clean-provenance.md"
clean_preflight="$work_dir/clean-preflight.md"
dirty_provenance="$work_dir/dirty-provenance.md"
dirty_preflight="$work_dir/dirty-preflight.md"

mkdir -p "$probe_dir/mscompatdb-hook-surface"

cat > "$candidate_summary" <<'EOF'
# MetalSharp Wine 11.9 Parity Candidates

- dxmt32_source_kind: `clean-11.9-linked-default`

## clean

- gate: fail

## dxmt32

- gate: fail

## borrowed

- gate: pass

## Required Live Gates

- Manifest pass alone is not a release gate for any candidate.
EOF

cat > "$fetch_report" <<'EOF'
repo=aaf2tbz/metalsharp
tag=bundles
release_tag_name=bundles
asset=metalsharp_bundle.tar.zst
sha256=833f63566b0c1b98fa917337716f57d689c42d0c2878204b4716ba29637d7372
verified=1
EOF

cat > "$clean_provenance" <<EOF
- result: review
- reason: source tree is clean and linker metadata references the shaped Wine 11.9 runtime; still requires live M9 proof before release.
- artifact_sha256: \`$sha\`
EOF

cat > "$clean_preflight" <<'EOF'
- PASS [source-clean] DXMT source tree is clean
- PASS [wine-version] wine reports wine-11.9
EOF

cat > "$dirty_provenance" <<'EOF'
- result: fail
EOF

cat > "$dirty_preflight" <<'EOF'
- PASS [wine-version] wine reports wine-11.9
- FAIL [source-clean] DXMT source tree is dirty
EOF

cat > "$probe_dir/mscompatdb-hook-surface/hook-surface.json" <<'EOF'
{"symbolSurfaceReady":true,"hookReady":false}
EOF

write_readiness() {
    local include_clean="$1"
    cat > "$readiness" <<'EOF'
# MetalSharp Wine 11.9 Readiness Audit

- PASS [parity-home] active state has no source-home references
- PASS [electron-route] app-facing route audit keeps controls on /steam/launch-game
- PASS [mscompatdb-hook] ntdll hook symbols are present in the parity runtime
- FAIL [live-gate] no passing live control suite supplied; set METALSHARP_LIVE_SUITE_DIR after running guarded live controls
EOF
    if [[ "$include_clean" == "1" ]]; then
        cat >> "$readiness" <<'EOF'
- PASS [candidate-dxmt32-source] provenance records clean i386 WineMetal source/destination SHA256
- PASS [parity-home] install report source matches the audited clean dxmt32 candidate
EOF
    fi
}

run_audit() {
    local out="$1"
    METALSHARP_READINESS_REPORT="$readiness" \
    METALSHARP_CANDIDATE_SUMMARY="$candidate_summary" \
    METALSHARP_FETCH_REPORT="$fetch_report" \
    METALSHARP_PARITY_PROBE_DIR="$probe_dir" \
    METALSHARP_I386_WINEMETAL_PROVENANCE="$dirty_provenance" \
    METALSHARP_I386_WINEMETAL_BUILD_PREFLIGHT="$dirty_preflight" \
    METALSHARP_I386_WINEMETAL_CLEAN_PROVENANCE="$clean_provenance" \
    METALSHARP_I386_WINEMETAL_CLEAN_PREFLIGHT="$clean_preflight" \
        "$repo_root/scripts/audit-wine119-objective-completion.sh" "$out"
}

write_readiness 0
if run_audit "$work_dir/stale" >/dev/null 2>&1; then
    echo "expected stale readiness audit to keep objective incomplete" >&2
    exit 1
fi
grep -q 'Prove non-live parity home/backend/app route readiness | incomplete' "$work_dir/stale/objective-completion-audit.md"

write_readiness 1
if run_audit "$work_dir/clean" >/dev/null 2>&1; then
    echo "expected objective audit to remain failed without live proof" >&2
    exit 1
fi
grep -q 'Prove non-live parity home/backend/app route readiness | proven' "$work_dir/clean/objective-completion-audit.md"
grep -q 'Pass Nidhogg 2, Schedule I, and Subnautica BZ under Wine 11.9 | missing' "$work_dir/clean/objective-completion-audit.md"
grep -q 'Recover or produce release-proven 11.9 i386 WineMetal | unverified' "$work_dir/clean/objective-completion-audit.md"
grep -q 'active dxmt32 candidate/parity home use the clean artifact' "$work_dir/clean/objective-completion-audit.md"

echo "wine119 objective-audit guard fixtures passed"
