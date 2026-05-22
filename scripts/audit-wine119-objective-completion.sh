#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/audit-wine119-objective-completion.sh [output-dir]

Example:
  scripts/audit-wine119-objective-completion.sh /tmp/metalsharp-wine119-objective-audit

Builds a requirement-by-requirement completion audit for the Wine 11.9 rebuild
objective. This script does not launch games and does not mutate runtimes.

Environment:
  METALSHARP_READINESS_REPORT       readiness.md path
  METALSHARP_CANDIDATE_SUMMARY     parity candidate summary.md path
  METALSHARP_FETCH_REPORT          release fetch-report.txt path
  METALSHARP_PARITY_PROBE_DIR      backend probe dir for route/hook evidence
  METALSHARP_I386_WINEMETAL_PROVENANCE provenance.md path
  METALSHARP_LIVE_SUITE_DIR        optional live suite dir to prove live gate
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${1:-/tmp/metalsharp-wine119-objective-audit-$timestamp}"
report="$out_dir/objective-completion-audit.md"

mkdir -p "$out_dir"

status_for_file() {
    [[ -f "$repo_root/$1" ]] && printf 'proven' || printf 'missing'
}

status_for_path() {
    [[ -e "$1" ]] && printf 'proven' || printf 'missing'
}

contains_file() {
    local file="$1"
    local pattern="$2"
    [[ -f "$file" ]] && rg -q "$pattern" "$file"
}

requirement_row() {
    local requirement="$1"
    local status="$2"
    local evidence="$3"
    local remaining="$4"
    printf '| %s | %s | %s | %s |\n' "$requirement" "$status" "$evidence" "$remaining" >> "$report"
}

forensics="$repo_root/docs/wine-119-rebuild-forensics.md"
runbook="$repo_root/docs/wine-119-implementation-runbook.md"
readiness="${METALSHARP_READINESS_REPORT:-/tmp/metalsharp-wine119-readiness-current/readiness.md}"
candidate_summary="${METALSHARP_CANDIDATE_SUMMARY:-/tmp/metalsharp-wine119-parity/summary.md}"
fetch_report="${METALSHARP_FETCH_REPORT:-/tmp/metalsharp-wine-assets/fetch-report.txt}"
parity_probe="${METALSHARP_PARITY_PROBE_DIR:-/private/tmp/metalsharp-home-wine119-dxmt32-state/backend-probe-main03327-guard-v2}"
i386_provenance="${METALSHARP_I386_WINEMETAL_PROVENANCE:-/tmp/metalsharp-i386-winemetal-provenance-current/provenance.md}"
live_suite="${METALSHARP_LIVE_SUITE_DIR:-}"

main_head="$(git -C "$repo_root" rev-parse --short HEAD)"
main_descends="no"
if git -C "$repo_root" merge-base --is-ancestor cebf936 HEAD 2>/dev/null; then
    main_descends="yes"
fi

{
    echo "# Wine 11.9 Objective Completion Audit"
    echo
    echo "- captured_at_utc: \`$timestamp\`"
    echo "- repo: \`$repo_root\`"
    echo "- head: \`$main_head\`"
    echo "- descends_from_rollback_cebf936: \`$main_descends\`"
    echo
    echo "| Requirement | Status | Evidence | Remaining proof |"
    echo "| --- | --- | --- | --- |"
} > "$report"

if [[ "$main_descends" == "yes" ]] && contains_file "$forensics" 'v0[.]33[.]27' && contains_file "$forensics" '797db720c852b7f46356fa2541fb4dac81afcc69'; then
    requirement_row "Identify yin/yang source of truth" "proven" "rollback base and full regression tip are documented in docs/wine-119-rebuild-forensics.md" "none"
else
    requirement_row "Identify yin/yang source of truth" "missing" "expected rollback/regression refs not found in forensics doc" "document and verify main/v0.33.27 versus 797db72"
fi

