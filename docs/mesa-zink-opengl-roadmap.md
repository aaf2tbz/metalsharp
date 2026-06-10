# Mesa Zink OpenGL Support for MetalSharp
## Hardened Technical Plan — Local-First Build

**Date**: 2026-06-09
**Branch**: `feat/mesa-zink-opengl` (off `main` @ `d9c7e39`, v0.44.1)
**Status**: Phase 1 validated — Zink produces GL 3.3 via MoltenVK on Apple M4
**Target**: MetalSharp v0.45.x
**Goal**: OpenGL 3.2+ for Windows games under Wine on macOS via Mesa Zink (GL → Vulkan → MoltenVK → Metal)

---

## 0. Local Build Toolchain — Ready

All tools are present on this machine (AverySSD, arm64 macOS 26, Docker 29.4.3):

| Tool | Status | Version |
|------|--------|---------|
| `x86_64-w64-mingw32-gcc` | Installed | 15.2.0 |
| `meson` | Installed | 1.11.1 |
| `ninja` | Installed | 1.10.0 |
| `python3` | Installed | 3.14.4 |
| `flex` | Installed | 2.6.4 |
| `pkg-config` | Installed | 2.5.1 |
| `zstd` | Installed | 1.5.7 |
| `rustc`/`cargo` | Installed | 1.96.0 |
| `git` | Installed | — |
| Disk space | 456 GB free | — |

**Action items before first build:**
```bash
pip3 install mako                    # Python Mako templating (Mesa codegen)
brew install bison && brew link bison --force   # System bison 2.3 too old; need 3.2+
```

**Staged scripts (no CI needed):**
- `tools/zink/build-mesa-zink.sh` — Fetch, configure, build, package
- `tools/zink/install-zink-local.sh` — Install DLLs into `~/.metalsharp/runtime/wine/lib/zink/`
- `tools/zink/verify-zink-readiness.sh` — Check Zink + MoltenVK + Vulkan readiness
- `tools/zink/cross-x86_64-w64-mingw32.ini` — Meson cross-compilation file

**Quick start:**
```bash
pip3 install mako && brew install bison && brew link bison --force
cd repo
./tools/zink/build-mesa-zink.sh check     # verify toolchain
./tools/zink/build-mesa-zink.sh build     # full build (~10-15 min)
./tools/zink/install-zink-local.sh         # install into runtime
./tools/zink/verify-zink-readiness.sh      # check everything
```

---

## 1. Corrections to Original Plan

### 1.1 Pipeline Architecture — Don't Extend M9

**Original**: Extend M9 pipeline node with Zink DLLs.
**Correction**: Create a **new `PipelineId::M9Zink`** variant, OR add Zink as a **conditional overlay** that any DXMT-family pipeline can use. Here's why:

- M9's `wine_overrides` is `"d3d9=n,b;..."` — it does NOT override `opengl32`. Adding `opengl32=n` to M9 would load Mesa's opengl32 for ALL M9 games, including D3D9-only games that never touch GL. Mesa's opengl32.dll will try to initialize Zink/Vulkan at load, potentially causing issues for games that don't need it.
- The `DllDeploy` struct (`engine.rs:22-26`) has no `only_if_imports` field. Adding one requires changing the struct and all deployment logic in `launcher.rs:522-581` (`deploy_recipe_dlls`).
- **Better approach**: Use PE import detection (already exists in `pe.rs:29` `parse_pe_imports`) at pipeline selection time in `rules.rs`, and add Zink DLLs only when `opengl32.dll` is in the import table.

### 1.2 `DllDeploy` Struct — No `only_if_imports` Field

The original plan proposed adding `only_if_imports` to `DllDeploy`. This is a struct change that affects:
- `engine.rs:22-26` (struct definition)
- Every pipeline node that constructs `DllDeploy` instances
- `launcher.rs:522-581` (`deploy_recipe_dlls`) — would need PE parsing at deploy time
- `launcher.rs:584-622` (`deploy_prefix_route_dlls`) — same

**Simpler alternative**: Deploy Zink DLLs from the recipe phase (`recipe.rs`) based on PE analysis, not from the static pipeline node. The recipe builder already calls `parse_pe_imports` at `recipe.rs:584`.

### 1.3 `VK_ICD_FILENAMES` — Already Handled

