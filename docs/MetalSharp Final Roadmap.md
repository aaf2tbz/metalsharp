# MetalSharp Final Roadmap

**Created:** 2026-05-29
**Branch:** `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp`
**Philosophy:** One month of hardening. Every surface verified. No claimed implementation without proof. No moving forward until the current phase is closed.

---

## I. Architecture

MetalSharp runs Windows PE binaries through Wine on macOS with D3D→Metal translation. This is the correct architecture for macOS. Proton runs the same pattern on Linux with D3D→Vulkan. Proton cannot run on macOS — VKD3D-Proton needs Vulkan extensions MoltenVK doesn't provide, Proton's Wine fork has no macdrv backend, and Steam Linux Runtime uses Linux namespaces.

MetalSharp's direct D3D→Metal path avoids the double-translation tax (D3D→Vulkan→Metal via MoltenVK) and doesn't depend on Apple's closed-source D3DMetal. The play is to accelerate DXMT's DXIL→MSL coverage and mine Proton's ecosystem for runtime patterns — per-game prefixes, redistributable auto-install, game fix databases, Steam API bridging.

### What's Proven

- D3D12 launch works. DXMT M12 loads through MetalSharp backend.
- Agility SDK and UE5 capability checks pass far enough for Subnautica 2 to select D3D12 RHI.
- Draws and dispatches reach the D3D12 replay path.
- Swapchain creation, R10 present, drawable blit, and readback are alive.
- Nonzero full-frame color output has been observed. The pipeline is connected.
- Mini probes pass for loader, device, command queue, swapchain, RTV clear, root signatures, descriptors, graphics PSO, geometry shader PSO, compute dispatch, texture sampling.
- `probe_mini_dxil_texture_color_output` proves SM6 DXIL vertex/pixel shaders can sample a texture and write nonzero color through DXMT/Metal.
- Winemetal ABI checks prevent Steam wrapper breakage.

### What's Not Proven

- Semantic render correctness. Frames assemble from wrong or incomplete inputs.
- The DXIL→MSL converter produces MSL that compiles but outputs wrong values for most shaders.
- Per-game prefix isolation doesn't exist. One game's VC++ install breaks another's.
- Steam API bridge forwards 6 functions. No overlay, no achievements, no cloud saves.
- No DXIL converter tests. Zero. All validation is manual.

### Metal Platform Constraints

Every design decision must respect these limits:

| Constraint | Value | Impact |
|-----------|-------|--------|
| Buffer slots per stage | 31 (`[[buffer(0)]]` through `[[buffer(30)]]`) | Must use argument buffers for descriptor heaps |
| Threadgroup size (M1/M1 Pro/Max) | 256 | D3D12 supports 1024 — must split workloads on M1 |
| Threadgroup size (M2+) | 1024 | Matches D3D12 |
| Threadgroup memory | 32 KB | Matches D3D12 exactly |
| SIMD width | 32 | Wave ops map directly |
| Geometry shaders | None | Must convert to compute or mesh shaders |
| Ray tracing | M3+ only | Software fallback required for M1/M2 |
| Mesh shaders | M2+ only | No geometry amplification via mesh pipeline on M1 |
| Command buffers | Single-use, transient | Cannot reuse translated command lists |
| Resource barriers | Automatic tracking | No explicit barrier API — use MTLEvent/MTLFence |
| Argument buffers | Tier 2 on all Apple Silicon | Can contain arrays of textures/samplers |

D3D12→Metal mapping:

| D3D12 | Metal | Notes |
|-------|-------|-------|
| Descriptor heap | Argument buffer via MTLArgumentEncoder | Batch into fewer buffer slots |
| Root signature | Buffer/texture/sampler bindings + argument buffers | Direct mapping possible |
| CBV | `[[buffer(N)]]` in `constant` address space | Use `setBytes:` for small constants |
| SRV (buffer) | `[[buffer(N)]]` in `device` address space | |
| SRV (texture) | `[[texture(N)]]` | Dimension must match: 1D/2D/3D/cube/array/MS |
| UAV | `[[buffer(N)]]` or `[[texture(N)]]` with `access::write` | |
| Sampler | `[[sampler(N)]]` | |
| Push constants | `setBytes:length:atIndex:` or `[[buffer(30)]]` | |
| Wave ops | `simd_*()` functions | Direct 1:1 mapping for most ops |
| Heap placement | `MTLHeap` sub-allocation + `makeAliasable()` | |
| Fence (CPU-GPU) | `MTLSharedEvent` | |
| 64-bit atomics | `atomic_uint64` | M2+ only |
| DXR ray tracing | Metal ray tracing API | M3+ only |
| Mesh/amplification shaders | Object + mesh functions | M2+ only |

---

## II. DXIL→MSL Converter

The converter is the #1 blocker. Before this session it produced MSL that compiled but output all-zero pixels for most shaders. 151,224 SSA resolution failures, 423 unhandled intrinsics, opcode decode errors.

