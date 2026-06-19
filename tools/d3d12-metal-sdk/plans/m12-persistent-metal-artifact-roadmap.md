# M12 Persistent Metal Artifact Roadmap

## Problem statement

Elden Ring's Type A/B -> character creator/name-finalization transition still freezes after fixing repeated DXIL->MSL lowering. The latest force-source-off run proved:

- cached `.msl` sidecars are reused (`DXIL loaded from cached MSL source by libm12core OK`).
- post-click samples no longer show `_WMTM12CoreLowerDXILToMSL`, `m12core_lower_dxil_to_msl`, or `MSLLowering::lower`.
- post-click samples now show Metal `newLibraryWithSource` activity.
- the live Elden cache has thousands of `.msl` files but almost no `.metallib` files.

Therefore the current warm cache is not a compiled Metal cache. It is a source cache. Real game loading can still stall because MSL source is compiled live into Metal libraries/functions in the transition window.

## Current architecture facts

From `research/local-m12-cache-implementation.md`:

- Runtime writes `.msl`, `.dxbc`, reflection JSON, module/report/error files.
- Runtime reads `.metallib` if present.
- Runtime does not write `.metallib`.
- `m12core_create_shader_function(MSL_SOURCE)` calls `newLibraryWithSource` and stores only an in-process `MTLFunction` cache.
- `m12core_create_shader_function(METALLIB)` uses `newLibraryWithData` and is the desired fast warm-cache path.
- Render/compute PSO caches are in-memory only in both PE and native layers.
- `MTLBinaryArchive` hooks exist in native pipeline creation, but PE initializes `binary_archive_for_serialization` to null and no disk archive lifecycle is wired.

## Operating constraints

- No more game launches until offline gates pass.
- Preserve PE fallback at every seam.
- Keep libm12core ABI C/POD/scalar-only.
- Native execution gates default off unless explicitly enabled.
- Do not commit generated `.metallib`, `.air`, DXBC blobs, raw D3DMetal payloads, or logs.
- Preserve existing live shader caches unless an explicit validation step requires a copy/sandbox.
- Every slice is serial: plan -> implement one slice -> run focused offline gate -> inspect result -> only then continue.

## Acceptance target

Before another visual Elden launch, the runtime/offline pipeline must prove:

1. hot `.msl` sidecars can be materialized into `.metallib` sidecars atomically.
2. existing runtime `.metallib` path loads those sidecars via `newLibraryWithData` / `M12CORE_SHADER_FUNCTION_INPUT_METALLIB`.
3. snapshot trace for an offline/probe workload shows no `newLibraryWithSource` for pre-materialized hashes.
4. force-source default remains off unless explicitly requested.
5. failed materialization is quarantined and does not poison future runtime loads.
6. API/Shader validation knobs are documented and opt-in, not default-on for performance runs.

---

# Slice 0 — Evidence + docs consolidation (read-only)

## Goal
Create a compact evidence base from Apple docs and current code explaining why persistent compiled Metal artifacts are mandatory for M12 warm-cache behavior.

## Work
- Keep `research/local-m12-cache-implementation.md` as local code evidence.
- Add/collect Apple official doc notes for:
  - `newLibraryWithSource` vs `newLibraryWithData`/URL.
  - `xcrun metal` + `xcrun metallib` offline compilation.
  - `MTLBinaryArchive` and pipeline descriptor preloading/serialization.
  - asynchronous library compilation APIs.
  - Metal API Validation and Shader Validation.
- Update this plan with doc links once background researchers finish.

## Gate
- Plan contains official-doc links or notes and exact local file references.
- No source/runtime changes.

## Verified Apple docs

Background researchers completed but noted SearXNG was down. Direct parent `web_fetch` verified the core Apple pages:

- [Shader libraries](https://developer.apple.com/documentation/metal/shader-libraries): Apple says Xcode compiles shader source into libraries at build time; apps can load libraries from URL/data or create one from source; binary archives are precompiled static libraries for specific GPU architectures that avoid runtime shader compilation.
- [Metal libraries](https://developer.apple.com/documentation/metal/metal-libraries): Apple says Metal IR is GPU-independent and still compiles to a GPU-specific binary at app runtime; string-provided shaders first compile to Metal IR on device, then go through secondary GPU compilation; to distribute GPU-specific binaries and avoid runtime shader compilation, use Metal binary archives.
- [Building a shader library by precompiling source files](https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files): Apple documents command-line precompile flow with `xcrun -sdk macosx metal -c` to intermediate representation and then library/archive toolchain output.
- [Metal binary archives](https://developer.apple.com/documentation/metal/metal-binary-archives): Apple says binary archives are precompiled GPU-specific binaries shipped with an app to avoid runtime compilation of Metal shaders.
- [Creating binary archives from device-built pipeline state objects](https://developer.apple.com/documentation/metal/creating-binary-archives-from-device-built-pipeline-state-objects): Apple documents serializing `MTLBinaryArchive` data from app runtime and using Metal translator tooling for GPU-specific archive builds.

## Status
Complete for planning purposes. Evidence supports the staged approach: first persist loadable `.metallib` sidecars from existing `.msl`, then add freshness/quarantine gates, then consider binary archives / PSO persistence.

---

# Slice 1 — Offline `.msl` -> `.metallib` materializer

## Goal
Provide a deterministic offline tool that compiles persisted M12 `.msl` sidecars into persistent `.metallib` sidecars without launching games.

## Implemented
- Added `tools/d3d12-metal-sdk/scripts/materialize-m12-msl-metallibs.py`.
- Added `tools/d3d12-metal-sdk/scripts/test-materialize-m12-msl-metallibs.py`.
- The materializer:
  - compiles via `xcrun metal -x metal -std=metal3.1 -c` then `xcrun metallib`.
  - writes atomically via temp file + `os.replace`.
  - keeps `.air` in temporary directories only.
  - normalizes trace hashes to 16 hex, fixing `63ef...` -> `063ef...` and `e43ff...` -> `0e43ff...`.
  - reports missing `.msl` sources instead of silently dropping requested hashes.
  - supports `--dry-run`, `--force`, `--strict`, `--hash`, `--hash-file`, JSON reports, and bounded worker count.
  - writes `<hash>.metallib.err.txt` on compile/link failure and leaves no final `.metallib`.

## Gate evidence
Run without game launch:

```bash
python3 -m py_compile \
  tools/d3d12-metal-sdk/scripts/materialize-m12-msl-metallibs.py \
  tools/d3d12-metal-sdk/scripts/test-materialize-m12-msl-metallibs.py
./tools/d3d12-metal-sdk/scripts/test-materialize-m12-msl-metallibs.py
```

Result: `materialize-m12-msl-metallibs self-test: ok`.

Hot-window cache gate:

- Hash manifest: `tools/d3d12-metal-sdk/results/elden-ring-hot-window-hashes-20260618-150512.txt`
- Dry-run report: `tools/d3d12-metal-sdk/results/elden-ring-hot-window-msl-metallib-dryrun-slice1-20260618-152308.json`
  - `total=21`, `fresh=20`, `would_build=1`
- Materialize report: `tools/d3d12-metal-sdk/results/elden-ring-hot-window-msl-metallib-materialize-slice1-20260618-152308.json`
  - `total=21`, `fresh=20`, `ok=1`
- Final strict dry-run: `tools/d3d12-metal-sdk/results/elden-ring-hot-window-msl-metallib-strict-dryrun-slice1-20260618-152321.json`
  - `total=21`, `fresh=21`, `nonfresh=[]`

Acceptance met:
- exit 0 for valid hashes.
- generated reports have only `ok`/`fresh` after materialization.
- all hot hashes with `.msl` have fresh `.metallib`.
- no `.air` remains in live cache.

## Status
Complete.

---

# Slice 2 — Offline runtime metallib-load probe

## Goal
Prove the existing M12 runtime path uses `.metallib` sidecars and bypasses `newLibraryWithSource` for pre-materialized hashes without launching Elden Ring.

## Implemented
- Added direct Metal load probe:
  - `tools/d3d12-metal-sdk/probes/probe_metal_metallib_load/probe_metal_metallib_load.m`
  - `tools/d3d12-metal-sdk/scripts/probe-metal-metallib-load.sh`
- Added libm12core ABI load probe:
  - `tools/d3d12-metal-sdk/probes/probe_m12_metallib_load/probe_m12_metallib_load.m`
  - `tools/d3d12-metal-sdk/scripts/probe-m12-metallib-load.sh`
- Added source-contract gate:
  - `tools/d3d12-metal-sdk/scripts/verify-m12-metallib-runtime-contract.py`
- Hardened native `libm12core.dylib` linkage metadata in `vendor/dxmt/src/m12core/meson.build`:
  - explicit `-mmacosx-version-min=12.0` for C/C++/link.
  - strong `Foundation`/`Metal` framework links instead of weak links for this standalone native dylib.
- Fixed standalone probe runtime loading:
  - root cause of the previous `EXC_BAD_ACCESS (address=0xbad4007)` was not invalid `.metallib` data and not `m12core_create_shader_function` itself.
  - `DYLD_LIBRARY_PATH` included the raw LLVM 15 x86_64 toolchain lib directory, which can poison Apple Metal's x86_64 framework path and crash `MTLCreateSystemDefaultDevice()` before libm12core is called.
  - the probe now defaults to staged `~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix/libm12core.dylib` and uses only that staged runtime directory at execution time; it does not append the raw toolchain lib directory to `DYLD_LIBRARY_PATH`.

## Gate evidence
Direct Metal `newLibraryWithData` proof:

- Result dir: `tools/d3d12-metal-sdk/results/probe-metal-metallib-load-hot-window-slice2-20260618-153035`
- `summary total=21 failures=0`
- All 21 hot-window `.metallib` sidecars loaded and resolved functions.

libm12core ABI `M12CORE_SHADER_FUNCTION_INPUT_METALLIB` proof:

- Single-hash debug result: `tools/d3d12-metal-sdk/results/probe-m12-metallib-load-single-slice2-20260618-154113`
  - first call: `status=0`, `cache_hit=0`.
  - second call: `status=0`, `cache_hit=1`, same function handle.
- Hot-window result: `tools/d3d12-metal-sdk/results/probe-m12-metallib-load-hot-window-slice2-clean-20260618-154312`
  - `summary total=21 failures=0`
  - every hash reports `first_cache_hit=0 second_cache_hit=1`.

DXMT source-contract proof:

- Report: `tools/d3d12-metal-sdk/results/m12-metallib-runtime-contract-slice2-20260618-154312.json`
- Gate command:

```bash
python3 -m py_compile tools/d3d12-metal-sdk/scripts/verify-m12-metallib-runtime-contract.py
./tools/d3d12-metal-sdk/scripts/verify-m12-metallib-runtime-contract.py \
  --json tools/d3d12-metal-sdk/results/m12-metallib-runtime-contract-slice2-20260618-154312.json
```

Result: `summary ok=true checks=9`.

The contract verifies:
- `WMTM12CoreProbeShaderCache` precedes `.metallib` open/policy.
- `.metallib` open occurs before `if (!mf)` fallback.
- cached `.msl` source reuse is contained inside the `!mf` fallback branch.
- DXIL parsing/lowering is contained inside the `!mf` fallback branch.
- on metallib hit, DXMT calls `M12CORE_SHADER_FUNCTION_INPUT_METALLIB` before WMT `newLibrary` fallback.

## Status
Complete. Offline evidence proves the hot `.metallib` sidecars are valid, `libm12core.dylib` can load them through the intended ABI, and DXMT's source path is guarded so cached `.msl`/DXIL lowering are only reached when `.metallib` is unavailable.

---

# Slice 3 — Cache freshness + quarantine gate

## Goal
Prevent stale/bad `.metallib` files from being trusted and prevent failed materialization from poisoning runtime.

## Implemented
- Added focused verifier:
  - `tools/d3d12-metal-sdk/scripts/verify-m12-metallib-freshness.py`
- Added crafted fixture self-test:
  - `tools/d3d12-metal-sdk/scripts/test-verify-m12-metallib-freshness.py`
- The verifier detects:
  - `.metallib` missing for a requested `.msl` hash.
  - `.metallib` older than `.msl`.
  - active `.metallib.err.txt` newer than or equal to `.metallib`.
  - zero-byte `.metallib`.
  - invalid `.metallib` header (`MTLB` expected).
  - optional `xcrun metal-objdump` validation via `--check-objdump`.
- Quarantine semantics:
  - old `.metallib.err.txt` is informational if a newer valid `.metallib` exists.
  - active error markers poison strict freshness.
  - partial/zero-byte/bad-header `.metallib` fails strict mode.

## Gate evidence
Run without game launch:

```bash
python3 -m py_compile \
  tools/d3d12-metal-sdk/scripts/verify-m12-metallib-freshness.py \
  tools/d3d12-metal-sdk/scripts/test-verify-m12-metallib-freshness.py
./tools/d3d12-metal-sdk/scripts/test-verify-m12-metallib-freshness.py
```

Result: `verify-m12-metallib-freshness self-test: ok`.

Hot-window strict gate:

```bash
./tools/d3d12-metal-sdk/scripts/verify-m12-metallib-freshness.py \
  --cache-dir /Users/alexmondello/.metalsharp/shader-cache/m12/1245620 \
  --hash-file tools/d3d12-metal-sdk/results/elden-ring-hot-window-hashes-20260618-150512.txt \
  --strict \
  --out tools/d3d12-metal-sdk/results/elden-ring-hot-window-metallib-freshness-slice3-20260618-154627.json
```

Result:
- `summary total=21 fresh=21 nonfresh=0`
- Report: `tools/d3d12-metal-sdk/results/elden-ring-hot-window-metallib-freshness-slice3-20260618-154627.json`

## Status
Complete for the Elden hot-window manifest. Strict freshness/quarantine now has a repeatable offline gate.

---

# Slice 4 — Prewarm pack / transition hotset materialization

## Goal
Make cache warm-up a repeatable artifact-generation step for Elden hot transition shaders before a visual run.

## Implemented
- Added metadata-only hotset manifest builder:
  - `tools/d3d12-metal-sdk/scripts/build-m12-metallib-prewarm-manifest.py`
- Built the minimal Elden Type A/B hot-window pack from:
  - `tools/d3d12-metal-sdk/results/elden-ring-hot-window-hashes-20260618-150512.txt`
- The manifest records hash order/readiness only. It does not copy `.msl`, `.metallib`, DXBC, raw D3DMetal payloads, or logs.

## Gate evidence
Run without game launch:

```bash
python3 -m py_compile tools/d3d12-metal-sdk/scripts/build-m12-metallib-prewarm-manifest.py
./tools/d3d12-metal-sdk/scripts/build-m12-metallib-prewarm-manifest.py \
  --cache-dir /Users/alexmondello/.metalsharp/shader-cache/m12/1245620 \
  --hash-file tools/d3d12-metal-sdk/results/elden-ring-hot-window-hashes-20260618-150512.txt \
  --game elden-ring \
  --appid 1245620 \
  --profile elden-ring-type-ab-hot-window \
  --out tools/d3d12-metal-sdk/results/elden-ring-type-ab-hot-window-prewarm-manifest-slice4-20260618-154744.json
```

Result:
- `summary hashes=21 ready=21 nonfresh=0`
- Manifest: `tools/d3d12-metal-sdk/results/elden-ring-type-ab-hot-window-prewarm-manifest-slice4-20260618-154744.json`

Strict materializer dry-run:

- Report: `tools/d3d12-metal-sdk/results/elden-ring-type-ab-hot-window-materialize-strict-dryrun-slice4-20260618-154744.json`
- Result: `total=21`, `fresh=21`.

Strict freshness gate:

- Report: `tools/d3d12-metal-sdk/results/elden-ring-type-ab-hot-window-freshness-slice4-20260618-154744.json`
- Result: `summary total=21 fresh=21 nonfresh=0`.

## Status
Complete for the minimal Elden Type A/B hot-window pack. A broader Elden high-priority pack can be generated later from wider traces or cache inventory, but it is not required for the first post-gate visual validation.

---

# Slice 5 — Runtime source-compile throttling / async fallback

## Goal
If `.metallib` is absent, prevent unbounded `newLibraryWithSource` spikes from blocking game-load transition threads.

## Work options
Pick one after Slice 1-4 evidence:

A. throttle synchronous source compiles with a small semaphore (e.g. 1-2 concurrent `newLibraryWithSource`) and explicit trace/counters.

B. move source compilation to async Metal API (`newLibraryWithSource:options:completionHandler:`) where compatible with DXMT's PSO creation contract.

C. keep runtime synchronous but require/offload materialization before launch for known games.

## Gate
- Offline stress probe creates many `.msl` loads without `.metallib` and proves max concurrency is bounded.
- Existing probes still pass.
- No game launch.

---

# Slice 6 — MTLBinaryArchive / persistent PSO cache design

## Goal
Move beyond shader-library persistence toward persistent PSO/pipeline compilation artifacts.

## Work
- Design libm12core-owned binary archive lifecycle:
  - C/POD ABI only.
  - create/open archive under cache root.
  - attach archive to render/compute pipeline descriptors.
  - serialize atomically.
  - fail-on-miss opt-in only for validation.
- Do not implement until Slices 1-5 are stable.

## Gate
- Design doc approved.
- Small native-only probe demonstrates archive create/add/serialize/load behavior.
- Runtime gate remains off by default.

---

# Slice 7 — API/Shader validation profile

## Goal
Add an explicit validation launch/probe profile for development, not performance.

## Work
- Document Metal API Validation / Shader Validation options usable from CLI/Xcode/environment.
- Add MetalSharp profile/env gating where possible.
- Ensure validation is opt-in and not used for performance/freeze timing runs.

## Gate
- Dry-run shows validation profile env/options only when requested.
- Normal dry-run remains unchanged.

---

# Slice 8 — First post-gate visual launch

## Prerequisites
- Slices 1-4 complete and passing.
- Slice 5 either complete or intentionally deferred with rationale.
- Hot transition `.metallib` pack strict freshness passes.
- Runtime trace for offline probe proves `newLibraryWithData`, not `newLibraryWithSource`, for hot hashes.

## Launch constraints
- User approval required.
- Use `snapshot90` or a new tiny `metallib90` profile.
- No broad logging.
- Expected trace assertions:
  - no DXIL lowering.
  - no cached MSL source load for hot materialized hashes.
  - metallib cache hits present.
  - no queue/fence/present deadlock evidence.

## Visual acceptance
- Type A/B -> character creator/name-finalization either progresses or, if it still freezes, samples/trace must show a new bottleneck other than DXIL lowering or `newLibraryWithSource` for materialized shaders.
