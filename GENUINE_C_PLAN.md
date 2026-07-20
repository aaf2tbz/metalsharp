# Genuine C Backend Rewrite — Comprehensive Execution Plan

> **Branch**: `codex/genuine-c-backend-rewrite` (from `origin/main` at b2719291)
> **Status**: PLANNING — no code changes committed yet

---

## Objective

Replace every transpiled `.rcgu.c` file in `app/src-c/runtime/c/` (374 files,
~5.7M lines, ~253MB) and `app/src-c/tests/c/` (394 files, ~5.4M lines,
~140MB) with clean, documented, hand-written C11. The 5 maintained C files, 4
maintained C tests, and 38 native arm64 `.o` objects are **preserved**. The
existing HTTP API (264 routes, contract v1), extern ABI (8 functions), and
build system contract are **preserved** so that `make -C app/src-c verify` and
all CI jobs continue to pass.

---

## Pass 1: Architecture & Data Flow Map

### 1.1 System Boundary

```
[Electron app] ──HTTP──▶ [metalsharp-backend binary]
                              │
    ┌─────────────────────────┼─────────────────────────┐
    │                         │                         │
    ▼                         ▼                         ▼
[Maintained C]          [New C Backend]          [Native .o Objects]
 launcher.c/h    ◀────  main.c, routes.c,       sqlite3, zstd
 bottles.c/h     ◀────  steam.c, setup.c,       (38 files, arm64)
 installer.c/h   ◀────  launch.c, mtsp_*.c,
 migration.c/h   ◀────  kt_*.c, gog.c,
 runtime_surface.c/h    sharp_library.c, ...
     │                                              │
     └────────── 8 extern calls ◀───────────────────┘
```

### 1.2 The 8 Extern Functions (Maintained C ← Called by Backend)

| # | Function | Header | Called When |
|---|----------|--------|-------------|
| 1 | `metalsharp_launcher_preflight(appid, pipeline_code, prefix, pl, wd, wdl, exe, exel) → bool` | `launcher.h` | Game launch — validates runtime, DLLs, receipts, writes launch-preflight.json |
| 2 | `metalsharp_launcher_prepare_eac(appid, game_dir, gdl) → bool` | `launcher.h` | EAC title startup — renames start_protected_game.exe, copies real exe |
| 3 | `metalsharp_reconcile_bottle_manifest(path, pl) → bool` | `bottles.h` | Bottle save — validates and reconciles DXMT deployment manifest |
| 4 | `metalsharp_game_requires_agility(game_path, gpl) → bool` | `installer.h` | Install/repair — PE byte-scan for D3D12 Agility SDK dependency |
| 5 | `metalsharp_find_bytes(haystack, hsl, needle, ndl, *offset) → bool` | `installer.h` | PE scanning — Boyer-Moore byte search in large binaries |
| 6 | `metalsharp_extract_agility_package(archive, al, dest, dl) → bool` | `installer.h` | Setup/repair — extract D3D12 Agility SDK DLLs from bundled archive |
| 7 | `metalsharp_migration_refresh_saved_bottles(void) → bool` | `migration.h` | Migration — refresh all saved bottles (no-arg adapter) |
| 8 | `metalsharp_m12_runtime_complete(root, rl) → bool` | `installer.h` | Startup validation — verify M12 runtime layout and artifact integrity |

### 1.3 The 264-Route HTTP API

The backend binds to `127.0.0.1:$METALSHARP_PORT` and serves JSON over HTTP/1.1.
Route groups by prefix:

| Group | Routes | Purpose |
|-------|--------|---------|
| `/status` | 1 | Health/version/pid/home |
| `/setup/*` | 11 | Setup wizard state, deps install, Agility versions, device name |
| `/steam/*` | 23 | Steam status, library scan, install/launch/stop, API key, bridge, compatdata |
| `/bottles/*` | 20 | Bottle CRUD, profiles, repair, refresh, sync, doctor, route-contracts, fonts, wineboot |
| `/sharp-library/*` | 21 | Unified library (imported apps), GOG integration (status/games/auth/install/play/sync), covers, engines, launch args |
| `/game/*` | 5 | Launch-auto, prepare, resolve-routing, dual-info, running |
| `/launch` | 1 | Legacy launch |
| `/launcher/*` | 1 | Launch evidence |
| `/kernel-translation/*` | ~85 | anti-debug (11), APC (10), driver (12), ES/es-live (16), handle/handle-bridge (13), integration (18), integrity (7), OB callbacks (11), probe (2), thread-notify (12) |
| `/diagnostics/*` | 14 | Pipeline dry-run, M12 dry-run, FNA classify/explain/signals, launch timing, cache-doctor, binding/command contracts, PSO manifests, runtime artifacts, wineboot-state |
| `/mtsp/*` | 7 | Pipelines list, launch-shape, default-rules, prepare, recipe, doctor |
| `/update/*` | 9 | Check, start, progress, cleanup, DMG path, migrate (check/progress/report/start) |
| `/cache/*` | 2 | Size, clear |
| `/config` | 2 | Get/set config |
| `/metalfx/*` | 2 | State, toggle |
| `/goldberg/*` | 2 | Status, toggle |
| `/wine-mono/*` | 3 | Install, reset, status |
| `/d3dmetal/*` | 7 | D3DMetal/GPTK bottles (install-homebrew-gptk, rosetta, x64-redist, play, repair, save, seed-prefix, status) |
| `/scan` | 1 | Library scan |
| `/runtime/*` | 1 | Host ABI |
| `/kill` | 1 | Graceful shutdown |
| `/processes/*` | 1 | Force-kill |

**Total**: 264 frozen routes as declared in `contracts/electron-backend.v1.json`.

### 1.4 Pipeline System (MTSP Engine)

The Multi-Title Shader Pipeline (MTSP) routes every game to one of 9 pipeline
IDs. The pipeline determines Wine binary path, DLL overrides, library paths,
graphics backend, and runtime components:

| ID | Backend | D3D Level | Lane | Example Count |
|----|---------|-----------|------|---------------|
| `m12` | DXMT | D3D12 | `dxmt_m12` (isolated) | ~31 games |
| `m11` | DXMT | D3D11 | `dxmt` | ~300+ games |
| `m11_32` | DXMT | D3D11 (32-bit) | `dxmt` | ~10 games |
| `m10` | DXMT | D3D10 | `dxmt` | ~5 games |
| `m10_32` | DXMT | D3D10 (32-bit) | `dxmt` | ~3 games |
| `m9` | DXVK/OpenGL | OpenGL | `dxmt` | 76 games |
| `fna_arm64` | FNA/Mono | N/A | `dxmt` | ~50 games |
| `fna_x86` | FNA/Mono (x86) | N/A | `dxmt` | ~20 games |
| `wine_bare` | None | N/A | `dxmt` | ~10 games |

Pipeline selection order:
1. Check `configs/mtsp-rules.toml` for a game-specific `[overrides.APPID]`
2. Fall back to PE binary analysis via `mtsp_pe` module
3. Fall back to pipeline defaults in `mtsp_rules.c`

**Critical**: The `m9` pipeline (DXVK/OpenGL) is the one mentioned in PR #307's
OpenGL bridge work. It routes through the same DXMT runtime but selects DXVK's
Direct3D→Vulkan→MoltenVK→Metal path for OpenGL games rather than the native
D3DMetal path.

### 1.5 Bottle Save Pipeline

When a bottle is saved (`POST /bottles/prepare` → `save_bottle`):

1. **Pipeline resolution**: `resolve_steam_pipeline_for_request` → `manifest_preferred_pipeline`
2. **Pre-save refreshes**:
   - `refresh_dxmt_runtime_before_save` — ensures DXMT runtime is deployed
   - `refresh_mono_fna_components_before_save` — ensures Mono/FNA runtimes
3. **DLL staging** (M12 only):
   - `stage_m12_dlls_for_saved_steam_bottle` — stages D3D12/DXGI/D3D11 DLLs in prefix/system32 and game dir
   - `stage_route_dlls_for_saved_steam_bottle` — stages route-contract-specific DLLs
4. **Compatibility overrides**: `save_compatibility_overrides` — writes compatibility matrix entries
5. **Component repair**: Launches background threads via `repair_components` (3 parallel repair threads)
6. **Bottle manifest**: `metalsharp_reconcile_bottle_manifest` (calls into maintained C)
7. **Steam compatdata**: `save_steam_compatdata` — records Steam compatdata path
8. **Write receipt**: Deployment receipt written to `dxmt-deployment.json`

