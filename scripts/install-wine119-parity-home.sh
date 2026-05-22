#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  scripts/install-wine119-parity-home.sh <candidate-runtime-root> [parity-home]

Example:
  scripts/install-wine119-parity-home.sh \
    /tmp/metalsharp-wine119-parity/candidates/dxmt32/wine \
    /tmp/metalsharp-home-wine119-dxmt32

Installs a prepared Wine 11.9 candidate into an isolated HOME:

  <parity-home>/.metalsharp/runtime/wine

By default this only installs the runtime and writes launch/proof helper files.
It does not modify ~/.metalsharp and does not clone Steam prefixes or game state.

Optional:
  METALSHARP_CLONE_USER_STATE=1

Copies selected user state from ~/.metalsharp into the parity home so live Steam
game launches can be tested without mutating the working runtime. Copied dirs:
prefix-steam, compatdata, bottles, games, configs, shader-cache, pipeline-cache.
Active cloned manifests are rewritten from the source MetalSharp home path to
the parity MetalSharp home path before any live-control run should use them.

Copy mode:
  METALSHARP_COPY_MODE=auto   Prefer APFS clone-copy on macOS, fallback to copy
  METALSHARP_COPY_MODE=clone  Require APFS clone-copy with cp -cR
  METALSHARP_COPY_MODE=copy   Force ordinary recursive copy

Safety:
  Parity homes must live under /tmp or /private/tmp by default. Set
  METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1 only for an intentional alternate
  scratch location. The installer refuses to target the real HOME/.metalsharp.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

candidate_root="${1:-}"
parity_home="${2:-/tmp/metalsharp-home-wine119-parity}"

if [[ -z "$candidate_root" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -d "$candidate_root" ]]; then
    echo "candidate runtime root not found: $candidate_root" >&2
    exit 1
fi

if [[ ! -x "$candidate_root/bin/wine" ]]; then
    echo "candidate runtime missing executable bin/wine: $candidate_root" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
source_ms_home="${METALSHARP_SOURCE_HOME:-$HOME/.metalsharp}"
if [[ -d "$source_ms_home" ]]; then
    source_ms_home="$(cd "$source_ms_home" && pwd -P)"
fi
parity_home_abs="$(mkdir -p "$parity_home" && cd "$parity_home" && pwd -P)"
parity_ms_home="$parity_home_abs/.metalsharp"
real_home_abs="$(cd "$HOME" && pwd -P)"
runtime_dest="$parity_ms_home/runtime/wine"
report="$parity_home_abs/parity-install-report.txt"
state_sizes="$parity_home_abs/state-sizes.txt"
copy_log="$parity_home_abs/copy.log"
rewrite_log="$parity_home_abs/state-path-rewrite.log"
copy_mode="${METALSHARP_COPY_MODE:-auto}"

if [[ "$parity_home_abs" == "$real_home_abs" ]]; then
    echo "refusing parity install into real HOME: $parity_home_abs" >&2
    exit 1
fi

if [[ "$parity_ms_home" == "$source_ms_home" ]]; then
    echo "refusing parity install into source MetalSharp home: $parity_ms_home" >&2
    exit 1
fi

