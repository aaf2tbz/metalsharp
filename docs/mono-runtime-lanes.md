# MetalSharp Mono Runtime Lanes

Status: Phase 11 active

MetalSharp now treats Mono as a runtime choice, not a global dependency. The old working FNA path and the current Wine installer path are different compatibility lanes and should stay isolated per bottle/game.

## Lanes

| Lane | Runtime | Known version | Scope | Notes |
|------|---------|---------------|-------|-------|
| Wine Mono | Wine prefix component | bundled by Wine prefix | Windows CLR/bootstrapper apps inside a bottle | Used by .NET installers and Windows apps that call `mscoree.dll` through Wine. Minecraft currently reaches this lane and crashes in the native Mono/runtime path. |
| Native Mono ARM64 | `~/.metalsharp/runtime/mono-arm64/bin/mono` | 6.14.1 | Terraria/FNA ARM64 style games | This is the dinosaur path that made Terraria work: native macOS Mono, FNA/XNA dllmaps, native SDL/FNA3D/FAudio/shims, and no Wine prefix ownership. |
| Native Mono x86_64 | `~/.metalsharp/runtime/mono-x86/bin/mono` | 6.12.0.122 | Celeste/FNA legacy lane under Rosetta | This lane exists because some older FNA/Mono dependencies were x86_64-only or behaved better with Mono 6.12 and explicit dllmaps. |

## Rules

- Sharp Library installer bottles should use Wine Mono only when the target is actually a Windows CLR/bootstrapper app.
- Steam games should request native Mono lanes only for known FNA/XNA targets or proof targets that need the old macOS Mono path.
- Native Mono ARM64/x86_64 should not be injected into every Wine bottle.
- Wine Mono failures should be logged separately from native Mono/FNA failures.
- A hard case like Minecraft should remain in the Wine bottle lane until evidence shows it needs a native launcher bridge or a different Wine Mono strategy.

## Phase 11 Minecraft Finding

Minecraft is a useful hard case because it exercises installer classification, bottle readiness, Wine Mono, embedded browser/runtime needs, and app detection. Current evidence shows:

- The bottle classifies as `java_launcher` and launches from a stable installer bottle.
- Fonts and Gecko can be repaired or detected correctly.
- WebView2 is still not available as a local runtime asset.
- M9, bare Wine, and `WINEDLLOVERRIDES=mscompatdb=d` all reproduce the same native Mono crash.

The next implementation path is to keep Minecraft as the proof target while adding better Wine Mono diagnostics and, if needed, a bottle-scoped alternate Mono strategy rather than changing global runtime behavior.