### 1.6 Launch Pipeline

1. **Request**: `POST /game/launch-auto {appid, pipeline?}` or `POST /launch`
2. **Pipeline resolution**: `mtsp_launcher` computes `LaunchShape`
3. **Bottle check**: `ensure_steam_game_bottle` / `prepare_steam_game_launch`
4. **Preflight**: `metalsharp_launcher_preflight` (maintained C) — validates:
   - Runtime completeness (`metalsharp_m12_runtime_complete`)
   - All DLL artifacts present with correct hashes
   - Wine binary exists and is executable
   - Receipt is ready (status, surface_id, manifest_sha256)
   - EAC executable substitution for Elden Ring / Armored Core 6
5. **Launch receipts**:
   - Writes `dxmt-launch-preflight.json` (launch evidence)
6. **Process spawn**: Wine process launched with computed env vars

### 1.7 Route Contract System (Bottles)

The bottles module generates per-bottle route contracts via `steam_route_contracts()`.
These contracts define which DLLs a bottle needs for each pipeline ID. They
are returned by `GET /bottles/route-contracts` and used to validate route
conflicts on launch. The contract table maps pipeline codes (1-13) to sets of
required DLL paths.

### 1.8 The `_metalsharp_m12_components` Static Table

This is a compile-time constant table in cgu.01 defining every supported
redistributable component profile. Decoded from the hex bytes:

| Profile | Display Name | Components |
|---------|-------------|------------|
| 0 | "D3D12 Metal" | d3d11, d3d12, dxgi, d3d10, vcrun2019_x64, vcrun2019_x86, gpu_vendor_subs |
| 1 | "GPTK D3DMetal" | gptk, rosetta, gptk_prefix, vcrun2019_x64, vcrun2019_x86 |
| 2 | "D3DMetal (GPTK)" | wine-mono, gecko, dotnet48, vcrun2019_x64, vcrun2019_x86, corefonts |
| 3 | ".NET" | wine-mono, gecko, dotnet48, vcrun2019_x64, vcrun2019_x86, corefonts |
| 4 | "32-bit .NET" | gecko, webview2, dotnet48, vcrun2019_x64, vcrun2019_x86, directx_jun2010, openal, corefonts |
| 5 | "WebView" | vcrun2019_x64, vcrun2019_x86, corefonts |
| 6 | "Java Launcher" | mono-arm64, fna, xna, sdl2, fna3d, faudio |
| 7 | "FNA / Mono ARM64" | mono-x86, fna, xna, sdl2, fna3d, faudio, fmod |
| 8 | "FNA / Mono x86_64" | REGEDIT4 registry for font substitutions (Helvetica→Arial, Times→Times New Roman, etc.) |
| 9 | Font subs registry | (registry data for font substitution) |

---

## Pass 2: Global State Map

### 2.1 Atomic Booleans (Process-Lifetime Flags)

| Variable | Purpose | Set By | Read By |
|----------|---------|--------|---------|
| `STEAM_INSTALLING` | Steam install in progress | `/steam/install` | `/steam/status`, launch gate |
| `MIGRATING` | Migration running | `/update/migrate/start` | `/update/migrate/progress`, status |
| `UPDATING` | Self-update running | `/update/start` | `/update/progress`, status |
| `INSTALLING` | Setup install running | `/setup/install-*` | `/setup/installing`, status |
| `IPC_LISTENER_RUNNING` | IPC bridge active | `kt_ipc_bridge_start` | `kt_ipc_bridge_status` |

### 2.2 Atomic Counters

| Variable | Type | Purpose |
|----------|------|---------|
| `ISSUE_LOG_COUNTER` | `uint64_t` | Monotonic log event ID |
| `NEXT_HANDLE` | `uint64_t` | Virtual NT handle ID counter |
| `NEXT_APC_ID` | `uint64_t` | APC injection ID counter |
| `NEXT_EVENT_ID` | `uint64_t` | ES event ID counter |
| `NEXT_CALLBACK_ID` | `uint64_t` | ES callback registration ID |
| `IPC_ACTIVE_CLIENTS` | `uint32_t` | Connected IPC clients |
| `DOWNLOAD_PERCENT` | `uint8_t` | Update download progress (0-100) |
| `NEXT_PIPELINE_ID` | `uint64_t` | Kernel translation pipeline ID counter |

