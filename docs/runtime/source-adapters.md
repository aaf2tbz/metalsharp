# Source adapters

`GET /source-adapters` reports the canonical storefront/local-library adapters that can feed MetalSharp launch and diagnostics flows.

Schema: `metalsharp.source.adapters.v1`

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

| Adapter | Library | Launch | Prefix policy | Receipts |
|---|---|---|---|---|
| Steam | `/games` | `/mtsp/launch` | shared `~/.metalsharp/prefix-steam`, must not alias GOG | preview + runtime |
| GOG | `/sharp-library/gog/games` | `/sharp-library/gog/play` | dedicated `~/.metalsharp/bottles/gog-prefix/prefix`, must not alias Steam | runtime |
| Sharp Library | `/sharp-library` | `/sharp-library/launch` | per-app/imported bottles under `~/.metalsharp/bottles/` | runtime |

The endpoint is read-only. It does not initialize prefixes, launch games, run Wine, replace installs, or modify runtime payloads.
