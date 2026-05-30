# MetalSharp Final Roadmap

**Updated:** 2026-05-30
**Branch:** `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp`
**PR:** #129

---

## Completed Phases (0–8 + Root Cause Fix)

| Phase | Description | Commit | Result |
|-------|-------------|--------|--------|
| 0 | Opcode decode fix | `79d81a8` | 0 unknown intrinsics from decode |
| 1A | CFG reconstruction | `d995ca8` | Blocks, branches, PHI nodes |
| 1B | Struct field extraction | `5148777` | ResRet struct handling |
| 1C | Groupshared + memory ops | `075a66a` | threadgroup storage |
| 1D | Threadgroup size from metadata | `fa99aaf` | `[[numthreads]]` |
| 1E | Test infrastructure | `44cdd95` | 279/279 DXIL pass |
| 2 | SSA multi-pass resolution | `6bd4981` | 500K+ zero-cascade killed |
| 3 | Name-based intrinsic routing | `6c982c6` | 255/279 fully translated |
| 4 | Wave/quad ops + opcode IDs | `65f8c45` | 17 wave intrinsics |
| 5 | Hardened literalArg | `7a9de69` | Constant-pool fallback |
| 6 | Metal compilation fixes | `c313e97` | Switch CFG, 19→23 pass |
| 7 | Value-table overlap mitigation | `2752ea7` | 23→23 (stable) |
| 8 | Per-function constant scoping | `6fd1db1` | Protected function decls |
| Root | Value numbering fix | `d5bbee2` | 12/279 correct Metal pass |

**Root cause found and fixed:** Three parser bugs in `llvm_bitcode.cpp` — `instruction_start_value` excluded module-level constants, block labels incorrectly advanced `next_value`, PHI block operands decoded as relative values. Fix: `next_module_value_id`, no block advancement, raw block indices. Metal pass dropped from 23→12 but now produces **correct** MSL — the previous 23 were "accidentally compiling" with `auto vN = 0;` for all unresolved calls.

---

## Current State

| Metric | Value |
|--------|-------|
| DXIL parse rate | **279/279** (100%) |
| Metal compile rate | **12/279** (4.3%) — correct output |
| Total Metal errors | ~5,041 across 267 failing shaders |
| Top error | `initializer_list<int>` subscript (599) |
| Undeclared identifiers | ~2,000+ across many vNN values |

---

## Phase 9: Metal Error Category Fix Sprint pending

**Goal:** Fix the 5 systematic error categories killing 90%+ of shader compilation.
**Target:** 80+/279 Metal pass.

The root cause fix exposed real converter bugs. These are the categories:

### 9A: Computed Index Initializer List — 1,344 errors (599 + 745)

**Pattern:** `v34[({59,56})*64]` — Metal interprets `({59,56})` as `initializer_list<int>`, then fails because initializer_list has no subscript operator and there's a missing semicolon.

**Root cause:** The converter emits multi-component vector indices as brace-enclosed expressions `({a,b})`. Metal doesn't allow this — it needs a single scalar index or a properly-typed vector.

**Fix in `dxil_to_msl.cpp`:**
- When emitting a subscript index that's a vector expression, extract the appropriate component (e.g., `.x`) based on context
- Or emit the full computation as a single scalar expression without braces
- Audit all GEP / buffer index emission paths

### 9B: Undeclared Identifiers — ~2,000 errors

**Pattern:** `use of undeclared identifier 'v99'`, `v94`, `v90`, etc.

**Root cause:** Some instruction results aren't being emitted as variable declarations. Likely causes:
- Instructions that produce results but the emitter skips the declaration
- Forward references to values defined later in the same block
- Values from constants or parameters not in scope

**Fix in `dxil_to_msl.cpp`:**
- Audit the emit loop: every instruction with a result must emit `auto vN = expr;`
- Verify parameter loading covers all function parameters
- Verify module constant and function constant loading covers all constants
- Add pre-scan: collect all value IDs that will be defined, emit forward declarations for any used before definition

### 9C: Texture2D Subscript Mismatch — 85 errors

**Pattern:** `type 'texture2d<float, access::read_write>' does not provide a subscript operator`

