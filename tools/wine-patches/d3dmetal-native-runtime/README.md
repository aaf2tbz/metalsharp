# D3DMetal Native Runtime Wine 11.5 Patches

Status: **safe loader-compat candidate; create-path compatibility still blocked**.

These patches are for the MetalSharp Wine 11.5 Rosetta x86_64 host + all-PE
runtime shape. Apply from the Wine source root after the host ABI patch.

```sh
patch -p0 < tools/wine-patches/d3dmetal-host-abi/0001-d3dmetal-host-abi.patch
patch -p0 < tools/wine-patches/d3dmetal-native-runtime/0001-ms-d3dmetal-loader-compat-minimal.patch
```

## Active patch

`0001-ms-d3dmetal-loader-compat-minimal.patch` is the restored safe candidate
from the 2026-07-05 loader-compat investigation. It intentionally avoids:

- `dlls/ntdll/ntdll.spec` exports
- `dlls/ntdll/unix/signal_x86_64.c`
- `dlls/ntdll/unix/syscall.c`
- `dlls/ntdll/unix/system.c`
- broad syscall dispatcher / GS / TLS mutation

It provides only the Phase-A loader compatibility surface:

- backend-gated D3DMetal payload-root priority through `MS_D3DMETAL_PAYLOAD_DIR`
- backend-gated PE image registration through `MS_D3DMETAL_SHARED_PATH`
- bridge DLL image writability for the D3DMetal allowlist
- a narrow `RtlVirtualUnwind2` null optional-output guard observed during GPTK4
  crash diagnostics

## Current evidence

Verified with the restored safe candidate:

- backend-off internal Steam prefix `wineboot -u` succeeds
- GPTK4 `dxgi.dll`, `d3d12.dll`, and `d3d11.dll` load with backend enabled
- `CreateDXGIFactory2`, `D3D12CreateDevice`, and `D3D11CreateDevice` exports resolve
- forbidden `ntdll`/syscall/TLS files remain unchanged

Still blocked:

- `CreateDXGIFactory2` crashes before returning HRESULT
- `D3D12CreateDevice` crashes before returning HRESULT
- `D3D11CreateDevice` crashes before returning HRESULT
- common native crash site: `libsystem_kernel.dylib task_policy_set +331`

Detailed blocker audit:

```text
/Volumes/AverySSD/wine-build-ms/metalsharp-wine-11.5-d3dmetal-loader-compat-experiment/audits/roadmap-completion-blocker-audit-20260705-0341.md
```

## Deprecated patch

`0001-ntdll-d3dmetal-native-loader-tls.patch.deprecated-do-not-apply` is retained
only as historical evidence. Do not apply it to product runtime builds. It
contains the earlier broad TLS/export approach that this investigation rejected
for the Steam-safe Wine 11.5 runtime shape.

## Product rule

The D3DMetal Native lane must remain experimental/not broadly launchable until
a separate fix proves the create-call results required by the roadmap without
regressing backend-off Steam/Wine behavior.
