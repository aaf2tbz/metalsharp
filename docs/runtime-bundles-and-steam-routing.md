# Runtime Bundles and Steam Routing

This is the operational contract for bundle provenance and Wine Steam launch routing.

## Bundle Provenance

Runtime assets are downloaded from the `bundles` GitHub release into `app/bundles/` during app packaging and into `~/.metalsharp/cache/bundles/` during installer fallback downloads.

The manifest-tracked assets are listed in `tools/bundles/asset-manifest.tsv`. Assets in that manifest must match both size and SHA256 before they are accepted.

Current guarded assets:

| Asset | Why it is guarded |
|---|---|
| `dxmt.tar.zst` | Owns the D3D10/D3D11/D3D12 DXMT runtime used by M9-M12. A stale archive can make clean installs silently lose the current shader/runtime work. |
| `steamwebhelper.exe` | Wine Steam CEF wrapper. A stale wrapper can make Steam appear broken even when Wine and the backend are fine. |
| `steamwebhelper-wrapper.c` | Source for the Steam CEF wrapper shipped beside the binary. |

Verification commands:

```bash
tools/bundles/verify-bundles.sh --require mac
tools/bundles/verify-bundles.sh --require linux
tools/bundles/verify-bundles.sh --release
```

## Installer Acceptance Rules

The installer accepts `dxmt.tar.zst` only when its SHA256 equals the compiled `DXMT_BUNDLE_SHA256` in `app/src-rust/src/installer.rs`. The unit test in that file checks the compiled constant against the tracked manifest.

Installed DXMT runtime state is recorded in:

```text
~/.metalsharp/runtime/wine/lib/dxmt/metalsharp-dxmt-runtime.json
```

Do not trust a runtime by version string alone. Check the manifest, required DLLs, and source archive hash when diagnosing deployment drift.

## Steam Launch Route

The app launches Wine Steam through:

```text
Renderer button -> POST /steam/launch -> steam::launch_wine_steam()
```

Game launches that need M12/DXMT route through:

```text
Renderer Play -> POST /steam/launch-game {"launchMethod":"dxmt_metal12"} -> prepare_steam_pipeline_env() -> Steam game launch
```

Wine Steam must be launched by the backend so it gets the managed Wine prefix, runtime library env, DLL overrides, and wrapper deployment. Launching `Steam.exe` directly from a shell is not equivalent to pressing the app button.

## Steam Wrapper Rules

Before launching Steam, MetalSharp calls `ensure_steam_launch_ready()` and redeploys `steamwebhelper.exe` when Steam has overwritten it. The backend only accepts the wrapper if it matches `STEAMWEBHELPER_WRAPPER_SHA256` in `app/src-rust/src/steam.rs`.

The expected deployed Steam CEF layout is:

```text
~/.metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/bin/cef/cef.win64/
├── steamwebhelper.exe       # MetalSharp wrapper
├── steamwebhelper_real.exe  # Steam's original helper
└── .ms_wrapper_deployed
```

If Steam login stops rendering after bundle or wrapper work, verify the wrapper hash before changing launch args.
