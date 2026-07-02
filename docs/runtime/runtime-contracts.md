# Runtime Contracts

MetalSharp exposes a read-only runtime lane contract endpoint:

```http
GET /runtime/contracts
```

This endpoint is the foundation for MetalSharp Wine 2.0. It reports the canonical runtime surfaces and launch lanes that the backend understands, without mutating runtime state or launching anything.

## Purpose

Runtime contracts give one backend-owned source of truth for:

- route names and families;
- Wine/native/GPTK ownership;
- Win32/Win64 support;
- source scope (`steam`, `gog`, `sharp_library`);
- prefix policy;
- required PE DLLs;
- required Unix/macOS sidecars;
- DYLD and `WINEDLLPATH` route directories;
- shader cache lane;
- doctor checks and repair actions;
- fallback lane ordering.

The frontend, docs, Runtime Doctor, migration, and release gates should converge on this contract instead of each hardcoding route metadata independently.

## Canonical M12 Naming

The installed modern M12 runtime surface is canonicalized as:

```text
dxmt_m12
```

Installed path:

```text
~/.metalsharp/runtime/wine/lib/dxmt_m12/
```

The graphics release bundle may still use a hyphenated archive root:

```text
Graphics/dll/dxmt-m12/
```

That is packaging terminology. Runtime contracts and installed runtime doctors should report `dxmt_m12`.

## Initial Lanes

Available lanes:

- `native_mono_arm64`
- `native_mono_x86`
- `m9`
- `m10`
- `m11`
- `m12_dxmt_m12`
- `d3dmetal_gptk` externally provisioned through Homebrew GPTK
- `wine_bare`
- `steam_background`
- `gogdl_wine`

Planned lanes:

- `dxvk_d9`
- `dxvk_d11`
- `vkd3d_d12`

Planned lanes are intentionally visible in the contract so UI, docs, doctors, and migration can reserve stable IDs before the actual launch implementations are promoted.

As of the Wine 2.0 private fork, `dxvk_d9`, `dxvk_d11`, and `vkd3d_d12` also have hidden MTSP pipeline definitions. These definitions reserve route parsing, environment shape, cache buckets, `WINEDLLPATH` directories, and DLL deployment manifests, but they are not user-selectable and remain `planned` until the actual DXVK/VKD3D/MoltenVK runtime payloads and doctors are staged.

## Compatibility Priority Model

Recommended graphics fallback model:

```text
D3D9:   M9 -> DXVK-D9 -> WineD3D/plain Wine
D3D10:  M10 -> DXVK-D11 -> M11 -> WineD3D/plain Wine
D3D11:  M11 -> DXVK-D11 -> D3DMetal -> WineD3D/plain Wine
D3D12:  M12/dxmt_m12 -> D3DMetal -> VKD3D-Proton -> M11 when applicable
```

Native FNA/XNA/MonoGame titles should prefer the native Mono/FNA lanes when proven.
