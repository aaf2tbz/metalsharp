# D3D12 Pipeline Complete Roadmap

**Created:** 2026-05-30
**Branch:** `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp`
**PR:** #129

---

## Mission

Complete the D3D12→Metal translation pipeline so that every DXBC/DXIL shader compiles to valid Metal with zero errors, no missed shaders, and accurate PSO/ABI runtime behavior. The end state is Subnautica Below Zero rendering correctly end-to-end through DXMT + MetalSharp.

## Hard Constraint: Preserve the 765

The old converter (`DXILToMSL::convert`) passes **765/766** shaders. This represents months of accumulated fixes across Phases 0–9. Every phase in this roadmap must:

1. **Never regress the old converter.** It stays in `d3d12_pipeline_state.cpp` as the production path until the new lowering matches or exceeds it.
2. **Learn from the old converter.** When the new lowering fails on a shader the old one passes, diff the two MSL outputs. The old converter's output is ground truth for that shader.
3. **Match first, then exceed.** The new lowering must reach 765/766 pass rate before it replaces the old converter. Only then do we add features the old converter can't support.

The 765 shaders that already compile are the foundation. We build on top of them, not around them.

---

## Current State (Phase 10E, commit `277c330`)

| Metric | Old Converter (`DXILToMSL`) | New Typed Lowering (`MSLLowering`) |
|--------|----------------------------|----------------------------------|
| DXIL parse | 766/766 (100%) | 766/766 (100%) |
| Metal compile | **765/766** (99.9%) | **16/766** (2.1%) |
| Total Metal errors | ~0 | 12,963 |
| Corpus | Subnautica 2 (766 shaders) | Same |

The old converter is production-quality. The new typed lowering is the future architecture but needs to close the gap.

---

## Architecture Overview

```
Game (D3D12 calls)
    │
    ▼
DXMT (Wine DLL layer)
    │
    ├── d3d12.dll ─── d3d12_pipeline_state.cpp
    │                    │
    │                    ├── DXILToMSL::convert()    ← OLD: production, 765/766 pass
    │                    └── MSLLowering::lower()     ← NEW: typed IR, 16/766 pass
    │
    │   Shared infrastructure:
    │     ├── llvm_bitcode.cpp        Bitcode parser (both use this)
    │     ├── dxil_container.cpp      DXIL container parser (both use this)
    │     ├── dxil_ir.hpp/cpp         MSLType system, DXILIRBuilder (new only)
    │     └── msl_lowering.hpp/cpp    Typed lowering pass (new only)
    │
    ▼
Metal Compiler (xcrun metal)
    │
    ▼
Metallib → loaded into GPU via WMT
```

---

## Error Analysis (New Typed Lowering, 766 Shaders)

| # | Error Category | Count | Root Cause | Fix Strategy |
|---|---------------|-------|------------|-------------|
| 1 | `device char *` vs `int`/`float` binary | 390+221+72=683 | DXIL pointers are integers; Metal's aren't. Buffer refs used in arithmetic. | Expression coercion: wrap pointer operands in `static_cast<int>` when used in integer ops |
| 2 | `subscripted value not array` | 349 | Indexing a scalar value (expression resolves to non-vector) | Type-aware subscript: only emit `[N]` when operand is confirmed vector/array/pointer |
| 3 | `float` vs `int` binary | 334+69=403 | Mixed-type arithmetic without casts | Expression coercion: `static_cast<float>(int_val)` in float ops |
| 4 | `undeclared tex8+` | 995 | Compute shaders reference >8 textures; Metal limits read_write textures to 8 | Argument buffer binding for textures beyond tex7 |
| 5 | `undeclared v39/v76/v77/v10/v7/agg` | 326+73=399 | Value numbering gaps; constants not in value_table | Fix constant resolution in getValue/resolveValue |
| 6 | `indirection requires pointer operand` | 92+82=174 | Dereferencing a scalar (Load of non-pointer expression) | Track which values are actual pointers vs scalars |
| 7 | `reinterpret_cast` errors | 93+81=174 | Invalid reinterpret_cast between incompatible types | Only emit reinterpret_cast when source and dest are same size |
| 8 | `device char* = int` / `= float` | 87+74=161 | IntToPtr/BitCast emitting pointer type for scalar expressions | Pass through raw values, don't wrap in pointer casts |
| 9 | `as_type cast half→float` | 81 | as_type requires same bit width | Use static_cast for half↔float |
| 10 | `float vs float` binary | 75 | Likely an expression resolving to a struct/function not a scalar | Investigate specific cases |
| 11 | `texture2d subscript operator` | 121 | `.read()` result subscripted instead of using component access | Use `.read()[coord]` or proper texture sample patterns |

