# D3D12 / DXMT Pipeline Completion Roadmap

**Updated:** 2026-05-30  
**Branch:** `codex/beta7-dxmt-cohesion` on `aaf2tbz/metalsharp`  
**PR:** #129  
**Workspace:** `/Volumes/AverySSD/metalsharp/pr129-dx12-pipeline/`  
**Current PR head:** `0062505` (`[codex] Phase 12A: dispatch value predeclaration`)

---

## Mission

Complete the D3D12 to Metal pipeline so DXMT can run D3D12 titles through MetalSharp with valid shader translation, correct root-signature/descriptor binding, correct PSO ABI behavior, and visible rendering. The immediate target corpus is the 766-shader Subnautica Below Zero trace in this workspace.

The roadmap has two tracks:

1. **Translator correctness:** make `MSLLowering::lower()` generate valid Metal for the whole corpus.
2. **Runtime correctness:** prove the compiled shaders bind to the right D3D12 resources and render correctly through DXMT/WMT.

---

## Non-Negotiable Constraint

The old converter, `DXILToMSL::convert()`, currently compiles **765/766** shaders and remains the production path until the typed lowering matches or exceeds it.

- Do not regress the old converter.
- Do not route production D3D12 through the new typed lowering until it reaches parity.
- Use old-converter MSL output as ground truth when a shader passes old lowering and fails new lowering.
- Keep experimental typed-lowering fixes contained to `msl_lowering.cpp`, `dxil_ir.*`, `llvm_bitcode.*`, `dxil_container.*`, and explicit test harness code unless a runtime phase requires D3D12 integration.

---

## Current State

Validation command:

```bash
./BUILD.sh && ./RUN.sh
```

Latest staged validation:

```text
DXIL Lowering: 766 pass, 0 fail, 0 skip
Metal: 97 ok, 669 fail
```

| Metric | Old Converter (`DXILToMSL`) | New Typed Lowering (`MSLLowering`) |
|---|---:|---:|
| DXIL parse/lower | 766/766 | 766/766 |
| Metal compile | 765/766 | 97/766 |
| Production status | Active | Experimental |
| Current role | Runtime ground truth | Future typed architecture |

Recent progress:

| Commit | Phase | Result |
|---|---|---|
| `87141bb` | Phase 11A typed operand coercion groundwork | New lowering moved to 75/766 Metal passes |
| `0062505` | Phase 12A dispatch value predeclaration | New lowering moved to 97/766 Metal passes |

Important Metal limits observed during validation:

- `[[buffer(N)]]` is valid only for `N <= 30`.
- Compute `texture2d<float, access::read_write>` declarations are limited to 8 textures.
- Wide placeholder binding declarations can make every shader fail; binding expansion must respect Metal limits and eventually use argument buffers or access-specific texture declarations.

---

## Architecture

```text
Game D3D12 calls
    |
    v
DXMT Wine DLL layer
    |
    +-- d3d12_pipeline_state.cpp
    |     |
    |     +-- DXILToMSL::convert()    old converter, production, 765/766
    |     +-- MSLLowering::lower()    typed lowering, experimental, 97/766
    |
    +-- Shared parser/lowering support
          |
          +-- dxil_container.cpp
          +-- llvm_bitcode.cpp
          +-- dxil_ir.cpp/hpp
          +-- msl_lowering.cpp/hpp
    |
    v
Metal compiler -> metallib -> WMT / Metal runtime
```

---

## Current Error Buckets

These are the current high-signal categories after `0062505`.

| Category | Approx Count | What It Means | Roadmap Owner |
|---|---:|---|---|
| `int4/uint4/float4` assigned to scalar | ~950 | Vector operations are still choosing scalar result slots or PHI/predecl types | Phase 12B / 12C |
| `subscripted value is not an array, pointer, or vector` | ~530 | Scalar expression is being indexed; component extraction needs type guard | Phase 12C |
| `member reference base type 'int'/'float' is not a structure or union` | ~320 | A value was scalarized too early, then later used as `.x/.y/.z/.w` | Phase 12B |
| undeclared `vNN` and `agg` | ~1,700 total across many names | Value numbering, constants, aggregate literals, or function params are missing from the value table | Phase 12A / 12D |
| `int` with `texture2d<..., read_write>` | ~150 | Resource handles are leaking into numeric arithmetic/PHI values | Phase 12E |
| texture/buffer binding limits | still present | `tex8+` and large descriptor tables need argument-buffer design | Phase 13 |
| pointer arithmetic / scalar pointer use | still present | `device char* + int`, invalid load/store shape | Phase 11B / 12F |