if contains_file "$forensics" '^## Commit Replay Ledger$' && contains_file "$forensics" 'Do not replay merge commit' && contains_file "$forensics" '\| `797db72`'; then
    requirement_row "Compare post-0.33.27 changes commit by commit" "proven" "commit replay ledger covers the regression branch and marks rollup commits unsafe" "none"
else
    requirement_row "Compare post-0.33.27 changes commit by commit" "incomplete" "commit ledger evidence is missing or weak" "complete ledger for v0.33.27..797db72"
fi

if contains_file "$forensics" '^## Wine 11[.]9 Internal Map Against Working Wine 11[.]5$' && contains_file "$forensics" 'lib/wine/i386-windows/winemetal[.]dll' && contains_file "$forensics" 'lib/libMoltenVK[.]dylib'; then
    requirement_row "Map Wine 11.9 internals against working Wine 11.5" "proven" "forensics map covers runtime shape, WineMetal, Vulkan/MoltenVK, DXMT, and mscompatdb surfaces" "none"
else
    requirement_row "Map Wine 11.9 internals against working Wine 11.5" "incomplete" "internal map is missing expected route-critical surfaces" "expand 11.5/11.9 runtime map"
fi

if contains_file "$fetch_report" 'repo=aaf2tbz/metalsharp' &&
    contains_file "$fetch_report" 'tag=bundles' &&
    contains_file "$fetch_report" 'release_tag_name=bundles' &&
    contains_file "$fetch_report" 'asset=metalsharp_bundle[.]tar[.]zst' &&
    contains_file "$fetch_report" 'sha256=833f63566b0c1b98fa917337716f57d689c42d0c2878204b4716ba29637d7372' &&
    contains_file "$fetch_report" 'verified=1'; then
    requirement_row "Use GitHub release asset as Wine 11.9 source" "proven" "$fetch_report verifies bundles/metalsharp_bundle.tar.zst" "none"
else
    requirement_row "Use GitHub release asset as Wine 11.9 source" "missing" "$fetch_report not present or missing repo/tag/sha verification" "run scripts/fetch-wine119-release-assets.sh"
fi

if contains_file "$candidate_summary" '## dxmt32' && contains_file "$candidate_summary" '## borrowed' && contains_file "$candidate_summary" 'Manifest pass alone is not a release gate'; then
    requirement_row "Prepare 11.9 candidates in 11.5-compatible runtime shape" "proven" "$candidate_summary records clean, dxmt32, and borrowed candidates" "live M9 proof still required before release"
else
    requirement_row "Prepare 11.9 candidates in 11.5-compatible runtime shape" "missing" "$candidate_summary missing" "run scripts/prepare-wine119-parity-candidates.sh"
fi

if contains_file "$readiness" 'PASS \[electron-route\]' && contains_file "$readiness" 'PASS \[mscompatdb-hook\]' && contains_file "$readiness" 'PASS \[parity-home\] active state has no source-home references'; then
    requirement_row "Prove non-live parity home/backend/app route readiness" "proven" "$readiness passes backend, Electron route, mscompatdb symbol, and isolated state checks" "live suite still required"
else
    requirement_row "Prove non-live parity home/backend/app route readiness" "incomplete" "$readiness lacks expected non-live passes" "rerun probe/readiness scripts"
fi

live_verification=""
if [[ -n "$live_suite" ]]; then
    live_verification="$live_suite/verification.md"
fi

if contains_file "$readiness" 'PASS \[live-gate\]' &&
    contains_file "$live_verification" '## nidhogg2' &&
    contains_file "$live_verification" '## schedule-i' &&
    contains_file "$live_verification" '## subnautica-bz' &&
    contains_file "$live_verification" 'PASS \[nidhogg2\].*pipeline=m9' &&
    contains_file "$live_verification" 'PASS \[schedule-i\].*pipeline=m11' &&
    contains_file "$live_verification" 'PASS \[subnautica-bz\].*pipeline=m11' &&
    contains_file "$live_verification" 'gate: pass'; then
    requirement_row "Pass Nidhogg 2, Schedule I, and Subnautica BZ under Wine 11.9" "proven" "$live_verification proves all three live controls and readiness reports PASS [live-gate]" "none"