**Root cause:** SRVs mapped to `texture2d` but the shader code tries to use them as byte-addressed buffers via `[]` subscript. In D3D12, a structured buffer can be bound as either SRV or UAV with byte offsets. Metal textures don't support `[]` subscript — that's a buffer operation.

**Fix in `dxil_to_msl.cpp`:**
- Detect when a texture resource is being used with byte-offset access patterns
- Emit as `device float*` or `device float4*` buffer instead of `texture2d`
- Or use `texture2d::read(uint2)` / `texture2d::write(float4, uint2)` for coordinate-based access

### 9D: Subscript on Non-Array — 108 errors

**Pattern:** `subscripted value is not an array, pointer, or vector`

**Root cause:** Emitter produces `expr[index]` where `expr` is a scalar or struct. Happens when the converter assumes a value is a buffer/array but it's actually a scalar constant or struct field.

**Fix in `dxil_to_msl.cpp`:**
- Before emitting subscript, check the value's type from `value_type_ids`
- If scalar, the subscript is wrong — likely the base expression already contains the indexed value
- If struct, use member access instead of subscript

### 9E: Type Mismatch in Binary Expressions — 43 errors

**Pattern:** `invalid operands to binary expression ('float' and 'int')`

**Root cause:** DXIL operations on mixed float/int types where the converter doesn't insert explicit casts. Metal is stricter than HLSL about implicit conversions.

**Fix in `dxil_to_msl.cpp`:**
- Before emitting binary operator, check operand types
- Insert explicit `static_cast<float>()` or `static_cast<int>()` as needed
- Handle vector type promotion (e.g., `float + int` → `float + float(int)`)

### 9F: Remaining Error Categories

| Category | Count | Fix |
|----------|-------|-----|
| `no member 'sample'` on read_write texture | 26 | Use `.read()` for RO textures or `.write()` for RW |
| `device char * + int` | 12 | Pointer arithmetic needs explicit cast |
| `indirection requires pointer operand` | 12 | Dereference on non-pointer value |
| `initializer list on right of *` | 9 | Same as 9A — brace-init in expression |
| `as_type half→float` | 9 | Use `float(half_val)` not `as_type` |
| `expected expression` | 8 | Empty or malformed expression |
| `expected ')'` | 8 | Unclosed paren in expression |

**Hardening gate:**
- Metal pass: **80+/279** (29%+)
- `initializer_list` errors: **0**
- `expected ';' after expression`: **0**
- `undeclared identifier` errors: **< 50**
- `subscripted value not array`: **0**
- `texture2d no subscript`: **0**

---

## Phase 10: Intrinsic Translation Completeness

**Goal:** Every DXIL intrinsic in the Subnautica 2 corpus produces valid, compiling Metal.
**Target:** 200+/279 Metal pass.

### 10A: Texture Operations Full Coverage

- TextureLoad (`dx.op.textureLoad`) — all dimensions, all return types
- TextureStore (`dx.op.textureStore`) — write to UAV textures
- TextureGather (`dx.op.textureGather`) — component selection
- TextureSampleLevel — explicit LOD
- TextureSampleBias — bias
- TextureSampleCmp — comparison sampling for shadow maps
- BufferLoad / BufferStore — structured and raw buffers
- Texture2DMS — multisample read

### 10B: Compute-Specific Intrinsics

- `CreateHandleFromHeap` — descriptor heap dynamic indexing
- `AnnotateHandle` — handle metadata
- `Barrier` — group/shared/global barrier mapping
- `FlattenedThreadIdInGroup` — already mapped, verify

### 10C: Remaining Arithmetic and Utility

- `dot`, `cross`, `normalize`, `length` — verify vector width preservation
- `clamp`, `saturate`, `lerp` — verify type consistency
- `isfinite`, `isnan`, `isinf` — map to Metal equivalents
- `fma`, `mad` — fused multiply-add
- `countbits`, `firstbitlow`, `firstbithigh` — bit operations
- `reversebits`, `bits` — more bit ops

**Hardening gate:**
- Metal pass: **200+/279** (72%+)
- `unknown intrinsic` diagnostics: **0** for all IDs in Subnautica corpus
- All texture dimension variants compile
- Compute dispatch shaders compile and produce correct output

