# MetalSharp Final Roadmap

**Updated:** 2026-05-30
**Branch:** `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp`
**PR:** #129

---

## Completed Phases (0–8 + Root Cause + 9A/9B)

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
| 9A | Aggregate constants + binding structs + chained CBufferLoad | `b0dd0a1` + `6bee9f9` | initializer_list 599→0, reinterpret_cast 322→0 |
| 9B | ExtractValue/InsertValue parser fix + undeclared identifier reduction | `4d1869e` | Undeclared identifiers ~2000→~400, 12→13 Metal pass |

---

## Current State

| Metric | Value |
|--------|-------|
| DXIL parse rate | **279/279** (100%) |
| Metal compile rate | **13/279** (4.7%) |
| Total Metal errors | **4,492** across 266 failing shaders |
| Top errors | `dx` undeclared (233), subscript non-array (176), float/int mismatch (156), member ref on float (128) |

---

## Phase 9 Findings: Why Per-Error-Category Patching Failed

### What we tried

Phase 9A/9B patched the existing string-emitting converter (`dxil_to_msl.cpp`) to fix specific error categories:

- `initializer_list<int>` subscript: 599→0 (agg() format + ensureScalarIndex)
- `expected ';' after expression`: 745→0 (same root cause)
- `reinterpret_cast of vector element`: 322→0 (buffer_origin SSA tracking)
- ExtractValue/InsertValue parser bugs: fixed field offsets in `llvm_bitcode.cpp`
- Undeclared identifiers: ~2000→~400 (Call type_id==0 stores, getValue dot fix)

### What happened

Net improvement: 5,041→4,492 errors (-549, -10.9%). Metal pass: 12→13.

Each fix eliminated its targeted category but exposed or shifted errors into new categories. The architecture fights back:

1. **No type tracking.** The converter emits `auto vN = expr;` everywhere. Metal infers types, but when the inferred type is wrong (e.g., `int` instead of `float4`), all downstream uses break. Fixing one expression changes the inferred type and cascades.

2. **String-based value resolution.** `getValue(N)` returns a string from `value_table[N]`. That string might be `"v95"`, `"buf3"`, `"in.v7.y"`, `"2097152.0f"`, or `"dx.op.createHandle"`. The caller has no idea what type it is and emits it into expressions that may or may not be valid.

3. **No semantic context.** The converter doesn't distinguish between a buffer handle, a loaded float4, a texture reference, or a function name. They're all strings. When a buffer load result flows into a texture sample's handle argument, it produces nonsense that only fails at Metal compile time.

4. **Diminishing returns per fix.** Each category fix requires understanding which string combinations produce the error, tracing through the emit logic, and patching the specific case. The patches interact in unpredictable ways. Phase 9B's parser fix, for example, caused `member reference base type 'int'` errors because ExtractValue now correctly resolves aggregates but those aggregates are sometimes `0` from unhandled calls.

### Conclusion

The string-emitting architecture is a proof-of-concept that proved the concept. It demonstrated that DXIL bitcode can be parsed and Metal can be emitted. But it cannot reach 80%+ Metal pass through incremental patching. The approach needs a fundamental change.

---

## Phase 10: Typed MSL IR Architecture

**Goal:** Replace string-emitting translator with a typed IR that produces correct Metal.
**Target:** 150+/279 Metal pass (54%+).

### Architecture

```
DXIL Bitcode
     │
     ▼
┌─────────────┐
│  LLVM Parser │  ← llvm_bitcode.cpp (KEEP — already fixed)
│  (existing)  │
└─────┬───────┘
      │ LLVMModule, LLVMFunction, LLVMInstruction
      ▼
┌─────────────────┐
│  DXIL IR Builder │  ← NEW: converts LLVM instructions → typed DXIL IR
│                  │
│  • TypedValue    │    Every value knows its type (float, float4, int, uint4,
│  • TypedInst     │    buffer_handle, texture_ref, sampler_ref)
│  • TypedBlock    │
│  • TypedFunction │
└─────┬──────────┘
      │ Typed DXIL IR
      ▼
┌─────────────────┐
│  MSL Lowering    │  ← NEW: lowers typed DXIL IR → MSL AST
│                  │
│  • Type-aware    │    Inserts casts, resolves bindings, emits correct
│  • Binding-aware │    Metal types (no more `auto vN = ...`)
│  • SSA-resolved  │
└─────┬──────────┘
      │ MSL AST
      ▼
┌─────────────────┐
│  MSL Emitter     │  ← NEW: walks AST → final Metal source
│                  │
│  • Typed decls   │    Emits `float4 v95 = ...` instead of `auto v95 = ...`
│  • Correct refs  │    Uses typed buffer/texture access patterns
└─────────────────┘
```

