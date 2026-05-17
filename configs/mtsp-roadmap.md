# MTSP Engine Roadmap

## Overview

MTSP (Metal Translation & Shading Pipeline) replaces hardcoded per-game launch
logic with a unified, data-driven pipeline system. Adding a game means editing
TOML, not Rust code.

## Phase 0 — Core Engine (current)

- [x] PipelineId enum and declarative PipelineNode configs
- [x] PE import parser with D3D API auto-detection
- [x] TOML override rules (~70 appid mappings)
- [x] 3-tier resolver: TOML → directory heuristics → PE analysis → fallback
- [x] Unified launcher with DLL deployment matrix
- [x] API endpoints: GET /mtsp/pipelines, POST /mtsp/prepare
- [x] Backward-compatible launch delegates
- [x] GPTK dependency removed
- [x] Custom D3D9 Metal source (M9 pipeline)
- [ ] Clippy clean, fmt pass, CI green

## Phase 1 — Frontend Integration

- [ ] Pipeline selector UI in game library
- [ ] Pipeline info panel with description and alternatives
- [ ] Per-game pipeline override (persisted to TOML)
- [ ] Experimental pipeline warnings

## Phase 2 — Advanced Features

- [ ] Pipeline benchmark mode (run frame test, compare pipelines)
- [ ] Auto-pipeline tuning based on GPU family (M1 vs M4)
- [ ] Community pipeline recommendations (shared TOML rules)
- [ ] Hot-reload rules TOML without restart

## Pipeline ID Reference

| ID | Name | Backend | Notes |
|---|---|---|---|
| M11 | DXMT D3D11 → Metal | dxmt | Primary pipeline for D3D11 games |
| M12 | DXMT D3D12 → Metal | dxmt | WIP — compute works, graphics PSO in progress |
| M9 | D3D9 → Metal | dxmt | Primary D3D9 path under the DXMT launch family |
| M32W | WineD3D 32-bit | wined3d | Without Vulkan |
| M64 | Bare Wine 64-bit | wine | No translation layer |
| Steam | Steam Native | steam | macOS native |
| SteamMetalfx | D3DMetal + MetalFX | steam-d3dmetal | Spatial upscaling |
| SteamD3DMetalPerf | D3DMetal Perf | steam-d3dmetal | Performance optimizations |
| FnaArm64 | FNA ARM64 | mono | Mono + OpenGL/Metal |
| FnaX86 | FNA x86 | mono | x86 Mono + OpenGL |
| MonoGeneric | Mono Generic | mono | Generic .NET |
| WineBare | Wine Bare | wine | Plain Wine |
