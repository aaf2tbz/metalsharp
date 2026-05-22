#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/compare-runtime-manifests.sh <baseline-manifest-dir> <candidate-manifest-dir> [report-file]

Example:
  scripts/compare-runtime-manifests.sh \
    /tmp/metalsharp-runtime-11.5-current \
    /tmp/metalsharp-runtime-11.9-candidate \
    /tmp/metalsharp-runtime-11.9-candidate/classified-diff.md

Classifies route-critical Wine/DXMT runtime manifest deltas into expected
version changes, expected DXMT/anti-cheat changes, packaging defects, and
release blockers. Inputs are directories produced by scripts/runtime-manifest.sh.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

baseline="${1:-}"
candidate="${2:-}"
report="${3:-}"

if [[ -z "$baseline" || -z "$candidate" ]]; then
    usage >&2
    exit 1
fi

for dir in "$baseline" "$candidate"; do
    if [[ ! -d "$dir" ]]; then
        echo "manifest directory not found: $dir" >&2
        exit 1
    fi
    if [[ ! -f "$dir/critical-sha256.txt" ]]; then
        echo "missing critical-sha256.txt in $dir" >&2
        exit 1
    fi
done

if [[ -z "$report" ]]; then
    report="$candidate/classified-diff.md"
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-manifest-compare.XXXXXX")"
cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

extract_value() {
    local file="$1"
    local rel="$2"
    awk -v rel="$rel" '$2 == rel { print $1; found=1 } END { if (!found) exit 1 }' "$file" 2>/dev/null || true
}

classify_path() {
    local rel="$1"
    case "$rel" in
        bin/wine|bin/wineserver|share/wine/wine.inf|lib/wine/x86_64-unix/ntdll.so|lib/wine/x86_64-unix/win32u.so)
            echo "expected_wine_version_delta"
            ;;
        lib/dxmt/x86_64-windows/d3d12.dll|lib/dxmt/x86_64-windows/dxgi.dll|lib/dxmt/x86_64-unix/winemetal.so)
            echo "expected_dxmt_delta"
            ;;
        lib/wine/x86_64-unix/mscompatdb.so|lib/wine/x86_64-unix/mscompatdb.dylib)
            echo "expected_anticheat_delta"
            ;;
        lib/wine/x86_64-unix/winemetal.so|lib/wine/x86_64-windows/winemetal.dll)
            echo "expected_rebound_from_dxmt"
            ;;
        bin/metalsharp-wine|etc/dxmt.conf|etc/mscompatdb_rules.toml|etc/vulkan/icd.d/MoltenVK_icd.json|etc/vulkan/icd.d/MoltenVK_x86_64_icd.json|lib/dxmt/x86_64-windows/d3d10core.dll|lib/dxmt/x86_64-windows/d3d11.dll|lib/dxmt/x86_64-windows/winemetal.dll|lib/wine/x86_64-unix/libMoltenVK.1.dylib)
            echo "must_match"
            ;;
        lib/wine/x86_64-windows/d3d11.dll|lib/wine/x86_64-windows/dxgi.dll|lib/wine/i386-windows/d3d11.dll|lib/wine/i386-windows/dxgi.dll)
            echo "expected_wine_builtin_delta"
            ;;
        lib/wine/i386-windows/winemetal.dll)
            echo "must_exist_for_m9_or_live_prove_unneeded"
            ;;
        *)
            echo "unknown_release_blocking"
            ;;
    esac
}

all_paths="$tmp/all-paths.txt"
{
    awk '{ print $2 }' "$baseline/critical-sha256.txt"
    awk '{ print $2 }' "$candidate/critical-sha256.txt"
} | LC_ALL=C sort -u > "$all_paths"

release_blockers=0
packaging_defects=0
unknowns=0

{
    echo "# MetalSharp Runtime Manifest Classified Diff"
    echo
    echo "- Baseline: $baseline"
    echo "- Candidate: $candidate"
    echo

    if [[ -f "$baseline/versions.txt" || -f "$candidate/versions.txt" ]]; then
        echo "## Versions"
        echo
        echo '```text'
        [[ -f "$baseline/versions.txt" ]] && sed 's/^/baseline: /' "$baseline/versions.txt"
        [[ -f "$candidate/versions.txt" ]] && sed 's/^/candidate: /' "$candidate/versions.txt"
        echo '```'
        echo
    fi

    echo "## Critical Path Classification"
    echo
    echo "| Path | Baseline | Candidate | Classification | Gate |"
    echo "| --- | --- | --- | --- | --- |"

    while IFS= read -r rel; do
        [[ -z "$rel" ]] && continue
        base_hash="$(extract_value "$baseline/critical-sha256.txt" "$rel")"
        cand_hash="$(extract_value "$candidate/critical-sha256.txt" "$rel")"
        [[ -z "$base_hash" ]] && base_hash="ABSENT"
        [[ -z "$cand_hash" ]] && cand_hash="ABSENT"

        class="$(classify_path "$rel")"
        gate="ok"

        if [[ "$cand_hash" == "MISSING" || "$cand_hash" == "ABSENT" ]]; then
            case "$class" in
                expected_anticheat_delta)
                    gate="ok_if_intentional_new_surface_absent"
                    ;;
                must_exist_for_m9_or_live_prove_unneeded)
                    gate="release_blocker"
                    release_blockers=$((release_blockers + 1))
                    ;;
                *)
                    gate="packaging_defect"
                    packaging_defects=$((packaging_defects + 1))
                    ;;
            esac
        elif [[ "$class" == "must_match" && "$base_hash" != "$cand_hash" ]]; then
            gate="release_blocker_unexpected_change"
            release_blockers=$((release_blockers + 1))
        elif [[ "$class" == "unknown_release_blocking" && "$base_hash" != "$cand_hash" ]]; then
            gate="release_blocker_unknown_delta"
            release_blockers=$((release_blockers + 1))
            unknowns=$((unknowns + 1))
        elif [[ "$class" == "must_exist_for_m9_or_live_prove_unneeded" && "$base_hash" != "$cand_hash" ]]; then
            gate="release_blocker_until_live_m9_proves_unneeded"
            release_blockers=$((release_blockers + 1))
        elif [[ "$class" == "must_exist_for_m9_or_live_prove_unneeded" && "$base_hash" == "$cand_hash" ]]; then
            gate="ok_manifest_only_still_requires_live_m9"
        fi

        printf '| `%s` | `%s` | `%s` | `%s` | `%s` |\n' "$rel" "$base_hash" "$cand_hash" "$class" "$gate"
    done < "$all_paths"

    echo
    echo "## Summary"
    echo
    echo "- release_blockers: $release_blockers"
    echo "- packaging_defects: $packaging_defects"
    echo "- unknown_release_blocking: $unknowns"
    if [[ "$release_blockers" -eq 0 && "$packaging_defects" -eq 0 && "$unknowns" -eq 0 ]]; then
        echo "- gate: pass"
    else
        echo "- gate: fail"
    fi
} > "$report"

echo "classified diff written: $report"
if [[ "$release_blockers" -ne 0 || "$packaging_defects" -ne 0 || "$unknowns" -ne 0 ]]; then
    echo "gate: fail"
    exit 2
fi

echo "gate: pass"
