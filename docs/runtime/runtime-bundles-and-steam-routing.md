# Runtime Bundles and Steam Routing

This is the operational contract for bundle provenance and Wine Steam launch routing.

## Bundle Provenance

Runtime assets are downloaded from the `bundles` GitHub release into `app/bundles/` during app packaging and into `~/.metalsharp/cache/bundles/` during installer fallback downloads.

The manifest-tracked assets are listed in `tools/bundles/asset-manifest.tsv`. The verifier checks that each tarball exists and contains the expected bundle-named root.

Current split bundle roots:

| Asset | Why it is guarded |
|---|---|
| `metalsharp-electron.tar.zst` | Contains `electron/`, the built Electron application payload. |
| `metalsharp-graphics-dll.tar.zst` | Contains `Graphics/dll/`, the DXMT D3D10/D3D11/D3D12 DLLs and winemetal bridge. |
| `metalsharp-runtime.tar.zst` | Contains `runtime/`, the Wine runtime, host ABI, and backend executable. |
| `metalsharp-assets.tar.zst` | Contains `assets/`, Mono, GPTK, DXVK, Goldberg, EAC toggle, shims, and runtime support assets. |
| `metalsharp-scripts-tools.tar.zst` | Contains `scripts/tools/`, updater scripts, configs, native tools, and CEF helpers. |
| `metalsharp-steam.tar.zst` | Contains `steam/`, the Steam installer and Steam CEF wrapper assets. |
| `metalsharp-d3d12-developer-sdk.tar.zst` | Contains `developer-sdk/d3d12/`, the D3D12 contracts, probes, scripts, docs, staged developer Wine runtime, DXMT DLLs, Winemetal bridge files, and runtime provenance manifest. |

Verification commands:

```bash
tools/bundles/verify-bundles.sh --require mac
tools/bundles/verify-bundles.sh --release
tools/bundles/verify-developer-sdk.sh app/bundles/metalsharp-d3d12-developer-sdk.tar.zst
```

When repairing a published runtime bundle, start from the actual
`metalsharp-runtime.tar.zst` asset on the `bundles` release, not from an
unverified local archive. The repair flow is:

```bash
gh release download bundles --repo aaf2tbz/metalsharp \
  --pattern metalsharp-runtime.tar.zst --dir <work-dir>
cargo build --release --manifest-path app/src-rust/Cargo.toml
python3 tools/dmg/repair-runtime-bundle.py \
  --archive <work-dir>/metalsharp-runtime.tar.zst \
  --host-dir app/native/host \
  --backend app/src-rust/target/release/metalsharp-backend
tools/bundles/verify-bundles.sh --bundle-dir <verify-bundles-dir> --require mac
gh release upload bundles <work-dir>/metalsharp-runtime.tar.zst \
  --repo aaf2tbz/metalsharp --clobber
```

After upload, download the asset back and compare its SHA-256 against the
verified local archive. This closes the release-asset drift loop.

## Installer Acceptance Rules

The installer consumes the split runtime tarballs by root name. `metalsharp-graphics-dll.tar.zst` is the only source for the active DXMT runtime payload used by M9-M12.

Installed DXMT runtime state is recorded in:

```text
~/.metalsharp/runtime/wine/lib/dxmt/metalsharp-dxmt-runtime.json
```

Do not trust a runtime by version string alone. Check the manifest, required DLLs, and source archive hash when diagnosing deployment drift.

M12 has one extra acceptance rule: the runtime must include shader-engine corpus
material. The runtime archive contract requires the checked-in D3D12 shader
corpus proof material under:

```text
runtime/wine/share/d3d12-metal-sdk/shader-corpus/elden-ring-present-vb-pull-20260612/proof/SHA256SUMS
```

Install and migration validate that this corpus surface exists before the Wine
init surface is accepted. Presence of one random `.metallib` is not enough:
validation requires the proof file and at least one runtime-safe shader-engine
material file under a proven corpus source.

## Wine Init Runtime Surface

The M12/M11/M10/M9 routes use `~/.metalsharp/prefix-steam` as the shared Wine
Steam prefix. Installing or updating MetalSharp must refresh the prefix copy of
the current runtime before games are launched.

The staged prefix surface is:

```text
~/.metalsharp/prefix-steam/drive_c/windows/system32/
  d3d9.dll
  d3d10.dll
  d3d10_1.dll
  d3d10core.dll
  d3d11.dll
  d3d12.dll
  dxgi.dll
  dxgi_dxmt.dll
  winemetal.dll
  nvapi64.dll
  nvngx.dll
  metalsharp_ntdll_hook.dll

~/.metalsharp/prefix-steam/.metalsharp/unix/
  winemetal.so
  winemac.so
  ntdll.so
  libc++.1.dylib
  libc++abi.1.dylib
  libunwind.1.dylib
```

The source of truth for this list is `app/src-rust/src/prefix_runtime.rs`.
Install and migration both call the same staging and validation code. Validation
compares file contents against the installed runtime, so a stale `d3d12.dll` or
`winemetal.so` in the prefix fails even if the file exists.

Fresh install runs a bounded prefix probe after staging:

```text
metalsharp-wine cmd /c exit
```

The installer stages before the probe, runs the probe with `WINEDLLPATH` pointed
at the current runtime, stages again after the probe, then validates the prefix
surface and M12 shader material.

The bounded probe is fail-closed. If it exceeds the timeout, MetalSharp kills
the child process and reports an error. A killed or hung Wine init must never be
treated as an installed or migrated runtime.

Migration uses the same surface as an update operation. It preserves user and
bottle settings, refreshes runtime files, restores settings, stages the prefix
surface, and validates. It does not reinstall Steam when `Steam.exe` already
exists. It also does not require `Steam.exe` to validate an update, because the
goal is to prove the runtime surface Steam and games will load from.

Migration may skip runtime work only when both the core runtime files and
`validate_install_wine_init_surface()` pass. A core Wine/DXMT archive that
exists on disk but has stale prefix DLLs or missing M12 corpus proof is not
ready.

If migration reports a Wine init/runtime failure, it performs one repair pass:

1. Kill prefix-scoped Steam update/helper processes.
2. Repair `drive_c/windows/system32`, `.metalsharp/unix`, `drive_c`, and
   `dosdevices`.
3. Back up an invalid `dosdevices` file/path as `dosdevices.migration-repair-*`.
4. Recreate `dosdevices/c:`.
5. Remove only the staged runtime DLLs and sidecars.
6. Restage the current runtime, rerun `cmd /c exit`, and validate again.

This repair is intentionally narrow. It is for a broken runtime surface inside
an existing update; it is not a Steam reinstall path.

## Steam Launch Route

The app launches Wine Steam through:

```text
Renderer button -> POST /steam/launch -> steam::launch_wine_steam()
```

Game launches that need an explicit public route use M12/M11/M10/M9/Mono-FNA route IDs. Raw `dxmt` remains an internal auto-router and legacy compatibility value.

```text
Renderer Play -> POST /steam/launch-game {"launchMethod":"m12"} -> prepare_steam_pipeline_env() -> direct game launch with Wine Steam alive in the background
```

Wine Steam must be launched by the backend so it gets the managed Wine prefix, runtime library env, DLL overrides, and wrapper deployment. Launching `Steam.exe` directly from a shell is not equivalent to pressing the app button.

For M12 game launches, Wine Steam is the background ownership/session client.
The game executable is still launched through the prepared MTSP M12 route with
the DXMT DLLs, `WINEDLLPATH`, `DXMT_WINEMETAL_UNIXLIB=winemetal.so`, cache
paths, and `~/.metalsharp/logs/m12/<appid>/m12.log` diagnostics. A Steam
client-only handoff is diagnostic/bootstrap behavior, not the normal M12 route.

`POST /mtsp/prepare` is a staging preflight, not a launch. It must still stage
the same launch-critical M12 assets: Agility sidecars, prefix-route DLLs,
game-local DXMT DLLs, M12 Unix sidecars, Steam identity files, and game-local
path verification. If these cannot be staged, prepare should fail instead of
returning `ok`.

## Steam Wrapper Rules

Before launching Steam, MetalSharp calls `ensure_steam_launch_ready()` and redeploys `steamwebhelper.exe` when Steam has overwritten it. Steam assets come from `metalsharp-steam.tar.zst`, and the backend only accepts the extracted wrapper if it matches `STEAMWEBHELPER_WRAPPER_SHA256` in `app/src-rust/src/steam.rs`.

The expected deployed Steam CEF layout is:

```text
~/.metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/bin/cef/cef.win64/
|-- steamwebhelper.exe       # MetalSharp wrapper
|-- steamwebhelper_real.exe  # Steam's original helper
`-- .ms_wrapper_deployed
```

If Steam login stops rendering after bundle or wrapper work, verify the wrapper hash before changing launch args.
