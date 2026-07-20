# OpenGL Bridge PE Shim & Per-Game Activation

## Overview

PR #307 delivered the MetalSharp OpenGL bridge (`opengl32.dylib`, 172 GL exports, SPIRV-Cross pipeline, Metal draw emitter). However the bridge is not yet **activatable** for Windows games running under Wine:

- The bridge is built as a Mach-O dylib (`app/native/opengl32.dylib`)
- Wine only loads Windows PE-format DLLs into game processes
- Setting `-force-glcore` on a Unity game currently routes through Wine's builtin `opengl32.dll` → libGL → macOS native OpenGL, **not** MetalSharp's bridge

This plan activates the bridge by following the proven `metalsharp_ntdll_hook.dll` pattern: cross-compile a PE-format `opengl32.dll` via mingw-w64, wrap with `winebuild --builtin`, deploy alongside dxmt shims, and per-game TOML `exe_args` for forced OpenGL rendering.

## Background — Verified Environment

- Wine 11.5 installed at `~/.metalsharp/runtime/wine/`, native x86_64 running under Rosetta on arm64 Mac
- Built with multi-arch support: `i386-windows/`, `x86_64-windows/`, `x86_64-unix/`
- Existing PE shim pattern: `runtime/wine/lib/dxmt/{x86_64-windows,i386-windows}/*.dll`
- Each dxmt PE DLL pairs with a Mach-O `.so` (e.g., `winemetal.dll` ↔ `winemetal.so`)
- PE `winemetal.dll` calls into Mach-O `winemetal.so` (Metal/CoreFoundation bridge)
- Tooling available:
  - `x86_64-w64-mingw32-gcc` 16.1.0 (Homebrew mingw-w64)
  - `i686-w64-mingw32-gcc` 16.1.0
  - `~/.metalsharp/runtime/wine/bin/winebuild` (Wine builtin wrapper)
  - `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/libopengl32.a` (Wine static stub library)

## Architecture

```
                    ┌─────────────────────────────────────────────┐
                    │  Unity game process (Windows PE under Wine) │
                    └─────────────────────────────────────────────┘
                                       │
                          glDrawArrays / wglSwapBuffers
                                       ▼
        ┌──────────────────────────────────────────────────────────┐
        │  runtime/wine/lib/dxmt_opengl/x86_64-windows/opengl32.dll│
        │  (PE32+ shim, cross-compiled via mingw-w64, --builtin)   │
        │  - Forwards GL calls to macOS dylib via dlsym           │
        │  - DllMain initializes MetalSharp backend IPC           │
        └──────────────────────────────────────────────────────────┘
                          │ dlsym("glXxx")
                          ▼
        ┌──────────────────────────────────────────────────────────┐
        │  runtime/wine/lib/dxmt_opengl/x86_64-unix/opengl32.so    │
        │  (= app/native/opengl32.dylib, Mach-O, already built)    │
        │  - GL state tracking, GLShaderTracker, GLErrorTracker    │
        │  - GLSL → SPIR-V (glslang) → MSL (SPIRV-Cross)          │
        │  - Metal draw emitter (GLMetalRenderer)                  │
        └──────────────────────────────────────────────────────────┘
```

## Phases

### Phase 1: PE shim source & build infrastructure

- [ ] 1a: Create `src/opengl-pe-shim/` directory with `src/opengl-pe-shim/src/opengl32_pe_shim.c` (forwarder stubs)
- [ ] 1b: Create `src/opengl-pe-shim/src/opengl32_pe_shim.h` (forwarder macro + DllMain declaration)
- [ ] 1c: Create `src/opengl-pe-shim/Makefile` modeled on `src/ntdll-hook/Makefile` (mingw-w64 + winebuild --builtin)
- [ ] 1d: Generator script `src/opengl-pe-shim/generate_pe_shim.py` extracts GL exports from `src/opengl/EntryPoint.cpp` and writes forwarder source
- [ ] 1e: Verify build produces `opengl32.dll` (x86_64) and `opengl32.dll` (i386)
- [ ] 1f: Verify `winebuild --builtin` produces valid Wine builtin wrapper

### Phase 2: Wire bridge into pipelines