---

## Phase Plan

### Phase 11: Expression Coercion Pass

**Goal:** Eliminate categories 1, 3, 6, 8 (683+403+174+161 = 1,421 errors)
**Target:** 200+/766 Metal pass
**Method:** Diff-driven — compare new lowering output against old converter output for each of the 765 passing shaders. The old converter's MSL is ground truth.

The typed lowering currently emits correct declarations but incorrect expressions. When building expressions like `v77 + v75` where v77 is `device char*` and v75 is `int`, Metal rejects it. The fix: when building any binary expression, check operand types and insert casts.

**Validation approach:**
- For every shader where old converter passes and new lowering fails: diff the two MSL outputs
- Catalog the patterns the old converter uses that the new one doesn't
- Implement those patterns in the new lowering, not by copying strings, but by understanding the type coercion rules

**Files:** `msl_lowering.cpp`

#### 11A: Typed Expression Builder

Replace raw string concatenation in binary ops with a coercion-aware builder:

```cpp
std::string coerceOperand(uint32_t val_id, MSLType target_type) {
    auto val = getValue(val_id);
    auto val_type = val_id < ctx.value_types.size() ? ctx.value_types[val_id] : MSLType{};
    if (val_type.kind == MSLTypeKind::DeviceCharPtr || val_type.kind == MSLTypeKind::ThreadgroupCharPtr) {
        if (target_type.kind == MSLTypeKind::Int || target_type.kind == MSLTypeKind::UInt)
            return "static_cast<int>(" + val + ")";
        if (target_type.kind == MSLTypeKind::Float)
            return "static_cast<float>(" + val + ")";
    }
    if (val_type.kind == MSLTypeKind::Int && target_type.kind == MSLTypeKind::Float)
        return "static_cast<float>(" + val + ")";
    if (val_type.kind == MSLTypeKind::Float && target_type.kind == MSLTypeKind::Int)
        return "static_cast<int>(" + val + ")";
    return val;
}
```

Apply this in:
- Integer binary ops (Add/Sub/Mul/Div/Rem/And/Or/Xor/Shl/Shr)
- Float binary ops (FAdd/FSub/FMul/FDiv/FRem)
- Comparison ops (FCmp/ICmp)
- Select condition/true/false values

#### 11B: Pointer-Aware GEP and Load

GEP expressions should not be used as arithmetic operands. Track pointer values separately:
- GEP results: store the pointer expression but mark as `DeviceCharPtr`
- Load: when loading from a pointer, emit `*(target_type*)(ptr_expr)` only if the operand is confirmed pointer type
- Store: same pointer check

#### 11C: Cast Correctness

- `BitCast`: only emit `reinterpret_cast` when source and dest have same bit width
- `ZExt/SExt/Trunc/FPConvert`: emit `static_cast<target>(val)` with correct types
- `as_type`: only emit when bit widths match; fall back to `static_cast` otherwise

**Gate:** 200+/766 Metal pass, `device char *` binary errors < 50, `float vs int` errors < 50

---

### Phase 12: Value Resolution and Undeclared Identifiers

**Goal:** Eliminate category 5 (399 errors) and `undeclared vNN` entirely
**Target:** 350+/766 Metal pass

#### 12A: Constant Pool Resolution

The `getValue` function falls through to constants when value_table is empty. But constants with IDs below `instruction_start_value` (function params, globals) aren't always populated. Fix:
- Pre-populate value_table with ALL constants from both module-level and function-level
- Map function parameter value IDs to their MSL parameter names
- Map global variable value IDs to their allocated names

