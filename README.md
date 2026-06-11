<div align="center">

# MetalSharp

**Run Windows Steam games on macOS with Metal.**

<a href="https://github.com/aaf2tbz/metalsharp/actions"><img src="https://img.shields.io/github/actions/workflow/status/aaf2tbz/metalsharp/ci.yml?branch=main&style=for-the-badge" alt="CI"></a>
<a href="https://github.com/aaf2tbz/metalsharp/releases"><img src="https://img.shields.io/github/v/release/aaf2tbz/metalsharp?style=for-the-badge" alt="Release"></a>
<a href="https://github.com/aaf2tbz/metalsharp/discussions"><img src="https://img.shields.io/github/discussions/aaf2tbz/metalsharp?style=for-the-badge" alt="Discussions"></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT"></a>

</div>

---

MetalSharp is a macOS app for Apple Silicon that translates Direct3D calls to Metal via DXMT, bundles its own Wine runtime, and manages Steam game detection, launch routing, and runtime bottles.

## Quick Start

Download the latest DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases), drag MetalSharp into `/Applications`, and open it. The setup wizard handles the rest.

For building from source, see [Install from Source](docs/guides/install-from-source.md).

## Routes

| Route | Engine |
|---|---|
| **M12** | D3D12 to Metal |
| **M11** | D3D11 to Metal |
| **M10** | D3D10 to Metal |
| **M9** | D3D9 / 32-bit DXMT |
| **Mono/FNA** | Windows XNA/FNA via native Mono |
| **D3DMetal** | Apple Game Porting Toolkit |

## Features

- **Sharp Library** - Import and run standalone Windows programs, installers, and launchers
- **Runtime Bottles** - Per-game isolated Wine prefixes with component tracking and repair
- **MTSP Routing** - Automatic pipeline selection based on game compatibility data
- **Steam Integration** - Detects your Steam library, manages the Wine Steam session, and deploys the CEF wrapper

## Requirements

- Apple Silicon Mac, macOS 14+
- About 2 GB free space
- Homebrew (installed by setup wizard)

## Developer Setup

Current maintainer validation is happening on this hardware/software setup. This is not the recommended baseline or minimum requirement; it is here so readers know what MetalSharp is actively running on during development.

- MacBook Air (Mac16,12)
- Apple M4, 10-core CPU (4 performance, 6 efficiency)
- 16 GB memory
- macOS Golden Gate beta, version 27.0 (build 26A5353q)

## Documentation

- [Install from Source](docs/guides/install-from-source.md)
- [How to Use MetalSharp](docs/guides/how-to-use-metalsharp.md)
- [GPTK (D3DMetal) Guide](docs/guides/gptk-guide.md)
- [Game Compatibility](docs/compatibility/GAMES-SUPPORTED.md)
- [Launch Architecture](docs/architecture/launch-architecture.md)
- [Docs Map](docs/README.md)

## Community

- [Releases](https://github.com/aaf2tbz/metalsharp/releases)
- [Discussions](https://github.com/aaf2tbz/metalsharp/discussions)
- [Issues](https://github.com/aaf2tbz/metalsharp/issues)

## License

MIT licensed. Third-party components keep their original licenses; see [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).