---

## Phase 11: Pipeline Correctness

**Goal:** MSL that compiles correctly also produces correct visual output.
**Target:** Subnautica 2 shows nonzero pixel readback + recognizable rendering.

The converter can produce compiling MSL that still outputs wrong values. These are pipeline binding and state issues:

### 11A: Root Signature → Metal Binding Mapping

- Audit root parameter → Metal buffer/texture binding
- Verify descriptor table offsets are correct
- Check CBV/SRV/UAV byte offsets match between D3D12 and Metal
- Verify push constant `setBytes:length:atIndex:` mapping

### 11B: Resource State and Barriers

- Verify `ResourceBarrier` transitions produce correct Metal resource states
- Check UAV barriers map to `MTLFence` or implicit sync
- Verify aliasing barriers work with `MTLHeap`

### 11C: Render Target and Depth

- Verify render target format mapping (DXGI_FORMAT → MTLPixelFormat)
- Check depth-stencil state creation and binding
- Verify viewport and scissor rect mapping
- Check blend state maps correctly

### 11D: Vertex Input and Stage-In

- Verify vertex buffer strides and offsets
- Check input layout → Metal vertex descriptor mapping
- Verify `LoadInput` intrinsic reads correct vertex attributes
- Check instance rate and step rate

**Hardening gate:**
- Subnautica 2 readback: `nonzero_pixels > 0` on multiple consecutive frames
- At least one recognizable rendered element (skybox, terrain, UI)
- No crash during 60-second gameplay session
- Frame rate stable (not counting converter recompiles)

---

## Phase 12: Test Infrastructure & Regression Prevention

**Goal:** Every converter change validated automatically.
**Target:** CI prevents regressions.

### 12A: DXIL Test Suite Expansion

- Expand from 279 real shaders to include targeted fixtures:
  - Simple compute (add buffer)
  - Vertex + pixel pair (transform + color)
  - Texture sampling (2D, cube, array)
  - Structured buffer read/write
  - Wave ops
  - Groupshared memory + barrier
  - Branch/loop control flow
  - PHI node chains
- Gold file comparison for each fixture
- Track Metal compile pass rate per fixture

### 12B: Automated Error Census

- CI job runs converter on full Subnautica corpus
- Compiles all MSL output with `xcrun -sdk macosx metal`
- Tracks error category counts across commits
- Flags any increase in error count as CI failure

### 12C: Per-Phase Regression Harness

- Each completed phase has a saved snapshot of error counts
- New commits must not regress any completed phase metric
- Auto-bisect on regression

**Hardening gate:**
- 50+ DXIL test fixtures
- CI green on all fixtures
- Error census job runs in < 5 minutes
- Zero regressions in Metal compile pass rate

---

## Phase 13: Advanced Features

These are needed for specific modern games but aren't blocking the majority.

| Feature | Metal Support | D3D12 Feature |
|---------|--------------|---------------|
| Enhanced barriers | MTLEvent for heap resources | `ResourceBarrier` BEGIN/END |
| Stream output | Compute + buffer writeback | `SOSetTargets` |
| Mesh shaders | Object + mesh functions (M2+) | `DispatchMesh` |
| Ray tracing | Metal RT API (M3+) | DXR 1.0/1.1 |

---

## Phase 14: Steam Bridge & Ecosystem

**Goal:** Full Steam functionality.
**Target:** Overlay, achievements, cloud saves working.

### 14A: Vendor Proton lsteamclient

- Vendor Proton `lsteamclient/` source
- Build both `steamclient64.dll` (PE) and `lsteamclient.dylib` (macOS native)
- Generate interface thunks from Steam API headers
- Adapt build for arm64 macOS

### 14B: Steam API Integration

- ISteamUser, ISteamFriends, ISteamUserStats
- ISteamRemoteStorage (cloud saves)
- ISteamUGC (workshop)
- ISteamApps, ISteamUtils
- Callback flow through bridge

**Hardening gate:**
- Steam overlay renders in-game
- Achievements unlock and sync
- Cloud saves upload/download
- No crash on Steam API callback delivery

---

## Phase 15: Runtime Infrastructure

