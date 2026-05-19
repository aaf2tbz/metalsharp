# MTSP Engine Roadmap

## Overview

MTSP (Metal Translation & Shading Pipeline) replaces hardcoded per-game launch
logic with a unified, data-driven pipeline system. Adding a game means editing
TOML, not Rust code.

## Phase 0 — Core Engine (current)

- [x] PipelineId enum and declarative PipelineNode configs
- [x] PE import parser with D3D API auto-detection
- [x] TOML override rules (~70 appid mappings)
- [x] Resolver: TOML → managed/FNA eligibility → PE analysis → directory heuristics → fallback
- [x] Unified launcher with DLL deployment matrix
- [x] API endpoints: GET /mtsp/pipelines, POST /mtsp/prepare
- [x] Backward-compatible launch delegates
- [x] GPTK dependency removed
- [x] M9 D3D9 route under the DXMT launch/cache family
- [x] Clippy clean, fmt pass, CI green

## Phase 1 — Frontend Integration

- [x] Pipeline selector UI in game library
- [x] Pipeline info panel with description and alternatives
- [ ] Per-game pipeline override (persisted to TOML)
- [ ] Experimental pipeline warnings

## Phase 1.5 — Runtime Bottles

- [x] Steam game bottle records (`steam_<appid>`) for launch preflight and runtime assets
- [x] Installer/Sharp Library bottle records for Windows installers and imported apps
- [x] Runtime Doctor surfaces for Steam cards, Sharp cards, and Runtime Bottles
- [x] Component/source policy reporting for Wine Mono, Gecko, .NET, VC runtime, DirectX, WebView2, and fonts

## Phase 2 — Advanced Features

- [ ] Pipeline benchmark mode (run frame test, compare pipelines)
- [ ] Auto-pipeline tuning based on GPU family (M1 vs M4)
- [ ] Community pipeline recommendations (shared TOML rules)
- [ ] Hot-reload rules TOML without restart

## Pipeline ID Reference

| ID | Name | Backend | Notes |
|---|---|---|---|
| M11 | DXMT D3D11 → Metal | dxmt | Primary pipeline for D3D11 games |
| M12 | DXMT D3D12 → Metal | dxmt | Primary D3D12 route, still game-compatibility dependent |
| M9 | D3D9 → Metal | dxmt | Primary D3D9 path under the DXMT launch family |
| M32 | Wine 32-bit | wine | 32-bit Wine fallback |
| Steam | Wine Steam | steam | Windows Steam in Wine, preflighted by Steam game bottles |
| MacOS Steam | Native Steam | steam | Native macOS Steam |
| FnaArm64 | FNA ARM64 | mono | Mono + OpenGL/Metal |
| WineBare | Wine Bare | wine | Plain Wine |
