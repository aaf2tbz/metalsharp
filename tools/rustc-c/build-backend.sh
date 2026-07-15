#!/usr/bin/env bash
# Build MetalSharp's shipped backend through rustc_codegen_c once per release.
# Rust remains build input only; the executable and its generated C sources are
# produced by clang and retain the historical metalsharp-backend path/name.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_root="${METALSHARP_RUSTC_C_CACHE:-$repo_root/.cache/rustc-c}"
deployment_target="${MACOSX_DEPLOYMENT_TARGET:-13.0}"
codegen_rev="6731c737826a04162acc5027089d2b54a321766c"
rust_ref="rustic-1.94.1"

[[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]] || {
  echo 'rustc-c backend builds require an Apple Silicon macOS host.' >&2; exit 2;
}

developer_dir="${DEVELOPER_DIR:-$(xcode-select -p)}"
export DEVELOPER_DIR="$developer_dir"
export SDKROOT="$(xcrun --show-sdk-path)"
export CC="$(xcrun --find clang)"
export CXX="$(xcrun --find clang++)"
export MACOSX_DEPLOYMENT_TARGET="$deployment_target"

codegen="$cache_root/codegen"
rust="$cache_root/rust"
mkdir -p "$cache_root"
if [[ ! -d "$codegen/.git" ]]; then git clone https://github.com/rustic-compiler/rustc_codegen_c "$codegen"; fi
if [[ ! -d "$rust/.git" ]]; then git clone --branch "$rust_ref" https://github.com/rustic-compiler/rust "$rust"; fi
git -C "$codegen" fetch --quiet origin "$codegen_rev"
git -C "$codegen" checkout --detach --force --quiet "$codegen_rev"
git -C "$rust" fetch --quiet origin "$rust_ref"
git -C "$rust" checkout --detach --force --quiet "$rust_ref"
if git -C "$codegen" apply --check "$repo_root/tools/rustc-c/patches/rustc_codegen_c-macos.patch"; then
  git -C "$codegen" apply "$repo_root/tools/rustc-c/patches/rustc_codegen_c-macos.patch"
fi
if git -C "$rust" apply --check "$repo_root/tools/rustc-c/patches/rust-macos.patch"; then
  git -C "$rust" apply "$repo_root/tools/rustc-c/patches/rust-macos.patch"
fi
ln -sfn ../../codegen "$rust/compiler/rustc_codegen_c"
cp "$repo_root/tools/rustc-c/bootstrap.toml" "$rust/bootstrap.toml"

export PATH="$repo_root/tools/rustc-c:$PATH"
(cd "$rust" && ./compiler/rustc_codegen_c/y build --stage=1 compiler)

# Xcode 27 SDK headers advertise these suffixed symbols although they are not
# available at our macOS 13 floor. This is an input-toolchain workaround, not
# a MetalSharp source change. The compiler build above fetches the pinned libc.
patched_libc=false
for libc in "$HOME"/.cargo/registry/src/*/libc-0.2.178/src/unix/mod.rs; do
  [[ -f "$libc" ]] || continue
  perl -0pi -e 's/link_name = "realpath\$DARWIN_EXTSN"/link_name = "realpath"/g; s/link_name = "syslog\$DARWIN_EXTSN"/link_name = "syslog"/g' "$libc"
  grep -q 'link_name = "realpath"' "$libc"
  patched_libc=true
done
"$patched_libc" || { echo 'pinned Rust libc source was not fetched by bootstrap.' >&2; exit 1; }
(cd "$rust" && ./compiler/rustc_codegen_c/y build --stage=1 library)

generated="$repo_root/app/src-c/generated"
target="$repo_root/app/src-rust/target"
rm -rf "$generated"
mkdir -p "$generated"
RUSTC="$rust/build/aarch64-apple-darwin/stage1/bin/rustc" \
  RUSTFLAGS='-C save-temps' RUSTC_CSOURCES_DIR="$generated" CARGO_TARGET_DIR="$target" \
  cargo build --manifest-path "$repo_root/app/src-rust/Cargo.toml" --release

backend="$target/release/metalsharp-backend"
test -x "$backend"
test "$(lipo -archs "$backend")" = arm64
file "$backend" | grep -q 'Mach-O 64-bit executable arm64'
find "$generated" -type f -name '*.c' -print | sort | shasum -a 256 > "$generated/SHA256SUMS"
printf 'compiler=rustic-compiler/rustc_codegen_c@%s\nrust=%s\nmacos_deployment_target=%s\n' "$codegen_rev" "$rust_ref" "$deployment_target" > "$generated/PROVENANCE.txt"
echo "C backend ready: $backend"
