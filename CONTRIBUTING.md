# Contributing to MetalSharp

Thanks for your interest. This project is early-stage and ambitious — there's plenty to work on.

## Quick start

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

All 4 tests should pass. If they don't, open an issue.

## Code conventions

- **C++20** for logic, **Objective-C++ (.mm)** for Metal API calls only
- No exceptions — use `HRESULT` return codes matching D3D patterns
- COM-style reference counting (`AddRef`/`Release`/`QueryInterface`)
- RAII wrappers for all Metal objects (`.mm` implementation files)
- No comments unless something is genuinely non-obvious

### File organization

| Extension | Purpose |
|-----------|---------|
| `.cpp` | Portable C++ logic |
| `.mm` | Objective-C++ — Metal/ObjC API calls only |
| `.h` | C++ headers |

### Naming

- Classes: `PascalCase` (`MetalDevice`, `D3D11DeviceContext`)
- Member variables: `m_camelCase` (`m_refCount`, `m_metalDevice`)
- Local variables: `camelCase` (`vertexBuffer`, `pipelineState`)
- Functions/methods: `camelCase` (`createBuffer`, `flushRenderState`)

### Commit style

Conventional commits:

```
feat: add DXBC bytecode parser
fix: correct swap chain resize handling
refactor: extract pipeline state caching
test: add texture creation tests
docs: update architecture diagram
```

## Areas that need help

### High priority

- **DXBC/DXIL bytecode parsing** — the shader translator currently accepts MSL source strings. We need a real bytecode parser to translate game shaders. This is the single hardest component.
- **D3D11 state object translation** — blend, rasterizer, depth stencil, sampler states need proper D3D11 → Metal mapping
- **Texture support** — subresource updates, mipmaps, multi-sampled textures
- **Map/Unmap** — CPU-accessible resource staging

### Medium priority

- **D3D12 implementation** — currently stubbed
- **Wine integration** — DLL injection, prefix management, HWND → NSWindow binding
- **Audio bridge** — XAudio2 → CoreAudio with spatial audio
- **Input bridge** — XInput → GameController with rumble/gyro

### Always welcome

- Bug fixes and test coverage
- Compatibility testing with specific games
- Documentation improvements
- Performance profiling and optimization

## Pull request process

1. Fork the repo
2. Create a feature branch (`feat/my-feature`, `fix/my-fix`)
3. Make your changes
4. Build and run all tests — they must pass
5. Open a PR against `main`

### PR checklist

- [ ] Builds cleanly with `cmake --build build`
- [ ] All existing tests pass (`cd build && ctest`)
- [ ] New code has corresponding tests
- [ ] No compiler warnings
- [ ] Commit messages follow conventional commits

## Architecture overview

The project has three layers:

1. **D3D shim layer** (`src/d3d/`, `src/dxgi/`) — implements the D3D COM interfaces. Games call these. Each shim delegates to the Metal backend.

2. **Metal backend** (`src/metal/`) — wraps Metal API objects (MTLDevice, MTLBuffer, MTLTexture, etc.) in C++ RAII classes. All ObjC/Metal calls live here.

3. **Translation layer** (inside the D3D shims) — converts D3D11 concepts to Metal equivalents. Vertex formats, pipeline states, draw calls, resource bindings.

The key rule: **Metal API calls only happen in `.mm` files**. The rest of the codebase stays platform-agnostic C++.

## Getting help

- Open a [GitHub issue](https://github.com/aaf2tbz/metalsharp/issues) for bugs or feature requests
- Check [ROADMAP.md](ROADMAP.md) to see what's planned
- Read the source — the architecture is deliberately straightforward
