#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/prepare-wine119-parity-candidates.sh <metalsharp_bundle.tar.zst> [work-dir]

Example:
  scripts/prepare-wine119-parity-candidates.sh \
    /tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst \
    /tmp/metalsharp-wine119-parity

Builds disposable Wine 11.9 parity candidates and manifest comparisons:

  clean    - release bundle only, with x86_64 WineMetal rebound into lib/wine
  dxmt32   - clean + explicit i386 WineMetal from METALSHARP_I386_WINEMETAL_SOURCE
  borrowed - clean + 11.5 baseline i386 WineMetal compatibility experiment

The script does not modify ~/.metalsharp. It reads the installed 11.5 runtime as
the baseline unless METALSHARP_BASELINE_WINE is set.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

bundle="${1:-}"
work_dir="${2:-/tmp/metalsharp-wine119-parity}"

if [[ -z "$bundle" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -f "$bundle" ]]; then
    echo "bundle not found: $bundle" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
baseline_wine="${METALSHARP_BASELINE_WINE:-$HOME/.metalsharp/runtime/wine}"
dxmt32_source="${METALSHARP_I386_WINEMETAL_SOURCE:-/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll}"

if [[ ! -d "$baseline_wine" ]]; then
    echo "baseline Wine runtime not found: $baseline_wine" >&2
    exit 1
fi

rm -rf "$work_dir"
mkdir -p "$work_dir"

summary="$work_dir/summary.md"
: > "$summary"

append_summary() {
    printf '%s\n' "$*" >> "$summary"
}

run_compare() {
    local candidate_name="$1"
    local prepare_env="$2"
    local candidate_root="$work_dir/candidates/$candidate_name/wine"
    local manifest_dir="$work_dir/manifests/11.9-$candidate_name"
    local report="$manifest_dir/classified-diff.md"
    local status_file="$work_dir/$candidate_name.status"

    mkdir -p "$(dirname "$candidate_root")" "$(dirname "$manifest_dir")"

    echo "== preparing $candidate_name =="
    if [[ -n "$prepare_env" ]]; then
        env METALSHARP_BASELINE_WINE="$baseline_wine" $prepare_env \
            "$repo_root/scripts/prepare-wine119-candidate.sh" "$bundle" "$candidate_root"
    else
        env METALSHARP_BASELINE_WINE="$baseline_wine" \
            "$repo_root/scripts/prepare-wine119-candidate.sh" "$bundle" "$candidate_root"
    fi

    "$repo_root/scripts/runtime-manifest.sh" "$candidate_root" "$manifest_dir"

    set +e
    "$repo_root/scripts/compare-runtime-manifests.sh" \
        "$work_dir/manifests/11.5-baseline" \
        "$manifest_dir" \
        "$report"
    local code=$?
    set -e

    printf '%s\n' "$code" > "$status_file"
    append_summary "## $candidate_name"
    append_summary
    append_summary "- candidate_root: \`$candidate_root\`"
    append_summary "- manifest: \`$manifest_dir\`"
    append_summary "- classified_diff: \`$report\`"
    append_summary "- compare_exit_code: \`$code\`"
    append_summary "- prepare_report: \`$work_dir/candidates/$candidate_name/prepare-report.txt\`"
    append_summary

    if [[ -f "$report" ]]; then
        sed -n '/## Summary/,$p' "$report" >> "$summary"
        append_summary
    fi
}

append_summary "# MetalSharp Wine 11.9 Parity Candidates"
append_summary
append_summary "- bundle: \`$bundle\`"
append_summary "- bundle_sha256: \`$(shasum -a 256 "$bundle" | awk '{ print $1 }')\`"
append_summary "- baseline_wine: \`$baseline_wine\`"
append_summary "- work_dir: \`$work_dir\`"
append_summary "- dxmt32_source: \`$dxmt32_source\`"
append_summary

"$repo_root/scripts/runtime-manifest.sh" "$baseline_wine" "$work_dir/manifests/11.5-baseline"

run_compare "clean" ""

if [[ -f "$dxmt32_source" ]]; then
    run_compare "dxmt32" "METALSHARP_I386_WINEMETAL_SOURCE=$dxmt32_source"
else
    append_summary "## dxmt32"
    append_summary
    append_summary "- skipped: i386 WineMetal source not found at \`$dxmt32_source\`"
    append_summary
fi

run_compare "borrowed" "METALSHARP_BORROW_BASELINE_I386_WINEMETAL=1"

append_summary "## Required Live Gates"
append_summary
append_summary "- Nidhogg 2 must pass M9 live process/module/cache proof."
append_summary "- Schedule I must pass M11 live process/module/cache proof."
append_summary "- Subnautica Below Zero must pass M11 live process/module/cache proof."
append_summary "- Manifest pass alone is not a release gate for any candidate."

echo "parity candidate summary: $summary"
cat "$summary"