---

## Phase 11: Finish Expression And Pointer Coercion

**Goal:** remove remaining invalid scalar/pointer arithmetic and unsafe casts.  
**Target:** 125+ Metal passes before deeper value work; no regression below 97.

### 11B: Pointer Arithmetic Normalization

Current symptoms:

- `device char *` participates in integer expressions.
- GEP and pointer-like handles sometimes flow into scalar math.
- Load/store sites still sometimes treat scalars as pointers or pointers as scalars.

Work:

- Introduce a single helper for pointer offset expressions:
  - pointer result: `ptr + offset`
  - scalar arithmetic result: default numeric value, not pointer text
- In `GetElementPtr`, keep pointer role and address space.
- In `Load` and `Store`, dereference only confirmed pointer values.
- In binary ops, never emit raw resource/pointer handles into numeric expressions.

Gate:

- `device char *` binary errors below 25.
- No new old-converter diff.

### 11C: Cast Correctness Cleanup

Work:

- `as_type<T>` only when source/destination bit widths match.
- `static_cast<T>` for numeric scalar conversions.
- Vector-to-scalar conversion uses explicit lane selection, normally `.x`.
- Scalar-to-vector conversion uses typed constructors, e.g. `int4(v)`.
- Avoid `static_cast<int>(texture)` and `static_cast<int>(sampler)` entirely.

Gate:

- No `static_cast from 'float4' to 'int'` class errors.
- No direct texture/sampler numeric casts.

---

## Phase 12: Typed Value Resolution

**Goal:** eliminate undeclared values and stop scalar/vector type drift.  
**Target:** 250+ Metal passes.

This is the critical next phase. The current failures are less about individual syntax and more about the typed lowering not owning every SSA value's role, type, and lifetime.

### 12A: Value Table Completeness

Status:

- Started in `0062505` with dispatch-mode predeclaration.
- Still incomplete for low IDs (`v3`, `v6`, `v7`, `v10`, `v15`, `v16`, `v17`), high drift IDs (`v454` vs `v354`), and aggregate literals (`agg`).

Work:

- Build a pre-pass over the whole function before emission.
- Assign every instruction result its exact SSA slot once.
- Populate `value_table`, `value_types`, and `value_roles` for:
  - module constants
  - function constants
  - aggregate constants
  - globals
  - declarations
  - function parameters
  - all instruction result IDs
- Add a debug-only report for unresolved value IDs before Metal emission.

Gate:

- `use of undeclared identifier 'agg'` = 0.
- Low-ID undeclared values (`v3` through `v17`) = 0.

### 12B: Intrinsic Result Type Pre-Inference

Current issue:

Some DXIL intrinsics have opaque LLVM result types but known MSL expression shapes. If these are predeclared as `int`, later `.x` access fails.

Work:

- Add a pure function:

```cpp
MSLType inferIntrinsicResultType(uint32_t intrinsic_id, const LLVMInstruction &inst);
```

- Known mappings:
  - `CBufferLoad`, `CBufferLoadLegacy`, `BufferLoad`, `TextureLoad`, `TextureSample*`, `TextureGather*` -> `float4`
  - `RawBufferLoad`, raw vector load, `GetDimensions` -> `uint4`
  - `TextureSampleCmp*`, `CalcLOD` -> `float`
  - `CheckAccessFullyMapped` -> `bool`
  - `CreateHandle*`, `AnnotateHandle` -> resource role, not numeric type

Gate:

- `member reference base type 'int'/'float'` reduced by 80%.

### 12C: Vector/Scalar Flow Rules

Current issue:

Operations such as `v424 = v423 / int4(v298)` assign vector results into scalar predeclared variables.

Work:

- Binary result type selection must preserve vector type if either operand is vector.
- If a result slot is scalar but expression is vector, emit lane extraction:
  - `v = (expr).x`
- If result slot is vector but expression is scalar, emit splat:
  - `v = int4(expr)`
