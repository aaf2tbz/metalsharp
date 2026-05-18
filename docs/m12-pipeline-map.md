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
| Launcher handoff | `app/src-rust/src/mtsp/launcher.rs` | M12 routes through `launch_dxmt_metal`, copies DLLs into the game directory, sets Wine/DYLD/cache env, and adds `-dx12`. | Present in current project |
| Shader/cache routing | `app/src-rust/src/mtsp/shader_cache.rs` | M12 uses `m12` and `dxmt-metal12` cache directories. | Present in current project |
| DXMT D3D12 implementation | External DXMT source tree | Conformance branch contains the real DXMT D3D12/DXIL/winemetal work used by M12 runtime DLLs. | External source tree |
| Native D3D12 target | `include/metalsharp/D3D12Device.h`, `src/d3d/d3d12/*` | Builds `build/d3d12.dylib` and exposes `D3D12CreateDevice`. | In-tree, smoke-tested |
| Cocoa surface | `src/win32/user32/WindowManager.mm`, `src/dxgi/DXGISwapChain.mm` | Creates NSWindow/CAMetalLayer for the native loader path. | In-tree, not the Wine M12 surface |
| Wine M12 surface | DXMT `winemetal.so` plus Wine/macOS windowing | DXMT presents through Wine/winemetal, not through the native `WindowManager` path. | External runtime path |

## M12 Launch Flow

1. The PE scanner sees `d3d12.dll` and rules select `PipelineId::M12`.
2. The launcher resolves the game directory and Wine prefix.
3. M12 deploys DXMT PE DLLs into the game directory:
   `d3d12.dll`, `d3d11.dll`, `dxgi.dll`, `d3d10core.dll`, and `winemetal.dll`.
4. M12 sets `WINEDLLOVERRIDES` so Wine prefers the deployed native DXMT DLLs.
5. M12 adds DXMT/Wine unix library paths to `DYLD_FALLBACK_LIBRARY_PATH`.
6. M12 sets shader and pipeline cache paths under the MetalSharp cache root.
7. Wine launches the executable with the M12 launch args, including `-dx12`.
8. DXMT handles D3D12/DXGI calls, compiles DXIL/MSL work, sends commands through
   `winemetal`, and presents through the Wine/macOS surface.

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