**Converter source:** `vendor/dxmt/src/airconv/dxil/dxil_to_msl.cpp`
**Bitcode parser:** `vendor/dxmt/src/airconv/dxil/llvm_bitcode.cpp`
**Build:** meson+ninja, LLVM 15.0.7 at `/opt/homebrew/opt/llvm@15/`
**Deploy:** `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
**Shader cache:** Subnautica 2 at `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache/m12/1962700/`

### Known Bugs Register

These bugs exist in the converter and must be tracked:

**Bug A — `isFunctionLikeSymbol` false positives**
- File: `dxil_to_msl.cpp` line ~815
- Impact: 12,528 "function symbol used as SSA value" errors
- Cause: Flags any string ending in `CS`, `VS`, `PS`, `Main` as function-like. SSA values like `texCS`, `vs`, `cs` get zeroed.
- Fix: Tighten to only match `dx.op.*` prefix and function declaration names.
- Status: **Fixed in Phase 0** (commit `79d81a8`)

**Bug B — `result_id` never set in bitcode parser**
- File: `llvm_bitcode.cpp` — `LLVMInstruction.result_id` declared but never assigned
- Impact: Emitter falls back to `value_counter` which must stay perfectly in sync with parser's `next_value`
- Risk: If any instruction type is skipped or counted differently, all subsequent SSA slots desync
- Status: **Verified aligned** during Phase 1A hardening. value_counter increments match noteResult() calls.

**Bug C — `inferDXIntrinsicIdFromName` incomplete coverage**
- File: `dxil_to_msl.cpp` line ~581
- Impact: Only ~30 intrinsics covered by name match. Missing: discard, all wave ops, raytracing, mesh ops.
- Fix: Add name mappings for all missing intrinsics.
- Status: **Fixed in Phase 0** (commit `79d81a8`, 50+ opcodes added)

**Bug D — New intrinsics must update 4 places**
- Files: `DXIntrinsicOpcode` enum, `isKnownDXIntrinsic`, `isSideEffectOnlyIntrinsic`, `inferDXIntrinsicIdFromName`
- Impact: Missing any of these causes the intrinsic to silently fall through even if a case exists in `translateDXIntrinsic`
- Status: **Checklist item** for all future intrinsic work

### Phase 0: Opcode Decode Fix — DONE

**Completed:** 2026-05-27. Commit `79d81a8` on `codex/d3d12-dxmt-metal-bridge`.

- Added numeric suffix extraction to `inferDXIntrinsicIdFromName` for `dx.op.NNN` names
- Added 50+ missing DXIL intrinsic opcodes to enum
- Updated `isKnownDXIntrinsic` for all new opcodes
- Added `DXOP_Discard` to `isSideEffectOnlyIntrinsic`
- Fixed `isFunctionLikeSymbol` false positives (Bug A)
- Stubbed newly-identified intrinsics with diagnostic messages

**Results:**
- `unknown intrinsic: 4294967295` — from 27 to **0**
- All intrinsic IDs reaching `translateDXIntrinsic` are real DXIL opcodes
- Top stubbed intrinsics identified: QuadReadLaneAt(1541), WaveReadLaneFirst(339), WaveActiveBitXor(259), WaveGetLaneIndex(168), WaveReadLaneAt(146)

### Phase 1: DXIL Converter Critical Fixes

**Status:** 1A and 1B DONE. 1C through 1E remaining.

#### Phase 1A: Control Flow Graph Reconstruction — DONE

**Completed:** 2026-05-29. Commits `d995ca8`, `a6baf5b`, `eb26878`.

**Parser fixes** (`llvm_bitcode.cpp/hpp`):
- Track basic block value IDs in `DECLAREBLOCKS` so PHI block references resolve to block indices
- Store both incoming value AND incoming block in PHI operands (was losing block refs)
- Parse `Switch` instructions (was falling through to `default: break`)

**Converter rewrite** (`dxil_to_msl.cpp`):
- Build successor/predecessor CFG from branch terminators
- Collect PHI info with (value, predecessor block) pairs
- Pre-declare PHI variables at function scope with typed defaults
- Emit blocks with labels (`bb0:`, `bb1:`, ...) and `goto`-based control flow
- Conditional `Br` → `if (cond) goto bb_true; else goto bb_false;`
- Unconditional `Br` to next block → fall through (no goto)
- Unconditional `Br` to non-next → `goto bbN`
- Switch → `switch/case/goto` dispatch
- PHI assignments at predecessor block exits before terminator
- Diagnostic trace for unresolved PHI block references

**Hardening:**
- value_counter alignment verified correct between pre-scan and emission loops
- PHI pre-declarations improved for int vectors (`uint4(0)`) and doubles (`0.0`)
- Single-block shaders verified no regression (falls through to Ret without labels)
- `clang++ -fsyntax-only -Wall -Wextra -Werror` clean on all 3 files

#### Phase 1B: Struct Field Extraction Fix — DONE

**Completed:** 2026-05-29. Commits `5148777`, `6882366`.

**Value type tracking** (`dxil_to_msl.hpp`):
- Added `value_type_ids` parallel vector to `EmitContext` — maps value IDs to LLVM type IDs
- Populated from: Call instruction results, InsertValue results, PHI results, constants
- Initial resize gap fixed (`value_table.resize(256)` now has matching `value_type_ids.resize(256)`)

**Struct-aware ExtractValue** (`dxil_to_msl.cpp`):
- Looks up aggregate type from `value_type_ids` to distinguish struct vs vector
- Struct field 0: passes through aggregate expression (e.g., `tex.read(coord)` IS the float4)
- Struct field 1+: emits typed default based on struct field type
- Vectors: retains `.x/.y/.z/.w` component suffix (unchanged)
- Unknown types: falls back to current behavior
- Result type propagated for chained extraction (`struct → float4 → .x` chain works)

**Struct-aware InsertValue:**
- Propagates aggregate type from source to result
- Skips `.x/.y/.z/.w` writes for struct aggregates

**Impact:** Fixes "member reference on device float" from chained ExtractValue on ResRet structs.

#### Phase 1C: Groupshared Memory Fix — PENDING

**Current state:** Alloca handler allocates `thread char storage[256]`. Thread-local, not threadgroup-shared. Compute shaders using shared memory silently use per-thread copies.

**Files:** `dxmt-src/src/airconv/dxil/dxil_to_msl.cpp` (Alloca handler)

**Work:**
- Detect DXIL `groupshared` address space from LLVM alloca metadata
- Emit `threadgroup char storage[N]` for groupshared allocations
- Size from the DXIL alloca size, not hardcoded 256
- Handle alignment requirements

**Validation:**
- Write a test compute shader that writes to groupshared, barriers, then reads
- Verify threadgroup barrier + groupshared interaction
- Verify non-groupshared allocas still emit `thread`

**Hardening gate:**
- Compute shader with groupshared produces correct shared-memory MSL
- Non-groupshared shaders unchanged
- MSL compiles without error on Metal

#### Phase 1D: Threadgroup Size from Metadata — PENDING

**Current state:** Threadgroup size hardcoded to `{1,1,1}`. Caller must override from DXIL metadata.

**Files:** `dxil_to_msl.cpp` (prologue), `dxil_container.cpp` (entry point metadata)

**Work:**
- Parse `[numthreads(X,Y,Z)]` from DXIL entry point metadata
- Emit correct threadgroup size in MSL compute kernel attribute
- Pass through to `MSLShader::tg_size`

**Hardening gate:**
- Compute shader emits correct `[[thread_position_in_threadgroup]]` dimensions
- M1 threadgroup limit of 256 respected (clamp or diagnostic if game requests >256)
- M2+ threadgroup limit of 1024 respected

#### Phase 1E: DXIL Converter Test Infrastructure — PENDING

**Current state:** Zero unit tests for DXIL path. All 6 conformance probes test DXBC SM 5.0 only.

**Work:**
- Create `dxmt-src/tests/dxil/` directory
- Build HLSL→DXC→DXIL→MSL→metallib pipeline test
- Fixture shaders: basic compute, vertex+pixel pair, texture sampling, structured buffer, constant buffer, branch/loop, groupshared
- Add to CI alongside existing DXBC probes
- Regression tests for Phase 0-1B fixes

**Hardening gate:**
- 10+ DXIL test shaders compile and produce valid Metal shaders
- CI green
- Gold file comparison catches regressions between phases

### Phase 2: SSA Multi-Pass Resolution

**Status:** NOT STARTED
**Impact:** Kills ~150K errors. This is the biggest single multiplier.

**Current state:** Single-pass emit loop. Forward references, cross-block data flow, and PHI nodes all resolve to zero via post-hoc `auto vN = 0;` fallback. 101,940 unresolved SSA aliases + 49,284 missing SSA values + 11,853 SSA fallback injections.

**Work:**
- Two-pass emit: First pass populates `value_table` without emitting MSL text. Second pass resolves all SSA references and emits MSL.
- PHI node deferral: Record all PHI nodes and incoming blocks. Resolve after first pass completes.
- Cross-block forward reference: Allow resolution from any block.
- After second pass, only truly undefined values trigger fallback.

**Expected result:** 101,940 unresolved → near zero. 49,284 missing → near zero. 11,853 fallbacks eliminated.

**Hardening gate:**
- Clear shader cache, recompile all PSOs
- `unresolved SSA alias` count drops from 101,940 to **< 5,000**
- `missing SSA value` drops from 49,284 to **< 2,000**
- `generated-source SSA fallback` drops from 11,853 to **< 500**
- Spot-check 5 individual shaders — no `auto vN = 0;` fallbacks in function bodies
- Readback: `nonzero_pixels > 0` in at least one frame
- **Do not proceed to Phase 3** until nonzero pixels appear or MSL diff shows >90% of previously-zero SSA values now resolve to real expressions

### Phase 3: Discard + Structured Buffers

**Status:** NOT STARTED
**Impact:** Kills ~70 errors. Quick win for pixel shaders.

**3a: Discard (intrinsic 82)** — 48 hits
- Emit `if (condition) { discard_fragment(); }` for pixel shaders
- Guard: only emit `discard_fragment()` for fragment stage, emit `/* discard */` for others
- Add to `isSideEffectOnlyIntrinsic`

**3b: RawBufferStructuredCount (intrinsic 118)** — 22 hits
- Return buffer count from structured buffer handle
- Verify DXMT's structured buffer handle representation

**Hardening gate:**
- `unknown intrinsic: 82` count → **0**
- `unknown intrinsic: 118` count → **0**
- MSL contains `discard_fragment()` inside conditional, not unconditional
- No Metal compile errors from discard in wrong shader stage

### Phase 4: Wave/Quad Operations

**Status:** NOT STARTED
**Impact:** Kills ~100 errors. Unblocks UE5 wave op usage.

DXIL → Metal SIMD mapping:

| DXIL ID | DXIL Name | Metal |
|---------|-----------|-------|
| 21 | WaveIsFirstLane | `simd_is_first()` |
| 22 | WaveGetLaneIndex | `simd_lane_id()` |
| 24 | WaveGetLaneCount | `simd_lane_count()` |
| 25 | WaveAnyTrue | `simd_any(expr)` |
| 26 | WaveAllTrue | `simd_all(expr)` |
| 27 | WaveActiveAllEqual | `simd_all_equal(expr)` |
| 28 | WaveActiveCountBits | `simd_popcount(expr)` |
| 29 | WaveActiveSum | `simd_sum(expr)` |
| 30 | WaveActiveProduct | `simd_product(expr)` |
| 31 | WaveActiveBitAnd | `simd_and(expr)` |
| 32 | WaveActiveBitOr | `simd_or(expr)` |
| 33 | WaveActiveBitXor | `simd_xor(expr)` |
| 34 | WaveActiveMin | `simd_min(expr)` |
| 35 | WaveActiveMax | `simd_max(expr)` |
| 44 | WaveReadLaneFirst | `simd_broadcast_first(expr)` |
| 45 | WaveReadLaneAt | `simd_broadcast(expr, lane)` |
| 131 | QuadReadLaneAt | `quad_broadcast(expr, lane)` |

**Work:**
- Add enum values for all wave ops to `DXIntrinsicOpcode`
- Add cases to `translateDXIntrinsic`
- Handle scalar/vector variants correctly (wave ops on float4 must preserve vector width)
- Update all 4 places per Bug D

**Hardening gate:**
- All 17 wave/quad intrinsics → **0** unknown diagnostic hits
- Metal compilation succeeds for all wave-using shaders
- Vector-width correctness: `simd_sum(float4_value)` doesn't accidentally scalarize
- Top stubbed intrinsics from Phase 0 now implemented: QuadReadLaneAt(1541), WaveReadLaneFirst(339), WaveActiveBitXor(259), WaveGetLaneIndex(168), WaveReadLaneAt(146)

### Phase 5: Sub-Opcode Fixes + Remaining Stubs

**Status:** NOT STARTED
**Impact:** Kills ~1400 sub-opcode errors + ~50 remaining intrinsic errors.

**5a: Unknown Unary/Binary/Tertiary Sub-Opcodes** — 1369 errors
- Audit failing sub-opcodes from dxil_report.txt files
- Cross-reference against DXIL spec §14.6
- Add missing cases with correct MSL translations
- Verify consistency with DXBC→MSL path for same operations

**5b: Remaining Stub Intrinsics** — ~50 errors

| DXIL ID | Name | Strategy |
|---------|------|----------|
| 1 | TempRegLoad | Stub: return 0 |
| 3 | MinPrecXRegLoad | Stub: return 0 |
| 11 | GetMeshPayload | Stub: return 0 |
| 20 | CycleCounterLegacy | Stub: return 0 |
| 37 | WaveMultiPrefixCountBits | Stub: return 0 |
| 39 | WaveMultiPrefixSum | Stub: return 0 |
| 164 | SetMeshOutputCounts | Stub: nop |
| 170-208 | Ray tracing ops | Stub: return 0/false |
| 1024 | FetchFlattenedThreadIDInGroup | Map to `simd_lane_id()` |

**Hardening gate:**
- `unknown intrinsic` total → **0** (every DXIL ID that appears in any Subnautica shader has a case)
- `unknown unary/binary/tertiary opcode` → **0** for any sub-opcode appearing >5 times
- Every stub has a `recordDiagnostic` so stubs are traceable
- Stub return types checked: pointer types return `nullptr`, structs return `{}`, not `0`
- No numeric regressions in `unsupported_opcodes` count

### Phase 6: Texture Dimension Coverage

**Status:** NOT STARTED
**Impact:** Cube maps, texture arrays, 3D volume textures, MSAA.

**Current state:** All textures are `texture2d<float>`. No 1D, 3D, cube, array, or MSAA.

**Work:**
- Detect texture dimension from DXIL resource kind (Texture1D/2D/3D/Cube/1DArray/2DArray/CubeArray/MS)
- Emit correct Metal types: `texture1d`, `texture2d`, `texture3d`, `texture_cube`, `texture2d_array`, `texturecube_array`, `texture2d_ms`
- Cube map: `sample(cube, float3)`
- Array: `sample(array, float2, uint/array_index)`
- MSAA: `read(uint2, uint/sample)`

**Hardening gate:**
- Cube map reflections render
- Texture arrays sample correctly
- 3D volume textures work
- MSAA textures read at correct sample indices

### Phase 7: Atomic Operation Completeness

**Status:** NOT STARTED

**Current state:** Only `atomic_fetch_add_explicit` and `atomic_load_explicit` implemented.

**Work:**
- All DXIL AtomicBinOp sub-opcodes: Add, Sub, And, Or, Xor, IMin, IMax, UMin, UMax
- Atomic compare-exchange with proper loop pattern
- 64-bit atomics on M2+ (`atomic_uint64`)
- Float atomics on M2+

**Hardening gate:**
- All DXIL atomic operations produce correct Metal atomic calls
- 64-bit atomics gated to M2+ (fallback or diagnostic on M1)

### Phase 8: Geometry and Tessellation

**Status:** NOT STARTED

**Current state:** No geometry shader support in DXIL converter. Tessellation has 5 TODO markers.

Metal has no geometry shader stage. Options:
1. Convert GS to compute shaders that output to buffers
2. Use mesh shaders (M2+) for geometry amplification
3. Tessellation via compute kernel → tessellator → post-tessellation vertex function

Metal tessellation model differs from D3D12:
- D3D12: VS → Hull Shader → Tessellator → Domain Shader
- Metal: Compute Kernel → Tessellator → Post-Tessellation Vertex Function
- Tessellation factors computed in separate compute pass (more flexible)

**Work:**
- Add GS handling: convert to compute output or mesh shader dispatch
- Add HS/DS handling for tessellation
- Map hull/domain shaders to Metal post-tessellation vertex function

**Hardening gate:**
- Game using geometry shaders renders correctly
- Tessellation produces correct vertex output
- M1 fallback path for GS (compute) works without mesh shader support

---

## III. Runtime Infrastructure

### Phase 9: Per-Game Prefix Isolation

**Status:** NOT STARTED
**Impact:** Eliminates "game A worked until I installed game B."

**Current state:** All Steam games use `~/.metalsharp/prefix-steam/`. Shared prefix means one game's VC++ install breaks another's.

**Work:**

**9a: Per-Appid Wine Prefix**
- Create `~/.metalsharp/compatdata/<appid>/pfx/` per Steam game on first launch
- Copy `prefix-steam` as template (Proton's `default_pfx` pattern)
- Wine Steam client stays in `prefix-steam`, only game processes move to per-game
- Update `steam_launch_prefix()` to return per-game path
- Migration: existing `prefix-steam` stays as Steam client prefix

**9b: Prefix Template with Base Redistributables**
- Create `~/.metalsharp/runtime/redist/` central DLL cache
- Cache: d3dcompiler_*, d3dx9_*, xinput1_3, xaudio*, vcruntime140, msvcp140, ucrtbase, atl, openal, physx
- Symlink/copy from cache into new per-game prefixes (Proton's `builtin_dll_copy` pattern)
- Source from: Steam CommonRedist, MetalSharp runtime, DXC redistributable
- Track install receipts per prefix

**9c: Redistributable Auto-Install**
- On first game launch, check component requirements from `mtsp-rules.toml`
- Auto-run silent VC++ / DirectX installers into per-game prefix when missing
- Handle DXSetup.exe, vcredist_x64.exe, vcredist_x86.exe via Wine with `/Q` silent flags
- Track receipts per prefix

**Hardening gate:**
- Two different games can install conflicting VC++ runtimes without breaking each other
- New per-game prefix pre-populated with standard redists
- Game requiring vcrun2019 + directx_jun2010 auto-installs both on first launch

### Phase 10: Compat Config Expansion

**Status:** NOT STARTED
**Impact:** Goes from 64 game rules to 200+.

**Current state:** 64 games in `mtsp-rules.toml`. Per-game fixes hardcoded in Rust.

**10a: Compat Config Flag System**
- Extend `mtsp-rules.toml` with flag fields (pipeline, flags, gpu_vendor_stubs, launch_args, config_patches)
- Migrate all hardcoded fixes from Rust to TOML data
- Eliminate shell scripts (`scripts/setup-*-deps.sh`)

**10b: Proton Game Fix Mining**
- Parse Proton's `default_compat_config()` appid→flag mapping
- Cross-reference with MetalSharp's existing rules
- Port applicable fixes: disablenvapi, heapdelayfree, heapzeromemory, WINEDLLOVERRIDES, WINE_CPU_TOPOLOGY
- Skip Linux/Vulkan-specific: PROTON_USE_WINED3D, PROTON_NO_FSYNC, Steam Linux Runtime flags
- Target: 200+ game entries

**10c: Config File Patching Generalization**
- Generalize Subnautica's hardcoded `write_marked_config_block()` to data-driven system
- Support: INI, JSON, XML formats
- Marked block replacement (only rewrite between MetalSharp markers)
- Per-game config paths with platform variable substitution

**Hardening gate:**
- Zero game-specific hardcoded logic in Rust
- Top 200 Steam games by player count all have TOML entries
- Any game can get INI config patches via TOML without code changes

### Phase 11: Steam Bridge Completion

**Status:** NOT STARTED
**Impact:** Full Steam functionality (overlay, friends, achievements, workshop, cloud saves).

**Current state:** 6 Steam API functions forwarded. `src/steam/proton/lsteamclient/` directory doesn't exist. Build infrastructure scaffolded but Proton source not vendored.

**Work:**

**11a: Vendor Proton lsteamclient Source**
- Vendor Proton's `lsteamclient/` source tree into `src/steam/proton/lsteamclient/`
- Pin to Proton 11.0 branch (matching Wine 11.x baseline)
- Adapt build for macOS: arm64 Unix dylib + x86_64 PE DLL
- Generate `cppISteam*.cpp` interface thunks from Steam API headers
- Build both `steamclient64.dll` (PE) and `lsteamclient.dylib` (macOS native)

**11b: Steam API Integration Testing**
- Test ISteamUser, ISteamFriends, ISteamUserStats
- Test ISteamRemoteStorage, ISteamUGC, ISteamApps, ISteamUtils
- Verify callbacks flow correctly through bridge

**Hardening gate:**
- Steam game shows overlay, records achievements, syncs cloud saves
- No crash on Steam API callback delivery

---

## IV. Advanced Features

These are needed for specific modern games but aren't blocking the majority of titles.

### Phase 12: Enhanced Barriers

- `ResourceBarrier` implemented but `BEGIN/END` split barriers are not
- UE5 increasingly uses enhanced barriers
- Strategy: Metal automatic tracking for non-heap resources, MTLEvent for heap resources

### Phase 13: Stream Output

- `SOSetTargets` is empty. Stream output (transform feedback) not implemented.
- Needed for: particle systems, GPU-driven rendering
- Metal has no stream output — implement via compute shader + buffer writeback

### Phase 14: Mesh Shaders

- `DispatchMesh` is empty. Mesh/amplification shader kinds fall through to `unknown_main`
- Metal 3 supports mesh shaders via `object` and `mesh` function roles (M2+)
- Needed for: Nanite in UE5, DX12 Ultimate titles

### Phase 15: Ray Tracing (DXR)

- All ray tracing command list methods empty. No DXR intrinsics in converter.
- Metal ray tracing API available on M3+ (hardware acceleration)
- M1/M2 requires software fallback via compute shaders
- Long-term feature

---

## V. Proof Target — Subnautica 2

Subnautica 2 (appid `1962700`) is the live proof target. The goal is to prove the D3D12→Metal SDK can explain and validate modern UE5 D3D12 rendering through DXMT.

**Current status:** Past global black-window failure. Frame is assembled from wrong or incomplete inputs — giant colored primitives, incorrect triangles, flat color planes.

### Most Likely Root Causes (in order)

1. Critical compute dispatches skipped on first use when PSOs still compiling
2. UE5 offscreen passes guarded/quarantined instead of correctly implemented
3. Vertex stage-in, LoadInput, input layout, or raster state mis-decodes
4. Root descriptor tables, SRV/UAV offsets, texture view ownership diverge
5. Nanite/visibility/GBuffer passes need stronger compute + offscreen replay

### Subnautica Workstreams

These run in parallel with converter work but have their own validation gates.

**WS-A: Shader Cache and MSC Prewarm**
- Move all Metal Shader Converter usage out of live gameplay
- Internal DXIL→MSL live fallback is the normal path
- MSC is for offline prewarm/diagnostics only
- Gate: No `metal-shaderconverter` process during normal gameplay

**WS-B: Compute PSO First-Use Correctness**
- Patch compute dispatch replay so `compiled=0` triggers one-time compile before dispatch
- If compile fails: hard PSO failure with shader hash, root sig hash, dispatch dimensions, Metal error
- Gate: Zero skipped compute dispatches from pending PSO compilation

**WS-C: Replace UE5 Offscreen Quarantines**
- Inventory every active quarantine/skip by env gate, code path, PSO hash, render target tuple
- Convert each to no-game repro probe or captured offline replay
- Fix underlying Metal render pass/stage-in/descriptor/resource-state
- Gate: Subnautica reaches frontend without relying on skip paths

**WS-D: Vertex Stage-In and Raster Correctness**
- Harden DXIL `LoadInput` from real operands, not sequence guessing
- Log exact unknown DXGI vertex formats with full metadata
- Add missing Metal format mappings only from observed exact formats
- Gate: No defaulting unknown formats to `R32G32B32A32_FLOAT`. No giant incorrect triangles.

**WS-E: Root Tables, Textures, and Final Composite**
- Snapshot root parameters for final swapchain-targeted PSOs
- Detect root-table binding collisions across stages
- Confirm SRV/UAV byte offsets are stage-correct
- Add final composite texture readback before present
- Gate: Recognizable sampled texture in R10 and BGRA targets

**WS-F: Nanite / UE5 Stress Gate**
- Preserve captured Nanite/Voxelize/visibility PSO and shader corpus as offline fixtures
- Build offline stress gate from real PSO descriptors
- Gate: Offline Nanite stress replay passes or produces precise unsupported feature

**WS-G: Live Validation Rules**

Before live launch:
- Backend `/status` healthy
- `dxmt_metal12` route selected
- Deployed DLL hashes match build outputs
- ABI checker passes
- Mini probes pass
- Offline PSO/corpus replay for latest cache

During live launch:
- Launch through backend route only
- Check logs after 45-90 seconds
- Record: splash, build window, music, cursor, frontend widgets, nonzero readback, visible scene

---

## VI. Validation Protocol

This is the hardening discipline. No phase is done until validation passes.

### After Every Phase

1. Build: `ninja -C vendor/dxmt/build-metalsharp-x64 src/d3d12/d3d12.dll`
2. Deploy: `cp build/src/d3d12/d3d12.dll ~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
3. Delete shader cache: `rm -rf /Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache/m12/1962700/`
4. Launch Subnautica 2 via `POST /steam/launch-game` with DXMT trace env
5. Wait for PSO compilation to settle
6. Check `/tmp/dxmt_dxil_trace.log` for reduced error counts
7. Check readback for `nonzero_pixels > 0`
8. Regenerate error census and diff against baseline