- For branch conditions:
  - scalar: `if (v)`
  - vector numeric: `if (any(v != typeN(0)))`
  - vector bool: `if (any(v))`

Gate:

- `assigning to 'int' from incompatible type 'int4/uint4/float4'` below 25.
- `subscripted value is not an array, pointer, or vector` below 100.

### 12D: Aggregate Literal Handling

Current issue:

`agg(...)` still escapes into emitted Metal.

Work:

- Convert aggregate constants at load time into typed MSL constructors.
- Preserve aggregate element types:
  - int aggregate -> `intN(...)`
  - uint aggregate -> `uintN(...)`
  - float aggregate -> `floatN(...)`
- For struct-like aggregates, emit local temp only if the type is real and addressable.

Gate:

- `undeclared identifier 'agg'` = 0.

### 12E: Resource Handle Role Isolation

Current issue:

Textures and samplers leak into arithmetic, comparisons, and PHI assignments.

Work:

- Track `ValueRole` aggressively:
  - `BufferHandle`
  - `TextureHandle`
  - `SamplerHandle`
  - `ThreadID`
  - `Constant`
  - `Generic`
- Numeric contexts must coerce resource handles to safe defaults, never to raw handle text.
- Texture contexts must reject numeric fallback and preserve handle text.
- PHI involving resource + numeric should become numeric default unless the PHI is explicitly a handle.

Gate:

- `invalid operands to binary expression ('int' and 'texture2d...')` = 0.
- No assignment from `sampler`/`texture2d` into scalar variables.

### 12F: Function Parameter And Low-ID Mapping

Current issue:

Low `vNN` identifiers often represent function params, root entries, or parser-relative values that were never mapped.

Work:

- Inspect `LLVMFunction` parameter metadata from `llvm_bitcode.cpp`.
- Map parameter IDs before instruction emission.
- For unknown pointer params, map by role:
  - CBV/SRV/UAV buffers -> `bufN`
  - textures -> `texN`
  - samplers -> `sampN`
  - system values -> `dtid`, `gtid`, `ggid`, `vid`, `vin`

Gate:

- Generic `use of undeclared identifier 'vNN'` reduced by 90%.

---

## Phase 13: Binding And Descriptor Architecture

**Goal:** stop relying on wide placeholder resource declarations and implement D3D12-style binding correctly.  
**Target:** 450+ Metal passes.

### 13A: Root Signature Extraction

Work:

- Parse root signature data from DXBC/DXIL container chunks.
- Record descriptor table ranges:
  - CBV
  - SRV
  - UAV
  - Sampler
- Preserve register space, lower bound, count, and visibility.

Gate:

- Per-shader binding manifest emitted in test output.

### 13B: Metal Binding Plan

Observed limits:

- `[[buffer(N)]]` max is 30.
- `texture2d<..., access::read_write>` max is 8.
- Blindly declaring `tex0..tex31` as read/write fails every shader.

Work:

- Buffers:
  - Keep direct `buf0..buf30` only as a temporary compatibility layer.
  - Move large descriptor tables into argument buffers.
- Textures:
  - SRV textures should use `access::sample` or `access::read`.
  - UAV textures should use `access::read_write`.
  - More than eight UAV-style textures require argument-buffer strategy or per-shader access narrowing.
- Samplers:
  - Declare only needed static/dynamic samplers.
  - Map sampler descriptor ranges to `sampN`.

Gate:

- No binding-limit compiler errors.
- No undeclared `bufN`/`texN`/`sampN` caused by descriptor table width.

### 13C: Handle Translation

Work:

- `CreateHandle`
- `CreateHandleForLib`
- `CreateHandleFromBinding`
- `CreateHandleFromHeap`
- `AnnotateHandle`

All must return typed handle records internally, not just strings. Final string emission should happen only at texture/buffer operation sites.

Gate:

- Resource handle values do not appear in arithmetic or generic PHI paths.

---

## Phase 14: Texture, Buffer, And Intrinsic Completeness

**Goal:** every DXIL intrinsic in the corpus emits valid, typed Metal.  
**Target:** 650+ Metal passes.

### 14A: Texture Operations

Work:

- `TextureLoad`: dimensions, mip, array slice, multisample.
- `TextureStore`: UAV writes, sample index variants.
- `TextureSample`: bias, level, grad.
- `TextureSampleCmp`: comparison samplers and depth textures.
- `TextureGather`: component selection and compare gather.

