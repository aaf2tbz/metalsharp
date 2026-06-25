# Code Context — Local M12/DXMT Shader & Pipeline Cache Implementation

Scout of the M12/DXMT shader and pipeline cache stack in
`/Users/alexmondello/metalsharp-m12-lab` (branch `fix/m12-shader-probe-lab`,
uncommitted edits present in `d3d12_pipeline_state.cpp`/`d3d12_device.cpp`).

## Files Retrieved

1. `vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp` (lines 1-130, 400-705, 980-1100, 1468-1500, 1500-2140, 2420-2530, 2880-2990) — **the runtime cache core**. Holds in-memory shader cache, metallib lookup/load, cached-MSL fast path, DXIL→MSL lowering dispatch, and both PSO cache layers.
2. `vendor/dxmt/src/d3d12/d3d12_pipeline_state.hpp` (lines 173-174) — `s_shader_cache` / `s_shader_mutex` static members.
3. `vendor/dxmt/src/d3d12/d3d12_m12core_counters.hpp` (whole file) — PE→native counter batching bridge (64-event flush window).
4. `vendor/dxmt/src/d3d12/d3d12_shader_compiler.cpp` (whole file, 379 lines) — a **second, NOT-yet-wired** modular compiler backend (`MetalIRCompilerBackend`/`DebugMSLEmitterBackend`) with its own persistent-metallib read path.
5. `vendor/dxmt/src/d3d12/d3d12_shader_compiler.hpp` (whole file) — `D3D12ShaderCompileRequest`/`Output` POD contract for the unwired backend.
6. `vendor/dxmt/src/m12core/m12core.h` (lines 1-1788) — full libm12core C ABI; cache-relevant structs: `M12CoreShaderCachePaths`, `M12CoreShaderCacheLookup`, `M12CoreShaderFunctionDesc/Result`, `M12CorePipelineCacheQuery/Result`, `M12CorePipelineCreateDesc/Result`.
7. `vendor/dxmt/src/m12core/m12core.cpp` (lines 96-131, 330-460) — `copyRoot`/`formatCachePath`/`hashShaderBytecode`, `m12core_format_shader_cache_paths`, `m12core_probe_shader_cache` (lookup policy owner).
8. `vendor/dxmt/src/m12core/m12core_metal.c` (whole file, 508 lines) — Objective-C native side: **in-process** `g_shader_function_cache` + `g_pipeline_cache` NSMutableDictionarys, `m12core_create_shader_function`, `m12core_create_pipeline_state` (lookup+create+store), `m12core_lookup/store_pipeline_cache`.
9. `vendor/dxmt/src/winemetal/unix/winemetal_unix.c` (lines 440-640) — PE↔native unixcall bridge wrappers for every cache call (`_WMTM12CoreProbeShaderCache`, `_WMTM12CoreCreateShaderFunction`, `_WMTM12CoreLookupPipelineCache`, etc.).
10. `vendor/dxmt/src/winemetal/winemetal.h` (lines 64-88) — `WMTM12Core*` public PE entrypoints.
11. `app/src-rust/src/mtsp/launcher.rs` (lines 1968-2010, 2038-2150, 2226-2276) — `cache_env_pairs` (sets `DXMT_SHADER_CACHE_PATH`), `app_compat_env_pairs` (M12 diagnostic appids), `preferred_shader_cache_base`.
12. `app/src-rust/src/mtsp/engine.rs` (lines 158, 505) — `shader_cache_subdir = "m12"` for the M12 lane.
13. `tests/test_phase21.cpp` (whole file) — **tests the HOST `metalsharp::ShaderCache`/`PipelineCache`, NOT the M12 PE cache.**
14. `tests/test_phase17.cpp` (line 169) — `test_shader_cache_hash_stability` (host cache).
15. `vendor/dxmt/compile_shaders.sh` — offline metallib pre-builder (external `metal-shaderconverter`).
16. `tools/d3d12-metal-sdk/scripts/verify-m12-cache-freshness.py` — inspects `<hash>.{dxbc,msl,metallib,...}` freshness on disk.
17. `tools/d3d12-metal-sdk/scripts/build-metal-shader-corpus.py` — offline metallib builder via `xcrun`/`metal-lib`.

## Critical architectural facts

### There are TWO unrelated cache implementations
- **(A) M12/DXMT PE runtime cache** — the focus of this task. Lives in `vendor/dxmt/src/{d3d12,m12core,winemetal}/`. This is what D3D12-on-Wine uses at game runtime.
- **(B) Host Metal-engine perf cache** — `src/perf/ShaderCache.cpp`, `src/perf/PipelineCache.cpp`, `include/metalsharp/{ShaderCache,PipelineCache}.h`, consumed by `src/metal/pipeline/PipelineState.mm` (binary-archive path). `tests/test_phase21.cpp` exercises **this** subsystem. It is **not** wired to the M12 DXMT path. Do not conflate the two.