### Pre-Flight Error Census (run before starting any new phase)

```bash
cd /Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache/m12/1962700/

echo "=== Error Census Baseline ===" > /tmp/dxil_baseline.txt
for pattern in "unresolved SSA alias" "missing SSA value" "function symbol used as SSA value" \
    "unknown intrinsic" "unknown unary opcode" "unknown binary opcode" "unknown tertiary opcode" \
    "generated-source SSA fallback" "pointer add fallback" "pointer sub fallback" \
    "pointer mul fallback" "pointer and fallback" "pointer lshr fallback" \
    "generic load fallback" "gep pointer fallback"; do
  count=$(grep -c "$pattern" *.dxil_report.txt 2>/dev/null | tail -1 | cut -d: -f2)
  echo "$pattern: $count" >> /tmp/dxil_baseline.txt
done

echo "=== Unknown Intrinsic Distribution ===" >> /tmp/dxil_baseline.txt
grep "unknown intrinsic: [0-9]*" *.dxil_report.txt | \
  grep -o "unknown intrinsic: [0-9]*" | sort | uniq -c | sort -rn >> /tmp/dxil_baseline.txt
```

### Quick Diagnostic: Force Known-Good Shader

Replace main draw PSO's MSL with:
```metal
fragment float4 ps_main() { return float4(1.0, 0.0, 0.0, 1.0); }
```
Place in MSL cache (bypassing converter). If screen turns red, pipeline is correct and converter is the problem. If screen stays black, pipeline itself is broken.

