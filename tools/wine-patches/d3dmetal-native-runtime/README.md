# D3DMetal Native Runtime Wine 11.5 Patches

These patches capture the proven MetalSharp Wine 11.5 x86_64 build-tree fixes
needed after the `d3dmetal-host-abi` patch is applied.

Apply from the Wine source root.

```sh
patch -p0 < tools/wine-patches/d3dmetal-host-abi/0001-d3dmetal-host-abi.patch
patch -p0 < tools/wine-patches/d3dmetal-native-runtime/0001-ntdll-d3dmetal-native-loader-tls.patch
patch -p0 < tools/wine-patches/d3dmetal-native-runtime/0002-mscompatdb-compat-surface.patch
```

Patch groups:

- `d3dmetal-host-abi/0001-d3dmetal-host-abi.patch`
  - winemac host ABI bridge
  - client-surface presentation plumbing
  - registry callback thunks
  - WDDM caps expected by the D3DMetal payload
- `d3dmetal-native-runtime/0001-ntdll-d3dmetal-native-loader-tls.patch`
  - D3DMetal bridge PE image writeability policy gated by `MS_ACTIVE_GRAPHICS_BACKEND=d3dmetal_native`
  - PE module registration with `libd3dshared.dylib` via `MS_D3DMETAL_SHARED_PATH`
  - narrow Apple pthread/TLS restoration around native timezone calls
  - temporary `ntdll.__wine_unix_call` compatibility export retained pending final ABI decision
- `d3dmetal-native-runtime/0002-mscompatdb-compat-surface.patch`
  - restores the MetalSharp 0.49 `mscompatdb.so` autoload/process-routing surface
  - gates autoload on `MS_ROOT` so direct `bin/wine` diagnostics do not fall back to `/etc/mscompatdb_rules.toml`
  - exports `KeServiceDescriptorTable` for the preserved `mscompatdb` hook

The runtime patches are x86_64/win64-oriented. Do not claim arm64 Wine support
for this D3DMetal payload until a separate arm64-compatible ABI and payload are
proven.

Current no-game packaged-runtime proof with this patch stack:

- `CreateDXGIFactory2` succeeds.
- `D3D12CreateDevice(NULL)` succeeds.
- `D3D11CreateDevice(NULL)` succeeds when GPTK4 `dxgi.dll`, `d3d11.dll`, and `d3d12.dll` are staged app-locally and selected with native-first `WINEDLLOVERRIDES` (`=n,b`). `WINEDLLPATH`/payload-root priority alone is not accepted as a route proof.
- `D3D10CreateDevice` / `D3D10CreateDevice1` still return `E_FAIL` and remain an extended compatibility workstream.
