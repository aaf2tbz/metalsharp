#!/usr/bin/env bash
# Convert a captured rustc_codegen_c final-link invocation into an auditable
# C-source manifest. This is a generation-time tool only; CI consumes the
# checked-in manifest and never calls Cargo or rustc.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <cargo-target-dir> <final-link-log>" >&2
  exit 2
fi

target="$(cd "$1" && pwd)"
link_log="$2"
out="$target/c-runtime-manifest"
mkdir -p "$out"

# The final executable is the only link invocation that contains the backend
# crate. Preserve its arguments exactly, one per line, for review/replay.
awk '
  /__METALSHARP_LINK_BEGIN__/ { inside=1; block=""; next }
  /__METALSHARP_LINK_END__/ {
    if (block ~ /metalsharp_backend-/) last=block
    inside=0; next
  }
  inside { block=block $0 "\n" }
  END { printf "%s", last }
' "$link_log" > "$out/runtime-link-arguments.txt"

test -s "$out/runtime-link-arguments.txt"

# Every Rust rlib member ending rcgu.o has a same-named generated C unit in
# release/deps. Native members (SQLite/zstd) are explicitly recorded for the
# separate C vendoring step rather than silently shipping opaque objects.
rg '/lib[^/]+\.rlib$' "$out/runtime-link-arguments.txt" \
  | sed 's#^/private##' \
  | while IFS= read -r rlib; do
      [[ -f "$rlib" ]] || continue
      ar -t "$rlib" | while IFS= read -r member; do
        case "$member" in
          *.rcgu.o)
            candidate="$target/release/deps/${member%.o}.c"
            if [[ -f "$candidate" ]]; then
              printf '%s\n' "$candidate"
            else
              printf 'missing-c-source\t%s\t%s\n' "$rlib" "$member" >&2
              exit 1
            fi
            ;;
          *.o)
            printf '%s\t%s\n' "$rlib" "$member" >> "$out/native-object-members.tsv"
            ;;
        esac
      done
    done \
  | sort -u > "$out/runtime-c-sources.txt"

# Backend objects are passed directly, not inside an rlib.
rg 'metalsharp_backend-.*\.rcgu\.o$' "$out/runtime-link-arguments.txt" \
  | sed 's#^/private##; s#\.o$#.c#' \
  | while IFS= read -r candidate; do
      test -f "$candidate"
      printf '%s\n' "$candidate"
    done \
  | sort -u >> "$out/runtime-c-sources.txt"
sort -u -o "$out/runtime-c-sources.txt" "$out/runtime-c-sources.txt"

printf 'runtime_c_sources=%s\n' "$(wc -l < "$out/runtime-c-sources.txt" | tr -d ' ')" \
  > "$out/summary.txt"
printf 'native_object_members=%s\n' "$(wc -l < "$out/native-object-members.tsv" | tr -d ' ')" \
  >> "$out/summary.txt"
