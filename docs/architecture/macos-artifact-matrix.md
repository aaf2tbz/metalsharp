# macOS artifact architecture matrix

| Artifact | Architecture | Boundary |
| --- | --- | --- |
| Electron app and C backend | arm64 | Native macOS process boundary |
| Host runtime and update migrator | arm64 | Native macOS helper boundary |
| D3D11, D3D12, DXGI, audio/input, loader, native launcher | x86_64 | Wine/Rosetta Windows PE boundary |

Release validation must reject host artifacts with the wrong architecture and
must keep Wine-side x86_64 libraries out of Electron and backend processes.