### ⚠️ No runtime code writes a `.metallib` to disk
This is the central gap. The PE runtime only **reads** `.metallib`. It writes `.dxbc`, `.msl`, `.json` (reflection), `.module.txt`, `.dxil_report.txt`, `.msl.err.txt`, `.metallib.err.txt`. `.metallib` files only ever appear from:
- offline `compile_shaders.sh` → external `/usr/local/bin/metal-shaderconverter` on `.dxbc`, or
- `build-metal-shader-corpus.py` → `xcrun -sdk metal` / `metal-lib`.

`m12core_metal.c` *does* support `binary_archive_for_serialization` (lines 220, 333: `addCompute/RenderPipelineFunctionsWithDescriptor`), but the PE **never sets it** — `vendor/dxmt/src/winemetal/Metal.hpp` initializes `info.binary_archive_for_serialization = NULL_OBJECT_HANDLE` in every `Initialize*PipelineInfo`. So warm `.metallib` files are an offline-only artifact today, and the cached-MSL fast path exists precisely to compensate (recompile MSL source from `.msl` at runtime).

## Key code per task dimension

### 1. DXIL → MSL persistence
- **Lowering dispatch**: `d3d12_pipeline_state.cpp:1707` `LowerDXILToMSLWithM12Core(blob, blob_size, type, m_ia_input_elements, core_msl, typed_msl_used)` → libm12core first; PE fallback at `:1719` to `dxmt::dxil::MSLLowering::lower(...)` then `dxmt::dxil::DXILToMSL::convert(...)`.
- libm12core lowering helper (two-call probe+fill pattern): `d3d12_pipeline_state.cpp:519-575` (`LowerDXILToMSLWithM12Core`).
- **Persist write**: `d3d12_pipeline_state.cpp:1762` `DumpShaderText(msl_path, msl_result->source.c_str())`. `msl_path` is `<cache_root>/<hash>.msl` (`:1592-1593`).
- Hash function (stage-namespaced): `m12core.cpp:120-131` `hashShaderBytecode` — vertex shaders add marker `0x4d3132506833` ("M12Ph3") to keep their key namespace distinct.

### 2. `.metallib` lookup / load
- **Lookup policy owner**: `m12core.cpp:424-444` `m12core_probe_shader_cache` — formats paths and sets `metallib_available = (!force_source_compile && metallib_exists)`. Path layout via `m12core_format_shader_cache_paths` (`m12core.cpp:342-389`): suffix is `%016llx` (16-hex shader hash).
- **PE gate + fopen**: `d3d12_pipeline_state.cpp:1559-1607`. When libm12core probe valid, `metallib_path`/`dxbc_path`/etc. come from `core_lookup.paths.*`; otherwise PE formats locally via `FormatShaderCachePath("%016zx")` (`:74-78`). `mf = fopen(metallib_path,"rb")` only when `core_lookup.metallib_available` (`:1596-1602`).
- **Load + function**: `d3d12_pipeline_state.cpp:1908-2090`. Reads `lib_data`, parses reflection entry from `.json`, then `CreateM12CoreShaderFunction(..., M12CORE_SHADER_FUNCTION_INPUT_METALLIB, hash, lib_data.data(), ...)` (`:1975-1980`) → native dict cache; PE fallback `wmt_device.newLibrary(dispatch_data, err)` + `library.newFunction(fn_name)` with fallback chain `main/cs_main/vs_main/ps_main` (`:2026-2052`).

### 3. Cached MSL source fast path
- `d3d12_pipeline_state.cpp:1614-1665`, inside the metallib-miss branch.
- **Gating**: only when `!force_source_compile` AND `type == Vertex || type == Pixel`. Other stages → `g_cached_msl_skipped_stage++` (`:1618-1620`); forced → `g_cached_msl_skipped_force++` (`:1614-1616`).
- Reads `<hash>.msl` via `ReadShaderText(msl_path, cached_msl)` (`:1624`, helper at `:985-1009`, 16 MiB cap), then `CreateM12CoreShaderFunction(..., MSL_SOURCE, hash, cached_msl.data(), ...)` (`:1629-1631`).
- Counters: `g_cached_msl_probe_count/source_hits/read_misses/source_failures/skipped_force/skipped_stage` (`:63-74`). One-shot trace snapshot in `TraceCachedMslDecisionSnapshotOnce` (`:1021-1045`). Reasons enum `CachedMslDecisionReason` (`:76-83`).