### 2.3 Complex Global State (Structures)

| Struct | Module | Purpose |
|--------|--------|---------|
| `BOTTLE_SAVE_LOCK` | bottles | Mutex serializing bottle saves |
| `ES_STATUS` | kt_es_bridge | ES client connection state |
| `CALLBACKS` | kt_es_bridge | Registered ES event callbacks |
| `IMAGE_EVENTS` | kt_es_bridge | Image load event queue |
| `IPC_CHANNELS` | kt_es_bridge | Active IPC channels |
| `PROCESS_EVENTS` | kt_es_bridge | Process event queue |
| `THREAD_EVENTS` | kt_es_bridge | Thread event queue |
| `HANDLE_TABLES` | kt_handle_table | Per-process virtual handle tables |
| `APC_QUEUES` | kt_apc | Per-thread APC queues |
| `SAVED_CONTEXTS` | kt_apc | Saved thread contexts for APC |
| `TRAMPOLINE_STATE` | kt_apc | Trampoline allocation state |
| `CHECK_RESULTS` | kt_anti_debug | Anti-debug check results |
| `SANITIZE_RULES` | kt_anti_debug | Module sanitization rules |
| `PIPELINES` | mtsp_engine | Loaded pipeline definitions |
| `RUNNING_GAMES` | launch | Set of running game PIDs |
| `INIT` / `INIT_LOCK` | main | One-time initialization guard |

---

## Pass 3: Dependency Map (What Rust Crate → What C Replacement)

### 3.1 Direct Replacements (Standard C Libraries Exist)

| Rust Crate | C Replacement | Notes |
|------------|---------------|-------|
| `core` (16 cgu) | C11 stdlib | `<stdbool.h>`, `<stdint.h>`, `<stddef.h>` cover all |
| `alloc` (6 cgu) | libc `malloc`/`free` | Arena allocator for hot paths |
| `std` (16 cgu) | POSIX/libc + shims | Threads, I/O, env, fs, time, net |
| `compiler_builtins` (6 cgu) | Clang builtins | Compiler provides these |
| `libc` (2 cgu) | Actual libc | Already linked |
| `zstd`/`zstd_safe`/`zstd_sys` (3 cgu) | Native `.o` files | Already have 38 zstd objects |
| `libsqlite3_sys` (1 cgu) | Native `sqlite3.o` | Already linked |
| `rustix` (6 cgu) | POSIX syscalls | Use libc wrappers |
| `nix` (5 cgu) | POSIX | `fcntl`, `ioctl`, `mmap`, etc. |
| `getrandom` (1 cgu) | `arc4random_buf` | macOS native |
| `dirs`/`dirs_sys` (2 cgu) | `getenv("HOME")` | Trivial |
| `tempfile` (6 cgu) | `mkstemp` | POSIX |
| `walkdir` (4 cgu) | `opendir`/`readdir` | POSIX |
| `same_file` (1 cgu) | `stat`+`st_dev`/`st_ino` | Trivial |
| `once_cell` (1 cgu) | `pthread_once` | POSIX |
| `cfg_if` (2 cgu) | `#ifdef` | Preprocessor |
| `bitflags` (1 cgu) | C bitfields/enums | Trivial |
| `equivalent` (1 cgu) | `==` operator | Trivial |
| `option_ext` (1 cgu) | Nullable pointers | Trivial |
| `utf8_zero` (1 cgu) | NUL-terminated strings | Already C convention |
| `foldhash` (1 cgu) | `CC_SHA256` or FNV-1a | Trivial |
| `fastrand` (1 cgu) | `arc4random` | macOS native |
| `cpufeatures` (1 cgu) | `sysctlbyname` | macOS native |
| `errno` (1 cgu) | `errno.h` | Trivial |

### 3.2 Hand-Written Replacements Needed