### 10A: DXIL IR Types and Values

**File:** `vendor/dxmt/src/airconv/dxil/dxil_ir.hpp` (new)

Define the typed IR value system:

```cpp
enum class DXILTypeKind {
    Void, Float, Double, Int, UInt,
    Float2, Float3, Float4,
    Int2, Int3, Int4,
    UInt2, UInt3, UInt4,
    Bool,
    BufferHandle,     // device char* with known binding
    TextureHandle,    // texture2d/texture3d with known binding
    SamplerHandle,    // sampler with known binding
    Struct,           // aggregate with typed fields
    Pointer,          // typed pointer
    Function,         // function reference
};

struct DXILValue {
    uint32_t id;              // original SSA value ID
    DXILTypeKind type;
    uint32_t binding_index;   // for handles: which buffer/texture/sampler slot
    std::string name;         // resolved name (buf3, tex0, etc.)
};
```

**Why this matters:** When CBufferLoad extracts from `buf3`, the result is typed `float4`. When that float4 is used in an FMul with an `int`, the lowering pass knows to insert `float(int_val)`. No more `auto vN = ...` guessing.

### 10B: DXIL IR Builder

**File:** `vendor/dxmt/src/airconv/dxil/dxil_ir_builder.cpp` (new)

Walk the existing `LLVMFunction` and build typed IR:

1. **Type propagation pass:** Start from constants (known types from `type_id`), propagate through instructions. ExtractValue on `float4` struct → `float`. FMul of `float * float` → `float`. CBufferLoad → `float4`.

2. **Binding resolution pass:** `CreateHandle`/`CreateHandleForLib`/`AnnotateHandle` resolve to concrete `BufferHandle`/`TextureHandle`/`SamplerHandle` with binding index. Track which values are handles vs loaded data.

3. **Instruction conversion:** Each LLVM opcode maps to a typed DXIL IR instruction. No string emission — just typed value references.

### 10C: MSL Lowering Pass

**File:** `vendor/dxmt/src/airconv/dxil/msl_lowering.cpp` (new)

Lower typed DXIL IR to MSL AST:

1. **Type-aware expression lowering:** DXIL `CBufferLoad(handle, index)` → MSL `reinterpret_cast<device float4&>(buf_N[index * 64])` where handle.type = BufferHandle and result.type = Float4.

2. **Automatic type coercion:** When operands have mismatched types (e.g., `float * int`), insert `static_cast<float>()`. When buffer load result is used as a handle, follow buffer_origin chain.

3. **Correct Metal emission:**
   - `float4 v95 = reinterpret_cast<device float4&>(buf3[(55)*64]);` — typed, not auto
   - `float v59 = v58.x;` — ExtractValue knows it's extracting float from float4
   - `device float4& v102 = reinterpret_cast<device float4&>(buf1[(8)*64]);` — chained loads resolve to original buffer

### 10D: Integration and Validation

- Wire new IR pipeline into existing test harness
- Keep old converter as fallback (can be selected via flag)
- Validate: 279/279 DXIL pass unchanged, Metal pass rate measured
- Error census comparison: old pipeline vs new pipeline

**Hardening gate:**
- Metal pass: **150+/279** (54%+)
- `auto` keyword: **0** occurrences in output (all values explicitly typed)
- `undeclared identifier` errors: **0**
- `initializer_list` errors: **0**
- `reinterpret_cast of vector element` errors: **0**

---

## Phase 11: Intrinsic Translation Completeness

**Goal:** Every DXIL intrinsic in the Subnautica 2 corpus produces valid, compiling Metal.
**Target:** 220+/279 Metal pass.

With the typed IR in place, intrinsic translation becomes mechanical — each intrinsic has typed inputs and typed outputs, so the MSL lowering just emits the correct Metal pattern.

