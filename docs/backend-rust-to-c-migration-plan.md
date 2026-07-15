# MetalSharp Backend Rust-to-C Migration Plan

- **Status:** Completed; the repository and shipped Apple Silicon backend are C-only
- **Baseline:** `origin/main` at `e7bcba2e` (2026-07-14)
- **Planning branch:** `codex/rust-to-c-backend-plan`

## Decision summary

MetalSharp can replace the Rust backend without changing the Electron UI or the
native graphics engine because the main boundary is already process-oriented:
Electron spawns `metalsharp-backend` and communicates with it over loopback
HTTP. There is no Rust ABI embedded in Electron.

The completed end state is a C-only backend. The 45 former modules and their
629 tests are committed as auditable C translation units under `app/src-c` and
Xcode clang links the shipped arm64 executable. Rust/Cargo sources and the
one-time generation integration were removed after conversion.

## Implemented build path

`tools/rustc-c/build-backend.sh` is generation tooling only. It pins
`rustic-compiler/rustc_codegen_c` and its matching Rust 1.94.1 source tree,
then produces auditable C units for review and promotion. The release path
builds the checked-in C source directly with:

`make -C app/src-c backend`

That command writes `app/build/c-backend/metalsharp-backend`, which is the
only executable Electron, DMG repair, split bundles, and release packaging
may select. The release product contains no Rust runtime or Cargo-built
backend fallback.

The integration uses `ureq` with macOS native TLS (rather than Rustls/ring) and
pins `rusqlite` 0.39 because the selected code generator cannot compile the
newer `libsqlite3-sys` dependency. The normal Rust test suite remains a source
behavior oracle, but release CI builds and packages only the C-compiled binary.

Before selecting a compiler, run a time-boxed bakeoff. As of this plan:

| Candidate | Current fit | Decision |
| --- | --- | --- |
| [`rustic-compiler/rustc_codegen_c`](https://github.com/rustic-compiler/rustc_codegen_c) | Most capable candidate; lowers Rust MIR to C and can emit a C-source/Make bundle. Its published toolchains are Linux-only, its latest published toolchain targets Rust 1.94.1, and it identifies known problems with CPU-specific code such as `ring`/NEON. | **Primary spike candidate**, but it must prove an Apple arm64 source build and the full MetalSharp dependency graph. |
| [`thepowersgang/mrustc`](https://github.com/thepowersgang/mrustc) | Mature bootstrap compiler with C output and secondary arm64 macOS support. It currently targets Rust 1.90 and explicitly describes its output as ugly C and itself as unsuitable for everyday use. | **Fallback/reference candidate**, not the default production pipeline. |
| [`rust-lang/rustc_codegen_c`](https://github.com/rust-lang/rustc_codegen_c) | Official Rust organization experiment, but its README says it is highly experimental, production-unready, and Linux-only. | Track upstream; do not put it on the release critical path today. |
| [`FractalFir/rustc_codegen_clr`](https://github.com/FractalFir/rustc_codegen_clr) | Can emit C, but warns against serious use and reports incomplete C `std` behavior. | Reject for this migration. |

The conventional Rust test build remains a source-behavior oracle while the C
backend evolves. The release path is now the C compiler entry point; it retains
`app/src-rust` only as source input and keeps Electron's external contract
unchanged.

## Current scope

The backend is not small:

- 45 Rust source files and approximately 56,600 lines.
- 629 in-tree Rust tests.
- 264 HTTP routes, including 123 `/kernel-translation/*` routes.
- 88 literal backend paths used directly by Electron/renderer code; additional
  diagnostic and migration callers exist outside the renderer.
- Native/system behavior includes process spawning, Wine and Steam lifecycle,
  filesystem migration, SQLite, ZIP/Zstandard, HTTPS, Mach ports, `proc_pidinfo`,
  `csops`, `mmap`/`mprotect`, `dlopen`/`dlsym`, EndpointSecurity, signals, and
  auxiliary TCP listeners.

The current Rust dependency graph also contains translation risk:

| Rust dependency/surface | C migration implication |
| --- | --- |
| `serde`, `serde_json`, `toml` | JSON and TOML field names, omission/default rules, number handling, and error behavior are protocol/state contracts. |
| `tiny_http` | Preserve loopback bind behavior, request parsing, response codes, CORS, content types, and the initial single-dispatch behavior. |
| `ureq` + `rustls` + `ring` | `ring` is a known risk for the leading C codegen candidate on NEON. Replace this lane behind an HTTP-client adapter before the full translation spike. |
| `rusqlite` + bundled SQLite | Keep the SQLite file schema and transaction semantics; the C backend can call a pinned SQLite amalgamation directly. |
| `zstd`/`zstd-sys` | Keep archive compatibility and pin the same C zstd implementation/version initially. |
| `zip` | Replace behind an archive adapter; validate path traversal defenses, permissions, timestamps, and partial-extract cleanup. |
| `ctrlc`, `libc`, macOS FFI | Reimplement in explicit C/Objective-C platform adapters and test each native call on arm64. |

## Compatibility boundary to preserve

These are the "Rust links" that must become explicit, versioned contracts. The
C backend should initially be a drop-in executable with the same name and
behavior.

### 1. Electron lifecycle contract

Preserve all of the following:

- Executable name: `metalsharp-backend`.
- Packaged location: `Contents/Resources/runtime/metalsharp-backend`.
- Existing development and `/usr/local/bin` discovery fallbacks in
  `app/src/main/rust-bridge.ts` until that file is renamed later.
- `METALSHARP_PORT`, `METALSHARP_HOME`, and `METALSHARP_DEV` semantics.
- Production port 9274 and development default port 9276.
- `/status` readiness, startup retry timing, PID reporting, and version matching.
- Graceful `SIGTERM`, forced `SIGKILL`, child cleanup, and port-release behavior.
- Stdout/stderr behavior expected by Electron and crash-report discovery.

The bridge class can be renamed only after the C backend is proven; renaming it
does not belong in the compatibility phase.

### 2. HTTP and JSON contract

Build a checked-in route manifest from the current router and record, for every
route:

- method, path, query parameters, request JSON schema, response JSON schema;
- status codes and whether failures use transport errors or `{ok:false}`;
- absent vs. `null`, integer width/sign, enum strings, defaults, and ordering
  where existing consumers or snapshots observe it;
- headers (`Content-Type`, `Access-Control-Allow-Origin`) and raw-body routes;
- timeout/long-running behavior and whether work continues in a background
  thread;
- state mutation, locking, reentrancy, and idempotency.

Special cases include `/sharp-library/cover` raw image responses,
`/logs/stream`, query-heavy diagnostic routes, and the default 404 body.

### 3. Non-renderer HTTP consumers

Preserve these callers as first-class tests, not incidental scripts:

- Electron main/preload IPC: `backend:request`, restart, liveness, and PID.
- `tools/migrator/main.m`: `/update/migrate/start` and progress polling.
- `app/updater/update.sh`: `/status` version verification and process-name kill.
- D3D12 corpus tools and local diagnostic gates under
  `tools/d3d12-metal-sdk/` and `docs/optimization-roadmap/`.
- Direct cover-image URLs in the renderer, which bypass the IPC request helper.

### 4. Runtime, native, and TCP contracts

- `GET /runtime/host-abi` and the versioned C ABI in
  `include/metalsharp/HostRuntimeABI.h` are separate but related contracts.
- Keep the host ABI major/minor rules, `struct_size` forward compatibility, and
  `runtime/host` packaging intact.
- Preserve the Steam bridge default port 18733 and
  `METALSHARP_STEAM_BRIDGE_PORT` propagation.
- Preserve the kernel-translation IPC listener framing, byte order, length
  validation, connection lifetime, and port selection.
- Preserve all dynamic library names, symbol names, `dlopen` flags, C layouts,
  syscall numbers, Mach types, ownership rules, and OS-version guards.
- Compile-time `static_assert`s must verify every shared struct's size,
  alignment, and field offset on Apple arm64.

### 5. On-disk state contract

Do not migrate user state merely because the implementation language changes.
The C backend must read and write the current formats in place:

- `~/.metalsharp/bottles/<id>/bottle.json` and bottle prefixes/logs;
- setup state, app configuration, Sharp Library metadata, Steam metadata, GOG
  state, route recipes/rules, compatibility records, progress files, and
  migration markers/reports;
- SQLite files and their schema/user-version/transaction behavior;
- shader/pipeline caches and diagnostic manifests;
- atomic temp-file + rename behavior, permissions, symlink handling, path
  canonicalization, and cleanup after interruption.

Create golden fixtures for every persisted format, including malformed,
partially written, old-version, Unicode, symlink, and external-volume cases.

### 6. Child-process contract

For every spawned program, capture and preserve:

- resolved executable, exact argument vector, environment additions/removals,
  working directory, stdin/stdout/stderr mode, architecture selection, and
  process-group/session behavior;
- Wine prefix, wineserver, Steam identity, MTSP route, Mono/FNA, GPTK, GOG, and
  shader-cache variables;
- detachment, PID registration, wait strategy, timeouts, exit interpretation,
  tree termination, and orphan cleanup;
- codesigning, `install_name_tool`, `otool`, Homebrew, Rosetta, Xcode tools, and
  system executable path assumptions.

The existing command and launcher evidence modules should be converted into a
language-neutral launch ledger so Rust and C can be compared without launching
a real game for every test.

## Target C architecture

Add a parallel `app/src-c/` tree and a CMake target whose output remains
`metalsharp-backend`. Keep platform-specific code narrow:

```text
app/src-c/
  CMakeLists.txt
  include/metalsharp_backend/     public internal contracts
  src/
    main.c                        startup, signals, listener
    http/                         router, request/response adapters
    contract/                     JSON schemas and route manifest
    platform/                     POSIX process/filesystem adapter
    platform/apple/               .c/.m Mach, Security, ES, launch services
    state/                        atomic files, SQLite, manifests
    services/                     setup, updater, Steam, GOG, bottles, MTSP
    kernel_translation/           native translation services
  tests/
  third_party/                    pinned, licensed sources or lock manifest
```

Use opaque handles and explicit ownership rules. Return a uniform result type
from service functions; convert to HTTP/JSON only at the router edge. Keep
shared mutable state inside documented context objects rather than scattered
globals. Use sanitizers in tests and keep production behavior equivalent before
attempting concurrency improvements.

Suggested native components, subject to license/security review:

- HTTP server: a small pinned C server library such as CivetWeb, wrapped so it
  can be replaced. Do not expose library types to services.
- JSON: `yyjson` or another strict, pinned C implementation with integer and
  Unicode behavior covered by fixtures.
- TOML: `tomlc99` or a minimal parser selected by compatibility tests.
- Outbound HTTPS: a libcurl adapter, preferably statically linked and pinned,
  or a narrow Objective-C `NSURLSession` adapter. This removes `ring` from the
  translation path while retaining Apple trust-store behavior.
- SQLite: pinned SQLite amalgamation with the existing compile options recorded.
- Zstandard/ZIP: pinned zstd plus a reviewed ZIP implementation; preserve
  decompression limits and path validation.
- SHA-256: an Apple/CommonCrypto adapter or a pinned portable implementation.
- Tests: CTest plus Criterion/CMocka (or a minimal in-repo harness),
  AddressSanitizer, UndefinedBehaviorSanitizer, leak checks, and fuzz targets.

Every third-party component needs a pinned commit/version, checksum, license,
SBOM entry, and update owner. Avoid adding runtime dylib dependencies that are
not guaranteed by the minimum supported macOS version.

## Execution phases and gates

### Phase 0 — Record the baseline (2–4 days)

1. Build and test the current Rust backend on a clean Apple silicon host.
2. Record binary architecture, linked libraries, size, startup time, idle RSS,
   request latency, clean-install behavior, and notarization/codesign results.
3. Generate the 264-route manifest and identify the 88 current literal UI
   consumers plus non-UI callers.
4. Capture the current Rust toolchain, Cargo lockfile, native compile flags, and
   release artifact hashes.

**Exit gate:** reproducible baseline artifacts and no unexplained failing tests.

### Phase 1 — Freeze behavior as contracts (2–3 weeks)

1. Add schema/golden tests at the HTTP boundary rather than relying only on Rust
   unit tests.
2. Add a backend conformance runner that starts any executable on an isolated
   port and temporary `METALSHARP_HOME`.
3. Add command-spawn capture hooks and fixtures for launch/environment behavior.
4. Add state fixtures and migration round-trip tests.
5. Add native ABI layout tests and IPC framing vectors.
6. Classify routes as read-only, sandboxed mutation, process-launching,
   destructive, privileged, streaming, or diagnostic.

**Exit gate:** the existing Rust binary passes the language-neutral conformance
suite; destructive tests cannot touch the user's real MetalSharp home.

### Phase 2 — Compiler bakeoff and dependency reduction (1–2 weeks)

1. Pin candidate compiler commits/releases in a reproducible tool container or
   source bootstrap recipe. Never download an unpinned compiler during release.
2. Build each candidate for `aarch64-apple-darwin` with Apple Clang.
3. First compile a probe exercising threads, mutexes, unwinding/panics, atomics,
   TLS, filesystem, proc macros, FFI, `dlopen`, signals, and Mach calls.
4. Replace `ureq`/`rustls`/`ring` behind an outbound HTTP adapter if `ring`
   prevents translation. Keep the adapter usable by both backends.
5. Attempt the full locked MetalSharp graph; archive generated C, compiler logs,
   patches, build duration, and unsupported features.
6. Run all 629 tests and the conformance suite against the translated build.

**Go/no-go requirements:** Apple arm64 build from a clean host; zero known
miscompilations; all safe tests pass; generated C can be compiled by Apple
Clang without a Rust toolchain; ASan/UBSan show no new failures; codesign and
notarization work; startup/RSS are no worse than the recorded acceptance budget.

If no candidate passes, stop using transpilation as an implementation shortcut.
Continue with the same contract-first plan and port services manually. Do not
patch around unexplained compiler defects in production.

### Phase 3 — Establish the owned C core (2–4 weeks)

1. Create the CMake target, router, JSON/result layer, state adapters, logging,
   signal handling, and Apple platform adapters.
2. Implement `/status`, `/runtime/host-abi`, 404 behavior, raw responses, and
   liveness/startup first.
3. Make packaging accept a backend path supplied by the build, rather than a
   hard-coded Cargo target path.
4. Keep output name, resource destination, version, entitlements, and signing
   behavior unchanged.
5. Add a build switch: `METALSHARP_BACKEND=rust|c`. Rust remains the default
   until the C gates pass.

**Exit gate:** Electron can start, health-check, restart, stop, sign, and package
the C skeleton without UI changes.

### Phase 4 — Port by behavior domain (8–16+ weeks)

Move one domain at a time, using generated C only as a semantic reference. A
reasonable order is:

1. Status, configuration, logs, cache, and read-only diagnostics.
2. Scanning, route catalogs, recipes/rules, and other deterministic logic.
3. Update/setup state and read-only migration inspection.
4. Updater, installer, and migration mutations with failure injection.
5. Steam, GOG, Sharp Library, and process lifecycle.
6. Bottles and MTSP preparation/launch—the largest behavior surface.
7. Kernel-translation state models and IPC.
8. Mach/EndpointSecurity/dynamic-symbol operations last, behind explicit Apple
   adapters.

For each domain:

- translate/port types and pure logic;
- port its Rust unit tests to language-neutral fixtures or C tests;
- compare Rust and C responses bytewise where appropriate and structurally
  otherwise;
- compare filesystem trees, SQLite state, command ledgers, logs, and exit
  behavior after each scenario;
- enable the C domain only after its gate passes; retain an immediate build-time
  rollback to Rust.

Never point Rust and C mutation tests at the same state directory concurrently.
Clone a fixture for each backend, execute once, then diff the results.

### Phase 5 — Apple silicon and release hardening (2–4 weeks)

1. Run clean-machine tests on every supported macOS version and at least two
   Apple silicon generations.
2. Exercise APFS and the supported external-volume filesystems, long paths,
   Unicode, low disk space, permission denial, interrupted writes, sleep/wake,
   and app/backend crashes during updates and migration.
3. Run ASan/UBSan, fuzz HTTP/JSON/manifest/IPC/archive inputs, and stress signal,
   thread, and process cleanup paths.
4. Audit architecture and load commands with `file`, `lipo`, `otool`, and
   `codesign`; validate hardened runtime, entitlements, notarization, Gatekeeper,
   and DMG install/update.
5. Verify that the backend has no accidental Homebrew or developer-machine
   dylib references.
6. Compare real Steam, GOG, FNA/Mono, M9/M10/M11/M12, installer, bottle repair,
   updater, and migration flows against the C-compiled release.

**Exit gate:** all PR/release gates pass with the C backend, plus a release
candidate soak with no unexplained behavioral differences.

### Phase 6 — Controlled cutover and Rust removal (2 releases minimum)

1. Ship an opt-in preview with C as the selected backend and Rust available as a
   packaged fallback only for the preview channel.
2. Store backend selection and failure reason locally; do not silently switch
   implementations without leaving a diagnostic record.
3. After the preview gate, make C the default for a full beta release while
   retaining a documented rollback build.
4. After two successful release cycles, remove the Rust binary from the DMG and
   release workflow.
5. Then remove Cargo scripts, Cargo CI, `src-rust` packaging paths, Rust version
   bump locations, Dependabot Cargo config, and Rust-specific documentation.
6. Rename `rust-bridge.ts` only in this cleanup phase; its protocol should remain
   stable.
7. Retain the final Rust oracle source/tag and conformance fixtures for forensic
   comparison, even if it is no longer built in normal CI.

**Exit gate:** release artifacts contain no Rust-built backend or Rust runtime
dependency, the build/release process requires no Rust toolchain, and rollback
is possible by installing the last known-good release rather than by mixing two
backends in one stable build.

## CI and release changes

During migration, CI should have four lanes:

1. **Rust oracle:** current fmt/clippy/test/release build.
2. **C backend:** format/static analysis, unit tests, ASan/UBSan, release build.
3. **Differential conformance:** start both binaries on separate ports and
   isolated homes; compare approved scenarios.
4. **Packaging:** package the selected backend, verify arm64/load commands,
   codesign, build DMG, install, start, update, and query `/status`.

Recommended C checks include `clang-format`, `clang-tidy`, Apple Clang warnings
as errors, CMake/CTest, CodeQL C/C++, sanitizer jobs, dependency checksum/license
verification, route-manifest drift detection, and reproducible artifact hashes.

Update these known Rust-specific integration points only when their replacement
is ready:

- `app/package.json` build scripts and `extraResources` source path;
- `.github/workflows/{pr-ci,ci,release}.yml`;
- `tools/dmg/build-dmg.sh`, `tools/dmg/create-bundles.sh`, and bundle verifiers;
- `tools/release/set-version.sh`, `tools/release/bump-version.py`, and the
  all-surface release-version contract;
- updater process/version verification;
- `AGENTS.md`, local gates, issue templates, and CodeQL/Dependabot configuration.

## Acceptance criteria

The migration is complete only when all of these are true:

- The app, updater, migrator, diagnostic tools, and direct cover URLs require no
  caller-side compatibility changes.
- All 264 routes are either implemented and conformant or deliberately removed
  through a separately reviewed API change.
- All 629 existing tests have equivalent coverage in retained language-neutral
  tests or C tests; no test disappears solely because Rust was removed.
- Existing user state is opened in place and round-trips without unintended
  changes.
- Command/env/process behavior matches for every supported runtime route.
- Host ABI, IPC, Mach, EndpointSecurity, and dynamic-symbol layout tests pass on
  Apple arm64.
- Clean install, launch, game stop, repair, migration, update, rollback, signing,
  notarization, and DMG verification pass.
- The stable release build and packaging pipeline do not install or invoke
  `rustc`, Cargo, rustup, or a Rust-to-C compiler.
- The shipped backend is a native arm64 executable with only approved system or
  bundled dependencies.

## Estimated effort and staffing

This is a subsystem rewrite, not a mechanical compiler switch. A realistic
planning range is:

- 1 engineer: approximately 4–8 months, with release work slowed during the
  highest-risk porting phases.
- 2 engineers: approximately 10–18 weeks if one owns contracts/release and the
  other owns service/native porting.
- Compiler feasibility can be answered in 1–2 weeks, but a successful compiler
  spike does not remove the hardening and ownership work.

The largest uncertainty is not HTTP. It is the combination of bottles/MTSP
process orchestration, migration correctness, and the native kernel-translation
surface. The contract suite and isolated differential harness are therefore the
first deliverables, not cleanup of Cargo files.

## Immediate next milestone

Create a small PR containing only:

1. the generated route manifest;
2. the language-neutral conformance runner and temporary-home safety guard;
3. baseline measurements from the pre-conversion Rust release build;
4. a pinned Apple arm64 compiler-bakeoff script; and
5. a short ADR selecting either maintained-C conversion or generated-C
   distribution (the generated-C release path is implemented).

That milestone produces a defensible go/no-go result without committing the
project to an experimental compiler or destabilizing the current backend.
