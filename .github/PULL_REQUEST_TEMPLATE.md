## Summary

<!-- Brief description of what this PR does and why -->

## Changes

<!-- List the key changes -->

-

## Checklist

> Run locally before pushing. All of these also run automatically in CI.

- [ ] `cargo fmt --all -- --check` passes (Rust)
- [ ] `cargo clippy --all-targets -- -D warnings` passes (Rust)
- [ ] `cargo build --release` passes (Rust backend)
- [ ] `cargo test` passes (Rust — 628+ tests)
- [ ] C++ compiles if changed: `cmake -B build-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON && cmake --build build-native --parallel $(sysctl -n hw.ncpu)`
- [ ] `clang-format --dry-run --Werror` passes on any changed C/C++/Obj-C file
- [ ] `ctest --test-dir build-native` passes if tests changed
- [ ] TypeScript compiles if changed: `cd app && npx tsc --noEmit`
- [ ] Biome + Prettier pass if TS/JS changed: `cd app && npx @biomejs/biome ci src/ && npx prettier --check 'src/**/*.{ts,js,html,css,json}'`
- [ ] Shell scripts lint if changed: `tools/ci/shellcheck.sh`
- [ ] `tools/ci/validate-rules-toml.py` passes if `configs/mtsp-rules.toml` changed
- [ ] `python3 tools/ci/verify-dmg-workflow.py` passes if release/bundle tooling changed
- [ ] Tested with at least one game (which one? which launch method? which drive?)
- [ ] No hardcoded paths, secrets, or absolute `/Users/...` paths
- [ ] No new files should be added to the repo root; place them under `app/`, `tools/`, `tests/`, etc.

## Test notes

<!-- What did you test? Which game, which launch method, which drive? -->

## Risk

<!-- If this PR ships broken defaults or changes launch behavior, what's the rollback plan? -->