| Rust Crate | C Replacement File(s) | Est. Lines | Complexity |
|------------|----------------------|------------|------------|
| `tiny_http` (16 cgu) | `http_server.c` + `http_server.h` | ~1,500 | High — HTTP/1.1 parse + response + JSON content-type |
| `ureq` + `ureq_proto` (32 cgu) | `http_client.c` + `http_client.h` | ~1,200 | High — HTTPS with Security.framework |
| `serde_json` (9 cgu) | `json.c` + `json.h` | ~2,000 | High — streaming parser + serializer + DOM |
| `serde` + `serde_core` (2 cgu) | Part of `json.c` | — | Included |
| `toml` + `toml_edit` + `toml_datetime` + `toml_write` + `serde_spanned` + `winnow` (34 cgu) | `config_parser.c` + `config_parser.h` | ~1,200 | Medium — TOML subset for mtsp-rules |
| `rusqlite` (11 cgu) | `database.c` + `database.h` | ~600 | Medium — thin SQLite wrapper |
| `security_framework` + `_sys` (31 cgu) | `keychain.c` + `code_signing.c` | ~800 | Medium — Keychain, code signing queries |
| `block2` + `objc2` + `objc2_encode` + `core_foundation` + `_sys` + `dispatch2` (20 cgu) | Inline ObjC/CF calls | ~400 | Low — direct runtime calls |
| `sha2` (4 cgu) | CommonCrypto | Already linked |
| `base64` + `base64ct` (3 cgu) | `base64.c` | ~200 | Low |
| `zip` + `zopfli` + `flate2` + `miniz_oxide` + `zlib_rs` + `crc32fast` + `adler2` + `simd_adler32` (42 cgu) | `archive.c` | ~800 | Medium — zip extract using libz |
| `typed_path` (10 cgu) | `path_utils.c` | ~300 | Low |
| `hashbrown` + `indexmap` + `hashlink` (3 cgu) | `hash_table.c` | ~400 | Low |
| `bumpalo` (1 cgu) | `arena.c` | ~150 | Low |
| `bytes` (4 cgu) | Raw buffers | — | Not needed |
| `smallvec` (1 cgu) | Dynamic arrays | — | Not needed |
| `http` + `httparse` + `httpdate` + `chunked_transfer` (12 cgu) | Part of `http_server.c` | — | Included |
| `rustls_pki_types` (4 cgu) | Security.framework | — | Apple native |
| `webpki_root_certs` (1 cgu) | Security.framework trust store | — | Apple native |
| `der` + `pem_rfc7468` + `const_oid` + `hybrid_array` + `typenum` (33 cgu) | `crypto_utils.c` | ~300 | Low — PEM/DER parsing |
| `percent_encoding` (1 cgu) | `url.c` | ~100 | Low |
| `log` (1 cgu) | `logger.c` | ~200 | Low |
| `ctrlc` (1 cgu) | `signal(SIGINT, ...)` | — | Trivial |
| `native_tls` (4 cgu) | Security.framework TLS | — | Apple native |
| `gimli` + `object` + `addr2line` + `rustc_demangle` + `fallible_iterator` + `fallible_streaming_iterator` + `memchr` + `rustc_std_workspace_*` (18 cgu) | **NOT NEEDED** | — | Debug info / compiler helpers |
| `panic_unwind` + `unwind` (2 cgu) | `setjmp`/`longjmp` | — | C native |

### 3.3 Reuse Existing Native Objects

| Library | Files | Purpose |
|---------|-------|---------|
| SQLite | `c877a2978823c39d-sqlite3.o` | Game library database, bottles db, config db |
| zstd compress | `fb80479a5fb81f6a-*.o` (14 files) | Archive compression |
| zstd decompress | `88f362f13b0528ed-*.o` (3 files) | Archive extraction |
| zstd common | `44ff4c55aa9e5133-*.o` (6 files) | Shared zstd utils |
| zstd dict | `a6c81c75fc82913a-*.o` (4 files) | Dictionary compression |
| zstd v01-v06 | `3f451b2306bc13c8-*.o` (6 files) | Versioned zstd APIs |
| zstd AMD64 | `7faed3f8272f2313-huf_decompress_amd64.o` | AMD64-optimized Huffman |

---

## Pass 4: Module Structure of New C Backend

### 4.1 File Layout