The original plan said to add `VK_ICD_FILENAMES` env var to the pipeline node. **This is unnecessary.** The launcher's `cache_env_pairs()` (`launcher.rs:1859-1901`) already sets `VK_ICD_FILENAMES` for the `"dxvk"` backend case. Zink uses Vulkan the same way DXVK does — it calls `vulkan-1.dll` which thunks to the host loader.

However, the current code only sets `VK_ICD_FILENAMES` in the `"dxvk"` backend match (`launcher.rs:1879-1886`). Since Zink pipelines won't be `"dxvk"` backend, we need to **also set `VK_ICD_FILENAMES` for the Zink case**. This means adding a `"zink"` match arm or handling it in the pipeline's `env_vars` static list.

**Simplest fix**: Add `VK_ICD_FILENAMES` to the Zink pipeline node's `env_vars` list in `engine.rs` (with value resolved at launch time). The existing `cache_env_pairs` already resolves the ICD path dynamically.

### 1.4 Bundle Integration — Wrong Bundle

**Original**: Add Zink DLLs to `metalsharp-assets.tar.zst`.
**Correction**: Zink PE DLLs should go into `metalsharp-graphics-dll.tar.zst` alongside the DXMT PE DLLs. The graphics bundle is the natural home for translation layer DLLs. Its current structure:

```
Graphics/dll/
  dxmt/
    x86_64-unix/winemetal.so
    x86_64-windows/d3d10core.dll, d3d11.dll, d3d12.dll, dxgi.dll, ...
```

Zink additions:
```
Graphics/dll/
  zink/
    x86_64-windows/opengl32.dll
    x86_64-windows/libgallium_wgl.dll
```

The `ASSETS_REQUIRED_ARCHIVE_FILES` constant (`installer.rs:63-89`) should NOT include Zink — Zink belongs in `GRAPHICS_REQUIRED_ARCHIVE_FILES` (`installer.rs:52-62`).

### 1.5 Runtime Layout — Correct Path

**Original**: `~/.metalsharp/runtime/wine/lib/zink/`
**Correction**: This IS correct and follows the existing pattern. DXMT is at `lib/dxmt/`, GPTK at `lib/gptk/`, MetalSharp hooks at `lib/metalsharp/`. Zink at `lib/zink/` is consistent.

### 1.6 `WINEDLLPATH` — Critical for Zink

The original plan didn't mention `WINEDLLPATH`. Wine's `opengl32=n` override searches for native DLLs in `WINEDLLPATH` directories. Each pipeline node has a `winedllpath_dirs` field (`engine.rs:45`). The Zink pipeline must include `"lib/zink/x86_64-windows"` in `winedllpath_dirs` so Wine can find `libgallium_wgl.dll` (which `opengl32.dll` loads dynamically).

### 1.7 `dyld_paths` — Zink Has No Unix Component

Zink PE DLLs are pure Windows binaries — they call Vulkan through Wine's `vulkan-1.dll` builtin, which thunks to the host. There is no `.so` or `.dylib` component. The `dyld_paths` field does NOT need a Zink entry. However, it does need the existing `"lib/wine/x86_64-unix"` entry so the host Vulkan loader (used by MoltenVK) is on `DYLD_LIBRARY_PATH`.

### 1.8 Shader Cache — Needs New Backend Match

The `cache_env_pairs()` function (`launcher.rs:1859-1901`) matches on `node.backend` to set shader cache env vars. There's no `"zink"` match arm. Zink needs:
- `MESA_SHADER_CACHE_DIR` — Zink's own shader cache (GLSL→NIR→SPIR-V)
- `VK_ICD_FILENAMES` — Vulkan ICD selection
- The existing `METALSHARP_SHADER_CACHE_PATH` for MetalSharp's tracking

### 1.9 `pe.rs` Already Has Import Detection

`pe.rs:233-250` (`detect_d3d_api`) detects D3D API version from PE imports but does NOT detect OpenGL. We need to add `opengl32.dll` detection there, OR better: check for `opengl32.dll` in the import list directly in `recipe.rs` where pipeline selection happens.

### 1.10 Tests — Extensive Existing Coverage

`engine.rs:563-756` has 14 unit tests covering pipeline definitions. Any new pipeline variant MUST have matching tests:
- `PipelineId` serialization
- User-selectable pipelines list
- Legacy method parsing
- DLL deployment assertions

---

## 2. Implementation Plan — Revised

### Phase 1: Build Zink PE DLLs Locally ✅ DONE

