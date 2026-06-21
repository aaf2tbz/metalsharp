# M12 Binary Archive Activation Roadmap — Offline-First Refined

Status: refined implementation roadmap. AC6 live launches still require explicit approval.

## Goal

Reduce AC6 post-Continue Metal compiler / PSO pressure by activating persistent `MTLBinaryArchive` plumbing for M12 render/compute pipeline creation, but **do not touch the live menu path until an offline AC6 corpus proves the archive path works first**.

The last two days exposed the real process failure: fixes aimed at the post-Continue world-load path repeatedly regressed startup/menu before Continue could be tested. This roadmap therefore makes menu parity a hard gate and moves proof into an offline corpus first.

Design requirements:

- silent by default;
- no live-game validation until offline archive proof is complete;
- no logs/tracing in AC6 launch payloads;
- no timeout/safe-mode keys in baseline/menu canaries unless explicitly requested;
- archive lookup can be bypassed independently from archive population;
- archive lookup has a process-lifetime circuit breaker if validation fails;
- preserve existing PE ↔ WineMetal thunk ABI layout.

## Architecture facts / constraints

- DXIL shaders are lowered to generated MSL source at runtime on cache miss.
- Runtime MSL source currently compiles through `newLibraryWithSource:options:error:`.
- `MTLBinaryArchive` can reduce final Metal PSO creation cost when descriptors/functions match a warm archive.
- `MTLBinaryArchive` does **not** by itself remove first-run generated-MSL source-library compilation. A later persistent generated-MSL → `.metallib` cache is still required to remove that class of overhead.
- Existing `WMTComputePipelineInfo` and `WMTRenderPipelineInfo` already contain archive fields:
  - `binary_archives_for_lookup`
  - `binary_archive_for_serialization`
  - `num_binary_archives_for_lookup`
  - `fail_on_binary_archive_miss`
- Objective-C paths in `m12core_metal.c` and `winemetal_unix.c` already map these fields into Metal pipeline descriptor archive properties.
- Do not add ABI fields unless a separate ABI/layout gate proves the change safe.

---

## Phase 1 — Completed offline AC6 binary-archive corpus proof

Goal: prove the binary archive mechanism works against representative AC6 render/compute PSOs **before** staging any live runtime or launching AC6.

This phase is mandatory. It replaces the old “implement first, canary in menu” approach.

### Corpus inputs

Build a self-contained offline corpus from existing AC6 cache/manifests, not from a new live run:

```text
~/.metalsharp/shader-cache/m12/1888160/*.msl
~/.metalsharp/shader-cache/m12/1888160/*.metallib
~/.metalsharp/shader-cache/m12/1888160/pso-render-*.json
~/.metalsharp/shader-cache/m12/1888160/pso-compute-*.json
```

Materialize a corpus manifest under results, for example:

```text
tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-<stamp>/corpus-manifest.json
```

The manifest must classify each PSO candidate:

- complete render PSO descriptor;
- complete compute PSO descriptor;
- missing shader artifact;
- incomplete descriptor metadata;
- unsupported/offline-incompatible shape.

Only complete candidates participate in archive proof. Incomplete candidates are recorded but must not be counted as archive failures.

### Offline archive proof harness

Add a focused offline probe/harness, e.g.:

```text
tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_corpus/
tools/d3d12-metal-sdk/scripts/run-m12-binary-archive-corpus.py
```

The harness must not launch Steam, Wine game processes, or AC6.

For every complete corpus candidate, or a bounded representative sample if the full corpus is too large:

1. Create a temporary archive path:

```text
/tmp/m12_binary_archive_corpus_<stamp>.binarchive
```

2. Create an in-memory archive.
3. Construct the Metal render/compute descriptor from corpus metadata.
4. Add the pipeline functions to the archive.
5. Create the PSO once without lookup dependency.
6. Serialize the archive.
7. Verify the archive file exists and is nonzero.
8. Reload the archive from disk.
9. Recreate the same PSO with archive lookup attached.
10. Record whether lookup-backed creation succeeds.

Coverage must include both native paths when available:

- m12core path;
- WineMetal fallback path.

If one path is not available in the offline harness, that limitation must be explicit in the summary.

### Strict lookup proof

