# M9+Zink OpenGL Pipeline Map

M9+Zink adds OpenGL support to MetalSharp for Windows games that use OpenGL instead of Direct3D. It layers Mesa's Zink Gallium driver on top of the existing MoltenVK Vulkan runtime, producing the translation chain **OpenGL -> Vulkan -> MoltenVK -> Metal -> Apple GPU**.

## Status

Experimental. Phase 1 (build + validation) complete. OpenGL 3.3 Compatibility Profile confirmed on Apple M4 via MoltenVK 1.4.1 and Wine 11.5. Not yet integrated into the MetalSharp bundle or MTSP pipeline resolver.

## Runtime Shape

```text
Game EXE (PE32 or PE32+)
  -> opengl32.dll (Mesa Zink, cross-compiled PE DLL)
    -> libgallium_wgl.dll (Mesa WGL implementation)
      -> Vulkan API via Wine's builtin vulkan-1.dll
        -> Wine thunks to host MoltenVK ICD
          -> libMoltenVK.dylib
            -> Metal framework
              -> Apple GPU
```

Zink has no Unix-side `.so` or `.dylib` component. Everything above the Vulkan thunk is pure PE code running inside the Wine process under Rosetta 2, same as DXMT's PE DLLs.

## Engine Contract

| Field | Value |
|---|---|
| Pipeline | `M9Zink` |
| Backend | `dxmt` |
| Experimental | yes |
| Wine overrides | `d3d9=n,b;opengl32=n;gameoverlayrenderer,gameoverlayrenderer64=d` |
| Shader cache subdir | `m9-zink` |
| Requires Vulkan | yes (MoltenVK) |
| Requires Wine | yes |

M9+Zink deploys:

- `d3d9.dll` (standard M9 D3D9 DLL)
- `opengl32.dll` (Mesa Zink)
- `libgallium_wgl.dll` (Mesa WGL)
- `libwinpthread-1.dll` (MinGW runtime dependency)
- Standard DXMT/MetalSharp DLLs from M9

## DLLs

Three DLLs per architecture, all cross-compiled from Mesa 25.3.6 with MinGW:

| DLL | Size (x86_64) | Size (i686) | Purpose |
|---|---|---|---|
| `opengl32.dll` | 538K | 344K | OpenGL entrypoint, loads libgallium_wgl |
| `libgallium_wgl.dll` | 41M | 35M | Mesa Gallium Zink driver + WGL implementation |
| `libwinpthread-1.dll` | 345K | 328K | MinGW C++ threading runtime (dependency of libgallium_wgl) |

Both x86_64 and i686 builds are required. Many OpenGL Windows games are 32-bit (PE32 i386) and cannot use the x86_64 DLLs.

### Dependency Chain

```text
opengl32.dll
  imports: GDI32, KERNEL32, libgallium_wgl.dll, CRT APIs

libgallium_wgl.dll
  imports: GDI32, KERNEL32, USER32, libwinpthread-1.dll, CRT APIs

libwinpthread-1.dll
  imports: KERNEL32, CRT APIs
```

If any dependency in this chain is missing, `LoadLibraryA("opengl32.dll")` fails with Windows error 126 (module not found). All three DLLs must be co-located in the same directory or reachable via `WINEDLLPATH`.

## Environment Variables

| Variable | Value | Required | Purpose |
|---|---|---|---|
| `WINEDLLOVERRIDES` | `opengl32=n` | yes | Use Mesa's opengl32.dll instead of Wine's builtin |
| `VK_ICD_FILENAMES` | path to MoltenVK ICD JSON | yes | Select MoltenVK as the Vulkan driver |
| `MVK_CONFIG_API_VERSION_TO_ADVERTISE` | `4206592` | yes | Cap MoltenVK to Vulkan 1.3 (see Wine compatibility notes below) |
| `MESA_LOADER_DRIVER_OVERRIDE` | `zink` | yes | Force Mesa to use the Zink Gallium driver |
| `MESA_GL_VERSION_OVERRIDE` | `3.3COMPAT` | recommended | Advertise GL 3.3 Compatibility Profile to the game |
| `MESA_SHADER_CACHE_DIR` | cache path | recommended | Persistent Zink shader cache (GLSL -> NIR -> SPIR-V) |
| `LIBGL_KOPPER_DISABLE` | `true` | optional | Disable Kopper WSI if swapchain issues occur |
| `MESA_GLSL_VERSION_OVERRIDE` | `460` | optional | Force specific GLSL version |

