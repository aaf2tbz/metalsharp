<div align="center">

# M E T A L S H A R P

**Run Windows Steam games on macOS with Metal.**

<a href="https://github.com/aaf2tbz/metalsharp/actions"><img src="https://img.shields.io/github/actions/workflow/status/aaf2tbz/metalsharp/ci.yml?branch=main&style=for-the-badge" alt="CI"></a>
<a href="https://github.com/aaf2tbz/metalsharp/releases"><img src="https://img.shields.io/github/v/release/aaf2tbz/metalsharp?style=for-the-badge" alt="Release"></a>
<a href="https://github.com/aaf2tbz/metalsharp/discussions"><img src="https://img.shields.io/github/discussions/aaf2tbz/metalsharp?style=for-the-badge" alt="Discussions"></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT"></a>

**Beta 5**

</div>

---

MetalSharp is a macOS app for playing Windows Steam games on Apple Silicon.

It includes its own Wine runtime, DXMT Metal graphics support, Steam setup, game detection, and updater.

## Runtime

MetalSharp keeps its files in `~/.metalsharp/`:

```text
~/.metalsharp/
├── runtime/        Wine, DXMT, Goldberg, Mono, and shims
├── prefix-steam/   Windows Steam prefix
├── shader-cache/   Per-game graphics caches
├── configs/        Pipeline rules
└── logs/           Setup and launch logs
```

When you click Play, MetalSharp picks a pipeline, prepares the needed DLLs and cache, then starts the game through Wine, Steam, native macOS Steam, or the native Mono runtime.

## Download

Get the latest DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases), drag MetalSharp into `/Applications`, and open it.

## Requirements

- Apple Silicon Mac
- macOS 14 or newer
- About 2 GB free space

## Upgrade

Install the new DMG over the old app. MetalSharp keeps your Steam install and game data when it migrates the runtime.

## Help

- [Releases](https://github.com/aaf2tbz/metalsharp/releases)
- [Discussions](https://github.com/aaf2tbz/metalsharp/discussions)
- [Issues](https://github.com/aaf2tbz/metalsharp/issues)

## License

MIT