In offline mode only, include a strict validation pass using archive lookup with miss failure enabled when possible:

```text
fail_on_binary_archive_miss = true
```

Purpose:

- prove the archive actually contains usable entries;
- distinguish “normal PSO creation succeeded” from “archive lookup was ignored/missed and silently compiled anyway.”

This strict pass is offline-only. Runtime AC6 launches still use:

```text
fail_on_binary_archive_miss = false
```

### Required output

The Phase 1 evidence bundle must include:

```text
corpus-manifest.json
archive-proof-summary.json
archive-proof-summary.md
archive file path + size + sha256
per-candidate pass/fail/classification counts
strict lookup pass/fail counts
unsupported/incomplete candidate counts
```

Minimum pass criteria:

- at least one render PSO archive add/serialize/reload/lookup-backed recreate passes;
- at least one compute PSO passes if complete compute candidates exist;
- serialized archive file is nonzero;
- strict lookup proof passes for the representative complete candidates;
- failures are classified, not hidden in logs;
- no AC6 launch occurred;
- no live runtime staging occurred.

Hard fail criteria:

- archive file missing or zero bytes;
- all strict lookup candidates miss/fail;
- archive reload fails for the generated archive;
- the probe requires live AC6/menu execution;
- the proof depends on runtime logging/tracing timing.

Acceptance:

- We know the binary archive path can create, serialize, reload, and satisfy lookups for real AC6-like descriptors before touching the menu path.
- If Phase 1 fails, do not implement/stage live binary archive plumbing yet; fix the offline harness/path first.

---

## Phase 2 — Completed PE-side archive manager, streamlined

Status: completed with offline proof in:

```text
tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/phase2-proof-summary.md
```

Implemented in:

```text
vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp
```

### Context structure

Use a stripped-down state container. Avoid tracking counters and avoid hot-path string allocations.

```cpp
struct M12BinaryArchiveContext {
  std::mutex mutex;
  WMT::Reference<WMT::BinaryArchive> archive;
  const char *native_path = nullptr;
  bool enabled = false;
  bool allow_lookup = false;
};
```

Rules:

- `native_path` is preformatted once during initialization and remains process-lifetime storage.
- No `std::string` allocation in PSO hot paths.
- No per-PSO diagnostic strings or trace identifiers.
- `allow_lookup` is the circuit-breaker state used by later validation.

### Environment configuration

Use binary switches only:

```text
DXMT_D3D12_BINARY_ARCHIVE=1
DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP=1
```

Meaning:

- `DXMT_D3D12_BINARY_ARCHIVE=1`: enable archive creation/population/serialization path.
- `DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP=1`: populate/archive-add is allowed, but lookup is disabled for the run.

No logging envs are required for normal operation.

### Default path logic

On initialization, preformat exactly one native archive file path using the Metal device registry ID.

Preferred default:

```text
$DXMT_PIPELINE_CACHE_PATH/m12-metal-binary-archive-<device-registry-id>.binarchive
```

Fallbacks, only if pipeline cache env is absent:

```text
$DXMT_SHADER_CACHE_PATH/m12-metal-binary-archive-<device-registry-id>.binarchive
/tmp/dxmt_shader_cache/m12-metal-binary-archive-<device-registry-id>.binarchive
```

Store the final path as process-lifetime `const char *` memory in the context.

### Startup behavior

On device/archive startup:

1. If `DXMT_D3D12_BINARY_ARCHIVE` is not enabled, leave context disabled.
2. Create parent directory best-effort.
3. Attempt:

```cpp
archive = wmt_device.newBinaryArchive(context.native_path, nullptr);
```

4. If the returned handle is null, immediately retry with a null path:

```cpp
archive = wmt_device.newBinaryArchive(nullptr, nullptr);
```

5. If that also fails, disable the archive for the process.
6. Do not log failures in the normal path.

Acceptance:

- Archive context initializes once per process/device.
- No hot-path string formatting.
- Cold missing archive falls back to empty in-memory archive.
- Existing archive file can be loaded silently.

### Required offline proof before Phase 3

Add and run a focused offline unit/probe gate that proves:

- env parsing enables/disables archive state correctly;
- bypass env disables lookup without disabling archive population;
- archive path formatting happens once and uses process-lifetime storage;
- existing archive load succeeds for the Phase 1 generated archive;
- missing/corrupt archive falls back to an empty in-memory archive;
- no Wine, Steam, AC6 launch, runtime staging, logging, or tracing occurs during the proof;
- the proof process is wrapped with a hard timeout and process-group kill.

Do not proceed to Phase 3 unless this offline proof passes and leaves the `dxmt_m12` runtime snapshot unchanged.

---

## Phase 3 — Completed descriptor attachment, refined layout

Status: completed with offline proof in:

```text
tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-141018/phase3-proof-summary.md
```

Goal: attach archive handles to Metal pipeline descriptors without introducing stack-lifetime hazards or tracing overhead.

Phase 3 makes descriptor archive fields live when `DXMT_D3D12_BINARY_ARCHIVE=1`, but Phase 4 still gates any live/menu/AC6 validation because Objective-C archive mutation serialization and exception safety are not installed yet.

### Lifetime enforcement

Do **not** place:

```cpp
obj_handle_t archive_handles[1]
```

on a transient stack frame if the pipeline creation can cross async worker boundaries.

Instead, embed the archive handle array directly inside the heap-allocated pipeline compilation request/payload structure that accompanies the async worker thread.

Example shape:

```cpp
struct M12PipelineCompilePayload {
  obj_handle_t heap_archive_handles[1];
  // existing pipeline compile/request state...
};
```

The handle array must outlive:

- WMT info population;
- m12core native creation call;
- WineMetal fallback creation call;
- any async worker handoff that owns the pipeline creation request.

### Descriptor population

Populate raw WMT structs directly. No text parsing, identifier tracing, or logging.

```cpp
if (context.allow_population)
  info.binary_archive_for_serialization = context.archive.handle;

if (context.allow_lookup && !env_bypass_lookup) {
  info.binary_archives_for_lookup.set(payload->heap_archive_handles);
  info.num_binary_archives_for_lookup = 1;
} else {
  info.binary_archives_for_lookup.set(nullptr);
  info.num_binary_archives_for_lookup = 0;
}

info.fail_on_binary_archive_miss = false;
```

Rules:

- Serialization/archive-add is gated separately from lookup and requires explicit population enablement.
- Lookup bypass must also avoid implicit archive mutation/population unless population is explicitly enabled.
- `fail_on_binary_archive_miss` remains false; archive misses must never fail gameplay.
- No WMT ABI layout changes.
- No archive descriptor logging.

Acceptance:

- Compute and render PSO paths attach `binary_archive_for_serialization` only when archive population is explicitly enabled.
- Lookup archive array is attached only when `allow_lookup=true` and bypass is off.
- Heap payload lifetime is proven for async worker use.

### Required offline proof before Phase 4

Add and run a descriptor-attachment offline proof that exercises compute and render payloads with:

- archive disabled;
- archive enabled with lookup bypassed;
- archive enabled with lookup allowed;
- circuit breaker forcing `allow_lookup=false`;
- async-worker-shaped heap payload lifetime.

The proof must assert:

- `binary_archive_for_serialization` is set only when archive population is explicitly enabled;
- lookup pointer/count are null/zero under bypass or circuit breaker;
- lookup pointer/count reference heap-owned payload storage when lookup is allowed;
- no transient stack archive-handle array is used across async boundaries;
- runtime `fail_on_binary_archive_miss=false`;
- no Wine, Steam, AC6 launch, runtime staging, logging, or tracing occurs;
- timeout/process-group kill is active.

Do not proceed to Phase 4 unless all descriptor cases pass offline.

---

## Phase 4 — Completed ObjC-side archive safety, zero overhead

Status: completed with offline proof in:

```text
tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/phase4-proof-summary.md
```

Implemented in both:

```text
vendor/dxmt/src/m12core/m12core_metal.c
vendor/dxmt/src/winemetal/unix/winemetal_unix.c
```

WineMetal mesh/tile archive-add sites are also guarded because they mutate the same process/shared `MTLBinaryArchive` object. Cross-module serialization between m12core and WineMetal fallback paths is provided by object-level `@synchronized(archive)`, which locks on the shared Objective-C archive object. Each module's local pthread mutex is only an intra-module belt-and-suspenders guard and must not be treated as a cross-shared-library lock.

