# M12 Subnautica 2 Phase 5 visual notes

## 2026-06-16 hash-gated visual retest

Run artifact root:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/20260616-022010
```

Runtime hash gate:

```text
d3d12.dll      2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
dxgi.dll       dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24
dxgi_dxmt.dll  659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
winemetal.dll  7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
winemetal.so   167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
```

Bounded launch summary:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/20260616-022010/bounded-summary.md
```

Visual capture summary:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/20260616-022010/visual-capture-summary.md
```

Result:

```text
launch_ok=true
drawn/present=16/18
dxil_msl_compile_failed=2
render_pso_failed=0
compute_pso_failed=0
vertex_descriptor_missing=0
unsafe_draw_skips=2
verdict=black_window_observed
```

Important: `drawn/present=16/18` is runtime progress only. It is not visual correctness.

## Screenshot evidence

The first desktop capture was terminal-obscured and is retained only as capture-process evidence:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/20260616-022010/subnautica2-screen-live.png
sha256=9532955194873d24635da15f71974dcf8488e41116477d358b63afda7f95f7d3
```

The raised Subnautica window capture shows the Wine window itself is black:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/20260616-022010/subnautica2-screen-window-raised.png
sha256=ef940dac48f28539e43ab1ab463e54fdd414968ad46769c842e13d745c167bef
```

This is Phase 5 visual incorrectness evidence: the game launches and presents, but the visible game window is black.

## New concrete failure targets

New MSL error artifact copied into the run root:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/20260616-022010/failure-artifacts/c117351693325b0b.msl.err.txt
```

Source cache artifact:

```text
~/.metalsharp/shader-cache/m12/1962700/c117351693325b0b.msl.err.txt
```

Primary error:

```text
program_source:307:10: error: incompatible integer to pointer conversion assigning to 'device char *'
from 'enable_if_t<_valid_fetch_add_sub_type<device unsigned int *>::value && is_convertible<unsigned int, unsigned int>::value, unsigned int>' (aka 'unsigned int')
  v114 = atomic_fetch_add_explicit(reinterpret_cast<device atomic_uint*>(buf0 + (static_cast<int>(12))), (uint)(44), memory_order_relaxed);
```

Interpretation:

```text
The generated MSL declares/infers the destination for atomic_fetch_add_explicit as device char * when the Metal atomic returns uint.
This should be fixed in DXIL->MSL lowering or type inference, then replayed offline before another game launch.
```

Additional runtime warnings:

```text
M12 skipping unsafe DrawIndexedInstanced reason=zero_stride_vertex_buffer pso=0x13408e510 vs=50c44b6e9131571c ps=098a6622d4cf36d5
M12 skipping unsafe DrawIndexedInstanced reason=zero_stride_vertex_buffer pso=0x13404f660 vs=50c44b6e9131571c ps=35fddc4027a79a02
```

Next repair order:

1. Fix/replay the `c117351693325b0b` atomic return typing issue offline.
2. Re-run a no-game/offline replay or targeted compiler probe first.
3. Only then perform another hash-gated Subnautica 2 visual retest.
4. If the black window persists after the atomic fix, investigate the two zero-stride unsafe draw skips as the next visual blocker.

## Offline atomic typing fix proof — 2026-06-16

Patch target:

```text
vendor/dxmt/src/airconv/dxil/msl_lowering.cpp
```

The dispatch predeclare pass now treats void DX ops as non-producing calls so the predeclared SSA value numbering stays aligned with call emission. This prevents the atomic return value for `c117351693325b0b` from being polluted by a later pointer use.

Proof artifact:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/atomic-fix-c117351693325b0b-20260616-023722/atomic-fix-summary.md
```

Result:

```text
ok=true
shader=c117351693325b0b
air_exists=true
atomic_predecl_uint=true
bad_atomic_pointer_assignment=false
metal_error_count=0
```

The fixed generated MSL predeclares the atomic result as `uint`:

```text
uint v114 = 0u; // dispatch pre-decl
v114 = atomic_fetch_add_explicit(...);
```

Validation performed without staging runtime DLLs:

```text
ninja -C vendor/dxmt/build-metalsharp-x64 src/airconv/darwin/airconv
ninja -C vendor/dxmt/build-metalsharp-x64 src/d3d12/d3d12.dll
xcrun -sdk macosx metal -std=macos-metal2.4 -c -x metal c117351693325b0b.fixed.msl
```

Next step remains hash-gated runtime staging and a Subnautica 2 visual retest only after deciding to stage this narrow fix.

## Narrow staged runtime retest of atomic fix — 2026-06-16

A rollback-safe runtime retest was performed with only the rebuilt `d3d12.dll` kept staged. The staging script initially copied the full build set, but `dxgi.dll`, `dxgi_dxmt.dll`, `winemetal.dll`, and `winemetal.so` were restored from backup before launch so the actual hash gate was:

```text
d3d12.dll      60aacfd070ff710450b980da326060d636978f913341bad784b89edb31a8c81f  # atomic fix build
dxgi.dll       dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24  # known-good
dxgi_dxmt.dll  659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d  # known-good
winemetal.dll  7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85  # known-good
winemetal.so   167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58  # known-good
```

Backup root:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/pre-subnautica-atomic-runtime-20260616-031955
```

Retest artifact:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/atomic-runtime-retest-20260616-032251/atomic-runtime-retest-summary.md
```

Retest result:

```text
launch_ok=true
drawn/present=19/20
dxil_msl_compile_failed=0
render_pso_failed=0
compute_pso_failed=0
unsafe_draw_skips=0
```

Interpretation:

```text
The atomic predecl typing fix successfully removed the Subnautica 2 MSL compile failures and the previously observed unsafe zero-stride draw skips in this short run.
The raised Subnautica window remained black, so visual correctness is still not fixed.
Present/drawn counts remain runtime context only, not visual correctness.
```

Raised-window screenshot:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/atomic-runtime-retest-20260616-032251/subnautica2-window-raised.png
sha256=8cdd47e88fa7252702a50318f306d1baf4474a8c894c79eccea9c513e81bdf3b
```

Rollback was performed immediately after the retest. Rollback verification artifact:

```text
tools/d3d12-metal-sdk/results/phase5-subnautica-visual/rollback-after-atomic-retest-20260616-032531/rollback-verify.md
```

Rollback result:

```text
ok=true
runtime/game-local d3d12.dll restored to 2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
runtime/game-local dxgi.dll restored to dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24
runtime/game-local dxgi_dxmt.dll restored to 659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
runtime/game-local winemetal.dll restored to 7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
runtime/wine winemetal.so restored to 167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
```

Next blocker:

```text
Subnautica 2 now has a clean short translation/runtime smoke under the atomic fix, but the window is still black.
Next investigation should target swapchain/backbuffer contents or presentation path evidence rather than the fixed atomic MSL compile failure.
```