Gate:

- No invalid `.sample`, `.read`, `.write`, or `.gather` calls.

### 14B: Buffer Operations

Work:

- Raw buffer load/store.
- Structured buffer load/store.
- Byte-address buffer offsets.
- Correct vector width and component masks.
- Atomic buffer operations.

Gate:

- No invalid pointer dereference or scalar subscript errors from buffer operations.

### 14C: Math And Utility Intrinsics

Work:

- `dot2/3/4`
- `fma`, `mad`, `umad`
- `firstbitlow`, `firstbithigh`, `countbits`
- `isnan`, `isinf`, `isfinite`
- derivatives
- wave and quad operations
- barriers

Gate:

- `unsupported_intrinsics` = 0 for all shaders in the corpus.

---

## Phase 15: Old/New Converter Parity

**Goal:** typed lowering reaches or exceeds old converter compile rate.  
**Target:** 765/766 Metal passes.

Work:

- Generate old converter MSL and new typed MSL side by side for all 766 shaders.
- For every shader where old passes and new fails:
  - diff MSL
  - classify root cause
  - add typed lowering fix
  - rerun full corpus
- Maintain a scoreboard:
  - shader hash
  - old result
  - new result
  - first error
  - owning phase
  - fixed commit

Gate:

- `MSLLowering` compiles at least 765/766.
- No production path change yet.

---

## Phase 16: Runtime PSO And ABI Correctness

**Goal:** compiled shaders bind and execute correctly in DXMT.  
**Target:** visible rendering in Subnautica Below Zero.

### 16A: Root Signature To Metal ABI

Work:

- Verify root constants, CBV/SRV/UAV tables, static samplers.
- Align constant buffer offsets and sizes.
- Confirm Metal argument indices match generated MSL.

Gate:

- Shader reflection/binding manifest matches runtime PSO setup.

### 16B: Graphics Pipeline State

Work:

- Input layout -> Metal vertex descriptor.
- Render target formats.
- Depth/stencil formats and state.
- Blend state.
- Rasterizer/cull/scissor/viewport state.

Gate:

- Draw calls produce nonzero pixels without validation errors.

### 16C: Compute Pipeline State

Work:

- Threadgroup size from metadata.
- UAV and SRV binding.
- Barriers and resource state transitions.
- Dispatch dimensions.

Gate:

- Compute shaders compile and dispatch without Metal validation failures.

---

## Phase 17: Runtime Proof And Game Bring-Up

**Goal:** prove the D3D12 pipeline works beyond compiler acceptance.  
**Target:** Subnautica Below Zero reaches recognizable rendering; then expand to more games.

Work:

- Add runtime logging for:
  - selected converter path
  - shader hash
  - PSO creation success/failure
  - Metal library creation
  - binding manifest
  - draw/dispatch call counts
- Capture first-frame and 60-second traces.
- Confirm:
  - nonzero pixel readback
  - stable frame loop
  - no shader compile spam after cache warmup
  - no crash on menu/gameplay transition

Gate:

- Subnautica Below Zero shows recognizable rendering for 60 seconds.

---

## Phase 18: Production Switch

**Goal:** safely replace the old converter only after parity and runtime proof.

Work:

- Add feature flag:
  - old converter default
  - typed lowering opt-in
  - per-game override
- Add fallback:
  - if typed lowering fails, compile old converter output
  - log the fallback hash and reason
- Add cache versioning so old/new MSL outputs do not collide.
- Add CI or local corpus gate for the 766-shader set.

Gate:

- Typed lowering default only after:
  - 765/766 or better compile parity
  - Subnautica Below Zero visible rendering proof
  - no old converter regression

---

## Immediate Next Actions

1. Implement Phase 12B: central intrinsic result type inference.
2. Implement Phase 12C: vector/scalar assignment and condition rules.
3. Implement Phase 12D: aggregate literal conversion so `agg` never reaches Metal.
4. Implement Phase 12F: function parameter/low-ID mapping for `v3..v17`.
5. Rerun `./BUILD.sh && ./RUN.sh` after each patch and commit only if Metal pass count does not regress below 97.

Expected next milestone: **150+ Metal passes** with vector/scalar and low-ID fixes, before argument-buffer binding work.