### Mutex scope isolation

Use a localized archive mutation mutex strictly around the inner archive-add calls:

```objc
static pthread_mutex_t g_m12_binary_archive_mutex = PTHREAD_MUTEX_INITIALIZER;
```

Scope:

- `addComputePipelineFunctionsWithDescriptor:error:`
- `addRenderPipelineFunctionsWithDescriptor:error:`

Do **not** hold this mutex around full PSO compilation if the add call can be isolated.

### Silent exception guarding

Wrap archive-add calls in a silent `@try/@catch/@finally` block. No `printf`, no `NSLog`, no DXMT logging in failure paths.

```objc
pthread_mutex_lock(&g_m12_binary_archive_mutex);
@try {
  [archive addRenderPipelineFunctionsWithDescriptor:descriptor error:nil];
} @catch (NSException *exception) {
  descriptor.binaryArchives = nil;
} @finally {
  pthread_mutex_unlock(&g_m12_binary_archive_mutex);
}
```

Equivalent compute-path behavior:

```objc
pthread_mutex_lock(&g_m12_binary_archive_mutex);
@try {
  [archive addComputePipelineFunctionsWithDescriptor:descriptor error:nil];
} @catch (NSException *exception) {
  descriptor.binaryArchives = nil;
} @finally {
  pthread_mutex_unlock(&g_m12_binary_archive_mutex);
}
```

Rules:

- Archive-add exceptions must fall back to standard PSO creation.
- Failure must not poison final PSO creation error state.
- Failure must not generate logs or timing-sensitive output.
- If an exception occurs, clear descriptor archive lookup state for that descriptor before continuing.

Acceptance:

- Archive mutation is serialized.
- Exceptions cannot leak the mutex.
- Normal PSO creation continues if archive add fails.
- No logging overhead is introduced.

### Required offline proof before Phase 5

Add and run a native ObjC safety proof that verifies:

- render and compute `add*PipelineFunctionsWithDescriptor:error:` are isolated under the archive mutex;
- normal add success still allows baseline PSO creation;
- forced failure/exception paths unlock the mutex;
- failure clears descriptor archive lookup state for that descriptor;
- standard PSO creation continues after archive-add failure;
- stdout/stderr remain empty for archive failure paths;
- no Wine, Steam, AC6 launch, runtime staging, logging, or tracing occurs;
- timeout/process-group kill is active.

Do not proceed to Phase 5 unless silent fallback and mutex behavior are proven offline.

---

## Phase 5 — Completed serialization, batched and performance-focused

Status: completed with offline proof in:

```text
tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/phase5-proof-summary.md
```

Goal: serialize the warm archive periodically without stalling command recording or the broader device runtime. Periodic threshold flushes are dispatched asynchronously with a guarded best-effort fallback on thread-creation failure; a process-exit flush persists the trailing partial batch silently. Native serialization writes a temporary archive and atomically renames it into place only after a nonzero serialize, avoiding canonical archive corruption on teardown.

### Atomic flush threshold

Use a relaxed atomic PSO compile counter to decide when to flush.

```cpp
uint64_t current_count = pso_compile_counter.fetch_add(1, std::memory_order_relaxed);
if (current_count % 256 == 0) {
  MaybeSerializeM12BinaryArchive(context);
}
```

Rules:

- Counter increments after successful non-cache native PSO creation / archive-add opportunity.
- Flush interval defaults to 256.
- No logging on flush.
- Do not serialize every PSO.

### Lock sequence protection

Inside `MaybeSerializeM12BinaryArchive(context)`:

1. Check archive enabled and valid.
2. Check `native_path` non-null.
3. Grab the same archive mutation mutex used for Objective-C add calls immediately before serialization.
4. Call:

```cpp
archive.serialize(context.native_path, nullptr);
```

5. Release the archive mutex.
6. Ignore serialization failure silently; the next run can continue with normal compilation.

Design intent:

- Archive insertions and serialization are strictly ordered.
- The lock does not freeze global device/runtime state.
- The lock does not wrap command recording lists.
- Serialization failure never affects gameplay.

Acceptance:

- Archive file appears after enough PSO creations.
- Archive file is nonzero after successful serialization.
- No per-PSO serialization.
- No logging or tracing in normal operation.

### Required offline proof before Phase 6

Add and run a batched-serialization stress proof that verifies:

- relaxed atomic counter triggers serialization only at the intended interval;
- no per-PSO serialization occurs below the threshold;
- concurrent archive add + serialize is ordered by the archive mutex;
- output archive is nonzero and reloadable;
- strict lookup still passes for all initially valid selected corpus candidates after serialization;
- no global device/runtime lock is held around command recording simulation;
- no Wine, Steam, AC6 launch, runtime staging, logging, or tracing occurs;
- timeout/process-group kill is active.

Do not proceed to Phase 6 unless batched serialization passes offline under concurrency.

---

## Phase 5B — Generated-MSL `.metallib` loadability proof before gameplay canary

Status: planned as an offline confidence gate, not a runtime translation rewrite.

Goal: prove that persistent generated-MSL `.metallib` sidecars are not merely written and discovered, but are actually loadable and usable through the paths M12 will use when they exist. This phase respects the existing DXIL/HLSL → generated-MSL path: menu/UI rendering already proves that translation path is valuable and working, so Phase 5B must not replace, broaden, or perturb it.

### Scope

Use a temporary proof cache copied from the real AC6 generated shader cache:

```text
~/.metalsharp/shader-cache/m12/1888160/*.msl
```

Do not mutate the live shader cache during the proof. Do not stage runtime files. Do not launch Steam, Wine, wineserver, AC6, menu, or Continue.

### Proof shape

Add and run a focused offline runner:

```text
tools/d3d12-metal-sdk/scripts/run-m12-metallib-phase5b-proof.py
```

The runner must:

1. Select and record a bounded deterministic sample of real AC6 generated `.msl` sidecars.
2. Copy those `.msl` files into a results-local proof cache.
3. Materialize `.metallib` files using the existing Apple `metal`/`metallib` path.
4. Verify freshness, nonzero size, and `MTLB` header invariants.
5. Load every `.metallib` directly with Metal `newLibraryWithData` and find an entry function.
6. Load every `.metallib` through `m12core_create_shader_function` with `M12CORE_SHADER_FUNCTION_INPUT_METALLIB`.
7. Confirm the second `m12core_create_shader_function` call for each hash hits the in-process function cache.
8. Verify the DXMT source contract still attempts `<hash>.metallib` before generated-MSL source compile or DXIL lowering fallback.
9. Verify the `dxmt_m12` runtime snapshot is unchanged before/after the proof.
10. Wrap subprocesses in hard timeouts with process-group termination so a hung `metal`/`metallib`, `newLibraryWithData`, or `m12core_create_shader_function` probe cannot block the gate indefinitely.

### Acceptance

- Selected inputs are real AC6 generated-MSL sidecars and are copied into a proof-local cache.
- `.metallib` materialization succeeds for all selected hashes.
- Freshness validation passes for all selected hashes.
- Direct Metal load via `newLibraryWithData` passes for all selected hashes.
- M12Core metallib load via `M12CORE_SHADER_FUNCTION_INPUT_METALLIB` passes for all selected hashes.
- M12Core second-load cache-hit proof passes for all selected hashes.
- DXMT runtime contract proves `.metallib` is preferred before generated-MSL or DXIL fallback.
- Timeout/process-group kill behavior is active and proven by the runner.
- No live shader-cache mutation, runtime staging, Steam/Wine/AC6 launch, logging, or tracing occurs.

Do not use Phase 5B as justification for a gameplay/Continue canary unless the load/use gates pass. If direct Metal load passes but M12Core load fails, the artifact cache is not safe to rely on inside M12.

---

## Phase 6 — Silent validation gates / circuit breaker

Status: complete. Offline proof: `tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-164349/phase6-proof-summary.md` (`PASS`).

Goal: detect archive incompatibility before menu rendering and disable lookup for the process if the archive path is unsafe.

### Preflight validation timing

Run during initial device creation or equivalent early archive-context initialization. Do not do heavy validation in `DllMain` if loader-lock constraints apply.

The validation must be silent.

### Validation shape

Perform a minimal in-process check:

1. Create a temporary in-memory archive.
2. Construct a minimal dummy pipeline descriptor template suitable for the current device/path.
3. Add it to the temporary archive.
4. Serialize it to a process-unique temporary validation file under the resolved archive/cache directory.
5. Inspect serialization success and resulting file size.
6. Remove the validation file and its `.tmp` sidecar.

### Circuit breaker action

If serialization fails or produces an empty container:

```cpp
context.allow_lookup = false;
```

for the entire duration of the process.

If validation passes and a real archive exists/loads successfully:

```cpp
context.allow_lookup = true;
```

unless `DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP=1` is set.

Rules:

- No logs are generated.
- Archive population is disabled by default and requires explicit `DXMT_D3D12_BINARY_ARCHIVE_POPULATE=1`.
- Lookup is never enabled after validation failure.
- Invalid/corrupt archive layouts must degrade to standard compilation, not game failure.

Acceptance:

- Bad archive/driver/layout combinations safely disable lookup before gameplay.
- Good archive layouts allow lookup when bypass is off.
- Validation has no visible log output.

### Required offline proof before Phase 7

Add and run a circuit-breaker offline proof that covers:

- good archive: validation passes and `allow_lookup=true` when bypass is off;
- bypass env: validation may pass but lookup remains disabled;
- missing/corrupt/empty archive: `allow_lookup=false`;
- serialization failure: `allow_lookup=false`;
- archive population remains disabled by default when lookup is disabled;
- explicit `DXMT_D3D12_BINARY_ARCHIVE_POPULATE=1` enables population without enabling lookup;
- no logs/tracing are emitted by validation failure paths;
- no Wine, Steam, AC6 launch, runtime staging, logging, or tracing occurs;
- timeout/process-group kill is active.

Do not proceed to any live menu canary unless Phase 1 and all Phase 2–6B offline proofs pass.

---

## Phase 6B — DXMT generated-MSL `.metallib` safety guard / circuit breaker

Status: complete. Offline proof: `tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/phase6b-proof-summary.md` (`PASS`). This is a runtime-safety hardening phase, not runtime `.metallib` materialization.

Goal: ensure custom DXMT can safely consume pre-existing/generated-MSL `.metallib` sidecars when they exist, while never letting stale, corrupt, wrong, or failed `.metallib` artifacts poison startup/menu/gameplay. Phase 5B proved valid `.metallib` files are loadable through Metal and M12Core; Phase 6B makes the runtime lookup policy safe enough to coexist with Phase 6 binary-archive validation.

### Why this is needed

Current custom DXMT already attempts `<hash>.metallib` before generated-MSL source compile. However, the lookup policy currently treats file existence as sufficient availability. That is too weak for live/gameplay activation because a bad sidecar can force the cached-metallib path and return shader failure instead of falling back to the proven DXIL/HLSL → generated-MSL path.

Phase 6B must preserve the working generated-MSL fallback. It must not introduce runtime `xcrun metal`/`metallib` subprocesses, and it must not attempt to serialize `newLibraryWithSource` output into `.metallib` inside the game process.

### Runtime policy shape

Harden `m12core_probe_shader_cache()` / PE-side use so `metallib_available` means "safe to try", not merely "file exists".

A `.metallib` is available only when:

- `DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE` is not enabled;
- the file exists as a regular file;
- file size is nonzero;
- the first four bytes are `MTLB`;
- if `<hash>.msl` exists, `.metallib` is not older than `.msl`;
- no active `<hash>.metallib.err.txt` exists newer than or equal to the `.metallib`;
- the process-local bad-metallib denylist has not disabled this hash after a previous load/function failure.

Rules:

- A missing or unavailable `.metallib` must silently fall back to the existing generated-MSL / DXIL lowering path.
- A `.metallib` load failure or function-lookup failure must mark the hash unavailable for the current process and retry via generated-MSL fallback, not fail the PSO immediately.
- Fallback after a bad `.metallib` must not rewrite or weaken the proven DXIL/HLSL → MSL translation path.
- Runtime `.metallib` writeback/materialization remains out of scope. Offline/launcher-managed materialization can be considered later after 6B passes.
- Runtime cached-`.metallib` load/function failures use a process-local denylist and source fallback; they do not write new `.metallib.err.txt` sidecars. `.metallib.err.txt` is treated as an offline materializer/external quarantine marker for cross-session policy.
- Preserve existing C/POD ABI discipline. Do not add ABI fields unless a separate ABI/layout proof requires it.
- Silent by default: no logging/tracing in normal menu/gameplay paths.

