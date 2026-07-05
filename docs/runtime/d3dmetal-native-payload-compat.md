# D3DMetal Native Payload Compatibility Transform

MetalSharp does **not** redistribute Apple D3DMetal/GPTK binaries. Users or developers stage those files locally from their own GPTK source.

`tools/runtime/patch-d3dmetal-native-payload.py` is a post-stage compatibility transform for the local staged payload. It patches known GPTK4 PE thunk DLLs in place and writes a receipt, without adding any Apple binary to the repository.

## What it changes

For known GPTK4 `dxgi.dll`, `d3d11.dll`, and `d3d12.dll` byte patterns, the tool changes narrow conditional branches so these entries use the payload's existing Wine unix-call fallback path instead of direct native callback dispatch:

- `dxgi.dll` `Thunk_Thread` → unix-call fallback
- `dxgi.dll` `CreateDXGIFactory2` → unix-call fallback
- `d3d12.dll` `D3D12CreateDevice` → unix-call fallback
- `d3d11.dll` `D3D11CreateDevice` → unix-call fallback

This preserves backend-off Wine/Steam behavior and avoids the broader rejected TLS/syscall patch stack.

## Usage

```sh
# Runtime root form:
python3 tools/runtime/patch-d3dmetal-native-payload.py ~/.metalsharp/runtime/wine

# Payload directory form:
python3 tools/runtime/patch-d3dmetal-native-payload.py ~/.metalsharp/runtime/wine/lib/d3dmetal_native

# Dry-run validation:
python3 tools/runtime/patch-d3dmetal-native-payload.py ~/.metalsharp/runtime/wine --check
```

The tool is idempotent. It records:

- `.metalsharp-compat-backups/<timestamp>/` with pre-patch local DLL copies
- `metalsharp-d3dmetal-compat-patches.json` with patch ids, offsets, and before/after SHA-256 values

## Evidence from loader-compat investigation

The validated local candidate produced `RC=0` for backend-on split probes:

- load-only
- `CreateDXGIFactory2`
- `D3D12CreateDevice`
- `D3D11CreateDevice`

Backend-off validation also passed:

- `wineboot -u`
- TEB probe
- SteamSetup silent install/update smoke

Keep this lane experimental until follow-up game/device-object semantics are validated beyond no-game split probes.