**Tooling**: `tools/zink/build-mesa-zink.sh`
**Mesa version**: 25.3.6 (not 24.3.6 — latest stable, better Zink support)
**Built for**: x86_64 and i686 (both arches needed for 32-bit games)
**Build time**: ~15 min total

Validated output:
```
GL_VENDOR: Mesa
GL_RENDERER: zink Vulkan 1.3 (Apple M4 (MOLTENVK))
GL_VERSION: 3.3 (Compatibility Profile) Mesa 25.3.6
GL_SHADING_LANGUAGE_VERSION: 4.60
```

DLLs produced per arch:
- `opengl32.dll` (~340K i686, ~540K x86_64)
- `libgallium_wgl.dll` (~35M i686, ~41M x86_64)
- `libwinpthread-1.dll` (~328K) — MinGW runtime, required dependency

**Critical build notes:**
1. **nullDescriptor patch required**: Mesa 25.3.6 hard-requires `robustness2.nullDescriptor`. Wine 11.5's `winevulkan` thunk doesn't forward this feature bit even though MoltenVK 1.4.1 supports it. Patch applied: `zink_screen.c:3456` — overrides to `VK_TRUE` with a warning.
2. **libwinpthread-1.dll**: `libgallium_wgl.dll` links against `libwinpthread-1.dll`. Must be shipped alongside Zink DLLs. Copied from MinGW sysroot.
3. **zlib cross-compiled**: MinGW sysroots don't include zlib. Built manually for both arches.
4. **spirv-tools disabled**: `-Dspirv-tools=disabled` because host SPIRV-Tools dylib can't link for MinGW. Mesa builds fine without it.
5. **Vulkan 1.3 cap required**: Must set `MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592` (Vulkan 1.3). Vulkan 1.4 triggers assertion in Wine's `winevulkan/loader_thunks.c:6053` (`VkPhysicalDeviceLayeredApiPropertiesKHR` not thunked). Vulkan 1.3 avoids this.
6. **Both arches required**: 32-bit games (like Dead Cells) are PE32 i386 — need the i686 build.

Output: `app/bundles/zink-staging/` containing:
```
zink/
  i386-windows/
    opengl32.dll
    libgallium_wgl.dll
    libwinpthread-1.dll
  x86_64-windows/
    opengl32.dll
    libgallium_wgl.dll
    libwinpthread-1.dll
```

---

### Phase 2: Integrate into Graphics Bundle

**Files to modify:**

| File | Change |
|------|--------|
| `tools/bundles/create-split-bundles.py` | Add Zink DLLs from `zink-staging/` to `metalsharp-graphics-dll.tar.zst` under `Graphics/dll/zink/` |
| `tools/bundles/verify-bundles.sh` | Add `Graphics/dll/zink/x86_64-windows/opengl32.dll` to `verify_graphics_core()` |
| `app/src-rust/src/installer.rs` | Add Zink extraction in `install_dxmt_runtime()` or new `install_zink_runtime()` step |
| `app/src-rust/src/installer.rs` | Add `GRAPHICS_REQUIRED_ARCHIVE_FILES` entries for Zink |

**Runtime layout after install:**
```
~/.metalsharp/runtime/wine/lib/
  zink/
    x86_64-windows/
      opengl32.dll
      libgallium_wgl.dll
```

**Install step** (in `installer.rs`): Add as step 10.5 (after DXMT, before GPTK) or extract alongside DXMT since they share the graphics bundle.

---

### Phase 3: Pipeline Integration

**Option chosen**: Add Zink as a **conditional overlay** to M9/M10/M11/M12 pipelines, triggered by PE import detection of `opengl32.dll`.

**Files to modify:**

| File | Change |
|------|--------|
| `app/src-rust/src/mtsp/engine.rs` | Add `PipelineId::M9Zink` or add Zink DLLs to existing M9 with `only_if_gl: true` flag |
| `app/src-rust/src/mtsp/launcher.rs` | In `cache_env_pairs()`, add `"zink"` backend match arm |
| `app/src-rust/src/mtsp/launcher.rs` | In `deploy_recipe_dlls()`, add conditional Zink DLL deployment |
| `app/src-rust/src/mtsp/recipe.rs` | Detect `opengl32.dll` in PE imports, add Zink DLLs to deploy plan |
| `app/src-rust/src/mtsp/pe.rs` | Add `D3dApi::OpenGL` variant, detect `opengl32.dll` imports |
| `app/src-rust/src/mtsp/rules.rs` | Add Zink routing rules for known OpenGL games |

