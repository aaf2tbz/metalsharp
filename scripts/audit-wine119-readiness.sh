#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/audit-wine119-readiness.sh [parity-home] [output-dir]

Example:
  scripts/audit-wine119-readiness.sh \
    /private/tmp/metalsharp-home-wine119-dxmt32-state \
    /tmp/metalsharp-wine119-readiness

Runs a non-live Wine 11.9 readiness audit. This script does not launch games.
It checks whether the repo, installed 11.5 baseline, prepared 11.9 candidates,
isolated parity home, and existing live-suite proof satisfy the rebuild gates.

The final release gate still requires a passing live control suite.

Environment:
  METALSHARP_WINE119_CANDIDATE_WORK_DIR  candidate work dir, default prefers
                                         AverySSD clean-i386 output when present
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
parity_home="${1:-/private/tmp/metalsharp-home-wine119-dxmt32-state}"
default_candidate_work_dir="/tmp/metalsharp-wine119-parity"
clean_candidate_work_dir="/Volumes/AverySSD/metalsharp/wine119-parity-clean-i386"
if [[ -z "${METALSHARP_WINE119_CANDIDATE_WORK_DIR:-}" && -f "$clean_candidate_work_dir/summary.md" ]]; then
    candidate_work_dir="$clean_candidate_work_dir"
else
    candidate_work_dir="${METALSHARP_WINE119_CANDIDATE_WORK_DIR:-$default_candidate_work_dir}"
fi
candidate_summary="$candidate_work_dir/summary.md"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${2:-/tmp/metalsharp-wine119-readiness-$timestamp}"
report="$out_dir/readiness.md"
failures=0
warnings=0

mkdir -p "$out_dir"
: > "$report"

append() {
    printf '%s\n' "$*" >> "$report"
}

pass() {
    append "- PASS [$1] $2"
}

fail() {
    failures=$((failures + 1))
    append "- FAIL [$1] $2"
}

warn() {
    warnings=$((warnings + 1))
    append "- WARN [$1] $2"
}

contains() {
    local file="$1"
    local pattern="$2"
    [[ -f "$file" ]] && rg -q "$pattern" "$file"
}

canonical_dir() {
    local path="$1"
    if [[ -d "$path" ]]; then
        cd "$path" && pwd -P
    else
        printf '%s\n' "$path"
    fi
}

check_wine_version() {
    local label="$1"
    local wine_root="$2"
    local expected="$3"
    if [[ ! -x "$wine_root/bin/wine" ]]; then
        fail "$label" "missing executable wine at $wine_root/bin/wine"
        return
    fi
    local version
    version="$("$wine_root/bin/wine" --version 2>&1 || true)"
    if [[ "$version" == "$expected" ]]; then
        pass "$label" "wine version is $expected"
    else
        fail "$label" "wine version is '$version', expected '$expected'"
    fi
}

check_local_moltenvk_icd() {
    local label="$1"
    local wine_root="$2"
    local icd="$wine_root/etc/vulkan/icd.d/MoltenVK_x86_64_icd.json"
    local expected="$wine_root/lib/libMoltenVK.dylib"

    if [[ ! -f "$expected" ]]; then
        fail "$label" "missing candidate-local MoltenVK ICD target: $expected"
        return
    fi
    if [[ ! -f "$icd" ]]; then
        fail "$label" "missing MoltenVK_x86_64_icd.json"
        return
    fi
    if grep -Fq "\"library_path\": \"$expected\"" "$icd"; then
        pass "$label" "MoltenVK ICD points at candidate-local runtime"
    else
        fail "$label" "MoltenVK ICD does not point at candidate-local runtime"
    fi
}

candidate_gate() {
    local candidate="$1"
    local summary="$2"
    awk -v candidate="$candidate" '
        $0 == "## " candidate { in_section=1; next }
        in_section && /^## (clean|dxmt32|borrowed|Required Live Gates)$/ { in_section=0 }
        in_section && /^- gate: / {
            print $3
            exit
        }
    ' "$summary"
}

append "# MetalSharp Wine 11.9 Readiness Audit"
append
append "- captured_at_utc: \`$timestamp\`"
append "- repo: \`$repo_root\`"
append "- parity_home: \`$parity_home\`"
append "- candidate_work_dir: \`$candidate_work_dir\`"
append

append "## Source State"
append
main_head="$(git -C "$repo_root" rev-parse --short HEAD)"
main_tag="$(git -C "$repo_root" describe --tags --exact-match HEAD 2>/dev/null || true)"
if [[ "$main_head" == "cebf936" ]]; then
    pass "repo" "main HEAD is rollback commit cebf936"
elif git -C "$repo_root" merge-base --is-ancestor cebf936 HEAD 2>/dev/null; then
    pass "repo" "current branch descends from rollback commit cebf936"
else
    warn "repo" "current HEAD is $main_head and does not descend from rollback commit cebf936"
fi
if [[ "$main_tag" == "v0.33.27" ]]; then
    pass "repo" "HEAD is exactly tag v0.33.27"
