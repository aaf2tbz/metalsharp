#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/audit-i386-winemetal-provenance.sh <dxmt-source-root> <winemetal.dll> [output-dir]

Example:
  scripts/audit-i386-winemetal-provenance.sh \
    /Volumes/AverySSD/metalsharp/dxmt-src \
    /Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll \
    /tmp/metalsharp-i386-winemetal-provenance

Captures read-only provenance for a candidate i386 winemetal.dll. This does not
bless the artifact for release; it records whether the source tree is clean, the
exact git/build identity, PE architecture/import/export evidence, and whether
the build metadata links against Wine 11.5 or Wine 11.9 inputs.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

src_root="${1:-}"
artifact="${2:-}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${3:-/tmp/metalsharp-i386-winemetal-provenance-$timestamp}"

if [[ -z "$src_root" || -z "$artifact" ]]; then
    usage >&2
    exit 1
fi
if ! git -C "$src_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "DXMT source root is not a git checkout: $src_root" >&2
    exit 1
fi
if [[ ! -f "$artifact" ]]; then
    echo "artifact not found: $artifact" >&2
    exit 1
fi

mkdir -p "$out_dir"
report="$out_dir/provenance.md"

src_abs="$(cd "$src_root" && pwd -P)"
artifact_dir="$(cd "$(dirname "$artifact")" && pwd -P)"
artifact_abs="$artifact_dir/$(basename "$artifact")"
build_root="$src_abs/build32"
search_dir="$artifact_dir"
while [[ "$search_dir" != "/" ]]; do
    if [[ -d "$search_dir/meson-info" ]]; then
        build_root="$search_dir"
        break
    fi
    search_dir="$(dirname "$search_dir")"
done
target_info="$build_root/meson-info/intro-targets.json"
machines_info="$build_root/meson-info/intro-machines.json"
compilers_info="$build_root/meson-info/intro-compilers.json"
buildoptions_info="$build_root/meson-info/intro-buildoptions.json"

git_head="$(git -C "$src_abs" rev-parse HEAD)"
git_branch="$(git -C "$src_abs" branch --show-current 2>/dev/null || true)"
git_dirty="no"
if [[ -n "$(git -C "$src_abs" status --porcelain)" ]]; then
    git_dirty="yes"
fi
artifact_sha="$(shasum -a 256 "$artifact_abs" | awk '{ print $1 }')"
artifact_file="$(file "$artifact_abs")"

git -C "$src_abs" status --short --branch > "$out_dir/git-status.txt"
git -C "$src_abs" diff --stat > "$out_dir/git-diff-stat.txt"
git -C "$src_abs" diff -- src/winemetal/main.c src/winemetal/unix/winemetal_unix.c src/winemetal/wineunixlib.h > "$out_dir/winemetal-source-diff.patch" || true
objdump -p "$artifact_abs" > "$out_dir/objdump-p.txt" 2>&1 || true
file "$artifact_abs" > "$out_dir/file.txt"
shasum -a 256 "$artifact_abs" > "$out_dir/sha256.txt"

if [[ -f "$target_info" ]] && command -v jq >/dev/null 2>&1; then
    jq '.[] | select(.filename[]? | contains("winemetal.dll"))' "$target_info" > "$out_dir/meson-winemetal-target.json"
else
    : > "$out_dir/meson-winemetal-target.json"
fi

if [[ -f "$machines_info" ]]; then
    cp "$machines_info" "$out_dir/intro-machines.json"
fi
if [[ -f "$compilers_info" ]]; then
    cp "$compilers_info" "$out_dir/intro-compilers.json"
fi
if [[ -f "$buildoptions_info" ]]; then
    cp "$buildoptions_info" "$out_dir/intro-buildoptions.json"
fi

linker_wine_refs="$out_dir/linker-wine-refs.txt"
if [[ -s "$out_dir/meson-winemetal-target.json" ]]; then
    rg -o '/[^" ]*(wine-[0-9][^" ]*|metalsharp-wine119-parity[^" ]*)' "$out_dir/meson-winemetal-target.json" > "$linker_wine_refs" || true
else
    : > "$linker_wine_refs"
fi

{
    echo "# i386 WineMetal Provenance Audit"
    echo
    echo "- captured_at_utc: \`$timestamp\`"
    echo "- dxmt_source_root: \`$src_abs\`"
    echo "- artifact: \`$artifact_abs\`"
    echo "- artifact_sha256: \`$artifact_sha\`"
    echo "- artifact_file: \`$artifact_file\`"
    echo "- git_branch: \`${git_branch:-unknown}\`"
    echo "- git_head: \`$git_head\`"
    echo "- git_dirty: \`$git_dirty\`"
    echo
    echo "## Gate"
    echo
    if [[ "$git_dirty" == "yes" ]]; then
        echo "- result: fail"
        echo "- reason: source checkout has uncommitted changes, so this artifact is not release-proven."
    elif rg -q 'wine-11[.]5' "$linker_wine_refs"; then
        echo "- result: fail"
        echo "- reason: linker/build metadata references Wine 11.5 inputs, so this is not a 11.9-built i386 WineMetal."
    elif rg -q 'metalsharp-wine119-parity|wine-11[.]9' "$linker_wine_refs"; then
        echo "- result: review"
        echo "- reason: source tree is clean and linker metadata references the shaped Wine 11.9 runtime; still requires live M9 proof before release."
    else
        echo "- result: review"
        echo "- reason: source tree is clean and no Wine 11.5 linker refs were detected, but Wine 11.9 linker refs were not explicit; still requires build log review and live M9 proof before release."
    fi
    echo
    echo "## Evidence Files"
    echo
    echo "- git status: \`$out_dir/git-status.txt\`"
    echo "- git diff stat: \`$out_dir/git-diff-stat.txt\`"
    echo "- winemetal source diff: \`$out_dir/winemetal-source-diff.patch\`"
    echo "- file: \`$out_dir/file.txt\`"
    echo "- objdump: \`$out_dir/objdump-p.txt\`"
    echo "- meson target: \`$out_dir/meson-winemetal-target.json\`"
    echo "- linker Wine refs: \`$linker_wine_refs\`"
    echo
    echo "## Linker Wine References"
    echo
    if [[ -s "$linker_wine_refs" ]]; then
        sed 's/^/- `/' "$linker_wine_refs" | sed 's/$/`/'
    else
        echo "- none detected"
    fi
} > "$report"

echo "i386 WineMetal provenance audit written: $report"
if rg -q '^- result: fail$' "$report"; then
    exit 2
fi