### Commit Discipline

- One commit per phase. Never combine phases.
- Format: `[dxil-msl] Phase N: <description>` + error census delta + readback delta
- If a phase is large, break into sub-commits but keep on same branch
- Every commit must pass syntax check: `clang++ -fsyntax-only -Wall -Wextra -Werror`

### Rollback Protocol

1. `git log --oneline -5` — identify pre-phase commit
2. `git checkout <pre-phase-commit> -- vendor/dxmt/src/airconv/` — restore converter
3. Rebuild + redeploy
4. Verify readback returns to previous baseline

---

## VII. Dependency Graph

```
Phase 0 (opcode decode)  ── DONE
Phase 1A (CFG)           ── DONE
Phase 1B (struct fix)    ── DONE
Phase 1C (groupshared)   ──→ Phase 1E (tests)
Phase 1D (threadgroup)   ──→ Phase 1E (tests)

Phase 1E (tests)         ──→ Phase 2 (SSA multi-pass)

Phase 2 (SSA)            ──→ Phase 3 (discard+struct)
Phase 3                  ──→ Phase 4 (wave ops)
Phase 4                  ──→ Phase 5 (sub-opcodes + stubs)
Phase 5                  ──→ Phase 6 (texture dimensions)

Phase 6 (textures)       ──→ Phase 7 (atomics)
Phase 7                  ──→ Phase 8 (geometry + tessellation)

Phase 8                  ──→ Phase 12 (enhanced barriers)
Phase 8                  ──→ Phase 13 (stream output)
Phase 8                  ──→ Phase 14 (mesh shaders)
Phase 14                 ──→ Phase 15 (ray tracing)

-- Parallel tracks --
Phase 9  (per-game prefix)  ── independent
Phase 10 (compat configs)   ── independent
Phase 11 (Steam bridge)     ── independent

-- Proof target --
Subnautica WS-A through WS-G run parallel to converter work
```

