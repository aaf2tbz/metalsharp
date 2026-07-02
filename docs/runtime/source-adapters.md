# Source adapters

`GET /source-adapters` reports the canonical storefront/local-library adapters that can feed MetalSharp launch and diagnostics flows.

`POST /source-adapters/prepare` returns a unified, read-only prepare preview for Steam, GOG, and Sharp Library sources. It emits `metalsharp.source.prepare.preview.v1` with a nested `metalsharp.launch.receipt.v1` preview, but does not stage DLLs, initialize prefixes, launch games, run Wine, or mutate manifests.

Schemas:

- `metalsharp.source.adapters.v1`
- `metalsharp.source.prepare.preview.v1`

Each adapter uses schema `metalsharp.source.adapter.v1` and includes:

- `id`: stable source id (`steam`, `gog`, `sharp`)
- `displayName`
- `kind`: `storefront` or `local_library`
- `libraryEndpoint`, `statusEndpoint`, `launchEndpoint`, and optional `prepareEndpoint`
- `runtimeContractIds`: runtime contracts this source can route to
- `prefixPolicy`: ownership, path, and aliasing rules
- `ready`, `status`, `installed`, and source-specific `details`
- `capabilities`: scan/install/prepare/launch/stop/receipt support
- `limitations`: non-blocking gaps for roadmap tracking

Current adapter shape:

| Adapter | Library | Prepare preview | Launch | Prefix policy | Receipts |
|---|---|---|---|---|---|
| Steam | `/games` | `/source-adapters/prepare` | `/mtsp/launch` | shared `~/.metalsharp/prefix-steam`, must not alias GOG | preview + runtime |
| GOG | `/sharp-library/gog/games` | `/source-adapters/prepare` | `/sharp-library/gog/play` | dedicated `~/.metalsharp/bottles/gog-prefix/prefix`, must not alias Steam | runtime |
| Sharp Library | `/sharp-library` | `/source-adapters/prepare` | `/sharp-library/launch` | per-app/imported bottles under `~/.metalsharp/bottles/` | runtime |

Example preview requests:

```json
{ "source": "steam", "appId": 620, "route": "m12" }
{ "source": "gog", "productId": "1876546888" }
{ "source": "sharp", "id": "custom-game", "route": "dxvk_d11" }
```

Both endpoints are read-only. They do not initialize prefixes, launch games, run Wine, replace installs, or modify runtime payloads.
