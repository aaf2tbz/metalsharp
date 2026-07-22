#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: tools/wine/verify-ubisoft-ntdll.sh CANDIDATE [BASELINE]" >&2
  exit 2
fi

CANDIDATE="$1"
BASELINE="${2:-$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so}"

[ -s "$CANDIDATE" ] || { echo "candidate missing or empty: $CANDIDATE" >&2; exit 1; }
[ -s "$BASELINE" ] || { echo "baseline missing or empty: $BASELINE" >&2; exit 1; }

for tool in codesign file lipo nm otool shasum strings; do
  command -v "$tool" >/dev/null || { echo "required tool missing: $tool" >&2; exit 1; }
done

archs="$(lipo -archs "$CANDIDATE")"
[ "$archs" = "x86_64" ] || { echo "candidate must be thin x86_64, got: $archs" >&2; exit 1; }
codesign --verify --strict "$CANDIDATE"

grep -aFq 'wine-11.5' "$BASELINE" || { echo "baseline is not Wine 11.5" >&2; exit 1; }
for marker in 'wine-11.5' 'WINE_SIMULATE_WRITECOPY' 'UplayWebCore.exe' 'MS_ROOT' 'mscompatdb.so'; do
  grep -aFq "$marker" "$CANDIDATE" || { echo "candidate marker missing: $marker" >&2; exit 1; }
done

if nm -u "$CANDIDATE" | grep -q ' _pipe2$'; then
  echo "candidate unexpectedly imports macOS 27-only pipe2" >&2
  exit 1
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/metalsharp-ntdll-verify.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
nm -gjU "$BASELINE" | sort > "$tmp/baseline.exports"
nm -gjU "$CANDIDATE" | sort > "$tmp/candidate.exports"
if ! cmp -s "$tmp/baseline.exports" "$tmp/candidate.exports"; then
  echo "candidate exported-symbol set differs from installed baseline" >&2
  diff -u "$tmp/baseline.exports" "$tmp/candidate.exports" >&2 || true
  exit 1
fi

otool -L "$BASELINE" | tail -n +2 | awk '{print $1}' > "$tmp/baseline.dependencies"
otool -L "$CANDIDATE" | tail -n +2 | awk '{print $1}' > "$tmp/candidate.dependencies"
if ! cmp -s "$tmp/baseline.dependencies" "$tmp/candidate.dependencies"; then
  echo "candidate dependency identities differ from installed baseline" >&2
  diff -u "$tmp/baseline.dependencies" "$tmp/candidate.dependencies" >&2 || true
  exit 1
fi

candidate_id="$(otool -D "$CANDIDATE" | tail -1)"
baseline_id="$(otool -D "$BASELINE" | tail -1)"
[ "$candidate_id" = "$baseline_id" ] || {
  echo "candidate install name differs: $candidate_id != $baseline_id" >&2
  exit 1
}

candidate_rpaths="$(otool -l "$CANDIDATE" | awk '/cmd LC_RPATH/{getline; getline; print $2}')"
baseline_rpaths="$(otool -l "$BASELINE" | awk '/cmd LC_RPATH/{getline; getline; print $2}')"
[ "$candidate_rpaths" = "$baseline_rpaths" ] || {
  echo "candidate rpaths differ from baseline" >&2
  exit 1
}

candidate_minos="$(otool -l "$CANDIDATE" | awk '/cmd LC_BUILD_VERSION/{found=1} found && $1=="minos"{print $2; exit}')"
baseline_minos="$(otool -l "$BASELINE" | awk '/cmd LC_BUILD_VERSION/{found=1} found && $1=="minos"{print $2; exit}')"
[ "$candidate_minos" = "$baseline_minos" ] || {
  echo "candidate deployment target differs: $candidate_minos != $baseline_minos" >&2
  exit 1
}

printf 'candidate\t%s\n' "$(shasum -a 256 "$CANDIDATE" | awk '{print $1}')"
printf 'baseline\t%s\n' "$(shasum -a 256 "$BASELINE" | awk '{print $1}')"
printf 'architecture\t%s\n' "$archs"
printf 'exports\t%s\n' "$(wc -l < "$tmp/candidate.exports" | tr -d ' ')"
printf 'minos\t%s\n' "$candidate_minos"
printf 'install_name\t%s\n' "$candidate_id"
echo "Ubisoft ntdll verification passed"
