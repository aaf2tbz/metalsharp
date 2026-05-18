# M12 Pipeline Map

Last verified: 2026-05-17.

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
2. The launcher resolves the game directory and Wine prefix. By default the
   MetalSharp home is `~/.metalsharp`; setting `METALSHARP_HOME` moves the M12
   runtime root, Wine prefix, shader cache, and pipeline cache together.
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

The backend also exposes a runtime verification route that stages a D3D12 probe
into the active MetalSharp home, deploys the same M12 DLL set used by games, and
runs it with the same Wine/DYLD/DXMT/cache environment:

```sh
METALSHARP_HOME=/Volumes/AverySSD/metalsharp METALSHARP_PORT=9374 \
  app/src-rust/target/debug/metalsharp-backend

curl -sS -X POST http://127.0.0.1:9374/mtsp/verify-m12 \
  -H 'Content-Type: application/json' \
  -d '{"probePath":"/Volumes/AverySSD/metalsharp/dxmt-src/tests/probe6/probe6_texture.exe","timeoutMs":30000}'

curl -sS -X POST http://127.0.0.1:9374/mtsp/verify-m12-suite \
  -H 'Content-Type: application/json' \
  -d '{"timeoutMs":30000}'

curl -sS -X POST http://127.0.0.1:9374/mtsp/verify-m12-parity \
  -H 'Content-Type: application/json' \
  -d '{"dxmtRoot":"/Volumes/AverySSD/metalsharp/dxmt-src"}'

curl -sS -X POST http://127.0.0.1:9374/mtsp/m12-readiness \
  -H 'Content-Type: application/json' \
  -d '{"timeoutMs":30000,"dxmtRoot":"/Volumes/AverySSD/metalsharp/dxmt-src"}'

curl -sS -X POST http://127.0.0.1:9374/mtsp/deploy-m12-runtime \
  -H 'Content-Type: application/json' \
  -d '{"dxmtRoot":"/Volumes/AverySSD/metalsharp/dxmt-src"}'

curl -sS -X POST http://127.0.0.1:9374/mtsp/m12-title-readiness \
  -H 'Content-Type: application/json' \
  -d '{"dxmtRoot":"/Volumes/AverySSD/metalsharp/dxmt-src","appids":[1583230]}'

curl -sS -X POST http://127.0.0.1:9374/mtsp/m12-title-smoke \
  -H 'Content-Type: application/json' \
  -d '{"appid":1583230,"timeoutMs":15000}'
```

The suite enforces default performance gates of 15s per probe and 60s total.
Override them with `maxProbeMs` or `maxTotalMs` for diagnostic runs. The latest
suite, readiness, deploy, and title-readiness reports are persisted at:

```text
<METALSHARP_HOME>/probes/m12-runs/latest-suite.json
<METALSHARP_HOME>/probes/m12-runs/latest-readiness.json
<METALSHARP_HOME>/probes/m12-runs/latest-deploy.json
<METALSHARP_HOME>/probes/m12-runs/latest-title-readiness.json
```

`/mtsp/deploy-m12-runtime` is a dry run unless the request includes
`{"apply":true}`. It blocks dirty DXMT source trees unless the request also
includes `{"allowDirty":true}`; do not use that override unless the dirty Avery
DXMT changes have been reviewed and accepted for runtime deployment. Apply runs
copy deployable DXMT build artifacts into the active runtime and first back up
existing runtime artifacts under:

```text
<METALSHARP_HOME>/backups/m12-runtime/<timestamp>/
```

Latest local result: `probe6_texture.exe` exited `0` through M12 using
`/Volumes/AverySSD/metalsharp/runtime/wine` in 4.5s.

Latest local suite result: probes 2-6 all exited `0` through M12 using
`/Volumes/AverySSD/metalsharp/runtime/wine` in 15.1s total with the performance
gate passing: compute, triangle, indexed draw, depth, texture sampling, SM50
shader compile, DXMT DLL deployment, and external-drive runtime.