```
app/src-c/                          (new structure)
├── maintained/                     (unchanged)
│   ├── launcher.c / launcher.h
│   ├── bottles.c / bottles.h
│   ├── installer.c / installer.h
│   ├── migration.c / migration.h
│   └── runtime_surface.c / runtime_surface.h
├── runtime/                        (NEW — hand-written C backend)
│   ├── main.c                      Entry point, signal handlers
│   ├── server.h                    Shared types, error codes
│   ├── json.c / json.h             JSON parser + serializer
│   ├── http_server.c / http_server.h   HTTP/1.1 server
│   ├── http_client.c / http_client.h   HTTP client (TLS)
│   ├── config_parser.c / config_parser.h TOML rules parser
│   ├── database.c / database.h     SQLite wrapper
│   ├── logger.c / logger.h         Structured logging
│   ├── arena.c / arena.h           Arena allocator
│   ├── hash_table.c / hash_table.h Hash table
│   ├── base64.c / base64.h         Base64 codec
│   ├── archive.c / archive.h       Zip/tar extraction
│   ├── path_utils.c / path_utils.h UTF-8 path helpers
│   ├── keychain.c / keychain.h     Keychain queries
│   ├── code_signing.c / code_signing.h Code signing checks
│   ├── crypto_utils.c / crypto_utils.h PEM/DER
│   │
│   ├── routes.c / routes.h         Route dispatch table (264 routes)
│   ├── setup.c                     Setup wizard
│   ├── steam.c                     Steam integration
│   ├── gog.c                       GOG integration
│   ├── sharp_library.c             Game library management
│   ├── bottles_backend.c           Bottle CRUD, save pipeline, route contracts
│   ├── installer_backend.c         Runtime installer, dependency download
│   ├── updater.c                   Self-update engine
│   ├── migrate_backend.c           Migration engine
│   ├── launch.c                    Launch orchestrator
│   ├── launcher_evidence.c         Launch preflight evidence
│   ├── platform.c                  Platform helpers (migrate_game_to_external)
│   ├── scan.c                      Library scanning
│   ├── mono.c                      Wine Mono management
│   ├── d3d12_doctor.c              D3D12/DXMT runtime health
│   ├── d3dmetal_gptk.c             D3DMetal/GPTK routes
│   ├── fna_profile.c               FNA game classification
│   ├── metalfx.c                   MetalFX toggle
│   ├── diagnostics.c               Diagnostic endpoints
│   ├── binding_contract.c          Binding contract validation
│   ├── command_contract.c          Command contract replay
│   │
│   ├── mtsp_engine.c               Pipeline engine core
│   ├── mtsp_default_rules.c        Default pipeline rules
│   ├── mtsp_launcher.c             Launch shape computation
│   ├── mtsp_pe.c                   PE binary analysis
│   ├── mtsp_recipe.c               Recipe dispatch
│   ├── mtsp_rules.c                Rule evaluation
│   ├── mtsp_shader_cache.c         Shader cache
│   │
│   ├── kt_types.c                  NT type definitions
│   ├── kt_nt_to_xnu.c              Syscall translation table
│   ├── kt_es_bridge.c              Endpoint Security bridge
│   ├── kt_es_live.c                Live ES monitoring
│   ├── kt_ipc_bridge.c             Wine IPC bridge
│   ├── kt_handle_table.c           Virtual handle table
│   ├── kt_handle_bridge.c          Handle bridge (fd↔handle)
│   ├── kt_handle_callbacks.c       ObRegisterCallbacks
│   ├── kt_anti_debug.c             Anti-debug checks
│   ├── kt_apc.c                    APC injection
│   ├── kt_code_integrity.c         Module signing
│   ├── kt_driver_model.c           NT driver model
│   ├── kt_integration.c            Bottle kernel config
│   ├── kt_probe.c                  Host probe
│   └── kt_thread_notify.c          Thread notifications
├── native/objects/                 (unchanged — 38 .o files)
├── tests/                          (NEW + maintained)
│   ├── dxmt_surface_test.c         (unchanged)
│   ├── installer_test.c            (unchanged)
│   ├── policy_test.c               (unchanged)
│   ├── bottle_deployment_test.c    (unchanged)
│   ├── json_test.c                 (new)
│   ├── http_test.c                 (new)
│   ├── config_parser_test.c        (new)
│   ├── database_test.c             (new)
│   ├── ...                         (~15 new test files)
│   └── test_main.c                 Test runner
├── manifests/
│   ├── runtime-sources.txt         (UPDATED — points to new files)
│   ├── maintained-sources.txt      (unchanged)
│   ├── test-sources.txt            (UPDATED)
│   ├── maintained-test-sources.txt (unchanged)
│   ├── native-objects.txt          (unchanged)
│   └── backend-modules.txt         (unchanged — modules map to new files)
├── Makefile                        (UPDATED)
├── entitlements.plist              (unchanged)
└── README.md                       (UPDATED)
```