**Key engine.rs changes:**

New pipeline node (recommended over modifying M9):
```rust
PipelineNode {
    id: PipelineId::M9Zink,
    name: "M9+Zink",
    description: "D3D9 + OpenGL 4.6 (Zink) -> Metal via DXMT",
    backend: "dxmt",
    graphics_backend: "dxmt",
    experimental: true,
    requires_wine: true,
    wine_overrides: Some(
        "d3d9=n,b;opengl32=n;gameoverlayrenderer,gameoverlayrenderer64=d",
    ),
    dyld_paths: vec!["lib/wine/x86_64-unix", "lib/dxmt/x86_64-unix"],
    winedllpath_dirs: vec![
        "lib/wine/x86_64-windows",
        "lib/dxmt/x86_64-windows",
        "lib/zink/x86_64-windows",
        "lib/metalsharp/x86_64-windows",
    ],
    deploy_dlls: vec![
        // Same as M9 D3D9 DLLs:
        DllDeploy { source_subpath: "lib/wine/x86_64-windows", filename: "d3d9.dll", dest_filename: None },
        DllDeploy { source_subpath: "lib/wine/i386-windows", filename: "d3d9.dll", dest_filename: None },
        DllDeploy { source_subpath: "lib/dxmt/x86_64-windows", filename: "nvapi64.dll", dest_filename: None },
        DllDeploy { source_subpath: "lib/metalsharp/x86_64-windows", filename: "metalsharp_ntdll_hook.dll", dest_filename: None },
        // Zink OpenGL DLLs:
        DllDeploy { source_subpath: "lib/zink/x86_64-windows", filename: "opengl32.dll", dest_filename: None },
        DllDeploy { source_subpath: "lib/zink/x86_64-windows", filename: "libgallium_wgl.dll", dest_filename: None },
    ],
    env_vars: vec![
        EnvVar { key: "DXMT_METALFX_SPATIAL_SWAPCHAIN", value: "1" },
        EnvVar { key: "DXMT_ASYNC_PIPELINE_COMPILE", value: "1" },
        EnvVar { key: "DXMT_CONFIG", value: DXMT_70_PERCENT_UPSCALE_CONFIG },
        EnvVar { key: "MESA_LOADER_DRIVER_OVERRIDE", value: "zink" },
        EnvVar { key: "MESA_GL_VERSION_OVERRIDE", value: "4.6COMPAT" },
    ],
    launch_args: vec![],
    alternatives: vec![PipelineId::M9, PipelineId::M11],
    shader_cache_subdir: Some("m9-zink"),
}
```

**Key launcher.rs changes — `cache_env_pairs()` addition:**
```rust
"zink" => {
    env.push(("DXMT_SHADER_CACHE_PATH".to_string(), shader_dir.clone()));
    env.push(("DXMT_PIPELINE_CACHE_PATH".to_string(), pipeline_dir.clone()));
    env.push(("DXMT_LOG_PATH".to_string(), pipeline_dir));
    env.push(("MESA_SHADER_CACHE_DIR".to_string(), shader_dir));
    let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
    if moltenvk_icd.exists() {
        env.push(("VK_ICD_FILENAMES".to_string(), moltenvk_icd.to_string_lossy().to_string()));
    }
},
```

**Key pe.rs changes — OpenGL detection:**
```rust
pub enum D3dApi {
    D3D9,
    D3D10,
    D3D11,
    D3D12,
    OpenGL,
    Unknown,
}

// In detect_d3d_api(), add before Unknown return:
if lower.iter().any(|d| d == "opengl32.dll") {
    return D3dApi::OpenGL;
}
```

---

### Phase 4: Frontend

- Add M9Zink to user-selectable pipelines (experimental)
- Show "OpenGL 4.6 (Zink)" badge on GameCard for OpenGL games
- Runtime doctor: check Zink DLLs + MoltenVK ICD

### Phase 5: Testing

- Unit tests for new PipelineId variant
- `verify-bundles.sh` passes with Zink entries
- `cargo fmt`, `cargo clippy`, `cargo test` all pass
- Manual test: launch a Source Engine game with `WINEDLLOVERRIDES="opengl32=n"` and verify GL version

---

## 3. Key Architectural Notes

### Zink + MoltenVK Vulkan Path