elif contains_file "$readiness" 'FAIL \[live-gate\] no passing live control suite supplied'; then
    requirement_row "Pass Nidhogg 2, Schedule I, and Subnautica BZ under Wine 11.9" "missing" "$readiness explicitly reports no passing live suite" "run guarded live suite with METALSHARP_RUN_LIVE_GAMES=1 and METALSHARP_REQUIRE_PREEXISTING_WINE_STEAM=1"
else
    requirement_row "Pass Nidhogg 2, Schedule I, and Subnautica BZ under Wine 11.9" "unverified" "$readiness does not clearly prove live controls" "inspect METALSHARP_LIVE_SUITE_DIR and verification.md"
fi

if contains_file "$i386_provenance" '^- result: fail$'; then
    requirement_row "Recover or produce release-proven 11.9 i386 WineMetal" "missing" "$i386_provenance reports the current i386 bridge is not release-proven" "build/recover true 11.9 i386 winemetal.dll or bless fallback only after live proof"
elif contains_file "$candidate_summary" 'dxmt32' && contains_file "$candidate_summary" 'release_blockers: 1' && contains_file "$forensics" 'no release-proven 11[.]9-built i386 `winemetal[.]dll`'; then
    requirement_row "Recover or produce release-proven 11.9 i386 WineMetal" "missing" "candidate summary and forensics mark i386 WineMetal as unresolved" "run scripts/audit-i386-winemetal-provenance.sh, then build/recover true 11.9 i386 winemetal.dll or bless fallback only after live proof"
else
    requirement_row "Recover or produce release-proven 11.9 i386 WineMetal" "unverified" "i386 WineMetal status is not explicit" "audit candidate provenance and live M9 result"
fi

if contains_file "$parity_probe/mscompatdb-hook-surface/hook-surface.json" '"symbolSurfaceReady"[[:space:]]*:[[:space:]]*true' &&
    contains_file "$parity_probe/mscompatdb-hook-surface/hook-surface.json" '"hookReady"[[:space:]]*:[[:space:]]*false'; then
    requirement_row "Separate anti-cheat hook surface from runtime hook readiness" "proven-nonlive" "$parity_probe/mscompatdb-hook-surface/hook-surface.json proves symbols and refuses hookReady" "protected runtime launch proof required before hookReady can be true"
else
    requirement_row "Separate anti-cheat hook surface from runtime hook readiness" "incomplete" "mscompatdb hook surface audit missing or ambiguous" "run scripts/audit-mscompatdb-hook-surface.sh"
fi

if contains_file "$runbook" '^## Pass 0:' &&
    contains_file "$runbook" '^## Pass 6:' &&
    contains_file "$runbook" '^## Final Release Checklist$' &&
    contains_file "$runbook" 'Required pass conditions:' &&
    contains_file "$runbook" 'Required proof before live launch:' &&
    contains_file "$runbook" 'Do not bump version or tag until all live gates pass'; then
    requirement_row "Plan complete multi-pass Wine 11.9 rebuild" "proven" "docs/wine-119-implementation-runbook.md defines passes 0-6 and release checklist" "execute live/release gates before tag"
else
    requirement_row "Plan complete multi-pass Wine 11.9 rebuild" "incomplete" "runbook does not contain expected pass matrix" "finish pass-by-pass rebuild design"
fi

{
    echo
    echo "## Summary"
    echo
    if rg -q '\| [^|]+ \| (missing|incomplete|unverified)' "$report"; then
        echo "- gate: fail"
        echo "- reason: the objective is not complete until missing/incomplete rows are proven."
    else
        echo "- gate: pass"
    fi
} >> "$report"

cp "$report" "$out_dir/latest.md"
echo "objective completion audit written: $report"
if rg -q '\| [^|]+ \| (missing|incomplete|unverified)' "$report"; then
    exit 2
fi
