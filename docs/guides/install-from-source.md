# Install from Source

Build MetalSharp from source without using the DMG. Requires macOS 14+ on Apple Silicon.

## Prerequisites

```bash
# Xcode CLI Tools
xcode-select --install

# Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
eval "$(/opt/homebrew/bin/brew shellenv)"

# Build dependencies
brew install cmake node zstd
```

## Clone

```bash
git clone --recurse-submodules https://github.com/aaf2tbz/metalsharp.git
cd metalsharp
```

## Build

```bash
# Native engine (C++ D3D/Metal layer) - x86_64 for Rosetta 2 PE translation
mkdir -p build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build --parallel $(sysctl -n hw.ncpu)

# Rust backend
cd app/src-rust && cargo build --release && cd ../..

# Electron frontend
cd app && npm install && npm run build && cd ..
```

## Fetch Runtime Bundles

Downloads Wine, DXMT, GPTK DLLs, Steam setup files, the D3D12 developer SDK,
and other runtime assets from the `bundles` GitHub release. The script also
repairs the runtime bundle with the current backend, host ABI, safe
`mscompatdb.so`, MetalSharp hook DLL, and M12 shader corpus before verification:

```bash
./tools/dmg/create-bundles.sh
./tools/bundles/verify-bundles.sh --bundle-dir app/bundles --require mac
./tools/bundles/verify-developer-sdk.sh app/bundles/metalsharp-d3d12-developer-sdk.tar.zst
```

## Run

```bash
cd app && npx electron .
```

## Build a Signed App

For an ad-hoc signed `.app` (no Apple Developer account needed):

```bash
cd app && npx electron-builder --dir --mac --arm64
codesign --force --deep --sign - ../dist/electron/mac-arm64/MetalSharp.app
open ../dist/electron/mac-arm64/MetalSharp.app
```

For a distributable DMG with hardened runtime (requires Apple Developer certificate):

```bash
cd app && npm run dmg
```

## Troubleshooting

- **`cmake` fails**: Ensure Xcode CLI tools are installed (`xcode-select -p` should return a path)
- **`npm install` fails**: Make sure Node 18+ is installed (`brew install node`)
- **Missing or stale bundles**: Run `./tools/dmg/create-bundles.sh`, then the
  bundle verifiers above. M12 requires the shader corpus proof inside the
  runtime archive.
- **App won't open**: If you see a Gatekeeper warning, run `xattr -cr /path/to/MetalSharp.app`