else
    warn "repo" "HEAD exact tag is '${main_tag:-none}', but rollback commit may still be tree-equivalent"
fi

check_wine_version "installed-11.5" "$HOME/.metalsharp/runtime/wine" "wine-11.5"
if [[ -f "$HOME/.metalsharp/runtime/wine/lib/wine/i386-windows/winemetal.dll" ]]; then
    pass "installed-11.5" "i386 WineMetal baseline exists"
else
    fail "installed-11.5" "missing i386 WineMetal baseline"
fi
append

append "## Candidate Runtime Shape"
fetch_report="/tmp/metalsharp-wine-assets/fetch-report.txt"
if [[ -f "$fetch_report" ]] &&
    contains "$fetch_report" 'asset=metalsharp_bundle\.tar\.zst' &&
    contains "$fetch_report" 'sha256=833f63566b0c1b98fa917337716f57d689c42d0c2878204b4716ba29637d7372' &&
    contains "$fetch_report" 'verified=1'; then
    pass "release-asset" "GitHub bundles/metalsharp_bundle.tar.zst was fetched and SHA256 verified"
else
    warn "release-asset" "verified fetch report not found at $fetch_report"
fi

for candidate in clean dxmt32 borrowed; do
    root="$candidate_work_dir/candidates/$candidate/wine"
    check_wine_version "candidate-$candidate" "$root" "wine-11.9"
    check_local_moltenvk_icd "candidate-$candidate" "$root"
done

if [[ -f "$candidate_summary" ]]; then
    pass "candidates" "candidate summary exists"
    [[ "$(candidate_gate clean "$candidate_summary")" == "fail" ]] \
        && pass "candidate-clean" "clean candidate is documented as fail" \
        || warn "candidate-clean" "clean candidate failure not found in summary"
    [[ "$(candidate_gate dxmt32 "$candidate_summary")" == "fail" ]] \
        && pass "candidate-dxmt32" "dxmt32 candidate is documented as needing live proof" \
        || warn "candidate-dxmt32" "dxmt32 live-proof blocker not found in summary"
    [[ "$(candidate_gate borrowed "$candidate_summary")" == "pass" ]] \
        && pass "candidate-borrowed" "borrowed candidate is manifest-complete" \
        || warn "candidate-borrowed" "borrowed manifest pass not found in summary"
    if contains "$candidate_summary" 'dxmt32_source_kind: `clean-11[.]9-linked-default`'; then
        pass "candidate-dxmt32-source" "dxmt32 defaults to clean 11.9-linked i386 WineMetal"
    else
        warn "candidate-dxmt32-source" "dxmt32 summary does not prove clean 11.9-linked source selection"
    fi
else
    fail "candidates" "missing $candidate_summary"
fi

dxmt32_prepare="$candidate_work_dir/candidates/dxmt32/prepare-report.txt"
dxmt32_provenance="$candidate_work_dir/candidates/dxmt32/provenance-report.txt"
if [[ -f "$dxmt32_prepare" ]] && contains "$dxmt32_prepare" 'i386_winemetal_source=/tmp/metalsharp-dxmt-clean-build32-wine119/src/winemetal/winemetal[.]dll'; then
    pass "candidate-dxmt32-source" "prepare report records the clean i386 WineMetal source"
else
    fail "candidate-dxmt32-source" "prepare report does not record the clean i386 WineMetal source: $dxmt32_prepare"
fi
if [[ -f "$dxmt32_provenance" ]] &&
    contains "$dxmt32_provenance" 'source_sha256=12b9343459d28dfe1d7668a31a58a0c3506948d7c533507fdaec65d59c6697ab' &&
    contains "$dxmt32_provenance" 'destination_sha256=12b9343459d28dfe1d7668a31a58a0c3506948d7c533507fdaec65d59c6697ab'; then
    pass "candidate-dxmt32-source" "provenance records clean i386 WineMetal source/destination SHA256"
else
    fail "candidate-dxmt32-source" "provenance does not prove clean i386 WineMetal SHA256 copy: $dxmt32_provenance"
fi
append

append "## Isolated Parity Home"
if [[ ! -d "$parity_home/.metalsharp/runtime/wine" ]]; then
    fail "parity-home" "missing parity runtime"
else
    parity_home="$(canonical_dir "$parity_home")"
    check_wine_version "parity-home" "$parity_home/.metalsharp/runtime/wine" "wine-11.9"
fi

install_report="$parity_home/parity-install-report.txt"
if [[ -f "$install_report" ]]; then
    pass "parity-home" "install report exists"
    source_ms_home="$(awk -F= '$1 == "source_metalsharp_home" { print substr($0, index($0, "=") + 1); exit }' "$install_report")"
    installed_candidate_root="$(awk -F= '$1 == "candidate_root" { print substr($0, index($0, "=") + 1); exit }' "$install_report")"
    expected_candidate_root="$candidate_work_dir/candidates/dxmt32/wine"
    if [[ "$(canonical_dir "$installed_candidate_root")" == "$(canonical_dir "$expected_candidate_root")" ]]; then
        pass "parity-home" "install report source matches the audited clean dxmt32 candidate"
    else
        fail "parity-home" "install report candidate_root '$installed_candidate_root' does not match audited dxmt32 '$expected_candidate_root'"
    fi