- [ ] 2a: Add `opengl32=n,b` to all `dll_overrides` in `configs/mtsp-rules.toml` (alongside `d3d11`, `d3d12`, `dxgi`)
- [ ] 2b: Add `opengl32.dll` artifact entries to bottle artifact lists in `app/src-c/bottles.c`:
  - `m12_artifacts`: add `x86_64-windows/opengl32.dll`
  - `m11_artifacts`: add `x86_64-windows/opengl32.dll`
  - `m10_artifacts`: add `x86_64-windows/opengl32.dll`
  - `m11_i386_artifacts`: add `i386-windows/opengl32.dll`
  - `m10_i386_artifacts`: add `i386-windows/opengl32.dll`
- [ ] 2c: Add `dxmt_opengl` runtime lane constant to `app/src-c/bottles.h` (e.g., `METALSHARP_DXMT_OPENGL_SURFACE_ID`)
- [ ] 2d: Add OpenGL policy variant to `app/src-c/bottles.c` `policies[]` array with `dxmt_opengl` lane
- [ ] 2e: Update `app/src-c/runtime_surface.c` (or equivalent) to validate the dxmt_opengl runtime lane

### Phase 3: Bundle into runtime / graphics tarballs

- [ ] 3a: Copy `opengl32.dll` (x86_64) → `runtime/wine/lib/dxmt_opengl/x86_64-windows/opengl32.dll`
- [ ] 3b: Copy `opengl32.dll` (i386) → `runtime/wine/lib/dxmt_opengl/i386-windows/opengl32.dll`
- [ ] 3c: Copy macOS `opengl32.dylib` → `runtime/wine/lib/dxmt_opengl/x86_64-unix/opengl32.so`
- [ ] 3d: Update `tools/bundles/dxmt-surfaces.json` to include OpenGL surface (`dxmt_opengl`)
- [ ] 3e: Update `tools/bundles/verify-dxmt-surfaces.py` to validate OpenGL DLLs in bundle
- [ ] 3f: Update `tools/bundles/asset-manifest.tsv` if bundle size changes
- [ ] 3g: Update `tools/dmg/stage-release-bundles.sh` and `tools/dmg/create-bundles.sh` if needed
- [ ] 3h: Verify bundle integrity check (`verify-dxmt-surfaces.py`) passes

### Phase 4: TOML schema for per-game `exe_args`

- [ ] 4a: Update `tools/ci/validate-rules-toml.py` to accept optional `exe_args = [string, ...]` field under `[overrides.APPID]`
- [ ] 4b: Validate each `exe_args` entry is a non-empty string with no shell metacharacters
- [ ] 4c: Reject `exe_args` containing `;`, `&`, `|`, `$`, `\``, `"`, `'`, `\n`, `\r`
- [ ] 4d: Add test cases in `tools/ci/validate-rules-toml.py` self-test

### Phase 5: Cross-reference 32 OpenGL games from PCGamingWiki

- [ ] 5a: Read local PCGamingWiki OpenGL games list from Obsidian clipping
- [ ] 5b: Match against `configs/mtsp-rules.toml` (32 matches identified)
- [ ] 5c: Add `exe_args = ["-force-glcore"]` to 16 Unity games:
  - 107100 Bastion
  - 219740 Don't Starve
  - 226840 Age of Wonders III
  - 247080 Crypt of the NecroDancer
  - 251570 7 Days to Die
  - 255710 Cities: Skylines
  - 257350 Baldur's Gate II: Enhanced Edition
  - 262060 Darkest Dungeon
  - 270880 American Truck Simulator
  - 322330 Don't Starve Together
  - 774361 Blasphemous
  - 861540 Dicey Dungeons
  - 977880 Eastward
  - 1055540 A Short Hike
  - 1158310 Crusader Kings III
  - 2379780 Balatro (LÖVE)
- [ ] 5d: Add `exe_args = ["-force-glcore"]` to 1 Godot game:
  - 1637320 Dome Keeper
- [ ] 5e: Add `exe_args = ["-gl"]` to 2 Source engine games:
  - 240 Counter-Strike: Source
  - 300 Day of Defeat: Source
- [ ] 5f: Add `exe_args = ["-opengl4"]` to 4 Unreal Engine 3 games:
  - 8850 BioShock 2
  - 8870 BioShock Infinite
  - 35140 Batman: Arkham Asylum
  - 261640 Borderlands: Pre-Sequel