### 4. In-memory MTLFunction cache
- **PE-side**: `MTLD3D12PipelineState::s_shader_cache` — `static std::unordered_map<size_t, WMT::Reference<WMT::Function>>` (`d3d12_pipeline_state.hpp:173-174`), guarded by `s_shader_mutex`. Keyed by raw shader hash `hash` (bytecode hash from `m12core_hash_shader_bytecode`).
  - Lookup: `:1484-1487` (gated by `reflection_independent_cache = dxil_shader || (!out_shader_handle && !out_reflection)`, `:1482`).
  - Insert sites: `:1644, :1818, :1888, :1993, :2044, :2229`.
- **Native-side**: `g_shader_function_cache` — `NSMutableDictionary<NSString*, id<MTLFunction>>` (`m12core_metal.c:34-35`), guarded by `g_shader_function_cache_mutex`. Key: `device_handle:stage:input_kind:shader_hash:entry` (`m12core_shader_function_cache_key`, `:84-89`). Populated by `m12core_create_shader_function` (`:466-505`).

### 5. Persistent pipeline cache
- **There is none on disk.** Both layers are **in-memory only**:
  - PE: `g_render_pipeline_cache` / `g_compute_pipeline_cache` — `std::unordered_map<size_t, WMT::Reference<WMT::PipelineState>>` (`d3d12_pipeline_state.cpp:86-87`), guarded by `g_metal_pipeline_cache_mutex`.
  - Native: `g_pipeline_cache` — `NSMutableDictionary<NSString*, id>` (`m12core_metal.c:37-38`), key `kind:key` (`:75-78`).
- **Key build**: `FinalizeM12CorePipelineCacheKeyFromFields` (`d3d12_pipeline_state.cpp:630-651`) — libm12core accumulates ordered `std::vector<uint64_t>` fields; PE fallback `PsoCacheHashCombine` (`:399-401`). Fields include root-signature structural key (`RootSignaturePipelineKey`, `:684-693`) and render descriptor fields (`BuildRenderMetalPipelineKeyFields`, `:2880-2890`).
- **Compute PSO flow**: lookup `LookupM12CorePipelineCache(COMPUTE, ...)` (`:2443`) → PE fallback `g_compute_pipeline_cache.find()` (`:2448`); on miss, `CreateM12CorePipelineState(COMPUTE, ...)` (`:2489`) → PE `wmt_device.newComputePipelineState(info, err)` with 4-attempt transient-error retry (`:2480-2530`); store `StoreM12CorePipelineCache` (`:2511`) → PE `g_compute_pipeline_cache[...]` (`:2515`).
- **Render PSO flow**: mirror at `:2900-2985`.
- Native create+cache: `m12core_create_pipeline_state` (`m12core_metal.c:353-441`) does cache lookup → Metal `newCompute/RenderPipelineStateWithDescriptor:options:reflection:error:` (`:200-238`, `:266-340`) → cache insert, atomically. Supports `MTLPipelineOptionFailOnBinaryArchiveMiss` when archive provided.

### 6. Tests / probes
- **Host cache (subsystem B), not M12**: `tests/test_phase21.cpp` (metallib store/lookup, hit/miss counts, descriptor hash roundtrip), `tests/test_phase17.cpp:169`.
- **No unit tests cover the M12 PE cache** (`d3d12_pipeline_state.cpp` metallib/MSL/pipeline-cache paths). The only coverage is runtime probes:
  - `tools/d3d12-metal-sdk/scripts/verify-m12-cache-freshness.py` — scans `<hash>.{dxbc,msl,metallib,dxil_report.txt,module.txt,msl.err.txt,fail}`, flags `metallib_older_than_msl`, `metallib_with_newer_error`, `active_msl_error`.
  - `vendor/dxmt/compile_shaders.sh` — offline `.metallib` pre-build.
  - Runtime trace logs: `probe_m12_detection_d3d12.log`, `probe_m12_detection_dxmt-d3d12-trace.log`, `tools/d3d12-metal-sdk/scripts/m12-bounded-launch.sh`, plus `PSO_PRESSURE` / `PSTRACE` lines.

