# Wine 2.0 Phase 11–16 Upgrade Readiness

MetalSharp Wine 2.0 private builds now expose backend-owned, read-only contracts for the remaining roadmap phases. These endpoints are designed to prove the installed system is upgraded without launching games.

## Phase 11 — Runtime Doctor Everywhere

```http
GET /diagnostics/doctors
```

Schema: `metalsharp.doctor.registry.v1`

Covers Wine runtime, prefixes, DXMT M12, DXVK, VKD3D, D3DMetal, Mono/FNA, Steam, GOG, launchers, migration, and save manager doctor families. Each family maps to standard states such as Ready, Needs Repair, Missing Runtime, Wrong Architecture, Auth Expired, Missing Redist, and Unsupported.

## Phase 12 — Known-Good Runtime Snapshots

```http
GET /known-good
POST /known-good/record
```

Schemas:

- `metalsharp.known-good.inventory.v1`
- `metalsharp.known-good.v1`

Snapshots persist app/source/route/runtime-surface/version/manifest/receipt state for a known working configuration. Recording a snapshot does not launch a game; it stores explicit evidence supplied by the caller.

## Phase 13 — Compatibility Database V2

```http
GET /compatibility/db-v2
```

Schema: `metalsharp.compatibility.db.v2.preview`

Records include preferred route, fallback matrix, dependencies, launch/proof level, anti-cheat status, known issues, and user override placeholders. Existing `/bottles/compatibility-matrix` remains available for compatibility override workflows.

## Phase 14 — Safe Mode Launch

```http
GET /safe-mode/profile
POST /safe-mode/preview
```

Schema: `metalsharp.safe-mode.profile.v1`

Safe mode disables overlays, MetalFX, async pipeline compile, nonessential route DLLs, and launcher GPU acceleration. It enables verbose logs, minimal env, WineD3D/plain-Wine fallback, CEF software path, and crash classification. The preview endpoint is read-only and does not launch.

## Phase 15 — Save Manager

```http
GET /save-manager/inventory
POST /save-manager/backup-plan
```

Schemas:

- `metalsharp.save-manager.inventory.v1`
- `metalsharp.save-manager.backup-plan.v1`

The save manager inventories Steam, GOG, Sharp Library, bottle prefix, known-good, and receipt paths. Backup plans are read-only; future backup/restore mutations require explicit user action.

## Phase 16 — Build and Release Gates

```http
GET /diagnostics/support-inventory
GET /diagnostics/toolchain-inventory
GET /diagnostics/release-gates
```

Schemas:

- `metalsharp.support.inventory.v1`
- `metalsharp.toolchain.inventory.v1`
- `metalsharp.release.gates.v1`

Support inventory checks installed runtime/support assets. Toolchain inventory checks local build/signing/rebuild tooling. Release gates aggregate manifest, DXMT/M12, contracts, support inventory, toolchain inventory, diagnostics, and migration readiness.

## Invariants

- These endpoints do not launch games.
- These endpoints do not replace the installed app.
- Mutating launch, backup, restore, or install replacement still requires explicit user action.
- The Settings runtime diagnostics area surfaces release gates, support inventory, toolchain inventory, source adapters, launcher evidence, receipt inventory, and the Phase 0–16 roadmap audit.