### 4.2 Module Dependency Graph

```
main.c
 ├── routes.c ───────────────────────────── (dispatches all 264 routes)
 │    ├── setup.c
 │    ├── steam.c ──── http_client.c, database.c
 │    ├── gog.c ────── http_client.c, database.c
 │    ├── sharp_library.c ── database.c, steam.c, gog.c
 │    ├── bottles_backend.c ── database.c, launch.c, archive.c
 │    │    ├── bottles.h (maintained)
 │    │    └── launcher.h (maintained)
 │    ├── installer_backend.c ── archive.c, http_client.c, installer.h
 │    ├── updater.c ──── http_client.c, archive.c
 │    ├── migrate_backend.c ── migration.h, bottles_backend.c
 │    ├── launch.c ───── bottles_backend.c, mtsp_launcher.c, launcher.h
 │    ├── launcher_evidence.c ── launch.c
 │    ├── mono.c ─────── archive.c, http_client.c
 │    ├── d3d12_doctor.c
 │    ├── d3dmetal_gptk.c
 │    ├── fna_profile.c
 │    ├── metalfx.c
 │    ├── diagnostics.c
 │    ├── binding_contract.c
 │    ├── command_contract.c
 │    ├── scan.c
 │    ├── platform.c
 │    ├── mtsp_*.c (7 modules) ── config_parser.c, mtsp_pe.c
 │    └── kt_*.c (16 modules) ── kt_es_bridge.c (ES framework)
 └── server.h (shared types, error codes)
```

---

## Pass 5: Startup/Shutdown & Error Patterns

### 5.1 Startup Sequence

```
main()
  ├─ Read $METALSHARP_HOME (env, default: $HOME/.metalsharp)
  ├─ Read $METALSHARP_PORT (env, default: 0 → OS-assigned)
  ├─ Initialize logger (issue_log_counter = 0)
  ├─ Initialize database (SQLite: ~/.metalsharp/metalsharp.db)
  ├─ Initialize security (keychain access, code signing)
  ├─ Start ES client (kt_es_bridge_init)
  │   └─ es_new_client() with ES_MACH_LOOKUP policy
  ├─ Start IPC listener (kt_ipc_bridge_start) → background thread
  ├─ Start HTTP server (bind 127.0.0.1:PORT)
  │   └─ Print "METALSHARP_PORT=<port>" to stdout for Electron
  ├─ Register signal handlers (SIGINT, SIGTERM → graceful shutdown)
  ├─ Enter accept loop
  │   └─ For each request: parse HTTP → route → handle → respond JSON
  └─ On shutdown (SIGINT/SIGTERM or POST /kill):
      ├─ Stop accepting new connections
      ├─ Drain in-flight requests (5s timeout)
      ├─ Kill running game processes
      ├─ Stop IPC listener
      ├─ Stop ES client
      ├─ Close database
      └─ exit(0)
```

### 5.2 Error Handling Convention

All functions follow the pattern:
```c
/* Returns: true on success, false on failure.
 * On failure, *error_out (if non-NULL) receives a heap-allocated
 * error message. Caller must free(*error_out). */
bool metalsharp_do_thing(const char *input, char **error_out);
```

HTTP routes always return `{"ok": true, ...}` or `{"ok": false, "error": "..."}`.

### 5.3 Signal Handling

- `SIGINT` / `SIGTERM`: Graceful shutdown (drain connections, stop children)
- `SIGPIPE`: Ignored (handled at write() level with EPIPE)
- `SIGCHLD`: Reaped for game process monitoring

### 5.4 Thread Safety Model

- SQLite: Single-writer with WAL mode, all DB access through mutex-guarded wrapper
- Bottle saves: Serialized through `BOTTLE_SAVE_LOCK` mutex
- ES events: Single-threaded event loop (one dispatch thread)
- HTTP server: One thread per connection from thread pool (4 workers)
- Global atomics: All simple counters use `_Atomic` types
- Background work: `repair_components` spawns 3 worker threads