if [[ "${METALSHARP_ALLOW_NON_TMP_PARITY_HOME:-0}" != "1" ]]; then
    case "$parity_home_abs" in
        /tmp/*|/private/tmp/*) ;;
        *)
            echo "refusing non-tmp parity home: $parity_home_abs" >&2
            echo "set METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1 for an intentional scratch location" >&2
            exit 1
            ;;
    esac
fi

copy_method="copy"
if [[ "$copy_mode" == "clone" ]]; then
    if ! [[ "$(uname -s)" == "Darwin" ]]; then
        echo "METALSHARP_COPY_MODE=clone requires macOS cp -cR" >&2
        exit 1
    fi
    copy_method="clone"
elif [[ "$copy_mode" == "auto" && "$(uname -s)" == "Darwin" ]]; then
    copy_method="clone"
elif [[ "$copy_mode" == "copy" ]]; then
    copy_method="copy"
else
    echo "unsupported METALSHARP_COPY_MODE: $copy_mode" >&2
    exit 1
fi

copy_dir() {
    local src="$1"
    local dst="$2"
    if [[ ! -e "$src" ]]; then
        return 0
    fi

    rm -rf "$dst"
    mkdir -p "$(dirname "$dst")"
    echo "copy $src -> $dst via $copy_method" >> "$copy_log"
    if [[ "$copy_method" == "clone" ]]; then
        if cp -cR "$src" "$dst" >> "$copy_log" 2>&1; then
            return 0
        fi
        if [[ "$copy_mode" == "clone" ]]; then
            echo "clone-copy failed for $src -> $dst; see $copy_log" >&2
            exit 1
        fi
        echo "clone-copy failed; falling back to ordinary copy for $src" >> "$copy_log"
    fi

    if command -v ditto >/dev/null 2>&1; then
        ditto "$src" "$dst"
    else
        cp -R "$src" "$dst"
    fi
}

write_size_line() {
    local label="$1"
    local path="$2"
    if [[ -e "$path" ]]; then
        printf '%s\t%s\t%s\n' "$label" "$(du -sh "$path" | awk '{ print $1 }')" "$path" >> "$state_sizes"
    else
        printf '%s\tmissing\t%s\n' "$label" "$path" >> "$state_sizes"
    fi
}

prune_copied_launch_logs() {
    if [[ -d "$parity_ms_home/compatdata" ]]; then
        find "$parity_ms_home/compatdata" -type d -name logs -prune -print -exec rm -rf {} + >> "$copy_log" 2>&1 || true
    fi
}

active_state_files() {
    local roots=()
    local rel
    for rel in bottles compatdata configs; do
        if [[ -d "$parity_ms_home/$rel" ]]; then
            roots+=("$parity_ms_home/$rel")
        fi
    done

    if (( ${#roots[@]} == 0 )); then
        return 0
    fi

    find "${roots[@]}" -type f \( \
        -name '*.json' -o \
        -name '*.toml' -o \
        -name '*.conf' -o \
        -name '*.env' \
    \) -print0
}

count_stale_active_state_refs() {
    local count=0
    local file
    while IFS= read -r -d '' file; do
        if grep -Fq -- "$source_ms_home" "$file"; then
            printf '%s\n' "$file" >> "$rewrite_log"
            count=$((count + 1))
        fi
    done < <(active_state_files)
    printf '%s\n' "$count"
}

rewrite_cloned_state_paths() {
    if [[ "$source_ms_home" == "$parity_ms_home" ]]; then
        echo "source and parity MetalSharp homes are identical; no rewrite needed" >> "$rewrite_log"
        return 0
    fi

    local before
    local after
    local file

    echo "source_metalsharp_home=$source_ms_home" >> "$rewrite_log"
    echo "parity_metalsharp_home=$parity_ms_home" >> "$rewrite_log"
    echo "[stale_active_files_before]" >> "$rewrite_log"
    before="$(count_stale_active_state_refs)"

    if [[ "$before" != "0" ]]; then
        while IFS= read -r -d '' file; do
            SOURCE_METALSHARP_HOME="$source_ms_home" \
            PARITY_METALSHARP_HOME="$parity_ms_home" \
            perl -0pi -e 's/\Q$ENV{SOURCE_METALSHARP_HOME}\E/$ENV{PARITY_METALSHARP_HOME}/g' "$file"
        done < <(active_state_files)
    fi

    echo "[stale_active_files_after]" >> "$rewrite_log"
    after="$(count_stale_active_state_refs)"
    echo "stale_active_file_count_before=$before" >> "$rewrite_log"
    echo "stale_active_file_count_after=$after" >> "$rewrite_log"

    if [[ "$after" != "0" ]]; then
        echo "active cloned state still references $source_ms_home; see $rewrite_log" >&2
        exit 1
    fi
}

mkdir -p "$parity_ms_home/runtime"
: > "$copy_log"
: > "$state_sizes"
: > "$rewrite_log"
write_size_line "source.runtime_candidate" "$candidate_root"
copy_dir "$candidate_root" "$runtime_dest"
write_size_line "dest.runtime" "$runtime_dest"

if [[ "${METALSHARP_CLONE_USER_STATE:-0}" == "1" ]]; then
    for rel in prefix-steam compatdata bottles games configs shader-cache pipeline-cache; do
        write_size_line "source.$rel" "$source_ms_home/$rel"
        copy_dir "$source_ms_home/$rel" "$parity_ms_home/$rel"
        write_size_line "dest.$rel" "$parity_ms_home/$rel"
    done
    prune_copied_launch_logs
    rewrite_cloned_state_paths
fi

"$repo_root/scripts/runtime-manifest.sh" "$runtime_dest" "$parity_home_abs/runtime-manifest"

cat > "$parity_home_abs/activate-parity-env.sh" <<EOF
#!/usr/bin/env bash
export HOME="$parity_home_abs"
export METALSHARP_HOME="$parity_ms_home"
export METALSHARP_PARITY_RUNTIME="$runtime_dest"
export METALSHARP_PARITY_SOURCE_HOME="$source_ms_home"
EOF
chmod +x "$parity_home_abs/activate-parity-env.sh"

cat > "$parity_home_abs/proof-commands.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export HOME="$parity_home_abs"
export METALSHARP_HOME="$parity_ms_home"
export METALSHARP_REQUIRE_PARITY_HOME=1
export METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1
export METALSHARP_WINE119_CANDIDATE_WORK_DIR="$(cd "$(dirname "$(dirname "$(dirname "$candidate_root")")")" && pwd -P)"

# Full release-gate capture. Intentionally guarded: this launches real games.
# METALSHARP_RUN_LIVE_GAMES=1 \\
# METALSHARP_REQUIRE_PREEXISTING_WINE_STEAM=1 \\
# "$repo_root/scripts/run-wine119-live-control-suite.sh" \\
#   "$parity_home_abs" \\
#   "$parity_home_abs/live-controls-clean-dxmt32"

"$repo_root/scripts/capture-steam-game-proof.sh" 535520 Nidhogg_2 "$parity_home_abs/proof-nidhogg2"
"$repo_root/scripts/capture-steam-game-proof.sh" 3164500 "Schedule I" "$parity_home_abs/proof-schedule-i"
"$repo_root/scripts/capture-steam-game-proof.sh" 848450 SubnauticaZero "$parity_home_abs/proof-subnautica-bz"
EOF
chmod +x "$parity_home_abs/proof-commands.sh"

{
    echo "candidate_root=$candidate_root"
    echo "parity_home=$parity_home_abs"
    echo "parity_metalsharp_home=$parity_ms_home"
    echo "runtime_dest=$runtime_dest"
    echo "source_metalsharp_home=$source_ms_home"
    echo "cloned_user_state=${METALSHARP_CLONE_USER_STATE:-0}"
    echo "copy_mode=$copy_mode"
    echo "copy_method=$copy_method"
    echo "copy_log=$copy_log"
    echo "state_sizes=$state_sizes"
    echo "state_path_rewrite_log=$rewrite_log"
    echo "activate_env=$parity_home_abs/activate-parity-env.sh"
    echo "proof_commands=$parity_home_abs/proof-commands.sh"
    echo "runtime_manifest=$parity_home_abs/runtime-manifest"
    if [[ -x "$runtime_dest/bin/wine" ]]; then
        printf "wine_version="
        "$runtime_dest/bin/wine" --version 2>&1 || true
    fi
    if [[ -x "$runtime_dest/bin/wineserver" ]]; then
        printf "wineserver_version="
        "$runtime_dest/bin/wineserver" --version 2>&1 || true
    fi
} > "$report"

echo "parity home installed: $parity_home_abs"
echo "report: $report"
echo "activate: source $parity_home_abs/activate-parity-env.sh"
echo "proof commands: $parity_home_abs/proof-commands.sh"
