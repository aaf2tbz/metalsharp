# M12 Pipeline Map

Last verified: 2026-05-16.

M12 is the D3D12 -> DXMT -> Metal path used by the game launcher. The current
MetalSharp tree also contains a native `metalsharp_d3d12` implementation and a
Cocoa/CAMetalLayer viewer path, but those are not the same runtime path that M12
uses for Wine-launched games.

## Runtime Ownership

| Layer | Current owner | Evidence | Status |
| --- | --- | --- | --- |
| Game detection | `app/src-rust/src/mtsp/pe.rs`, `rules.rs` | D3D12 imports select `PipelineId::M12` for 64-bit games. | Present in current project |
| Pipeline definition | `app/src-rust/src/mtsp/engine.rs` | M12 is named `D3D12 -> Metal via DXMT`, is the first stable pipeline, deploys DXMT DLLs, and sets D3D12/DXGI/D3D11 overrides. | Present in current project |
| Launcher handoff | `app/src-rust/src/mtsp/launcher.rs` | M12 routes through `launch_dxmt_metal`, copies D3D/DXGI DLLs into the game directory, binds winemetal into the prefix/runtime, and sets Wine/DYLD/cache env. | Present in current project |
| Shader/cache routing | `app/src-rust/src/mtsp/shader_cache.rs` | M12 uses `m12` and `dxmt-metal12` cache directories. | Present in current project |
| DXMT D3D12 implementation | External DXMT source tree | Conformance branch contains the real DXMT D3D12/DXIL/winemetal work used by M12 runtime DLLs. | External source tree |
| Native D3D12 target | `include/metalsharp/D3D12Device.h`, `src/d3d/d3d12/*` | Builds `build/d3d12.dylib` and exposes `D3D12CreateDevice`. | In-tree, smoke-tested |
| Cocoa surface | `src/win32/user32/WindowManager.mm`, `src/dxgi/DXGISwapChain.mm` | Creates NSWindow/CAMetalLayer for the native loader path. | In-tree, not the Wine M12 surface |
| Wine M12 surface | DXMT `winemetal.so` plus Wine/macOS windowing | DXMT presents through Wine/winemetal, not through the native `WindowManager` path. | External runtime path |

## M12 Launch Flow

1. The PE scanner sees `d3d12.dll` and rules select `PipelineId::M12`.
2. The launcher resolves the game directory and Wine prefix.
3. M12 deploys DXMT PE DLLs into the game directory:
   `d3d12.dll`, `d3d11.dll`, `dxgi.dll`, and `d3d10core.dll`.
4. M12 binds `winemetal.dll` into the prefix `C:\windows\system32`, ensures
   `winemetal.so` is present in the runtime Unix library roots, and removes
   stale game-local `winemetal` copies.
5. M12 sets `WINEDLLOVERRIDES`, parent-root `WINEDLLPATH`, and
   `DXMT_WINEMETAL_UNIXLIB=winemetal.so` so the PE stub can load the Unix Metal
   bridge even when Wine treats `winemetal.dll` as a native prefix DLL.
6. M12 adds DXMT/Wine unix library paths to `DYLD_FALLBACK_LIBRARY_PATH`.
7. M12 sets shader and pipeline cache paths under the MetalSharp cache root.
8. Wine launches the executable. Unity games receive `-force-d3d12` so they do
   not silently fall back to OpenGL before DXMT is tried.
9. DXMT handles D3D12/DXGI calls, creates a Wine client surface when needed,
   attaches a CAMetalLayer through `winemetal.so`, compiles DXIL/MSL work, sends
   commands through `winemetal`, and presents through the Wine/macOS surface.
10. Wine-backed Steam app launches also carry `SteamAppId` and `SteamGameId` in
    the direct game process environment so SteamAPI does not bounce the run back
    through a naked Steam confirmation handoff.

## Current Verification

These checks were run from the repository root:

```sh
cmake --build build --target test_d3d12
cmake --build build --target test_d3d12_entrypoint test_d3d12
./build/tests/test_d3d12
./build/tests/test_d3d12_entrypoint
ctest --test-dir build -R "d3d12|d3d12_entrypoint|phase18|phase19" --output-on-failure
nm -gU build/d3d12.dylib | rg "D3D12CreateDevice|D3D12GetDebugInterface|D3D12SerializeRootSignature"
otool -L build/d3d12.dylib
```

Results:

- `test_d3d12` passed: 50 passed, 0 failed.
- `test_d3d12_entrypoint` passed: 5 passed, 0 failed.
- `ctest` passed `d3d12`, `d3d12_entrypoint`, `phase18`, and `phase19`.
- `build/d3d12.dylib` exports `D3D12CreateDevice`.
- `build/d3d12.dylib` links Metal, Foundation, QuartzCore, AppKit, and
  `libmetalirconverter`.

The external DXMT source tree also rebuilt successfully with:

```sh
ninja -C <dxmt-source>/build src/winemetal/unix/winemetal.so src/d3d12/d3d12.dll
```

No runtime DLL deployment was performed during this mapping pass.

## Sons Of The Forest Runtime Check

The local M12 runtime now gets past the old Unity `d3d12: no D3D12 installed`
startup wall. The DXMT patch set includes a no-op `ID3D12InfoQueue` compatibility
surface because Unity queries that D3D12 SDK-layers interface during startup.
Without it, DXMT created the device but Unity collapsed into the generic
DirectX 11 initialization dialog.