---

## VIII. Success Metrics

| Metric | Current | After Phase 1 | After Phase 2 | After Phase 5 | After Phase 8 |
|--------|---------|---------------|---------------|---------------|---------------|
| DXIL shaders compiling correctly | ~25% | ~35% | ~60% | ~75% | ~85% |
| SSA resolution failures | 151K | 151K | <5K | <1K | <500 |
| Unknown intrinsics | 423 | ~360 | ~360 | **0** | **0** |
| Per-game prefix isolation | No | No | No | No | Yes |
| Game rules in mtsp-rules.toml | 64 | 64 | 64 | 64 | 200+ |
| Steam API functions bridged | 6 | 6 | 6 | 6 | 100+ |
| D3D12 games reaching main menu | 1-2 | 2-3 | 5-10 | 10-15 | 15-20 |

---

## IX. Key Paths

- **Build:** `vendor/dxmt/build-metalsharp-x64`
- **Deploy:** `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
- **Shader cache:** `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache/m12/1962700/`
- **DXIL trace:** `/tmp/dxmt_dxil_trace.log`
- **D3D12 trace:** `/tmp/dxmt_d3d12_trace.log`
- **Baseline snapshot:** `~/Documents/Obsidian Vault/MetalSharp/dxil-baseline-may27.txt`
- **Converter source:** `vendor/dxmt/src/airconv/dxil/dxil_to_msl.cpp`
- **Bitcode parser:** `vendor/dxmt/src/airconv/dxil/llvm_bitcode.cpp`
- **Pipeline state:** `vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp`
- **Command queue:** `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`
- **Swapchain:** `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp`
- **DXMT source (dev tree):** `/Volumes/AverySSD/metalsharp/dxmt-src/`
- **MetalSharp repo:** `/Volumes/AverySSD/metalsharp/metalsharp-repo/`
- **Metal framework cache:** `/var/folders/.../com.apple.metal/` (917MB)

---

## X. Execution Order — The Next Month

### Week 1: Close Phase 1

- Day 1-2: Phase 1C (groupshared memory)
- Day 3: Phase 1D (threadgroup size from metadata)
- Day 4-5: Phase 1E (test infrastructure, 10+ fixtures)
- Gate: All Phase 1 items DONE with tests passing

### Week 2: SSA Resolution

- Day 1-4: Phase 2 (SSA multi-pass resolution — highest risk, highest impact)
- Day 5: Phase 2 hardening + validation
- Gate: nonzero_pixels > 0 OR >90% SSA values resolving

### Week 3: Intrinsic Coverage

- Day 1: Phase 3 (discard + structured buffers)
- Day 2-3: Phase 4 (wave/quad operations)
- Day 4-5: Phase 5a (sub-opcode audit and fixes)
- Gate: Zero unknown intrinsics for all IDs appearing in Subnautica

### Week 4: Close + Expand

- Day 1: Phase 5b (remaining stubs)
- Day 2-3: Phase 6 (texture dimensions) if SSA resolved, else continue SSA hardening
- Day 4: Full validation pass — error census, readback baseline, test suite green
- Day 5: Roadmap checkpoint — record actual vs projected metrics, adjust remaining plan
- Gate: Converter no longer the rendering bottleneck

### Post-Month: Runtime Infrastructure

Phase 9 (per-game prefix) → Phase 10 (compat config expansion) → Phase 11 (Steam bridge) run on a parallel track. Start these as soon as converter work is stable enough to context-switch.

---

## XI. Post-Roadmap: If Converter Is Correct But Output Still Zero

If all phases complete and SSA failures < 1%, zero unhandled intrinsics, MSL expressions are correct, but readback is still all zeros:

1. **Root signature / descriptor heap mismatch** — binding indices off by one means every texture/buffer read returns zero
2. **Render target format mismatch** — float4 output vs uint4 target silently discarded
3. **Depth/stencil state blocking output** — depth test failing all fragments
4. **Vertex buffer not bound or stride mismatch** — degenerate triangles, no fragments
5. **Resource state barriers not applied** — Metal presents uninitialized texture

These are pipeline problems, not converter problems. Separate investigation roadmap required.

---

*This roadmap is the single source of truth for MetalSharp's D3D12→Metal work. Every phase must be hardened before moving on. No shortcuts, no claimed implementations without proof.*