```
Game EXE (x86_64 PE)
  → opengl32.dll (Mesa Zink, PE DLL built by MinGW)
    → Calls Vulkan API via Wine's builtin vulkan-1.dll
      → Wine thunks vulkan-1.dll to host libvulkan.dylib
        → Host loader reads VK_ICD_FILENAMES
          → MoltenVK ICD (~/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json)
            → libMoltenVK.dylib (already in Wine runtime at lib/wine/x86_64-unix/)
              → Metal framework
                → Apple GPU
```

### Why This Works Without Rosetta for the Host

The MinGW cross-compiler runs natively on ARM64 macOS but **produces x86_64 PE DLLs**. The resulting `opengl32.dll` and `libgallium_wgl.dll` are x86_64 Windows DLLs that run under Rosetta 2 inside the Wine process (which is itself x86_64 under Rosetta 2). This is the same as how DXMT's PE DLLs work.

### Environment Variables Reference

| Variable | Value | Purpose |
|----------|-------|---------|
| `WINEDLLOVERRIDES` | `opengl32=n` | Use Mesa's opengl32.dll, not Wine's builtin |
| `VK_ICD_FILENAMES` | Path to MoltenVK ICD JSON | Select MoltenVK as Vulkan driver |
| `MVK_CONFIG_API_VERSION_TO_ADVERTISE` | `4206592` | Cap Vulkan at 1.3 (avoids Wine thunk assertion with Vulkan 1.4) |
| `MESA_LOADER_DRIVER_OVERRIDE` | `zink` | Force Mesa to use Zink Gallium driver |
| `MESA_GL_VERSION_OVERRIDE` | `3.3COMPAT` | Advertise GL 3.3 compatibility profile (safe for MoltenVK) |
| `MESA_SHADER_CACHE_DIR` | Shader cache path | Persistent Zink shader cache |
| `LIBGL_KOPPER_DISABLE` | `true` | Disable Kopper WSI if swapchain issues |
| `MESA_GLSL_VERSION_OVERRIDE` | `460` | Force GLSL version (if needed) |

### Fallback Strategy

