# M12 D3D12 Shader Engine

M12 treats shader translation as a defined engine, not as a single converter
function. The engine boundary starts when a D3D12 game supplies DXBC or DXIL
bytecode and ends when a Metal render or compute pipeline is created, cached,
bound, and used by a draw, dispatch, or present pass.

## Engine Surfaces

- **DXIL intake:** `dxil_container`, `llvm_bitcode`, and `dxil_ir` parse DXIL
  containers, LLVM bitcode, shader model, entrypoint, values, types, and
  resource handles.
- **MSL lowering:** `msl_lowering` is the primary typed DXIL-to-MSL path. It
  owns typed value coercion, DX op lowering, vertex input handling, binding
  manifests, and targeted compatibility fallbacks. `dxil_to_msl` remains the
  fallback path and must follow the same resource-binding rules.
- **D3D12 shader compiler:** `d3d12_shader_compiler` owns runtime compilation,
  cache lookup, converter selection, generated MSL sidecars, metallib creation,
  and compile diagnostics.
- **PSO build:** `d3d12_device` and `d3d12_pipeline_state` map D3D12 graphics
  and compute PSO descriptors to Metal pipeline states.
- **Runtime binding:** command list and command queue code bind descriptor
  tables, root constants, direct buffers, vertex inputs, draw args, and present
  resources before command encoding.
- **Cache and diagnostics:** shader cache paths, MSL/metallib sidecars,
  root-signature reports, PSO manifests, and focused D3D12 trace output are part
  of the engine contract because stale or incomplete cache data can make a fixed
  shader look broken.

## Invariants

- A DXIL `createHandle` regression must be proven against generated MSL, not
  inferred from a game frame. The present-blend regression for Elden shader
  `6f0e7d2f3cfff83c` proves the engine samples `tex0` and `tex1`, not the old
  double-counted `tex2` path.
- Every typed MSL shader must emit `metalsharp.binding_manifest.v1` so offline
  tooling can audit direct buffers, textures, samplers, and descriptor ranges.
- Root-signature coverage, PSO manifests, and shader manifests are separate
  proof layers. Passing one layer does not imply the others are correct.
- Live game captures are diagnostics only. Contract status comes from source
  contracts, converter tests, corpus replay, PSO audits, and SDK probes.
- Shader cache invalidation is part of correctness. When lowering changes,
  stale `.msl`, `.metallib`, and pipeline-cache outputs must not be used as
  proof of current behavior.

## Required Gates

Run the structural engine check first:

```bash
python3 tools/d3d12-metal-sdk/scripts/validate-shader-engine.py \
  --json tools/d3d12-metal-sdk/results/shader-engine-audit-metalsharp.json
```

Then run the focused native converter gate against the relevant captured corpus:

```bash
ninja -C vendor/dxmt/build-metalsharp-x64-tests tests/test_dxil_converter
vendor/dxmt/build-metalsharp-x64-tests/tests/test_dxil_converter <shader-cache>
```

For generated MSL directories, run:

```bash
python3 tools/d3d12-metal-sdk/scripts/dxil-binding-manifest-audit.py \
  --msl-dir <generated-msl> \
  --markdown tools/d3d12-metal-sdk/results/binding-manifest-audit.md
```

For captured runtime PSO/root-signature corpora, run the root-signature,
graphics PSO, and compute PSO audits before a game launch is used as evidence.

The full SDK render proof remains:

```bash
tools/ci/m12-check.sh
```

## Contract Authority

The machine-readable source of truth is
`tools/d3d12-metal-sdk/contracts/d3d12-shader-engine-contract.json`.
It names the source surfaces, required offline gates, runtime artifacts, and
review evidence. `validate-contracts.py` includes this contract, while
`validate-shader-engine.py` verifies the engine-specific source and gate shape.