else
    fail "parity-home" "missing install report"
    source_ms_home=""
fi

if [[ -n "$source_ms_home" ]]; then
    stale_refs="$out_dir/stale-active-state-refs.txt"
    : > "$stale_refs"
    if find "$parity_home/.metalsharp/bottles" "$parity_home/.metalsharp/compatdata" "$parity_home/.metalsharp/configs" \
        -type f \( -name '*.json' -o -name '*.toml' -o -name '*.conf' -o -name '*.env' \) -print0 2>/dev/null \
        | xargs -0 rg -n --fixed-strings "$source_ms_home" -S > "$stale_refs" 2>/dev/null; then
        fail "parity-home" "active state still references source MetalSharp home; see $stale_refs"
    else
        pass "parity-home" "active state has no source-home references"
    fi
else
    fail "parity-home" "cannot check stale state refs without source_metalsharp_home"
fi

if find "$parity_home/.metalsharp/compatdata" -path '*/logs/*' -type f -print -quit 2>/dev/null | grep -q .; then
    fail "parity-home" "copied compatdata log files are present and can contaminate proof"
else
    pass "parity-home" "copied compatdata log files are pruned"
fi

for appid in 535520 3164500 848450; do
    bottle="$parity_home/.metalsharp/bottles/steam_$appid/bottle.json"
    compat="$parity_home/.metalsharp/compatdata/$appid/metalsharp-compatdata.json"
    if [[ -f "$bottle" ]] && grep -Fq "$parity_home/.metalsharp" "$bottle"; then
        pass "control-$appid" "bottle manifest references parity home"
    else
        fail "control-$appid" "bottle manifest missing or not bound to parity home"
    fi
    if [[ -f "$compat" ]] && grep -Fq "$parity_home/.metalsharp" "$compat"; then
        pass "control-$appid" "compatdata manifest references parity home"
    else
        fail "control-$appid" "compatdata manifest missing or not bound to parity home"
    fi
done
append

append "## Backend Preflight"
probe="$parity_home/backend-probe-main03327-guard-v2"
if [[ -f "$probe/status.json" ]]; then
    contains "$probe/status.json" '"version"[[:space:]]*:[[:space:]]*"0\.33\.27"' \
        && pass "backend" "preflight status reports version 0.33.27" \
        || fail "backend" "preflight status does not report version 0.33.27"
else
    warn "backend" "latest guard-v2 backend preflight not found"
fi

route_audit="$probe/electron-launch-routes/electron-launch-route-audit.json"
if [[ -f "$route_audit" ]]; then
    contains "$route_audit" '"ok"[[:space:]]*:[[:space:]]*true' \
        && pass "electron-route" "app-facing route audit keeps controls on /steam/launch-game" \
        || fail "electron-route" "app-facing route audit did not pass"
else
    warn "electron-route" "app-facing route audit not found at $route_audit"
fi

hook_audit="$probe/mscompatdb-hook-surface/hook-surface.json"
if [[ -f "$hook_audit" ]]; then
    contains "$hook_audit" '"symbolSurfaceReady"[[:space:]]*:[[:space:]]*true' \
        && pass "mscompatdb-hook" "ntdll hook symbols are present in the parity runtime" \
        || fail "mscompatdb-hook" "ntdll hook symbols are not ready in the parity runtime"
    contains "$hook_audit" '"hookReady"[[:space:]]*:[[:space:]]*false' \
        && pass "mscompatdb-hook" "hook readiness is explicitly not claimed without runtime proof" \
        || fail "mscompatdb-hook" "hook readiness state is ambiguous"
else
    warn "mscompatdb-hook" "hook surface audit not found at $hook_audit"
fi
append

append "## Live Gate"
live_suite="${METALSHARP_LIVE_SUITE_DIR:-}"
if [[ -n "$live_suite" && -d "$live_suite" ]]; then
    if "$repo_root/scripts/verify-wine119-live-control-suite.sh" "$live_suite" >/dev/null 2>&1; then
        pass "live-gate" "live control suite verifier passes: $live_suite"
    else
        fail "live-gate" "live control suite verifier fails: $live_suite"
    fi
else
    fail "live-gate" "no passing live control suite supplied; set METALSHARP_LIVE_SUITE_DIR after running guarded live controls"
fi
append

append "## Summary"
append
append "- failures: $failures"
append "- warnings: $warnings"
if [[ "$failures" -eq 0 ]]; then
    append "- gate: pass"
    echo "Wine 11.9 readiness audit passed: $report"
else
    append "- gate: fail"
    echo "Wine 11.9 readiness audit failed: $report"
    exit 2
fi
