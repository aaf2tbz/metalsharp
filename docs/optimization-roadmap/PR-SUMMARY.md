# Phased Optimization Roadmap — Draft PR Summary

Branch: `codex/phased-optimization-roadmap`
Base: `main` (3f2f5349, v0.46.8)
Roadmap: `MetalSharp-Phased-Optimization-Roadmap.md`

This draft PR implements the full 9-phase MetalSharp optimization roadmap as
one body of work. Each phase is a separate commit with its own proof gate
(`cargo fmt --check`, `cargo clippy -D warnings`, `cargo test` all green) and
keeps M9/M10/M11 launch behavior and artifact paths untouched.

Baseline before any work: **502 tests passed, 0 failed.**

## Phase 1: Baseline Observability ✅

**Purpose:** make future optimization measurable before changing behavior.

**What landed:**
- New `app/src-rust/src/diagnostics.rs` module — a single stable launch
  diagnostic surface (`schema_version: 1`) that reports:
  - `metalsharp_version`, generated timestamp, appid
  - resolved `pipeline`, `pipeline_name`, `backend`, `graphics_backend`
  - `runtime_profile`
  - `wine_binary_path` + existence, `wine_library_env`
  - `prefix_path` + existence, `game_install_path`
  - `bundle_hash` (best-effort, `null` when absent rather than fabricated)
  - `artifact_sources[]` — every pipeline deploy DLL with resolved source
    path, presence, sha256, size, and optional-stub flag
  - `staged_dll_hashes[]` — current sha256 of each staged destination read
    from `<game_dir>/.metalsharp/injections.json`, plus the recorded source
    hash and a `matches_source` boolean
  - `cache_directories[]` — shader cache roots per pipeline (M9/M10/M11 share
    the `dxmt-metal` family; M12/M13 use the isolated `dxmt-metal12` family)
- Missing required runtime artifacts produce a **structured failure**
  (`ok: false`, `missing_artifacts[]`, explanatory `error`) instead of a
  silent fallback. Optional stubs (nvapi/nvngx/atidxx) are tolerated.
- `LaunchTiming` checkpoint recorder threaded through `prepare_pipeline`
  (pipeline resolution, recipe build, runtime validation, legacy-injection
  cleanup, DLL staging, prefix-route deploy, complete) and persisted
  atomically to `<bottle>/logs/launch-timing-latest.json`.
- Scan timing for Steam library refresh and full library scan, persisted to
  `~/.metalsharp/logs/scan-timing-<kind>-latest.json`.
- `deploy_recipe_dlls` now records `sha256`, `source_sha256`, and
  `matches_source` per staged DLL in `injections.json`.
- Two new HTTP routes (no behavior change to existing routes):
  - `GET /diagnostics/launch?appid=&pipeline=` → live diagnostic JSON
  - `GET /diagnostics/launch/timing?appid=` → latest persisted launch timing

**New tests (11):** known appid diagnostic fields, structured failure on
missing artifacts, sha256 known-vector, sha256-of-missing, timing checkpoint
ordering, timing monotonicity, shader cache family isolation (M11 shares
legacy, M12 isolated), timing round-trip, scan-timing round-trip, staged DLL
hash matching source.

**Proof:**
```
cargo fmt --all -- --check        # clean
cargo clippy --all-targets -- -D warnings   # clean
cargo test                        # 513 passed, 0 failed
```

**Boundary check:** no M9/M10/M11 artifact path or launch behavior changed.
The only additions are diagnostic outputs, timing instrumentation, and
content hashes in the existing injection manifest.

## Phase 2: Runtime and Bottle Contract Hardening ✅

**Purpose:** make saved bottle and Steam route state hard to regress.

**What landed:**
- Declarative Steam route contract (`bottles::SteamRouteContract`) codifying,
  per pipeline: pipeline id, runtime profile, steam identity mode
  (`wine_steam_background` vs `offline_steam_emulation`), launch route
  (`/steam/launch-game` vs `/steam/launch-offline`), `requires_wine`, shared
  Steam prefix binding, prefix-idle wait policy, compat tool name, and the
  appid-scoped bottle id template. The contract is derived from the same
  primitives the runtime uses (`steam_pipeline_defaults_offline`,
  `runtime_profile_for_pipeline`, `pipeline_preference_id`, pipeline node's
  `requires_wine`) so it cannot drift from launch behavior.
- `steam_route_contract_for(pipeline)` and `steam_route_contracts()` table
  covering M9, M10, M11, M12, M13, FnaArm64, WineBare, D3DMetal.
- Passive-refresh preservation tests for M11 and M12 (the M9 case already
  existed): a saved M11 route survives a passive refresh that would resolve
  to M12, and a saved M12 route survives passive fallback to M11/M9.
- A data-driven route-contract test that builds a bottle per contract lane
  and asserts `steam_compatdata_record` matches the contract (appid scoping,
  bottle id, launch pipeline, identity mode, compat tool, launch route).
- `deploy_steam_appid` staging test proving `steam_appid.txt` lands in every
  standard binary subdir and that an active Goldberg `force_steam_appid.txt`
  is kept in sync.
- Migration preserve/skip report: `migrate::MigrationReport` records every
  preserved and skipped category with a reason during `preserve_user_data`
  and `restore_user_data`, persisted atomically to
  `~/.metalsharp/logs/migration-report-latest.json`. This does not change
  what is preserved — it only makes the behavior inspectable.
- New HTTP routes:
  - `GET /bottles/route-contracts` — the declarative contract table
  - `GET /update/migrate/report` — the latest migration preserve/skip report
- No test mutates the process-global `METALSHARP_HOME`; all new diagnostics
  and migration tests use explicit-home (`_for`) variants so they are safe
  under parallel test execution.

**New tests (8):** M11 passive preservation, M12 passive preservation,
route-contract table vs compatdata records, route-contract lane coverage,
M12 isolated-lane contract, D3DMetal offline contract, `deploy_steam_appid`
staging, migration preserve/skip report round-trip.

**Proof:**
```
cargo fmt --all -- --check        # clean
cargo clippy --all-targets -- -D warnings   # clean
cargo test bottles::tests         # 67 passed
cargo test mtsp                   # 111 passed
cargo test                        # 521 passed, 0 failed (3 consecutive runs)
```

**Boundary check:** M9/M10/M11 launch behavior and artifact paths unchanged.
The route contract is derived from existing primitives, not a new source of
truth. Migration preserve/restore logic is unchanged; only an observational
report was added.