## Environment / path defaults (launcher)
- `cache_env_pairs` (`launcher.rs:2226-2275`) sets, for `dxmt` backend: `DXMT_SHADER_CACHE_PATH=<shader_dir>/`, `DXMT_PIPELINE_CACHE_PATH=<pipeline_dir>/`, `DXMT_LOG_PATH=<log_dir>/`.
- shader dir = `~/.metalsharp/shader-cache/<subdir>/<appid>/`; M12 subdir = `"m12"` (`engine.rs:158`, `engine.rs:505`, asserted at `launcher.rs:4543`). Resolved by `preferred_shader_cache_base` (`launcher.rs:1988-2000`); `.metalsharp-cache/shader-cache` in-game-dir is preferred if it has runtime artifacts.
- PE fallback default (when env unset): `/tmp/dxmt_shader_cache` (`d3d12_pipeline_state.cpp:69`, `m12core.cpp:98-108`).
- M12 diagnostic appids `1962700 | 1888160 | 1245620` (AC6-family) get a heavy trace/diagnostic env block at `launcher.rs:2067-2160` (`DXMT_D3D12_TRACE`, `DXMT_D3D12_TRACE_COMPONENTS=...,PSO`, `DXMT_DUMP_MSL` via `METALSHARP_M12_DUMP_MSL`).
- `DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE` is **not** set by the launcher (manual override only) — when set, it bypasses both metallib cache and cached-MSL fast path (`d3d12_pipeline_state.cpp:1556, :1598-1606, :1614`).
- Other relevant manual env knobs: `DXMT_D3D12_PSO_TRACE`, `DXMT_DUMP_MSL`, `DXMT_M12CORE_ENABLE`, `DXMT_M12CORE_REQUIRED`, `DXMT_M12CORE_DUMP_COUNTERS` (first-call counter logging in `winemetal_unix.c:498,566,640`).

## Architecture (how the pieces connect)

```
D3D12 game (PE) ── D3D12CreatePipelineState ──▶ MTLD3D12PipelineState::InitGraphics/InitCompute
   │                                                 │  (d3d12_pipeline_state.cpp)
   │                                                 │
   │   per-shader CompileShader() ──────────────────┘
   │      1. s_shader_cache[hash] in-memory hit? ──▶ done (M12CORE_COUNTER_SHADER_MEMORY_CACHE_HITS)
   │      2. DXIL chunk? probe cache:
   │            WMTM12CoreProbeShaderCache ─unixcall─▶ m12core_probe_shader_cache (m12core.cpp)
   │              metallib_available = !force && exists
   │      3. metallib on disk (available)?
   │            fopen(.metallib) ─▶ CreateM12CoreShaderFunction(METALLIB) ─unixcall─▶
   │               m12core_create_shader_function (m12core_metal.c)
   │                 g_shader_function_cache dict  ─▶ newLibraryWithData + newFunction
   │            else PE fallback wmt_device.newLibrary(dispatch_data)
   │      4. metallib MISS, VS/PS, not forced:
   │            ReadShaderText(.msl) ─▶ CreateM12CoreShaderFunction(MSL_SOURCE) ─unixcall─▶
   │               newLibraryWithSource(MTLLanguageVersion3_1) + newFunction   [CACHED MSL FAST PATH]
   │      5. no .msl either:
   │            LowerDXILToMSLWithM12Core ─unixcall─▶ m12core_lower_dxil_to_msl (typed lowering / DXILToMSL)
   │            PE fallback MSLLowering::lower / DXILToMSL::convert
   │            DumpShaderText(.msl)  [PERSIST MSL SOURCE]
   │            CreateM12CoreShaderFunction(MSL_SOURCE) as in step 4
   │      always insert s_shader_cache[hash]
   │
   └─ PSO creation (render/compute):
          FinalizeM12CorePipelineCacheKeyFromFields ─unixcall─▶ m12core key accumulate
          LookupM12CorePipelineCache ─unixcall─▶ m12core_lookup_pipeline_cache (m12core_metal.c g_pipeline_cache dict)
             PE fallback g_render/compute_pipeline_cache.find()
          on miss: CreateM12CorePipelineState ─unixcall─▶ m12core_create_pipeline_state
                     (lookup dict ─▶ newRender/ComputePipelineStateWithDescriptor ─▶ insert dict)
             PE fallback wmt_device.newRender/ComputePipelineState + StoreM12CorePipelineCache
             PE fallback store g_render/compute_pipeline_cache[key]
```

**PE↔native boundary** is the winemetal unixcall layer (`winemetal_unix.c:444-640`). PE cannot call libm12core.dylib directly (it runs in the Wine PE half). All cache structs cross this boundary as POD (`m12core.h`), never C++/Obj-C objects. Metal object handles are scalar `obj_handle_t`/`uint64_t`.