---

## Implementation Phases

### Phase 0: Plan Commit & Draft PR (NOW) ✅
1. Commit `GENUINE_C_PLAN.md` to `codex/genuine-c-backend-rewrite`
2. Push branch → open draft PR
3. PR body with required sections: Summary, Type, PR Readiness (all unchecked), Changes, Testing, AI disclosure

### Phase 1: Contract Extraction (delegate: deepseekv4pro)
**Input**: The 17 `metalsharp_backend` rcgu.c files, contract JSON, maintained C headers
**Output**: `PLAN/contract-spec.json` — machine-readable interface specification containing:
- All 264 route signatures with full request/response schemas
- All 8 extern function signatures with call sites
- All 22+ global state variables with types and access patterns
- Full startup sequence (env reads, init order, thread spawns)
- Full shutdown sequence (signal handling, cleanup ordering)
- All 10 `_metalsharp_m12_components` table entries decoded
- All pipeline IDs with default configurations
- All native `.o` symbol requirements

### Phase 2: Dependency Shims Design (delegate: deepseekv4pro)
**Output**: `PLAN/dependency-shims.md` — per-dependency design with function signatures
- JSON API design (parse, serialize, query, build)
- HTTP server API (route registration, request/response types)
- HTTP client API (GET/POST with TLS)
- TOML parser API
- SQLite wrapper API
- Hash table, arena, path, logging, base64, archive APIs
- Keychain, code signing APIs

### Phase 3: Foundation Implementation (delegate: minimax-m3)
Implement all ~15 foundation `.c`/`.h` files:
`main.c`, `server.h`, `json.c`, `http_server.c`, `http_client.c`,
`config_parser.c`, `database.c`, `logger.c`, `arena.c`, `hash_table.c`,
`base64.c`, `archive.c`, `path_utils.c`, `keychain.c`, `code_signing.c`,
`crypto_utils.c`

### Phase 4: Route Dispatch (delegate: minimax-m3)
Implement `routes.c` + `routes.h` — full 264-route dispatch table with typed handlers

### Phase 5: Core Backend (delegate: minimax-m3)
`setup.c`, `steam.c`, `gog.c`, `sharp_library.c`, `bottles_backend.c`,
`installer_backend.c`, `updater.c`, `migrate_backend.c`

### Phase 6: Launch Pipeline (delegate: minimax-m3)
`launch.c`, `launcher_evidence.c`, `platform.c`, `scan.c`, `mono.c`,
`d3d12_doctor.c`, `d3dmetal_gptk.c`, `fna_profile.c`, `metalfx.c`
Plus MTSP: `mtsp_engine.c`, `mtsp_default_rules.c`, `mtsp_launcher.c`,
`mtsp_pe.c`, `mtsp_recipe.c`, `mtsp_rules.c`, `mtsp_shader_cache.c`

### Phase 7: Kernel Translation (delegate: minimax-m3)
All 16 `kt_*.c` files — ES bridge, IPC, handle table, anti-debug, APC,
code integrity, driver model, integration, probe, thread notify

### Phase 8: Diagnostics & Support (delegate: minimax-m3)
`diagnostics.c`, `binding_contract.c`, `command_contract.c`

### Phase 9: Tests & Build System (delegate: minimax-m3)
~15 new C test files, updated Makefile, updated manifests, updated README

### Phase 10: Integration & Verification (delegate: parent)
- Remove all 768 `.rcgu.c` files
- Copy new files into place
- Update manifests
- Run `make -C app/src-c verify`
- Run contract validation + conformance
- Run full CI suite
- Fix any failures

### Phase 11: PR Finalization (delegate: parent)
- Push all commits
- Update PR readiness (all gates → [x])
- Poll CI (10 min + 10 min)
- Fix failing CI
- Switch PR from draft → review ready

---

## Success Gates

- [ ] `make -C app/src-c verify` — all gates pass
- [ ] 264 routes contract validation passes
- [ ] 22 conformance routes pass against running binary
- [ ] All 46 modules verified against C symbols
- [ ] Version alignment verified
- [ ] Binary compiles, links, and runs
- [ ] Test binary compiles, links, all 629+ tests pass
- [ ] `clang-format --dry-run --Werror` clean
- [ ] All PR CI jobs green
- [ ] PR readiness format valid
