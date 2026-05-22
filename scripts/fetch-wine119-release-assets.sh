#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/fetch-wine119-release-assets.sh [output-dir] [asset ...]

Examples:
  scripts/fetch-wine119-release-assets.sh /tmp/metalsharp-wine-assets
  scripts/fetch-wine119-release-assets.sh /tmp/metalsharp-wine-assets metalsharp_bundle.tar.zst metalsharp_bundle2.tar.zst

Downloads Wine 11.9 release assets from the GitHub "bundles" release and
verifies each downloaded file against the asset digest reported by GitHub.

Defaults:
  repo: aaf2tbz/metalsharp
  tag:  bundles
  asset: metalsharp_bundle.tar.zst

Environment:
  METALSHARP_RELEASE_REPO  Override repo, default aaf2tbz/metalsharp
  METALSHARP_RELEASE_TAG   Override release tag, default bundles
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

repo="${METALSHARP_RELEASE_REPO:-aaf2tbz/metalsharp}"
tag="${METALSHARP_RELEASE_TAG:-bundles}"
out_dir="${1:-/tmp/metalsharp-wine-assets}"
shift || true

assets=("$@")
if (( ${#assets[@]} == 0 )); then
    assets=("metalsharp_bundle.tar.zst")
fi

if ! command -v gh >/dev/null 2>&1; then
    echo "gh is required to fetch release assets" >&2
    exit 1
fi

mkdir -p "$out_dir"
out_dir_abs="$(cd "$out_dir" && pwd -P)"
assets_json="$out_dir_abs/release-assets.json"
report="$out_dir_abs/fetch-report.txt"

gh release view "$tag" --repo "$repo" --json tagName,name,isDraft,isPrerelease,assets > "$assets_json"

: > "$report"
{
    echo "repo=$repo"
    echo "tag=$tag"
    gh release view "$tag" --repo "$repo" --json tagName,name,isDraft,isPrerelease \
        --jq '"release_tag_name=\(.tagName)\nrelease_name=\(.name)\nrelease_is_draft=\(.isDraft)\nrelease_is_prerelease=\(.isPrerelease)"'
    echo "output_dir=$out_dir_abs"
    echo "release_assets_json=$assets_json"
} >> "$report"

for asset in "${assets[@]}"; do
    expected_digest="$(
        ASSET="$asset" gh release view "$tag" --repo "$repo" --json assets \
            --jq '.assets[] | select(.name == env.ASSET) | .digest' 2>/dev/null || true
    )"
    expected_sha="${expected_digest#sha256:}"

    if [[ -z "$expected_digest" || "$expected_digest" == "$expected_sha" ]]; then
        echo "asset digest not found in release metadata: $asset" >&2
        exit 1
    fi

    gh release download "$tag" \
        --repo "$repo" \
        --pattern "$asset" \
        --dir "$out_dir_abs" \
        --clobber

    local_path="$out_dir_abs/$asset"
    if [[ ! -f "$local_path" ]]; then
        echo "downloaded asset missing: $local_path" >&2
        exit 1
    fi

    actual_sha="$(shasum -a 256 "$local_path" | awk '{ print $1 }')"
    if [[ "$actual_sha" != "$expected_sha" ]]; then
        echo "sha256 mismatch for $asset" >&2
        echo "expected: $expected_sha" >&2
        echo "actual:   $actual_sha" >&2
        exit 1
    fi

    {
        echo
        echo "asset=$asset"
        echo "path=$local_path"
        echo "sha256=$actual_sha"
        echo "verified=1"
    } >> "$report"
done

echo "release assets fetched and verified: $out_dir_abs"
echo "report: $report"