Latest local parity result: passed. The active runtime matches the current
`/Volumes/AverySSD/metalsharp/dxmt-src/build` outputs for `d3d12.dll`,
`d3d11.dll`, `dxgi.dll`, `winemetal.dll`, and `winemetal.so`. `d3d10core.dll` is
present in the runtime but has no comparable build artifact in that tree.

Latest local readiness result: ready. The M12 probe suite passed in 14.9s,
the performance gate passed, the Avery DXMT source tree was clean at `8f12ab1`,
and deployed runtime parity passed.

Latest local deploy apply result: deployed. The route copied `d3d12.dll`,
`d3d11.dll`, `dxgi.dll`, `winemetal.dll`, and `winemetal.so` into
`/Volumes/AverySSD/metalsharp/runtime/wine/lib/dxmt`, backing up the prior
runtime artifacts under
`/Volumes/AverySSD/metalsharp/backups/m12-runtime/1779040562/`.

Latest local title-readiness result: ready for installed appid `1583230` (High
On Life) from `/Volumes/AverySSD/SteamLibrary`. The launch doctor selected
`Oregon/Binaries/Win64/Oregon-Win64-Shipping.exe`, verified M12 DLL targets,
runtime assets, anti-cheat absence, and `-dx12` launch args.

Latest local title-smoke result: passed for installed appid `1583230` (High On
Life). `/mtsp/m12-title-smoke` launched the external-drive shipping executable
through M12, kept it alive until the 15s smoke timeout, then killed the launched
Wine process tree. The result status was `launched_timeout_killed` with
`ok:true`.

Latest Subnautica Below Zero result: passed for installed appid `848450`.
Below Zero was installed in the legacy Wine Steam library under
`~/.metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/steamapps/common/SubnauticaZero`
while the active runtime home was `/Volumes/AverySSD/metalsharp`. The scanner
now includes that legacy Wine Steam library as a fallback. The forced M12 title
readiness endpoint selected `SubnauticaZero.exe`, verified `-dx12`, and reported
`ok:true` with `resolved_pipeline=m11` retained as auto-rule context.
`/mtsp/m12-title-smoke` deployed the current AverySSD M12 DLLs beside the game
executable and kept the title alive until the 15s smoke timeout before killing
the process tree with `status=launched_timeout_killed`.

Current Avery DXMT accepted-change audit:

| Path | Classification | Deployment note |
| --- | --- | --- |
| `include/native/directx/d3d12.h` | API surface change | Adds `ID3D12Resource1/2` GUIDs and `ID3D12Device2`/pipeline-state stream declarations; committed locally in the nested DirectX headers worktree at `53ce4be`. |
| `include/native/directx/dxgi1_6.h` | API surface correction | Corrects `IDXGIFactory6` GUID bytes; committed locally in the nested DirectX headers worktree at `53ce4be`. |
| `src/d3d11/d3d11_swapchain.cpp` | Behavior change | Downgrades cross-process swapchain failure to a warning for CEF/ANGLE GPU-process cases; committed locally in DXMT at `8f12ab1`. |
| `src/d3d12/d3d12_pipeline_state.cpp` | Diagnostics | Adds library/function trace context and makes extra MSL dumps opt-in via `DXMT_DUMP_MSL`; committed locally in DXMT at `8f12ab1`. |
| `src/winemetal/unix/winemetal_unix.c` | Diagnostics | Adds null-library diagnostics and makes `/tmp/winemetal_debug.log` opt-in via `DXMT_WINEMETAL_DEBUG`; committed locally in DXMT at `8f12ab1`. |
| `src/winemetal/winemetal_thunks.c` | Diagnostics | Adds PE-side WineMetal diagnostics behind `DXMT_WINEMETAL_DEBUG` while preserving debug-build unix-call assertions; committed locally in DXMT at `8f12ab1`. |
| `.graphiq/` | Local generated state | Excluded through DXMT `.git/info/exclude`; not committed and no longer affects source-cleanliness checks. |