After the InfoQueue shim, the live trace showed:

- `D3D12CreateDevice` succeeds at feature level 12.1.
- `D3D11On12CreateDevice` succeeds for Unity compatibility.
- `D3D12_OPTIONS` initially reported `TypedUAVLoadAdditionalFormats = FALSE`
  even though the runtime format-support path reports typed UAV load/store
  capability for supported formats.
- Unity reports `GraphicsShaderLevel : 50` on the forced M12 path, while the
  staged DXMT source left zero shader-model probes unchanged instead of
  returning the SM 6.0 runtime cap.
- Direct Unity probes can start in a 1 x 1 fullscreen state under Wine. HDRP
  then emits `Thread group size must be above zero` during the render loop,
  matching the colored flicker seen after the shader-kernel blocker moved.
- The M12 swapchain presents frames through a CAMetalLayer at 1920 x 1080.
- The next observed blocker is Unity HDRP compute-kernel lookup:
  `Kernel 'KDepthDownsample8DualUav' not found`. The kernel string exists in
  `SonsOfTheForest_Data/globalgamemanagers.assets`, so this is tracked as a
  D3D12 capability/variant selection issue rather than absent game content.

Sons is still intentionally mapped to M11 in `configs/mtsp-rules.toml`. The
M12 diagnostic run proved the loader/device path, but forcing Unity into D3D12
on an M11-mapped title can select a D3D12/HDRP shader path that depends on
capability and shader-model reporting being internally consistent. MetalSharp
now only auto-adds `-force-d3d12` for Unity games whose configured route is M12;
M12 probes of M11 titles keep user launch arguments authoritative instead of
injecting the D3D12 flag.

The D3D12 patch set now also advertises typed UAV additional-format support so
HDRP compute-kernel selection sees the same capability contract that per-format
`CheckFeatureSupport` already exposes.

The shader-model default patch makes a zero/default
`D3D12_FEATURE_SHADER_MODEL` request return SM 6.0. That matches the in-tree
MetalSharp D3D12 implementation and keeps Unity's D3D12 shader variant
selection from seeing an accidental zero cap.

Unity MTSP recipes also inject a default 1920 x 1080 windowed launch size unless
the user already supplied `-screen-width`, `-screen-height`, or
`-screen-fullscreen`. This prevents Wine's initial 1 x 1 fullscreen report from
feeding zero-sized HDRP compute dispatches while keeping user launch arguments
authoritative.

## Completion State

| Area | State | Notes |
| --- | --- | --- |
| M12 app routing | Primary/stable | Current project maps D3D12 games to M12 before broad directory heuristics, uses M12 as the unresolved default, and invokes the backend launcher path. |
| M12 backend handoff | Present | The handoff copies DXMT DLLs, configures Wine/DYLD env, cache env, and launch args. |
| Subnautica-class M12 runtime | Demonstrated by local use | This validates the launcher/runtime path, not the native CMake D3D12 dylib. |
| Avery DXMT probes | Strongest external proof | `tests/ROADMAP.md` in `dxmt-src` marks probes 2-6 complete, including compute, triangle, indexed draw, depth, and texture sampling. |
| Deployed runtime parity | Needs cleanup | The rebuilt Avery `d3d12.dll` checksum does not match the deployed runtime DLL, while `winemetal.so` does match. |
| Avery source cleanliness | Needs cleanup | `dxmt-src` has dirty debug/probe changes and notes that prior dirty changes broke Steam launching. |
| Native in-tree D3D12 | Expanded coverage | Smoke, C entrypoint, MSL compute PSO dispatch, and MSL indexed draw tests pass. |
| Native compute PSO | Implemented for MSL/DXBC/DXIL paths | The in-tree native `CreateComputePipelineState` now creates a real Metal compute pipeline when shader bytecode is available. |
| Native indexed draw | Covered by offscreen test | GPU virtual-address lookup now binds real Metal vertex/index buffers and the test executes an indexed draw. |
| Native raytracing/mesh | Stubbed | Advanced D3D12 calls return success or placeholders without full Metal execution. |
| Native Cocoa viewer | Implemented separately | The NSWindow/CAMetalLayer path exists for native-loader presentation, but M12 Wine games present through DXMT/winemetal. |

## Stability Gaps To Close

1. Clean or commit the dirty Avery DXMT debug/probe changes before treating that
   branch as a stable source of truth.
2. Redeploy the rebuilt `d3d12.dll` only after confirming the dirty changes are
   intended; the current deployed runtime does not match the latest build.
3. Add a first-class M12 runtime verification command in this repo that launches
   a small D3D12 probe through the same `launch_dxmt_metal` environment used by
   games.
4. Add a native Cocoa viewer test target if the goal is to exercise the in-tree
   `metalsharp_d3d12` implementation through CAMetalLayer rather than through
   Wine/winemetal.
5. Expand native D3D12 tests beyond the current graphics/compute coverage:
   texture sampling, depth compare, and swapchain present.

## Practical Conclusion

The current MetalSharp project now treats M12 as the primary stable DXMT D3D
engine method. D3D12 PE import detection wins before broad directory heuristics,
unresolved games fall back to M12, the backend handoff deploys the DXMT D3D12
runtime, and the in-tree native D3D12 path has passing coverage for the C
entrypoint, command-list smoke behavior, MSL compute dispatch, and offscreen MSL
indexed draw.
