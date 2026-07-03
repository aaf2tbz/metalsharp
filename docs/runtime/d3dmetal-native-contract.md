# D3DMetal Native Runtime Contract

Status: **Phase 1 — frozen ABI names, reserved lane (not selectable/launchable).**

MetalSharp ships a single MetalSharp-owned native D3DMetal pipeline
(`d3dmetal_native`). It consumes a MetalSharp-owned Wine 11.5 host ABI plus an
Apple D3DMetal payload under MetalSharp's contract. There is **no Homebrew GPTK
dependency** and **no CrossOver/CX runtime naming**. CrossOver is only ever an
oracle for behavior.

## Frozen ABI variable names

| Concept | MetalSharp-owned name | Value |
|---|---|---|
| bottle graphics backend | `MS_GRAPHICS_BACKEND` | `d3dmetal_native` |
| active backend visible to Wine host modules | `MS_ACTIVE_GRAPHICS_BACKEND` | `d3dmetal_native` |
| D3DMetal shared library path | `MS_D3DMETAL_SHARED_PATH` | `…/d3dmetal_native/external/libd3dshared.dylib` |
| D3DMetal framework path | `MS_D3DMETAL_FRAMEWORK_PATH` | `…/d3dmetal_native/external/D3DMetal.framework/Versions/A/D3DMetal` |
| launch receipt backend key | `metalsharp.graphics_backend` | `d3dmetal_native` |
| route id | `d3dmetal_native` | — |

These names are defined as Rust constants in `app/src-rust/src/mtsp/engine.rs`
(`D3DMETAL_NATIVE_BACKEND`, `MS_GRAPHICS_BACKEND_ENV`,
`MS_ACTIVE_GRAPHICS_BACKEND_ENV`, `MS_D3DMETAL_SHARED_PATH_ENV`,
`MS_D3DMETAL_FRAMEWORK_PATH_ENV`) so they are grep-discoverable and frozen.

## Naming and ownership rule

No compiled MetalSharp runtime code, launch receipt, settings UI, or bottle
manifest may carry `CX_*`, `CrossOver`, or `codeweavers` identifiers. CrossOver
is referenced only in research docs/tests as an oracle. The GPTK lane
(`PipelineId::M13`, `d3dmetal_gptk`, the external Apple-GPTK-Wine launch, and
the Homebrew GPTK installer) has been **removed entirely**. Fresh install never
requires Homebrew GPTK.

## Reserved status

`PipelineId::D3DMetalNative` exists and is `experimental`, but it is **not
user-selectable and not launchable** until:

- **Phase 2** — the Wine 11.5 host ABI shim parity (`winemac.drv` Metal layer +
  client-surface present bridge, `win32u` `MS_ACTIVE_GRAPHICS_BACKEND` gate,
  `ntdll` unix-call + `MS_D3DMETAL_*` discovery) is implemented and passes
  `tools/runtime/check-d3dmetal-shim-abi.py --strict`.
- **Phase 3** — the payload contract
  (`~/.metalsharp/runtime/wine/lib/d3dmetal_native/`) is staged and verified.

Until then the launcher rejects `d3dmetal_native` with
`"D3DMetal Native lane is reserved and not launchable until the Wine host ABI
and payload are ready"`.

## Baseline evidence (Phase 1)

`tools/runtime/check-d3dmetal-shim-abi.py` against the current runtime reports:

- `baseline_ready: true` — DXMT/MoltenVK macOS host path present.
- `native_d3dmetal_host_abi_ready: false` — the CrossOver-style D3DMetal host
  ABI hooks (`WineMetalLayer` nextDrawable, `CLIENT_SURFACE_PRESENTED`,
  `client_surface_present`, `MS_ACTIVE_GRAPHICS_BACKEND`/`libd3dshared.dylib`
  discovery) are **absent** from the shipped MetalSharp Wine 11.5 modules. This
  is the precise gap Phase 2 closes.
