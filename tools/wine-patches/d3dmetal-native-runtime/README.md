# D3DMetal Native Runtime Wine 11.5 Patches

Status: **loader-safe Wine patch + local GPTK4 payload compatibility transform**.

These patches are for the MetalSharp Wine 11.5 Rosetta x86_64 host + all-PE
runtime shape. Apply from the Wine source root after the host ABI patch.

```sh
patch -p0 < tools/wine-patches/d3dmetal-host-abi/0001-d3dmetal-host-abi.patch
patch -p0 < tools/wine-patches/d3dmetal-native-runtime/0001-ms-d3dmetal-loader-compat-minimal.patch
```

## Active Wine patch

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

## Active payload transform

`tools/runtime/stage-d3dmetal-native-payload.py` applies
`tools/runtime/patch-d3dmetal-native-payload.py` by default after staging a
user/developer-provided GPTK4 payload. This transform modifies only local
user-staged Apple binaries and records a receipt. MetalSharp does not commit or
redistribute Apple D3DMetal/GPTK payload files.

The transform forces known GPTK4 PE thunk entrypoints through their existing
unix-call fallback paths:

- `dxgi.dll` `Thunk_Thread`
- `dxgi.dll` `CreateDXGIFactory2`
- `d3d12.dll` `D3D12CreateDevice`
- `d3d11.dll` `D3D11CreateDevice`

This avoids the native callback path that dirties Darwin pthread TLS/GS under
Rosetta while preserving backend-off Wine/Steam behavior.

## Validation helpers

- `tools/runtime/check-d3dmetal-native-payload.py` verifies the staged payload
  contract and host ABI readiness.
- `tools/runtime/run-d3dmetal-native-split-probes.py` compiles and runs the
  backend-on split probes:
  - load-only
  - `CreateDXGIFactory2`
  - `D3D12CreateDevice`
  - `D3D11CreateDevice`

## Current evidence

Verified during the loader-compat investigation:

- backend-off `wineboot -u` succeeds
- backend-off TEB probe succeeds
- backend-on `wineboot -u` succeeds
- backend-on load-only probe succeeds
- backend-on split create probes return process `RC=0` / HRESULT success for:
  - `CreateDXGIFactory2`
  - `D3D12CreateDevice`
  - `D3D11CreateDevice`
- backend-off SteamSetup silent install/update smoke succeeds
- forbidden broad `ntdll`/syscall/TLS files remain unchanged by the active Wine
  patch

The lane remains experimental until follow-up real-game/device-object semantics
are validated beyond no-game split probes.

## Deprecated patch

`0001-ntdll-d3dmetal-native-loader-tls.patch.deprecated-do-not-apply` is retained
only as historical evidence. Do not apply it to product runtime builds. It
contains the earlier broad TLS/export approach that this investigation rejected
for the Steam-safe Wine 11.5 runtime shape.