The dirty DXMT tree still builds the M12 runtime artifacts with:

```sh
ninja -C /Volumes/AverySSD/metalsharp/dxmt-src/build \
  src/d3d12/d3d12.dll \
  src/d3d11/d3d11.dll \
  src/dxgi/dxgi.dll \
  src/winemetal/winemetal.dll \
  src/winemetal/unix/winemetal.so
```

## Completion State

| Area | State | Notes |
| --- | --- | --- |
| M12 app routing | Primary/stable | Current project maps D3D12 games to M12 before broad directory heuristics, uses M12 as the unresolved default, and invokes the backend launcher path. |
| M12 backend handoff | Present | The handoff copies DXMT DLLs, configures Wine/DYLD env, cache env, and launch args. |
| Subnautica-class M12 runtime | Demonstrated by local use | This validates the launcher/runtime path, not the native CMake D3D12 dylib. |
| Avery DXMT probes | Strongest external proof | `tests/ROADMAP.md` in `dxmt-src` marks probes 2-6 complete, including compute, triangle, indexed draw, depth, and texture sampling. |
| M12 readiness gate | Ready | `/mtsp/m12-readiness` passes source cleanliness, probe suite, performance gate, and deployed runtime parity. |
| M12 runtime deployment | Applied | `/mtsp/deploy-m12-runtime` deployed current DXMT build artifacts with runtime backups. |
| M12 AAA title readiness | Ready for installed M12 titles | `/mtsp/m12-title-readiness` forces the M12 pipeline for requested title checks, while reporting the auto-resolved pipeline for context; High On Life and Subnautica Below Zero both pass locally. |
| M12 AAA title smoke | Passed | `/mtsp/m12-title-smoke` launched High On Life and Subnautica Below Zero through M12 for bounded 15s smokes and killed the process trees at timeout. |
| Legacy Wine Steam discovery | Fixed | Active AverySSD runtime discovery now also checks the legacy `~/.metalsharp/prefix-steam` Steam library, which is where Below Zero is installed locally. |
| Deployed runtime parity | Passed | The parity route shows all comparable M12 artifacts match the current Avery build outputs. |
| Avery source cleanliness | Clean locally | `dxmt-src` is clean and ahead one local commit; nested DirectX headers are clean on local branch `metalsharp-d3d12-compat`. |
| Native in-tree D3D12 | Expanded coverage | Smoke, C entrypoint, MSL compute PSO dispatch, and MSL indexed draw tests pass. |
| Native compute PSO | Implemented for MSL/DXBC/DXIL paths | The in-tree native `CreateComputePipelineState` now creates a real Metal compute pipeline when shader bytecode is available. |
| Native indexed draw | Covered by offscreen test | GPU virtual-address lookup now binds real Metal vertex/index buffers and the test executes an indexed draw. |
| Native raytracing/mesh | Stubbed | Advanced D3D12 calls return success or placeholders without full Metal execution. |
| Native Cocoa viewer | Implemented separately | The NSWindow/CAMetalLayer path exists for native-loader presentation, but M12 Wine games present through DXMT/winemetal. |

## Stability Gaps To Close

1. Publish or otherwise preserve the local DXMT commit `8f12ab1` and nested
   DirectX header commit `53ce4be` before relying on this state outside this
   machine.
2. Add a native Cocoa viewer test target if the goal is to exercise the in-tree
   `metalsharp_d3d12` implementation through CAMetalLayer rather than through
   Wine/winemetal.
3. Expand native D3D12 tests beyond the current graphics/compute coverage:
   texture sampling, depth compare, and swapchain present.

## Practical Conclusion

The current MetalSharp project now treats M12 as the primary stable DXMT D3D
engine method. D3D12 PE import detection wins before broad directory heuristics,
unresolved games fall back to M12, the backend handoff deploys the DXMT D3D12
runtime, and the in-tree native D3D12 path has passing coverage for the C
entrypoint, command-list smoke behavior, MSL compute dispatch, and offscreen MSL
indexed draw.