### 11A: Texture Operations Full Coverage

- TextureLoad — all dimensions, all return types
- TextureStore — write to UAV textures
- TextureGather — component selection
- TextureSampleLevel — explicit LOD
- TextureSampleBias — bias
- TextureSampleCmp — comparison sampling for shadow maps
- BufferLoad / BufferStore — structured and raw buffers
- Texture2DMS — multisample read

### 11B: Compute-Specific Intrinsics

- `CreateHandleFromHeap` — descriptor heap dynamic indexing
- `AnnotateHandle` — handle metadata
- `Barrier` — group/shared/global barrier mapping
- `FlattenedThreadIdInGroup` — already mapped, verify

### 11C: Remaining Arithmetic and Utility

- `dot`, `cross`, `normalize`, `length` — verify vector width preservation
- `clamp`, `saturate`, `lerp` — verify type consistency
- `isfinite`, `isnan`, `isinf` — map to Metal equivalents
- `fma`, `mad` — fused multiply-add
- `countbits`, `firstbitlow`, `firstbithigh` — bit operations

**Hardening gate:**
- Metal pass: **220+/279** (79%+)
- `unknown intrinsic` diagnostics: **0** for all IDs in Subnautica corpus
- All texture dimension variants compile

---

## Phase 12: Pipeline Correctness

**Goal:** MSL that compiles correctly also produces correct visual output.
**Target:** Subnautica 2 shows nonzero pixel readback + recognizable rendering.

### 12A: Root Signature → Metal Binding Mapping

- Audit root parameter → Metal buffer/texture binding
- Verify descriptor table offsets are correct
- Check CBV/SRV/UAV byte offsets match between D3D12 and Metal

### 12B: Resource State and Barriers

- Verify `ResourceBarrier` transitions produce correct Metal resource states
- Check UAV barriers map to `MTLFence` or implicit sync

### 12C: Render Target and Depth

- Verify render target format mapping (DXGI_FORMAT → MTLPixelFormat)
- Check depth-stencil state creation and binding
- Verify viewport and scissor rect mapping

### 12D: Vertex Input and Stage-In

- Verify vertex buffer strides and offsets
- Check input layout → Metal vertex descriptor mapping
- Verify `LoadInput` intrinsic reads correct vertex attributes

**Hardening gate:**
- Subnautica 2 readback: `nonzero_pixels > 0` on multiple consecutive frames
- At least one recognizable rendered element (skybox, terrain, UI)
- No crash during 60-second gameplay session

---

## Phase 13: Test Infrastructure & Regression Prevention

**Goal:** Every converter change validated automatically.
**Target:** CI prevents regressions.

### 13A: DXIL Test Suite Expansion

- Expand from 279 real shaders to include targeted fixtures
- Gold file comparison for each fixture
- Track Metal compile pass rate per fixture

### 13B: Automated Error Census

- CI job runs converter on full Subnautica corpus
- Compiles all MSL output with `xcrun -sdk macosx metal`
- Tracks error category counts across commits
- Flags any increase in error count as CI failure

### 13C: Per-Phase Regression Harness

- Each completed phase has a saved snapshot of error counts
- New commits must not regress any completed phase metric

**Hardening gate:**
- 50+ DXIL test fixtures
- CI green on all fixtures
- Error census job runs in < 5 minutes

---

## Phase 14: Advanced Features

Needed for specific modern games but aren't blocking the majority.

| Feature | Metal Support | D3D12 Feature |
|---------|--------------|---------------|
| Enhanced barriers | MTLEvent for heap resources | `ResourceBarrier` BEGIN/END |
| Stream output | Compute + buffer writeback | `SOSetTargets` |
| Mesh shaders | Object + mesh functions (M2+) | `DispatchMesh` |
| Ray tracing | Metal RT API (M3+) | DXR 1.0/1.1 |

---

## Phase 15: Steam Bridge & Ecosystem

**Goal:** Full Steam functionality.
**Target:** Overlay, achievements, cloud saves working.

### 15A: Vendor Proton lsteamclient

- Vendor Proton `lsteamclient/` source
- Build both `steamclient64.dll` (PE) and `lsteamclient.dylib` (macOS native)
- Adapt build for arm64 macOS