### Required offline proof before Phase 7

Add and run a focused Phase 6B proof harness using temporary shader-cache fixtures only. It must not use the live AC6 shader cache except as read-only source material copied into a proof cache.

The proof must cover:

- valid fresh `.metallib`: `metallib_available=true`, direct Metal load passes, M12Core load passes;
- missing `.metallib`: unavailable and source fallback remains reachable;
- zero-byte `.metallib`: unavailable;
- invalid-header `.metallib`: unavailable;
- stale `.metallib` older than `.msl`: unavailable;
- active `.metallib.err.txt`: unavailable;
- `DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1`: unavailable even if valid;
- load failure / function lookup failure: process-local denylist is set and fallback path is selected;
- no generated `.metallib`, `.air`, or error artifact is written into the live shader cache;
- no Wine, Steam, AC6 launch, runtime staging, logging, or tracing occurs;
- timeout/process-group kill is active.

Acceptance:

- Bad/stale/poisoned `.metallib` files cannot force shader failure when source fallback is available.
- Valid `.metallib` files remain usable through the Phase 5B-proven Metal and M12Core paths.
- The generated-MSL fallback remains byte-for-byte/source-contract equivalent outside the guarded retry path.
- Runtime writeback/materialization is still absent unless a later explicit phase adds it.
- The proof records source checks, fixture matrix, command outputs, and runtime/cache snapshots.

Do not proceed to a live menu canary that relies on binary archive or `.metallib` warm-cache behavior unless Phase 6 and Phase 6B offline proofs both pass.

---

## Phase 7 — Baseline/menu canary gate

Goal: prove the startup/menu path still behaves like Slice-3 before any Continue attempt.

Required before launch:

- Phase 1 offline corpus proof is complete and committed;
- Phase 2–6B offline proofs all pass on the exact source being staged;
- no proof required Wine, Steam, AC6, runtime staging, logging, or tracing;
- restored/rebuilt baseline can still launch menu;
- `dxmt_m12` route verified;
- `mscompatdb` absent;
- backend on `METALSHARP_PORT=9277`;
- dry-run `ok=true`, `pipeline=m12`;
- preflight passes, including staged-runtime hash parity against the exact local build being canaried;
- no logging/tracing/timeouts in request.

Launch env for binary archive menu canary:

```text
METALSHARP_M12_LOG_LEVEL=none
METALSHARP_M12_LOG_PATH=none
METALSHARP_M12_TRACE_CAPTURE=0
METALSHARP_M12_BINARY_ARCHIVE=1
METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP=1
# Intentionally omit METALSHARP_M12_BINARY_ARCHIVE_POPULATE for Phase 7 menu parity.
```

No Continue in this phase.

Acceptance:

- Menu renders normally.
- If menu no-renders or exits before D3D12 device creation, the change is rejected and restored.
- No post-Continue testing until menu parity is proven.

---

## Phase 8 — Armored Core VI deployment flow

AC6 live launches require explicit user approval.

### Current Phase 8 truth — full-DLL staged run fixed the apparent failure

Status: **fixed / do not treat the earlier Phase 8A mixed-runtime hang as current evidence**.

The important Phase 8 outcome was not a binary-archive runtime regression. The apparent post-Continue failure was invalidated after discovering that the staged M12 runtime had incomplete DLL coverage: `d3d11.dll` and `d3d10core.dll` were not explicitly rebuilt/staged/preflight-parity-checked with the rest of the M12 surface.

The corrected staging work added full build/stage/preflight coverage for the atomic M12 DLL set:

```text
d3d12.dll
d3d11.dll
d3d10core.dll
dxgi.dll
dxgi_dxmt.dll
winemetal.dll
winemetal.so
libm12core.dylib
```

Evidence:

```text
Full-DLL staging / preflight:
tools/d3d12-metal-sdk/results/m12-full-dll-staging-fix-20260620-215250/

Full-DLL AC6 launch + Continue/turtle capture:
tools/d3d12-metal-sdk/results/m12-full-dll-staging-fix-launch-20260620-215442/
```

Observed result:

- corrected full-DLL staging/preflight passed with `ok=true`, `failure_count=0`, and build-to-staged hash parity for the full atomic DLL set;
- AC6 launched via `POST /steam/launch-game` on `METALSHARP_PORT=9277` with pipeline `m12`;
- user confirmed the full-DLL staged run rendered / worked;
- post-Continue turtle capture was collected and AC6 was quit;
- the turtle sample did **not** show the earlier DXIL→MSL lowering storm; it mainly showed Metal/IOGPU command queue submission:

```text
com.Metal.CommandQueueDispatch
-[_MTLCommandQueue _submitAvailableCommandBuffers]
-[IOGPUMetalCommandQueue submitCommandBuffers:count:]
-[IOGPUMetalCommandQueue _submitCommandBuffers:count:]
```

Interpretation:

- the earlier `m12-phase8a-continue-canary-population-off-20260620-183420` black-screen/hang is **superseded** because it was captured under partial/mixed runtime staging;
- do not use that older Phase 8A result as proof that population-off binary archive or Phase 8 currently fails;
- before any future AC6 Continue canary, require full-DLL build/stage/preflight parity for the atomic set above;
- keep binary archive population split from enable/lookup: population remains opt-in only.

### Safe Phase 8 launch mode after the fix

Population-off / lookup-bypassed canaries should use:

```text
METALSHARP_M12_LOG_LEVEL=none
METALSHARP_M12_LOG_PATH=none
METALSHARP_M12_TRACE_CAPTURE=0
METALSHARP_M12_BINARY_ARCHIVE=1
METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP=1
# Omit METALSHARP_M12_BINARY_ARCHIVE_POPULATE unless explicitly testing population.
```

Required gates before treating a result as valid:

- route is `POST /steam/launch-game`;
- port is `METALSHARP_PORT=9277`;
- runtime route is `~/.metalsharp/runtime/wine/lib/dxmt_m12`;
- `mscompatdb` remains absent;
- no stale/partial staged runtime;
- full atomic DLL set above has build↔staged hash parity;
- `d3d11.dll` and `d3d10core.dll` are explicitly included in build/stage/preflight, not merely assumed because files exist.

### Phase A baseline lock guard

Use this offline guard before any future AC6/M12 live evidence run:

```text
tools/d3d12-metal-sdk/scripts/lock-m12-phase8-baseline.sh
```

The guard does not launch Steam, Wine games, or AC6. It verifies:

- `mscompatdb` is absent;
- `preflight-runtime-layout.py` passes against `~/.metalsharp/runtime/wine/lib/dxmt_m12`;
- the full atomic M12 DLL set has build↔staged SHA-256 parity.

Latest Phase A evidence:

```text
tools/d3d12-metal-sdk/results/m12-phase8-baseline-lock-20260620-221605/phase8-baseline-lock-summary.md
```

Result: `ok=true`, `failure_count=0`, `winemetal_abi_ok=true`, full atomic runtime parity green.

### Deferred — live archive population / warm lookup

Live binary archive population remains deferred until redesigned/proven offline. The original Run 1/Run 2 archive plan is still conceptually useful, but it must not be interpreted as the current successful Phase 8 path.

If population is reintroduced later, use separate explicit phases:

1. population omitted: menu + Continue parity baseline;
2. population enabled only after offline proof and menu parity;
3. warm lookup enabled only after population safety is proven.

---

## Follow-up — persistent generated-MSL / `.metallib` cache

`MTLBinaryArchive` is necessary but not sufficient for full first-run AC6 world-load compile pressure. A later phase must add persistent generated-MSL artifact caching:

- generated MSL source keyed by DXIL/root signature/PSO-affecting state;
- compiled `.metallib` artifact keyed by source + compile options + device compatibility;
- `newLibraryWithData:error:` fast path before `newLibraryWithSource`;
- freshness/invalidation checks;
- offline probe proving warm `.metallib` path avoids source compile.

This should be implemented separately after binary archive lookup/population is proven safe offline and through the menu canary.