**Goal:** Multiple games running independently without conflicts.
**Target:** 5+ D3D12 games reaching main menu.

### 15A: Per-Game Prefix Isolation

- Per-appid Wine prefix: `~/.metalsharp/compatdata/<appid>/pfx/`
- Template from `prefix-steam` base
- Wine client stays in shared prefix, game processes use per-game
- Migration: existing `prefix-steam` becomes Steam client only

### 15B: Redistributable Auto-Install

- Central DLL cache: `~/.metalsharp/runtime/redist/`
- Auto-install VC++ / DirectX / XNA on first game launch
- Per-prefix install receipts
- Handle DXSetup, vcredist, d3dx silent installs

### 15C: Compat Config Expansion

- Extend `mtsp-rules.toml` with full flag system
- Mine Proton's game fix database for applicable patterns
- Port: disablenvapi, heapdelayfree, WINEDLLOVERRIDES
- Skip Linux/Vulkan-specific flags
- Target: 200+ game entries

**Hardening gate:**
- 5+ D3D12 games reach main menu
- No cross-game prefix contamination
- Auto-install works for VC++ and DirectX runtimes
- 200+ game compat entries

---

## Validation Protocol

After every phase:

1. Build: `ninja -C vendor/dxmt/build-metalsharp-x64 src/d3d12/d3d12.dll`
2. Deploy: `cp build/src/d3d12/d3d12.dll ~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
3. Test: `./dxmt-src/tests/dxil/test_dxil_converter --dump-msl /tmp/test && ./dxmt-src/tests/dxil/test_dxil_converter --compile-metal /tmp/test`
4. Measure: Metal compile pass rate, error category counts
5. Gate: Pass rate must meet phase target before proceeding

### Commit Discipline

- One commit per phase
- Format: `[dxil-msl] Phase N: <description>` + metric delta
- `clang++ -fsyntax-only -Wall -Wextra -Werror` clean on all changed files
- Push to `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp` only

---

## Key Paths

| Path | Purpose |
|------|---------|
| `vendor/dxmt/src/airconv/dxil/dxil_to_msl.cpp` | DXIL→MSL converter |
| `vendor/dxmt/src/airconv/dxil/llvm_bitcode.cpp` | Bitcode parser |
| `vendor/dxmt/src/airconv/dxil/dxil_to_msl.hpp` | EmitContext struct |
| `vendor/dxmt/src/airconv/dxil/dxil_container.hpp` | DXIL container |
| `dxmt-src/tests/dxil/test_dxil_converter.cpp` | Test harness |
| `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/` | Deploy target |
| `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/` | Shader cache |
| `/Volumes/AverySSD/metalsharp/dxmt-src/` | Dev tree |
| `/Volumes/AverySSD/metalsharp/metalsharp-repo/` | Git repo |

---

## Dependency Graph

```
Phase 9 (Metal error fix sprint)      ── NEXT
  │
  ├── Phase 10 (intrinsic completeness)
  │     │
  │     └── Phase 11 (pipeline correctness)
  │           │
  │           └── Phase 12 (test infrastructure)
  │
  ├── Phase 13 (advanced features)        ── parallel
  │
  └── Phase 14 (Steam bridge)            ── parallel

Phase 15 (runtime infrastructure)      ── after Phase 12
```

---

## Success Metrics

| Metric | Now | After Phase 9 | After Phase 10 | After Phase 11 | After Phase 15 |
|--------|-----|---------------|-----------------|-----------------|-----------------|
| Metal compile pass | 12/279 | 80+/279 | 200+/279 | 200+/279 | 200+/279 |
| Metal errors | ~5,000 | < 500 | < 50 | < 50 | < 50 |
| Games reaching menu | 1-2 | 2-3 | 5-10 | 5-10 | 5+ |
| Recognizable rendering | No | No | Maybe | Yes | Yes |
| Per-game prefix | No | No | No | No | Yes |
| Steam API coverage | 6 funcs | 6 funcs | 6 funcs | 6 funcs | 100+ |

---

*This roadmap replaces all prior versions. Every phase must meet its gate before proceeding. No shortcuts, no claimed implementations without proof.*