### 15B: Steam API Integration

- ISteamUser, ISteamFriends, ISteamUserStats
- ISteamRemoteStorage (cloud saves)
- Callback flow through bridge

---

## Phase 16: Runtime Infrastructure

**Goal:** Multiple games running independently without conflicts.
**Target:** 5+ D3D12 games reaching main menu.

### 16A: Per-Game Prefix Isolation

- Per-appid Wine prefix: `~/.metalsharp/compatdata/<appid>/pfx/`
- Template from `prefix-steam` base

### 16B: Redistributable Auto-Install

- Central DLL cache: `~/.metalsharp/runtime/redist/`
- Auto-install VC++ / DirectX / XNA on first game launch

### 16C: Compat Config Expansion

- Extend `mtsp-rules.toml` with full flag system
- Mine Proton's game fix database for applicable patterns
- Target: 200+ game entries

---

## Dependency Graph

```
Phase 10 (Typed MSL IR)               ── NEXT
  │
  ├── Phase 11 (intrinsic completeness)
  │     │
  │     └── Phase 12 (pipeline correctness)
  │           │
  │           └── Phase 13 (test infrastructure)
  │
  ├── Phase 14 (advanced features)         ── parallel
  │
  └── Phase 15 (Steam bridge)              ── parallel

Phase 16 (runtime infrastructure)      ── after Phase 13
```

---

## Success Metrics

| Metric | Now | After Phase 10 | After Phase 11 | After Phase 12 | After Phase 16 |
|--------|-----|----------------|-----------------|-----------------|-----------------|
| Metal compile pass | 13/279 | 150+/279 | 220+/279 | 220+/279 | 220+/279 |
| Metal errors | 4,492 | < 500 | < 50 | < 50 | < 50 |
| Games reaching menu | 1-2 | 2-3 | 5-10 | 5-10 | 5+ |
| Recognizable rendering | No | No | Maybe | Yes | Yes |
| Per-game prefix | No | No | No | No | Yes |
| Steam API coverage | 6 funcs | 6 funcs | 6 funcs | 6 funcs | 100+ |
| Typed IR (no `auto`) | No | Yes | Yes | Yes | Yes |

---

## Key Paths

| Path | Purpose |
|------|---------|
| `vendor/dxmt/src/airconv/dxil/dxil_to_msl.cpp` | Current DXIL→MSL converter (keep as fallback) |
| `vendor/dxmt/src/airconv/dxil/llvm_bitcode.cpp` | Bitcode parser (KEEP — already fixed) |
| `vendor/dxmt/src/airconv/dxil/llvm_bitcode.hpp` | LLVM IR types (KEEP) |
| `vendor/dxmt/src/airconv/dxil/dxil_ir.hpp` | NEW: Typed DXIL IR types |
| `vendor/dxmt/src/airconv/dxil/dxil_ir_builder.cpp` | NEW: LLVM IR → typed DXIL IR |
| `vendor/dxmt/src/airconv/dxil/msl_lowering.cpp` | NEW: Typed DXIL IR → MSL AST |
| `dxmt-src/tests/dxil/test_dxil_converter.cpp` | Test harness |
| `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/` | Shader cache |
| `/Volumes/AverySSD/metalsharp/dxmt-src/` | Dev tree |
| `/Volumes/AverySSD/metalsharp/metalsharp-repo/` | Git repo |

---

## Validation Protocol

After every phase:

1. Build test harness: compile test_dxil_converter
2. DXIL pass: 279/279 must hold
3. Metal compile: `for f in *.metal; do xcrun -sdk macosx metal -c "$f" -o /dev/null 2> "errors/${f%.metal}.err"; done`
4. Measure: Metal compile pass rate, error category counts
5. Gate: Pass rate must meet phase target before proceeding

### Commit Discipline

- One commit per sub-phase
- Format: `feat(dxil): Phase N — <description>` + metric delta
- Push to `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp` only

---

*This roadmap replaces all prior versions. Phase 9A/9B demonstrated that per-error-category patching of the string-emitting converter cannot reach target Metal pass rates. Phase 10 rebuilds the translation layer with type awareness. Every phase must meet its gate before proceeding.*
