# m12_game.exe

`m12_game.exe` is a DX12-only Wine/Metal stress harness for M12. It is meant to
exercise backend surfaces without launching a real game.

The default harness case is a 10-second 3D RGB cube scene with a depth buffer,
ground plane, projected shadow, dynamic vertex updates, indexed draws, and
readback validation. It is intended to stress the render path without requiring
a real game launch.

The harness currently covers:

- device, queue, swapchain, RTV, fence, and present creation
- clear-only present
- sparse vertex buffers using slots 0 and 3
- `DrawInstanced` and `DrawIndexedInstanced`
- SRV/sampler descriptor tables and texture sampling
- dynamic 3D indexed cube rendering with depth, lighting, and shadows
- copy/readback validation using colored-pixel counts and a checksum

Build it from an enabled test build:

```sh
meson setup vendor/dxmt/build-metalsharp-x64-tests vendor/dxmt \
  --cross-file vendor/dxmt/build-win64.txt \
  -Denable_tests=true \
  -Dnative_llvm_path=/Volumes/AverySSD/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0 \
  -Dwine_install_path=$HOME/.metalsharp/runtime/wine
meson compile -C vendor/dxmt/build-metalsharp-x64-tests m12_game
```

Run it through the local staging script:

```sh
vendor/dxmt/tests/d3d12_game/run_m12_game.sh
```

Useful variants:

```sh
# Default 10-second RGB cube stress case.
vendor/dxmt/tests/d3d12_game/run_m12_game.sh

# Full focused smoke suite before a shorter cube run.
vendor/dxmt/tests/d3d12_game/run_m12_game.sh --quick-checks --seconds 1
```

The script stages the PR-built `m12_game.exe`, `d3d12.dll`, `dxgi.dll`,
`dxgi_dxmt.dll`, the deployed DXMT `winemetal.dll`, `d3dcompiler_47.dll`, and a
local Unix-side WineMetal dependency folder under
`$HOME/.metalsharp/tmp/m12_game_run`. For the standalone live harness it also
copies `winemetal.so`, `winemac.so`, and `ntdll.so` beside the executable and
uses `DXMT_WINEMETAL_UNIXLIB=winemetal.so`, matching the native-override shape
that made the cube path run independently of a game install. It writes:

- `$HOME/.metalsharp/tmp/m12_game_run/m12_game.log`
- `/tmp/winemetal_pe_debug.log` when `DXMT_WINEMETAL_DEBUG=1`

Repository PR CI runs this harness through `tools/ci/m12-check.sh` as the
`M12 Check` job. That CI path downloads the released Wine/DXMT runtime inputs,
rebuilds the PR DXMT artifacts with tests enabled, stages the corrected M12
WineMetal layout, builds `m12_game.exe`, and verifies the staged runtime
contract. Set `M12_CHECK_RUN_LIVE=1` to run the quick checks plus the cube scene
locally.

Current observed PR state: draw-bearing command lists are recorded, encoded,
submitted, presented, and read back successfully. The focused smoke suite
passes clear, sparse vertex/index draws, SRV/sampler texture sampling, and the
RGB cube stress case. The final log should end with `=== m12_game.exe PASS ===`
and nonzero `bright`/`chroma` counts.
