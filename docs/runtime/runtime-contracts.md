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

## User-Selectable Route Options

The UI-facing route selector endpoint is:

```http
GET /mtsp/pipelines
GET /mtsp/pipelines?appid=<steam appid>
```

It is backend-owned and derived from MTSP pipeline metadata that is expected to stay aligned with `/runtime/contracts`. Steam cards use it for app-specific recommendations and alternatives; Sharp Library uses it for bottle/runtime selectors with a static frontend fallback only for backend-unavailable states. Route selectors should not introduce new hardcoded runtime lanes without adding the corresponding backend pipeline and runtime contract.

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
- `dxvk_d9` experimental fallback
- `dxvk_d11` experimental fallback
- `vkd3d_d12` experimental fallback
- `d3dmetal_gptk` externally provisioned through Homebrew GPTK
- `wine_bare`
- `steam_background`
- `gogdl_wine`

Planned lanes: none in the current Wine 2.0 private fork contract.

As of the Wine 2.0 private fork, `dxvk_d9`, `dxvk_d11`, and `vkd3d_d12` are available experimental MTSP pipeline definitions. They reserve route parsing, environment shape, cache buckets, `WINEDLLPATH` directories, DLL deployment manifests, and Vulkan filesystem doctors. They remain fallback lanes, not defaults.

## Compatibility Priority Model

Recommended graphics fallback model:

```text
D3D9:   M9 -> DXVK-D9 -> WineD3D/plain Wine
D3D10:  M10 -> DXVK-D11 -> M11 -> WineD3D/plain Wine
D3D11:  M11 -> DXVK-D11 -> D3DMetal -> WineD3D/plain Wine
D3D12:  M12/dxmt_m12 -> D3DMetal -> VKD3D-Proton -> M11 when applicable
```

Native FNA/XNA/MonoGame titles should prefer the native Mono/FNA lanes when proven.