The Vulkan 1.3 cap (`MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592`) is **mandatory**. Without it, MoltenVK advertises Vulkan 1.4 which triggers an assertion in Wine's `winevulkan` thunk at `vkGetPhysicalDeviceProperties2` (unhandled `VkPhysicalDeviceLayeredApiPropertiesKHR` struct). Vulkan 1.3 avoids this.

### GL Version Override Fallbacks

If `3.3COMPAT` does not work for a specific game, try in order:

1. `3.3COMPAT` (conservative, works for most OpenGL games)
2. `4.1COMPAT` (matches Apple's native OpenGL on ARM64)
3. `4.3COMPAT` (VK_KHR_dynamic_rendering threshold)
4. `4.6COMPAT` (maximum, may hit missing MoltenVK extensions)

## Runtime Paths

### Install layout

```text
~/.metalsharp/runtime/wine/lib/zink/
  x86_64-windows/
    opengl32.dll
    libgallium_wgl.dll
    libwinpthread-1.dll
  i386-windows/
    opengl32.dll
    libgallium_wgl.dll
    libwinpthread-1.dll
```

### Cache layout

```text
~/.metalsharp/shader-cache/m9-zink/<appid>/
```

### Bundle layout (metalsharp-graphics-dll.tar.zst)

```text
Graphics/dll/zink/
  x86_64-windows/opengl32.dll
  x86_64-windows/libgallium_wgl.dll
  x86_64-windows/libwinpthread-1.dll
  i386-windows/opengl32.dll
  i386-windows/libgallium_wgl.dll
  i386-windows/libwinpthread-1.dll
```

### WINEDLLPATH

The pipeline node's `winedllpath_dirs` must include `lib/zink/x86_64-windows` (and the matching i386 path for 32-bit games) so Wine can find `libgallium_wgl.dll` and `libwinpthread-1.dll` when `opengl32.dll` loads them. The `metalsharp-wine` wrapper script appends the standard Wine DLL paths to `WINEDLLPATH`; the Zink path must be prepended by the pipeline launcher.

## Validated GL Output

Tested on Apple M4, macOS 26, Wine 11.5, MoltenVK 1.4.1:

```text
GL_VENDOR: Mesa
GL_RENDERER: zink Vulkan 1.3 (Apple M4 (MOLTENVK))
GL_VERSION: 3.3 (Compatibility Profile) Mesa 25.3.6 (git-06f9e28304)
GL_SHADING_LANGUAGE_VERSION: 4.60
```

Without `MESA_GL_VERSION_OVERRIDE`, Zink defaults to GL 2.1 / GLSL 1.20. The override is required for games that check the GL version at startup.

## Wine and MoltenVK Compatibility

Three compatibility issues were discovered during validation. All have workarounds applied.

### 1. Wine winevulkan Vulkan 1.4 assertion

Wine 11.5's `winevulkan` thunk does not handle `VkPhysicalDeviceLayeredApiPropertiesKHR`, a struct introduced in Vulkan 1.4. When MoltenVK advertises Vulkan 1.4, the thunk hits an assertion in `loader_thunks.c:6053`:

```text
Assertion failed: !status && "vkGetPhysicalDeviceProperties2",
  file dlls/winevulkan/loader_thunks.c, line 6053
```

**Workaround**: Cap MoltenVK to Vulkan 1.3 via `MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592`.

**Upstream fix needed**: Wine winevulkan must thunk `VkPhysicalDeviceLayeredApiPropertiesKHR` in the pNext chain of `vkGetPhysicalDeviceProperties2`.

### 2. Wine winevulkan robustness2 feature forwarding

Mesa 25.3.6 Zink requires the `nullDescriptor` feature of `VK_KHR_robustness2` (or `VK_EXT_robustness2`). MoltenVK 1.4.1 supports this extension and the feature, but Wine's `winevulkan` thunk does not forward the `nullDescriptor` field in `VkPhysicalDeviceRobustness2FeaturesKHR` back to the application. Zink sees `nullDescriptor = VK_FALSE` and refuses to initialize.

**Workaround**: Patch `zink_screen.c:3456` in Mesa source to override `nullDescriptor` to `VK_TRUE` with a warning log instead of failing. This is safe because MoltenVK actually supports the feature; the check is a false negative from Wine's thunk.

Applied patch in `tools/zink/build-mesa-zink.sh`:

```c
// Before (Mesa upstream):
if (!screen->info.rb2_feats.nullDescriptor) {
    mesa_loge("Zink requires the nullDescriptor feature of KHR/EXT robustness2.");
    goto fail;
}

// After (MetalSharp patch):
if (!screen->info.rb2_feats.nullDescriptor) {
    mesa_logw("Zink: nullDescriptor not reported (likely winevulkan thunk issue); "
              "proceeding anyway since MoltenVK supports VK_EXT_robustness2.");
    screen->info.rb2_feats.nullDescriptor = VK_TRUE;
}
```

**Upstream fix needed**: Wine winevulkan must thunk `VkPhysicalDeviceRobustness2FeaturesKHR` fields in `vkGetPhysicalDeviceFeatures2`.

### 3. libwinpthread-1.dll missing

`libgallium_wgl.dll` links against MinGW's `libwinpthread-1.dll` (C++ threading runtime). This DLL is not part of Wine's builtin runtime. Without it, `opengl32.dll` fails to load with error 126.

**Workaround**: Copy `libwinpthread-1.dll` from the MinGW sysroot into the Zink DLL directory alongside `opengl32.dll` and `libgallium_wgl.dll`.

### Non-fatal warnings

Zink emits this warning on MoltenVK (does not prevent GL context creation):

```text
WARNING: Some incorrect rendering might occur because the selected Vulkan device
(Apple M4) doesn't support base Zink requirements: feats.features.logicOp
have_EXT_custom_border_color
```

`logicOp` and `EXT_custom_border_color` are not supported by MoltenVK. Most games do not depend on these features. If a game shows rendering artifacts, this warning points to the likely cause.

## Building from Source

### Prerequisites

```bash
brew install mingw-w64 bison meson ninja flex pkg-config zstd
brew link bison --force
pip3 install mako pyyaml packaging
```

### Build

```bash
cd repo
./tools/zink/build-mesa-zink.sh check     # verify toolchain
./tools/zink/build-mesa-zink.sh build     # full build (~15 min, both arches)
```

The build script:
1. Clones Mesa from `https://gitlab.freedesktop.org/mesa/mesa.git` and checks out `mesa-25.3.6`
2. Applies the nullDescriptor compatibility patch to `zink_screen.c`
3. Cross-compiles for x86_64-w64-mingw32 and i686-w64-mingw32
4. Copies `libwinpthread-1.dll` from the MinGW sysroot
5. Packages into `app/bundles/zink-staging/mesa-zink.tar.zst`

### Install into runtime

```bash
./tools/zink/install-zink-local.sh
```

### Verify

```bash
./tools/zink/verify-zink-readiness.sh
```

### Clean

```bash
./tools/zink/build-mesa-zink.sh clean
```

### Environment variables for the build script

| Variable | Default | Purpose |
|---|---|---|
| `MESA_VERSION` | `mesa-25.3.6` | Mesa git tag or branch |
| `MESA_SOURCE_DIR` | `/tmp/mesa-zink-build/mesa-src` | Local Mesa clone |
| `BUILD_BASE` | `/tmp/mesa-zink-build` | Build directory |
| `OUTPUT_DIR` | `app/bundles/zink-staging` | Staging output |
| `ARCHS` | `x86_64 i686` | Space-separated arch list |
| `JOBS` | `$(sysctl -n hw.ncpu)` | Parallel compile jobs |

### Mesa configure flags

```text
--cross-file <arch>       # MinGW cross-compilation config
--buildtype=release
-Dplatforms=windows
-Dgallium-drivers=zink
-Dvulkan-drivers=[]
-Dopengl=true
-Dgles1=disabled
-Dgles2=disabled
-Dglx=disabled
-Degl=disabled
-Dllvm=disabled
-Dshared-llvm=disabled
-Dspirv-tools=disabled    # host SPIRV-Tools dylib can't link for MinGW target
-Dgbm=disabled
-Dgallium-va=disabled
-Dxmlconfig=disabled
-Dbuild-tests=false
--wrap-mode=nodownload
```

## Manual Testing

To test Zink with a game executable without going through the MetalSharp launcher:

```bash
# Determine game arch: 32-bit = i386-windows, 64-bit = x86_64-windows
ZINK_ARCH=i386-windows   # or x86_64-windows

env WINEPREFIX="$HOME/.metalsharp/prefix-steam" \
  WINEDLLOVERRIDES="opengl32=n" \
  VK_ICD_FILENAMES="$HOME/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json" \
  MVK_CONFIG_API_VERSION_TO_ADVERTISE=4206592 \
  MESA_LOADER_DRIVER_OVERRIDE=zink \
  MESA_GL_VERSION_OVERRIDE=3.3COMPAT \
  MESA_SHADER_CACHE_DIR="$HOME/.metalsharp/shader-cache/zink" \
  WINEDLLPATH="$HOME/.metalsharp/runtime/wine/lib/zink/$ZINK_ARCH" \
  "$HOME/.metalsharp/runtime/wine/bin/metalsharp-wine" /path/to/game.exe
```

The `metalsharp-wine` wrapper will append the standard Wine DLL paths to `WINEDLLPATH`, so Zink's `opengl32.dll` can find Wine's builtins while also finding `libgallium_wgl.dll` and `libwinpthread-1.dll` in the Zink directory.

## Selection Rules (Planned)

Once integrated into the MTSP resolver, the selection path will be:

1. `configs/mtsp-rules.toml` per-game override
2. PE import detection: `opengl32.dll` in the import table triggers `M9Zink`
3. Fallback to `M9` for games without OpenGL imports

The PE import detection uses the existing `parse_pe_imports()` in `pe.rs`. A new `D3dApi::OpenGL` variant will be added to the `D3dApi` enum.

## Files

### Tooling

| File | Purpose |
|---|---|
| `tools/zink/build-mesa-zink.sh` | Fetch, patch, configure, compile, package Mesa Zink PE DLLs |
| `tools/zink/install-zink-local.sh` | Install DLLs into `~/.metalsharp/runtime/wine/lib/zink/` |
| `tools/zink/verify-zink-readiness.sh` | Check Zink + MoltenVK + Vulkan readiness |

### Integration targets (Phase 2-3)

| File | Change |
|---|---|
| `tools/bundles/create-split-bundles.py` | Add Zink to `metalsharp-graphics-dll.tar.zst` |
| `tools/bundles/verify-bundles.sh` | Add Zink DLL verification |
| `app/src-rust/src/installer.rs` | Add Zink extraction from graphics bundle |
| `app/src-rust/src/mtsp/engine.rs` | Add `PipelineId::M9Zink` and pipeline node |
| `app/src-rust/src/mtsp/launcher.rs` | Add `zink` backend match in `cache_env_pairs()` |
| `app/src-rust/src/mtsp/recipe.rs` | Detect `opengl32.dll` in PE imports |
| `app/src-rust/src/mtsp/pe.rs` | Add `D3dApi::OpenGL` variant |
| `app/src-rust/src/mtsp/rules.rs` | Add Zink routing rules for known OpenGL games |
| `THIRD_PARTY_LICENSES` | Add Mesa MIT license |

## Why a Separate Pipeline

Zink cannot be merged into the existing M9 pipeline because:

- M9 sets `WINEDLLOVERRIDES="d3d9=n,b;..."` without overriding `opengl32`. Adding `opengl32=n` would load Mesa's `opengl32.dll` for every M9 game, including D3D9-only games that never touch OpenGL. Mesa's `opengl32.dll` initializes the Zink/Vulkan stack at load time, which is wasted work and can interfere with games that do not expect it.
- The DLL deployment list, `WINEDLLPATH`, and environment variables are all different from standard M9.
- Games that use both D3D9 and OpenGL (rare but possible) need both sets of DLLs deployed correctly.

The `M9Zink` pipeline variant keeps the D3D9 DLLs from M9 while adding the Zink OpenGL stack on top, and it only activates for games detected as OpenGL users.

## Game Candidates

Games known to use OpenGL on Windows and potentially testable through MetalSharp:

| Game | Notes |
|---|---|
| Dead Cells | 32-bit, uses HashLink/OpenGL, but has a pre-existing `sdl.hdll` loading failure under Wine that blocks testing |
| Source Engine games (HL2, Portal) | Can be launched with `-gl` flag to force OpenGL mode |
| Minecraft (Windows version) | Uses OpenGL natively |
| Indie games using SDL+OpenGL | Common pattern; depends on specific game |

## Known Limitations

- **GL 4.6 untested**: Only GL 3.3 Compatibility Profile has been validated. Higher versions may work but depend on MoltenVK exposing additional Vulkan extensions.
- **No full game test yet**: GL context creation is confirmed working. No game has been launched end-to-end through the MetalSharp pipeline with Zink.
- **Wine thunk bugs**: Two upstream Wine bugs (Vulkan 1.4 struct and robustness2 features) require workarounds. These should be tracked for removal when Wine fixes them.
- **MoltenVK missing features**: `logicOp` and `EXT_custom_border_color` are not supported. May cause rendering artifacts in games that depend on them.
- **Large DLL size**: `libgallium_wgl.dll` is 35-41 MB per arch (76 MB total for both arches). This adds to the graphics bundle size.