- [ ] 5g: Add `exe_args = ["-opengl"]` to 1 Unreal Engine 4 game:
  - 397540 Borderlands 3
- [ ] 5h: Add `exe_args = ["-opengl"]` to 1 Real Virtuality game:
  - 107410 Arma 3
- [ ] 5i: Add `exe_args = ["+set", "r_renderer", "opengl"]` to 2 id Tech games:
  - 208200 DOOM 3 (id Tech 4)
  - 1148590 DOOM 64 (id Tech 6)
- [ ] 5j: Add `exe_args = ["-force-glcore"]` to 2 Dawn Engine games:
  - 238010 Deus Ex: Human Revolution - Director's Cut
  - 337000 Deus Ex: Mankind Divided
- [ ] 5k: Document 2 deferred games (no documented OpenGL flag):
  - 504230 Celeste (FNA game; OpenGL via `FNA3D_DRIVER=OpenGL` env, handled by fna_x86 pipeline default)
  - 375520 Taimumari: Definitive Edition (Game Maker; no documented OpenGL flag)

### Phase 6: C backend validation hook

- [ ] 6a: Add `metalsharp_launcher_validate_exe_args(unsigned int appid, unsigned char pipeline_code, const char* const* args, size_t arg_count)` to `app/src-c/launcher.h`
- [ ] 6b: Implement in `app/src-c/launcher.c`:
  - Reject NULL pointer
  - Reject empty arg string
  - Reject shell metacharacters (`;`, `&`, `|`, `$`, `\``, `"`, `'`, `\n`, `\r`)
  - Reject path traversal (`..`, `/`, `\`)
  - Reject control characters
- [ ] 6c: Add policy test cases in `app/src-c/tests/policy_test.c`:
  - Valid args (e.g., `-force-glcore`, `+set`, `r_renderer`)
  - Rejection of shell injection (`-foo;rm -rf /`)
  - Rejection of path traversal (`../../../etc/passwd`)
  - Empty arg rejection
- [ ] 6d: Verify all C backend tests still pass

### Phase 7: Documentation & CI

- [ ] 7a: Update `docs/OPENGL-2-4-PLAN.md` with link to this plan
- [ ] 7b: Add `docs/OPENGL-BRIDGE-ACTIVATION.md` explaining per-game activation flow
- [ ] 7c: Update `docs/CONTROLLER-FIX-PLAN.md` if needed (cross-reference)
- [ ] 7d: Ensure all CI checks pass (C Backend CI, Metal CI, C/C++/Obj-C CI, CodeQL, Rules TOML CI, PR Readiness CI)
- [ ] 7e: Verify clang-format clean on all C/C++/Obj-C sources
- [ ] 7f: Verify all 31+ CTest suites pass

### Phase 8: Commit & verify

- [ ] 8a: Commit each phase separately with clear conventional commit messages
- [ ] 8b: Push branch `codex/opengl-pe-shim` to origin
- [ ] 8c: Open PR with PR Readiness checklist formatting
- [ ] 8d: Verify all CI checks green
- [ ] 8e: Real game validation deferred (Phase 5g of original plan, requires installation)

## Out of Scope

- **Real game smoke testing** (Phase 5g from PR #307) — requires installed MetalSharp on a target system
- **OpenGL 4.4+/4.5+/4.6 advanced features** — only 2.1 + 3.x shader translation tested so far
- **Cross-platform Linux/Windows** — Wine PE shim is macOS-only (Rosetta-specific path)
- **Compatibility profile** — fixed-function + shaders mixing deferred per original plan

## Risks

- **Symbol forwarding completeness**: 172 GL functions + WGL functions must be forwarded correctly
- **dlsym recursion**: similar to Phase 5 `glGetError` issue; PE shim must avoid calling back into itself
- **Wine builtin wrapping**: requires `winebuild` from installed Wine runtime
- **Bundle integrity**: changes to graphics bundle require SHA recalculation

## References

- PR #307: OpenGL 2-4 bridge (merged)
- `src/ntdll-hook/Makefile`: template for mingw-w64 PE shim build
- `~/.metalsharp/runtime/wine/lib/dxmt/`: existing dxmt PE shim pattern
- `app/native/opengl32.dylib`: existing macOS Mach-O bridge (no changes needed)
- `docs/OPENGL-2-4-PLAN.md`: original PR #307 plan