#### 12B: Function Parameter Mapping

DXIL functions have parameters (root signature entries, system values, resource bindings). Map them:
- `dtid` → `dtid` (thread_position_in_grid)
- `gtid` → `gtid` (thread_position_in_threadgroup)
- `ggid` → `ggid` (threadgroup_position_in_grid)
- Buffer/texture parameters → `bufN`/`texN`
- Vertex input → `vin.aN`

#### 12C: PHI Value Pre-Declaration

PHI nodes create values that need pre-declaration before the switch/case dispatch. Verify all PHI result slots are pre-declared with correct types.

**Gate:** `undeclared identifier` errors = 0, `use of undeclared identifier 'agg'` = 0

---

### Phase 13: Texture and Binding Architecture

**Goal:** Eliminate categories 4, 11 (995+121 = 1,116 errors)
**Target:** 550+/766 Metal pass

#### 13A: Argument Buffer Binding for Textures

Metal limits `texture2d<float, access::read_write>` to 8 bindings. DXIL shaders reference up to tex22+. Solution:
- Use `array<texture2d<float, access::read_write>, N>` in an argument buffer for textures beyond slot 7
- Or: use `texture2d<float, access::read>` for SRV textures (most don't need write access)
- Map DXIL `CreateHandle` texture bindings to the correct Metal binding strategy

#### 13B: Texture Sample/Read Patterns

- `.sample()` → correct sampler + coordinate + LOD patterns
- `.read()` → correct `uint2` coordinate patterns
- `.gather()` → component select patterns
- `.write()` → UAV write patterns
- Buffer load/store → `reinterpret_cast<device T&>(buf[offset])` patterns

#### 13C: Root Signature → Binding Mapping

- Parse root signature from DXBC container metadata
- Map root constants, descriptor tables, static samplers to Metal bindings
- Verify CBV/SRV/UAV byte offsets match

**Gate:** `undeclared identifier 'texN'` errors = 0, `subscript operator` errors < 20

---

### Phase 14: Intrinsic Translation Completeness

**Goal:** Every DXIL intrinsic produces valid Metal
**Target:** 700+/766 Metal pass

With expression coercion (Phase 11), value resolution (Phase 12), and texture bindings (Phase 13), most intrinsic translations should already work. This phase handles remaining edge cases.

#### 14A: Texture Operations

- TextureLoad all dimensions, all return types
- TextureStore write patterns
- TextureGather component selection
- TextureSampleLevel/Grad/Bias/Cmp variants
- BufferLoad/BufferStore structured and raw
- Texture2DMS multisample

#### 14B: Compute-Specific

- `CreateHandleFromHeap` — descriptor heap dynamic indexing
- `AnnotateHandle` — handle metadata
- `Barrier` — group/shared/global barrier
- Group shared memory (threadgroup) operations

#### 14C: Arithmetic and Utility

- `dot`, `cross`, `normalize`, `length` — vector width preservation
- `clamp`, `saturate`, `lerp` — type consistency
- `isfinite`, `isnan`, `isinf` — Metal equivalents
- `fma`, `mad` — fused multiply-add
- `countbits`, `firstbitlow`, `firstbithigh` — bit ops
- `umad`, `imax`, `umin` — integer intrinsics

**Gate:** `unknown intrinsic` diagnostics = 0 for all IDs in Subnautica 2 corpus

---

### Phase 15: Pipeline Correctness — PSO and ABI

**Goal:** MSL that compiles also produces correct visual output
**Target:** Subnautica 2 shows nonzero pixel readback + recognizable rendering

This phase ensures the compiled Metal shaders are *semantically correct*, not just syntactically valid.

#### 15A: Root Signature → Metal Binding Verification

- Audit root parameter → Metal buffer/texture binding
- Verify descriptor table offsets match between D3D12 and Metal
- Check CBV/SRV/UAV byte offsets
- Verify constant buffer sizing and alignment

#### 15B: Render Pipeline State

- Render target format mapping (DXGI_FORMAT → MTLPixelFormat)
- Depth-stencil state creation and binding
- Viewport and scissor rect mapping
- Blend state mapping
- Rasterizer state mapping

#### 15C: Command Buffer Correctness

- Draw call mapping (DrawInstanced, DrawIndexedInstanced)
- Dispatch mapping (Dispatch, DispatchThreadGroup)
- Resource barrier → Metal sync
- Copy/Clear operations

#### 15D: Vertex Input Pipeline

- Vertex buffer strides and offsets
- Input layout → Metal vertex descriptor
- `LoadInput` intrinsic reads correct vertex attributes
- Index buffer format mapping

**Gate:** Subnautica 2 readback: `nonzero_pixels > 0` on multiple consecutive frames. At least one recognizable rendered element. No crash during 60-second gameplay.

---

### Phase 16: Runtime Infrastructure

**Goal:** Multiple games running independently
**Target:** 5+ D3D12 games reaching main menu

#### 16A: Per-Game Prefix Isolation

- Per-appid Wine prefix: `~/.metalsharp/compatdata/<appid>/pfx/`
- Template from `prefix-steam` base

#### 16B: Redistributable Auto-Install

- Central DLL cache: `~/.metalsharp/runtime/redist/`
- Auto-install VC++ / DirectX / XNA on first game launch

#### 16C: Compat Config Expansion

- Extend `mtsp-rules.toml` with full flag system
- Mine Proton's game fix database for applicable patterns
- Target: 200+ game entries

---

### Phase 17: Steam Bridge

**Goal:** Full Steam functionality
**Target:** Overlay, achievements, cloud saves working

#### 17A: Vendor Proton lsteamclient

- Vendor Proton `lsteamclient/` source
- Build both `steamclient64.dll` (PE) and `lsteamclient.dylib` (macOS native)
- Adapt build for arm64 macOS

#### 17B: Steam API Integration

- ISteamUser, ISteamFriends, ISteamUserStats
- ISteamRemoteStorage (cloud saves)
- Callback flow through bridge

---

## Dependency Graph

```
Phase 11 (Expression Coercion)          ── NEXT
  │
  ├── Phase 12 (Value Resolution)        ── can parallel with 11C
  │     │
  │     └── Phase 13 (Texture Bindings)
  │           │
  │           └── Phase 14 (Intrinsics)
  │                 │
  │                 └── Phase 15 (PSO/ABI Correctness)
  │
  ├── Phase 16 (Runtime Infrastructure)  ── parallel with 14-15
  │
  └── Phase 17 (Steam Bridge)            ── after Phase 15

Phase 10 (complete) ← we are here
```

---

## Success Metrics

| Metric | Now (10E) | After 11 | After 13 | After 14 | After 15 |
|--------|-----------|----------|----------|----------|----------|
| DXIL parse | 766/766 | 766/766 | 766/766 | 766/766 | 766/766 |
| Metal compile | 16/766 | 200+/766 | 550+/766 | 700+/766 | 766/766 |
| Metal errors | 12,963 | < 3,000 | < 500 | < 50 | 0 |
| `device char*` binary | 683 | < 50 | 0 | 0 | 0 |
| `float vs int` | 403 | < 50 | 0 | 0 | 0 |
| `undeclared identifier` | 1,394 | < 200 | 0 | 0 | 0 |
| Recognizable rendering | No | No | No | Maybe | Yes |
| Games reaching menu | 0 | 0 | 1-2 | 3-5 | 5+ |

---

## Key Files

| File | Role | Status |
|------|------|--------|
| `src/airconv/dxil/llvm_bitcode.cpp` | Bitcode parser | Stable (shared) |
| `src/airconv/dxil/dxil_container.cpp` | DXIL container parser | Stable (shared) |
| `src/airconv/dxil/dxil_to_msl.cpp` | Old converter | Production (765/766) — fallback |
| `src/airconv/dxil/dxil_ir.hpp` | MSLType system | Active development |
| `src/airconv/dxil/dxil_ir.cpp` | DXILIRBuilder | Active development |
| `src/airconv/dxil/msl_lowering.cpp` | Typed lowering | Active development (16/766) |
| `src/d3d12/d3d12_pipeline_state.cpp` | PSO creation, shader compile entry | DXBC dump added |
| `tests/dxil/test_phase10.cpp` | Test harness (DXBC + Metal compile) | Working |

## Key Paths

| Path | Purpose |
|------|---------|
| `/Volumes/AverySSD/metalsharp/dxmt-src/` | Dev tree (compile from here) |
| `/Volumes/AverySSD/metalsharp/metalsharp-repo/` | Git repo (push from here) |
| `/tmp/dxil_corpus_full/` | 766-shader DXBC corpus |
| `/tmp/dxmt_shader_cache/` | Live game shader dump |
| `/Users/alexmondello/.metalsharp/` | Live MetalSharp install (internal SSD) |
| `/Users/alexmondello/Dev/metalsharp/` | Live MetalSharp source (internal SSD) |

---

## Validation Protocol

After every phase:

1. Build: `c++ -std=c++20 -I src -I include -DDXMT_PAGE_SIZE=4096 -DNOMINMAX -Wno-everything -fblocks -framework Foundation -L/opt/homebrew/opt/llvm@15/lib -lLLVM-15 -o /tmp/test_phaseNN tests/dxil/test_phase10.cpp src/airconv/dxil/dxil_ir.cpp src/airconv/dxil/msl_lowering.cpp src/airconv/dxil/llvm_bitcode.cpp src/airconv/dxil/dxil_container.cpp`
2. Run: `/tmp/test_phaseNN /tmp/dxil_corpus_full /tmp/metal-test-NN`
3. DXIL parse: 766/766 must hold
4. Metal compile: measure pass rate
5. Error census: `rg "error:" errors/*.err --no-filename | sed 's/.*error: //' | sort | uniq -c | sort -rn`
6. Gate: pass rate must meet phase target
7. Commit: `git add && git commit && git push origin codex/beta7-dxmt-cohesion`

### Commit Discipline

- One commit per sub-phase
- Format: `[codex] Phase NN: description — metric delta`
- Push to `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp` only
- Never push to `aaf2tbz/dxmt` fork
- Copy files from `dxmt-src/` to `metalsharp-repo/vendor/dxmt/` before committing

---

## Old Converter Integration Strategy

The old converter (`DXILToMSL::convert`) passes 765/766. It remains the production path in `d3d12_pipeline_state.cpp`. The new typed lowering (`MSLLowering::lower`) is being developed alongside it.

**Preservation rule:** The old converter is never modified or removed until the new lowering proves it can match 765/766 on its own. Both converters coexist. The old converter's output serves as the reference implementation.

Integration strategy:

1. **Now through Phase 14:** Both converters exist. Old is production, new is development. Every new lowering change is validated by diffing against old converter output.
2. **Phase 15:** When new converter reaches 765+/766 AND passes a manual review of 50+ shader diffs showing semantic equivalence, switch the production path.
3. **Phase 16+:** Remove old converter code only after 2+ weeks of stable new converter in production.

The switch point is in `d3d12_pipeline_state.cpp` line ~433:
```cpp
// Current: auto msl_result = dxmt::dxil::DXILToMSL::convert(*module, shader_info);
// Future:  auto msl_result = dxmt::dxil::MSLLowering::lower(*module, shader_info);
```

### Diff-Driven Development Protocol

For every shader where old passes and new fails:

1. Get old MSL: `cat /tmp/dxmt_shader_cache/<hash>.msl`
2. Get new MSL: `cat /tmp/metal-test-NN/<hash>.metal`
3. Diff: identify what pattern the old converter uses that the new one doesn't
4. Implement: add the missing type coercion/binding/emission pattern to the new lowering
5. Verify: recompile and confirm the shader now passes

This ensures the new lowering learns from 765 working examples rather than guessing.

---

*This roadmap replaces `MetalSharp Final Roadmap.md` as the authoritative plan for the D3D12 pipeline. The goal is zero Metal compilation errors, zero missed shaders, and accurate PSO/ABI runtime behavior across all D3D12 games.*