If `MESA_GL_VERSION_OVERRIDE=4.6COMPAT` fails (MoltenVK missing extensions):
- Try `4.3COMPAT` (VK_KHR_dynamic_rendering threshold)
- Try `3.3COMPAT` (conservative minimum for Source Engine)
- Try `4.1COMPAT` (matches Apple's native GL on ARM64)

---

## 4. Files Changed Summary

| Phase | File | Change |
|-------|------|--------|
| 1 | `tools/zink/build-mesa-zink.sh` | **New** — Local build script |
| 1 | `tools/zink/cross-x86_64-w64-mingw32.ini` | **New** — Meson cross-file |
| 1 | `tools/zink/install-zink-local.sh` | **New** — Local install script |
| 1 | `tools/zink/verify-zink-readiness.sh` | **New** — Readiness checker |
| 2 | `tools/bundles/verify-bundles.sh` | Add Zink DLLs to `verify_graphics_core()` |
| 2 | `app/src-rust/src/installer.rs` | Add Zink extraction from graphics bundle |
| 3 | `app/src-rust/src/mtsp/engine.rs` | Add `PipelineId::M9Zink`, new pipeline node |
| 3 | `app/src-rust/src/mtsp/launcher.rs` | Add `"zink"` backend in `cache_env_pairs()` |
| 3 | `app/src-rust/src/mtsp/recipe.rs` | Detect `opengl32.dll` imports, route to Zink pipeline |
| 3 | `app/src-rust/src/mtsp/pe.rs` | Add `D3dApi::OpenGL` variant |
| 3 | `app/src-rust/src/mtsp/rules.rs` | Add Zink routing rules for known GL games |
| 5 | `THIRD_PARTY_LICENSES` | Add Mesa MIT license |

---

## 5. Risk Assessment — Revised

| Risk | Severity | Status | Mitigation |
|------|----------|--------|------------|
| Wine winevulkan thunk assertion with Vulkan 1.4 | **High** | **Hit** | Cap MoltenVK to Vulkan 1.3 via `MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592` |
| Zink nullDescriptor hard-requirement vs Wine thunk | **High** | **Hit** | Patched `zink_screen.c` to override to `VK_TRUE` (MoltenVK supports it, Wine doesn't forward it) |
| libwinpthread-1.dll missing from Zink dir | **High** | **Hit** | Ship `libwinpthread-1.dll` alongside Zink DLLs (copied from MinGW sysroot) |
| MoltenVK missing Vulkan extensions for GL 4.6 | **Medium** | **Partial** | GL 3.3 confirmed; GL 4.6 untested. Fallback to `3.3COMPAT` |
| Zink crashes under Rosetta 2 (x86→ARM64) | **Medium** | **Not hit** | GL context creation works; full game test pending |
| libgallium_wgl.dll not found by opengl32.dll | **Medium** | **Hit** | Both DLLs + libwinpthread must be in same dir OR on WINEDLLPATH |
| Kopper (WSI) incompatibility with Wine | **Low** | **Not hit** | Swapchain created successfully in test |
| Deploying Zink DLLs for D3D-only games | **Low** | **Avoided** | New pipeline variant (M9Zink) instead of modifying M9 |
| SPIR-V validation errors in shader chain | **Low** | **Not hit** | `ZINK_DEBUG=validation` during development |

---

## 7. Wine/MoltenVK Compatibility Matrix (Validated)

| Config | Result |
|--------|--------|
| Vulkan 1.4 + no cap | Assertion: `vkGetPhysicalDeviceProperties2` in `loader_thunks.c:6053` (VkPhysicalDeviceLayeredApiPropertiesKHR) |
| Vulkan 1.3 + cap (`4206592`) | Works — GL 3.3 context created successfully |
| Vulkan 1.2 + cap | Zink fails: "requires nullDescriptor feature of robustness2" (feature only available at 1.3+) |
| No `MVK_CONFIG_API_VERSION_TO_ADVERTISE` | Vulkan 1.4 default → assertion crash |
| Without nullDescriptor patch | Zink fails: "requires nullDescriptor feature" (Wine thunk doesn't forward the bit) |
| Without `libwinpthread-1.dll` | `LoadLibraryA("opengl32.dll")` error 126 (missing dependency) |
| Without `WINEDLLPATH` + DLLs not in exe dir | `LoadLibraryA("opengl32.dll")` error 126 |
| DLLs in same dir as exe | Works (Windows DLL search order finds them) |

**Known warnings (non-fatal):**
- `WARNING: Some incorrect rendering might occur because the selected Vulkan device (Apple M4) doesn't support base Zink requirements: feats.features.logicOp have_EXT_custom_border_color`
- `fixme:vulkan:convert_VkPhysicalDeviceLayeredApiPropertiesKHR_win32_to_host Unexpected pNext` (Vulkan 1.3, suppressed by capping)

---

## 8. Known Issues for Future Resolution

1. **Wine winevulkan Vulkan 1.4 thunk**: `VkPhysicalDeviceLayeredApiPropertiesKHR` struct not handled. Workaround: cap to 1.3. Upstream Wine fix needed.
2. **Wine winevulkan robustness2 feature thunk**: `nullDescriptor` not forwarded in `VkPhysicalDeviceRobustness2FeaturesKHR`. Workaround: Mesa patch. Upstream Wine fix needed.
3. **Zink missing features on MoltenVK**: `logicOp` and `EXT_custom_border_color` not supported. May cause rendering artifacts in some games.
4. **Console app stdout swallowed by Wine**: `printf`/`cout` output not captured when running Wine exes via bash. Workaround: write to file.

---

## 9. Test Infrastructure

- `/tmp/mesa-zink-build/glversion.exe` — Full GL context + version query test (i686)
- `/tmp/mesa-zink-build/loadtest.exe` — Minimal DLL load test (i686)
- `/tmp/mesa-zink-build/filetest.exe` — File-based DLL load test (i686)
- Result files written to `C:\zink-gl-result.txt` in the Wine prefix
- Python venv: `/tmp/mesa-zink-build/.venv/` (mako, pyyaml, packaging)

1. ~~`tools/zink/build-mesa-zink.sh build` produces working `opengl32.dll` + `libgallium_wgl.dll`~~ ✅ Done (both arches)
2. ~~`tools/zink/verify-zink-readiness.sh` passes all checks~~ ✅ Done
3. ~~OpenGL 3.3+ context creation succeeds under Rosetta 2 Wine on Apple Silicon~~ ✅ Done (GL 3.3 Compatibility Profile, GLSL 4.60)
4. At least one Source Engine game (HL2 or Portal) launches and renders via Zink ⏳ Pending
5. `cargo test`, `cargo clippy`, `cargo fmt --check` all pass ⏳ Pending (Phase 3)
6. No regressions for D3D9 games on M9 pipeline ⏳ Pending
7. `verify-bundles.sh` passes with Zink entries added ⏳ Pending (Phase 2)