**Counter batching**: PE-side `dxmt::m12core::RecordCounter` (`d3d12_m12core_counters.hpp`) batches into relaxed atomics, flushes every 64 events via `WMTM12CoreRecordCounters`. The `PSO_PRESSURE` log lines remain the authoritative oracle.

## Recommended slice boundaries

Ranked by independence and risk. Each is independently shippable behind the existing PE-fallback seam.

1. **Wire a runtime `.metallib` writer** (highest-value gap, isolated). Surface: `m12core_metal.c` already supports `binary_archive_for_serialization` (`:220, :333`) but PE passes `NULL_OBJECT_HANDLE`. Add a libm12core-owned `MTLBinaryArchive` lifecycle (create/`addRender/ComputePipelineFunctionsWithDescriptor`/`serializeToURL:` to a `.metallib`/`.bin` under cache_root) and populate `info.binary_archive_for_serialization` from the PE PSO create path. Slice touches: `m12core.h` (new archive ABI), `m12core_metal.c`, `d3d12_pipeline_state.cpp` PSO create sites (`:2480-2530, :2950-2990`), `winemetal.h`/`Metal.hpp` Initialize* functions. Validate via existing metallib-load path — no DXIL lowering changes.

2. **Promote cached-MSL fast path to all stages** (small, contained). Currently VS/PS-only (`d3d12_pipeline_state.cpp:1617-1620`); compute/hull/domain/geometry always regenerate. Lift the stage gate after confirming `m12core_create_shader_function` MSL_SOURCE robustness. Touches only `:1614-1665` + the `g_cached_msl_skipped_stage` counter semantics.

3. **Add M12 PE cache unit tests** (no production change). Currently zero coverage — `test_phase21` only covers subsystem B. Add a `tests/test_m12_cache.cpp` (or extend `test_d3d12.cpp`) driving `m12core_probe_shader_cache` / `m12core_format_shader_cache_paths` / `m12core_hash_shader_bytecode` / `m12core_lookup_pipeline_cache` / `m12core_create_shader_function` directly (these are plain C, no unixcall needed when linked statically). Mirror the host-cache test names.

4. **Wire the modular `D3D12ShaderCompiler`** (`d3d12_shader_compiler.{hpp,cpp}`) — currently dead code (no call sites; verified by grep). Its `MetalIRCompilerBackend::Compile` already implements a clean persistent-metallib read + `from_persistent_cache` flag. Migrating `CompileShader` to call it would replace the inline ~600-line metallib/MSL block in `d3d12_pipeline_state.cpp:1554-2140`. Higher risk (moves the whole shader compile path) — defer until slices 1-3 land.

5. **Persist pipeline cache to disk** (largest). Today both PSO caches are in-memory only. Add a libm12core-owned serialized PSO index keyed by `kind:cache_key` (the `M12CoreCacheCompatibilityKey` ABI at `m12core.h:~1015-1090` already provisions for `CACHE_ARTIFACT_RENDER_PSO`/`COMPUTE_PSO` + invalidation key). Pair with the `binary_archive_for_serialization` work in slice 1 to reuse the same on-disk layout.

## Start here
Open `vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp:1500` (`CompileShader` DXIL branch) — every cache dimension converges in this ~600-line block. Read down to `:2140` for the full metallib/cached-MSL/lowering/load flow, then jump to `m12core_metal.c:466` (`m12core_create_shader_function`) and `:353` (`m12core_create_pipeline_state`) for the native-side in-memory caches.

## Risks / open questions
- **Vertex-cache-key namespace**: `m12core.cpp:127` hardcodes a vertex marker into the shader hash. Input-layout normalization is deferred, so two DXIL shaders differing only in IA layout currently collide or diverge depending on whether the hash includes layout — confirm intent before keying changes.
- **`reflection_independent_cache` gating** (`d3d12_pipeline_state.cpp:1482`): DXIL shaders always use the in-memory function cache; non-DXIL only when no out_handle/out_reflection requested. Misuse could return a stale function for a shader that needed fresh reflection.
- **Probe vs PE-local path divergence**: when libm12core probe is unavailable, PE formats paths with `%016zx` (`size_t`) vs libm12core's `%016llx` (`uint64_t`) — identical on this target but a latent portability smell.
- **No metallib write** means warm-cache behavior for AC6 currently depends entirely on the cached `.msl` + `newLibraryWithSource` recompile (slower than a real `.metallib` load) or an externally pre-seeded `.metallib` from `compile_shaders.sh`.
- **Two cache subsystems** (DXMT PE vs host metalsharp) share test naming but no code; an agent touching "shader cache" must disambiguate which.
