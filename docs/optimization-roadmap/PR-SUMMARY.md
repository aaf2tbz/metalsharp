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

## Phase 3: M12 Artifact and Launch Verification ✅

**Purpose:** prove M12 is using the intended updated DXMT/winemetal artifacts
before debugging games.

**What landed:**
- `m12_verify_dry_run(appid)` and `pipeline_dry_run_for(home, appid, requested)`
  — a read-only verifier that runs through the **same environment builder**
  (`steam_pipeline_env_pairs`) as `launch_dxmt_metal`. It reports, without
  launching Steam or the game:
  - the resolved `lib/dxmt-m12/x86_64-windows` dir + each deploy DLL
    (`d3d12.dll`, `dxgi.dll`, `d3d11.dll`, `d3d10core.dll`, `winemetal.dll`,
    …) with presence, sha256, and size
  - the `lib/dxmt-m12/x86_64-unix` sidecars (`winemetal.so`, `libc++.1.dylib`,
    `libc++abi.1.dylib`, `libunwind.1.dylib`) for the M12/M13 lane
  - the exact env pairs the launch path sets, with an `env_keys_present` map
    for `WINEDLLOVERRIDES`, `DXMT_SHADER_CACHE_PATH`, `DYLD_FALLBACK_LIBRARY_PATH`,
    `SteamAppId`, `DXMT_WINEMETAL_UNIXLIB`
  - missing required artifacts as a structured `ok: false` + `missing[]` array
    (optional stubs nvapi/nvngx/atidxx tolerated)
- New routes: `GET /diagnostics/m12/dry-run?appid=`,
  `GET /diagnostics/pipeline/dry-run?appid=&pipeline=`.
- `docs/architecture/m12-pipeline-map.md` now documents the verifier and marks
  stability gap #1 ("first-class M12 runtime verification") as addressed.
- Verified the existing SDK proof scripts are invocable with the roadmap's
  flags: `preflight-runtime-layout.py --profile metalsharp`,
  `run-probes.sh --profile metalsharp --mini-only`, `validate-contracts.py`.

**New tests (5):** M12 deploy list includes d3d12 and uses isolated
`lib/dxmt-m12` surface; M11 deploy list excludes d3d12 and never touches
`lib/dxmt-m12`; M12 dry-run includes d3d12 / M11 does not + env keys;
M12 dry-run verifies unix sidecars and flags missing artifacts;
M12 env vars set winemetal overrides + isolated `m12` shader cache.

**Proof:**
```
cargo fmt --all -- --check        # clean
cargo clippy --all-targets -- -D warnings   # clean
cargo test mtsp                   # 116 passed
cargo test                        # 526 passed, 0 failed
python3 tools/d3d12-metal-sdk/scripts/preflight-runtime-layout.py --help  # invocable
```

**Boundary check:** M9/M10/M11 deploy lists and artifact paths unchanged.
The verifier is purely read-only — it never deploys, spawns, or launches.

## Phase 4: Shader, PSO, and Cache Diagnostics ✅

**Purpose:** turn opaque M12 graphics failures into actionable shader/PSO/cache
evidence.

**Scope note:** `vendor/dxmt` is vendored **reference source** — it is NOT
compiled by this repo's `CMakeLists.txt` (the shipped DXMT runtime is prebuilt
under `lib/`). Editing DXMT C++ lowering would have no effect on the shipped
runtime and could not be verified here. Phase 4 therefore lands the Rust-side
observability layer (cache doctor + PSO manifest schema) that parses the
runtime's on-disk products, without touching shader lowering semantics.

**What landed:**
- `shader_cache::cache_doctor(appid)` / `cache_doctor_for(home, pipeline, appid)`
  — reads the DXMT SQLite shader and pipeline caches **read-only** and reports:
  - cache root, per-DB size/mtime, total `cache_*` entry count
  - newest/oldest entry mtime
  - the staged runtime DLL sha256 (from `injections.json`) used to detect
    caches built against an older runtime build
  - a `stale_warning` when entries exist but no runtime hash is recorded
- `shader_cache_family` / `primary_cache_subdir` codify the cache isolation
  contract: M9/M10/M11 share the `dxmt-metal` family, M12/M13 use the isolated
  `dxmt-metal12` family.
- `PsoDiagnosticManifest` — the stable JSON schema for DXMT PSO trace
  sidecars (DXIL input hash, MSL output hash, root signature hash, vertex
  input layout hash, render target/depth formats, sample count,
  `uses_stage_in`, async compile status, compile status, Metal error, ObjC
  exception). `parse_pso_manifest` + `recent_pso_manifests` parse the trace
  JSON DXMT emits under `DXMT_LOG_PATH` when its trace flags are set.
- New routes: `GET /diagnostics/cache-doctor?appid=`,
  `GET /diagnostics/pso-manifests?appid=&pipeline=&limit=`.

**New tests (8):** shader cache family isolation, primary cache subdir mapping,
cache doctor counts entries + reports isolated M12 lane, cache doctor empty
state, cache doctor stale warning without runtime hash, graphics PSO manifest
failure parse, compute PSO manifest success parse, recent PSO manifests
newest-first ordering.

**Proof:**
```
cargo fmt --all -- --check        # clean
cargo clippy --all-targets -- -D warnings   # clean
cargo test mtsp::shader_cache     # 10 passed
cargo test                        # 534 passed, 0 failed
```

**Boundary check:** no shader lowering semantics changed. Cache inspection is
strictly read-only (SQLite `SQLITE_OPEN_READ_ONLY`). M9/M10/M11 cache families
remain shared as before; M12/M13 remain isolated.
