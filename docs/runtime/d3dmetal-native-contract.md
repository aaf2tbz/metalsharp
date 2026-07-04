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
| D3DMetal payload root | `MS_D3DMETAL_PAYLOAD_DIR` | `…/runtime/wine/lib/d3dmetal_native` |
| D3DMetal shared library path | `MS_D3DMETAL_SHARED_PATH` | `…/d3dmetal_native/external/libd3dshared.dylib` |
| D3DMetal framework path | `MS_D3DMETAL_FRAMEWORK_PATH` | `…/d3dmetal_native/external/D3DMetal.framework/Versions/A/D3DMetal` |
| Wine DLL overrides | `WINEDLLOVERRIDES` | `d3d10,d3d10_1,d3d10core,d3d11,d3d12,dxgi,nvapi64,nvngx-on-metalfx=b,n;gameoverlayrenderer,gameoverlayrenderer64=d` |
| launch receipt backend key | `metalsharp.graphics_backend` | `d3dmetal_native` |
| route id | `d3dmetal_native` | — |

These names are defined as Rust constants in `app/src-rust/src/mtsp/engine.rs`
(`D3DMETAL_NATIVE_BACKEND`, `MS_GRAPHICS_BACKEND_ENV`,
`MS_ACTIVE_GRAPHICS_BACKEND_ENV`, `MS_D3DMETAL_PAYLOAD_DIR_ENV`,
`MS_D3DMETAL_SHARED_PATH_ENV`, `MS_D3DMETAL_FRAMEWORK_PATH_ENV`) so they are
grep-discoverable and frozen.

## Naming and ownership rule

No compiled MetalSharp runtime code, launch receipt, settings UI, or bottle
manifest may carry `CX_*`, `CrossOver`, or `codeweavers` identifiers. CrossOver
is referenced only in research docs/tests as an oracle. The GPTK lane
(`PipelineId::M13`, `d3dmetal_gptk`, the external Apple-GPTK-Wine launch, and
the Homebrew GPTK installer) has been **removed entirely**. Fresh install never
requires Homebrew GPTK.

## Reserved status

`PipelineId::D3DMetalNative` exists and is `experimental`, but it remains guarded
until the packaged runtime, payload, and regression gates pass. The route is not
a replacement for M12/DXMT yet.

It is **not user-selectable and not broadly launchable** until:

- **Phase 2** — the Wine 11.5 host ABI shim parity (`winemac.drv` Metal layer +
  client-surface present bridge, `win32u` `MS_ACTIVE_GRAPHICS_BACKEND` gate,
  `ntdll` unix-call + `MS_D3DMETAL_*` discovery) is implemented and passes
  `tools/runtime/check-d3dmetal-shim-abi.py --strict`.
- **Phase 3** — the payload contract
  (`~/.metalsharp/runtime/wine/lib/d3dmetal_native/`) is staged and verified.

Until then the launcher rejects `d3dmetal_native` with
`"D3DMetal Native lane is reserved and not launchable until the Wine host ABI
and payload are ready"`.

## Baseline evidence

Current isolated no-game packaged-runtime evidence (2026-07-03):

Latest post-restage proof log: `/Volumes/AverySSD/metalsharp-d3dmetal-isolated-probe/logs/packaged-d3dmetal-post-restage-full-20260703-183603`.

- `wineboot` succeeds under staged MetalSharp Wine 11.5 x86_64.
- `tools/runtime/check-d3dmetal-native-payload.py --runtime-root <staged-runtime> --json` reports `ready: true`.
- `CreateDXGIFactory2` returns `hr=0x00000000`.
- `D3D12CreateDevice(NULL)` returns `hr=0x00000000`.
- `D3D11CreateDevice(NULL)` returns `hr=0x00000000` after the Wine loader gives `MS_D3DMETAL_PAYLOAD_DIR` priority over the stock `lib/wine` builtin root for the `d3dmetal_native` backend.
- `D3D10CreateDevice` and `D3D10CreateDevice1` still return `hr=0x80004005`; this is tracked as the remaining extended compatibility step, not a route-shape failure.
- D3D10 compatibility retest with payload-root `d3d10_1.dll` and `d3d10core.dll` staged from GPTK still returned `hr=0x80004005` (`/Volumes/AverySSD/metalsharp-d3dmetal-isolated-probe/logs/packaged-d3dmetal-d3d10-with-gptk-core-20260703-182537`; reconfirmed in the post-restage proof log). This confirms the current D3D10 failure is not just a stock-Wine module mixing problem; those DLLs remain optional compatibility payload members, not a support guarantee.

Historical Phase 1 checks used `tools/runtime/check-d3dmetal-shim-abi.py` to show that the stock shipped MetalSharp Wine lacked the CrossOver-style D3DMetal host ABI hooks. The current Wine patch stack closes that host ABI gap for the staged runtime.
