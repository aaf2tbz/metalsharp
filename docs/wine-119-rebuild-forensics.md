# Wine 11.9 Rebuild Forensics

Date: 2026-05-21

This document records the evidence gathered after rolling `main` back to `v0.33.27`, the last confirmed working release. It is intended to be the rebuild contract for reintroducing Wine 11.9 without repeating the regressions that broke Nidhogg 2, Schedule I, Subnautica Below Zero routing confidence, Steam attach behavior, and shader-cache behavior.

## Current Truth

- `main` is intentionally reverted to the `v0.33.27` tree at rollback commit `cebf9362f47817580f5eceeb4000ef3987854b2e`.
- Last working tag: `v0.33.27` at `13228f20af6bff6be718f4520c423c76076d3985`.
- The preserved post-11.9 regression branch is `backup/main-before-v0.33.28-reset-20260521T223249Z` at `797db720c852b7f46356fa2541fb4dac81afcc69`.
- The public `v0.33.28`, `v0.33.29`, and `v0.33.30` releases/tags were deleted. Their code is still recoverable from local branch history and backups.
- Installed working app is MetalSharp `0.33.27`.
- Installed working Wine runtime is bundled Wine `11.5`, not system Wine.

## Live Control Sample: Nidhogg 2

Nidhogg 2 was running successfully on the rollback stack during this investigation.

Captured evidence:

- Process snapshot: `~/.metalsharp/diagnostics/live-nidhogg2-20260521T230054Z/`
- Fixed `lsof` snapshot: `~/.metalsharp/diagnostics/live-nidhogg2-20260521T230128Z-lsof-fixed/`
- Launch log: `~/.metalsharp/compatdata/535520/logs/launch-1779404379.log`
- Bottle manifest: `~/.metalsharp/bottles/steam_535520/bottle.json`
- Compatdata manifest: `~/.metalsharp/compatdata/535520/metalsharp-compatdata.json`

Facts from the live run:

- AppID: `535520`
- Game: `Nidhogg_2.exe`
- Runtime profile: `m9`
- Launch pipeline: `d3d9_metal`
- Steam identity mode: `wine_steam_background`
- Wine prefix: `~/.metalsharp/prefix-steam`
- Wine runtime: `~/.metalsharp/runtime/wine`
- Process: `C:\Program Files (x86)\Steam\steamapps\common\Nidhogg 2\Nidhogg_2.exe`
- It launched through Wine Steam, not native Steam.

Loaded files from the running process prove the working contract:

- `~/.metalsharp/runtime/wine/lib/wine/i386-windows/winemetal.dll`
- `~/.metalsharp/runtime/wine/lib/wine/i386-windows/dxgi.dll`
- `~/.metalsharp/runtime/wine/lib/wine/i386-windows/d3d11.dll`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/winemetal.so`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/libMoltenVK.1.dylib`
- `~/.metalsharp/shader-cache/m9/535520/shaders_320.db`
- `/private/var/folders/.../C/dxmt/Nidhogg_2.exe/com.apple.metal/...`

This means working Nidhogg 2 is not a plain M32 fallback. It is a 32-bit game under the M9 DXMT family, with Wine/DXMT builtins loaded from the runtime and shader cache under `m9/535520`.

The working launch log also shows:

- `pipeline=M9`
- `steam_identity_mode=wine_steam_background`
- `info: Found config file: ~/.metalsharp/runtime/wine/etc/dxmt.conf`
- `DirectX11: Using hardware device`
- `Entering main loop`
- `entered room r_Title_Screen`
- `entered room r_Level_Select`

## Live Control Sample: Schedule I

Schedule I was also launched successfully on the rollback stack during this investigation.

Captured evidence:

- Process snapshot: `~/.metalsharp/diagnostics/live-schedule-i-20260521T230550Z/`
- Log snapshot: `~/.metalsharp/diagnostics/live-schedule-i-20260521T230550Z-logs/`
- Loaded-file snapshot: `~/.metalsharp/diagnostics/live-schedule-i-20260521T230603Z-lsof/`
- Launch log: `~/.metalsharp/compatdata/3164500/logs/launch-1779404663.log`
- Bottle manifest: `~/.metalsharp/bottles/steam_3164500/bottle.json`
- Compatdata manifest: `~/.metalsharp/compatdata/3164500/metalsharp-compatdata.json`

Facts from the live run:

- AppID: `3164500`
- Game: `Schedule I.exe`
- Runtime profile: `m11`
- Launch pipeline: `dxmt_metal`
- Steam identity mode: `wine_steam_background`
- Wine prefix: `~/.metalsharp/prefix-steam`
- Wine runtime: `~/.metalsharp/runtime/wine`
- Process: `C:\Program Files (x86)\Steam\steamapps\common\Schedule I\Schedule I.exe`
- Unity crash handler: `UnityCrashHandler64.exe --attach ...`

Loaded files from the running process prove the working M11 contract:

- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/winemetal.so`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/libMoltenVK.1.dylib`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/opengl32.so`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/opengl32.dll`
- `~/.metalsharp/shader-cache/m11/3164500/shaders_320.db-shm`
- `/private/var/folders/.../C/org.winehq.wine/com.apple.metal/...`
- `~/.metalsharp/prefix-steam/drive_c/users/alexmondello/AppData/LocalLow/TVGS/Schedule I/Player.log`

The working launch log also shows:

- `pipeline=M11`
- `steam_identity_mode=wine_steam_background`
- `info: Failed to set Metal cache path, fallback to system default`
- `info: Found config file: ~/.metalsharp/runtime/wine/etc/dxmt.conf`
- `info: Maximum supported feature level: D3D_FEATURE_LEVEL_12_1`
- `info: Using feature level D3D_FEATURE_LEVEL_11_1`
- `info: Using feature level D3D_FEATURE_LEVEL_11_0`
- `warn: CreateSwapChain: unsupported swap effect 3 with backbuffer size 2`

This means working Schedule I is M11/DXMT, not M12, and not a Steam relaunch/protected-handoff path. It also shows that system-default Metal cache fallback is part of the current working behavior and must not be mistaken for a failure by itself.

## Live Control Sample: Subnautica Below Zero

Subnautica Below Zero was launched successfully on the rollback stack during this investigation.

Captured evidence:

- Process/log/loaded-file snapshot: `~/.metalsharp/diagnostics/live-subnautica-bz-20260521T230918Z/`
- Launch log: `~/.metalsharp/compatdata/848450/logs/launch-1779404936.log`
- Bottle manifest: `~/.metalsharp/bottles/steam_848450/bottle.json`
- Compatdata manifest: `~/.metalsharp/compatdata/848450/metalsharp-compatdata.json`

Facts from the live run:

- AppID: `848450`
- Game: `SubnauticaZero.exe`
- Runtime profile: `m11`
- Launch pipeline: `dxmt_metal`
- Steam identity mode: `wine_steam_background`
- Wine prefix: `~/.metalsharp/prefix-steam`
- Wine runtime: `~/.metalsharp/runtime/wine`
- Process: `SubnauticaZero.exe`
- Parent process: `metalsharp-backend`, while Wine Steam remains running as the background identity process
- Unity crash handler: `UnityCrashHandler64.exe --attach ...`

Loaded files from the running process prove the working M11/DXMT contract:

- `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/d3d11.dll`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/dxgi.dll`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/winemetal.dll`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/winemetal.so`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/libMoltenVK.1.dylib`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/opengl32.so`
- `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/opengl32.dll`
- `~/.metalsharp/shader-cache/m11/848450/shaders_320.db`
- `/private/var/folders/.../C/org.winehq.wine/com.apple.metal/...`
- `~/.metalsharp/prefix-steam/drive_c/users/alexmondello/AppData/LocalLow/Unknown Worlds/Subnautica Below Zero/Player.log`

The working launch log also shows:

- `pipeline=M11`
- `steam_identity_mode=wine_steam_background`
- `info: Found config file: ~/.metalsharp/runtime/wine/etc/dxmt.conf`
- `info: Maximum supported feature level: D3D_FEATURE_LEVEL_12_1`
- `info: Using feature level D3D_FEATURE_LEVEL_11_1`
- `info: Using feature level D3D_FEATURE_LEVEL_11_0`
- `warn: CreateSwapChain: unsupported swap effect 3 with backbuffer size 2`
- `warn: Emulate stream output`

This means working Subnautica Below Zero is confirmed on the M11/DXMT path. It is not just rendering through an unproven fallback: the live `SubnauticaZero.exe` process has the DXMT/WineMetal/DXGI/D3D11 runtime DLLs and Unix bridge loaded, and it is actively touching the M11 shader cache.

## Release Asset Evidence

Surviving GitHub Releases:

- `v0.33.27`: `MetalSharp-0.33.27-arm64.dmg`, size `797822705`, SHA256 `55b94b1c27d687af4b87ae92560113353bf17541a1da0cf4f1e718f0c7161176`
- `v0.33.26`: `MetalSharp-0.33.26-arm64.dmg`, size `797640027`, plus Linux `.deb`

Deleted release/tag state:

- `v0.33.28`: release/tag now 404; successful CI push run `26193479278`; DMG artifact size `1545677108`
- `v0.33.29`: release/tag now 404; successful CI push run `26195067092`; DMG artifact size `1545679918`
- `v0.33.30`: release/tag now 404; successful CI push run `26252075822`; DMG artifact size `1545720985`

Additional recovered asset from the deleted/working investigation path:

- `/tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst`, size `382242232`, SHA256 `833f63566b0c1b98fa917337716f57d689c42d0c2878204b4716ba29637d7372`
- GitHub source: special release/tag `bundles`, asset `metalsharp_bundle.tar.zst`
- Bundle root: `wine-11.9`
- `wine.inf` says `Version: Wine 11.9`
- `wine-11.9/bin/wine` SHA256: `8ac73223a37653b39212545a4d97c6302d5ea4e516f7b1ea5991dfc6848a90a4`
- `wine-11.9/etc/dxmt.conf` SHA256: `166e2fe829de03d64b225c65fa1682162e198852df51e9520adc3f3b37244621`
- `wine-11.9/share/wine/wine.inf` SHA256: `a4db2eb9c9746ec670dd982e306e419dda2fcb5625de577fddc109d8e6e888b6`
- `wine --version` from the minimally extracted bundle returns `wine-11.9`
- Release/upload tar listing contains `wine-11.9/lib/dxmt/x86_64-windows/winemetal.dll` and `wine-11.9/lib/dxmt/x86_64-unix/winemetal.so`.
- Release/upload tar listing does not contain `wine-11.9/lib/wine/i386-windows/winemetal.dll`.

Reproducible fetch path:

- `scripts/fetch-wine119-release-assets.sh /tmp/metalsharp-wine-assets metalsharp_bundle.tar.zst`
- output asset: `/tmp/metalsharp-wine-assets/metalsharp_bundle.tar.zst`
- fetch report: `/tmp/metalsharp-wine-assets/fetch-report.txt`
- digest source: GitHub release metadata for the `bundles` release
- required SHA256: `833f63566b0c1b98fa917337716f57d689c42d0c2878204b4716ba29637d7372`

The same `bundles` release also has `metalsharp_bundle2.tar.zst`:

- Local download: `/tmp/metalsharp-bundle2-aSicSS/metalsharp_bundle2.tar.zst`
- Size: `1047321594`
- SHA256: `c0cc9e4e0503b52076ab9dcb9b16a9baafb017778f987d20ec5a25126c374455`
- It is not a second Wine runtime bundle. Its archive contains dependency payloads and metadata.
- Metadata file: `./metadata/metalsharp-wine-11.9-candidate.txt`
- Metadata says:
  - Built: `2026-05-20T20:46:26Z`
  - Source: `Wine 11.9`
  - Stage: `/Volumes/AverySSD/MetalSharpWine119/stage-20260520-1354`
  - Patch: `mscompatdb hook contract exported from Unix ntdll.so; PE ntdll copy inert`
- The referenced `/Volumes/AverySSD/MetalSharpWine119/stage-20260520-1354` path is not currently present, so the metadata is provenance evidence but not a live runtime source.

The surviving `v0.33.30` Actions DMG artifact embeds that exact Wine 11.9 bundle:

- Local artifact: `/tmp/metalsharp-wine-assets-IjmErR/v0.33.30-artifact-dmg/MetalSharp-0.33.30-arm64.dmg`
- DMG size: `1547350844`
- DMG SHA256: `de5922406ed03cc7b3d4231905b3c3767e157a6ff38204f268bb18549140f037`
- Embedded `metalsharp_bundle.tar.zst` SHA256: `833f63566b0c1b98fa917337716f57d689c42d0c2878204b4716ba29637d7372`

For comparison, the 0.33.27 DMG contains:

- Embedded `metalsharp_bundle.tar.zst` SHA256: `1f18186f010d4f8dcbb0a0d786c807cfd7e01652cc272477ff736d5f78ede835`
- Bundle root: `wine-11.5`
- `wine-11.5/bin/wine` SHA256: `e621bf88dd07872b391198aee50bf1503fe18d43b7a9c0183fa23075efc61395`
- `wine-11.5/bin/metalsharp-wine` SHA256: `6873d7f19ff16c707b76806732f531f06cd2b5305231f7142d12b3ebc8ad9cd0`
- `wine-11.5/share/wine/wine.inf` SHA256: `14f55c9a641549b253445ffabc2fccf4ede4852f2ffc5aaea780f2c5da19c763`

The recovered Wine 11.9 bundle should be treated as the candidate source asset, but not as automatically valid. It still needs a parity audit against the 11.5 runtime layout.

## Wine 11.9 Internal Map Against Working Wine 11.5

The working 0.33.27 install is not just "some Wine 11.5". It is a shaped MetalSharp runtime rooted at `~/.metalsharp/runtime/wine`, with route-critical DXMT/WineMetal files already present in `lib/wine`, not only staged under `lib/dxmt`.

Runtime file-count snapshot:

- Installed working Wine 11.5 runtime: `2349` files under `~/.metalsharp/runtime/wine`
- Wine 11.9 bundle asset: `3935` files under archive root `wine-11.9`
- Difference driver: Wine 11.9 includes `include/wine/...` headers, but is missing several MetalSharp-shaped runtime bindings that exist in the working install.
- The partial extraction folders under `/tmp/metalsharp-wine-assets-IjmErR/extracted-evidence/` and `/tmp/metalsharp-wine-assets-IjmErR/v0.33.30-evidence/` are evidence scratch dirs only. They are not complete runnable Wine 11.9 runtime candidates.
- No architecture mismatch was found in the sampled route-critical binaries: both installed 11.5 and recovered 11.9 binaries are Mach-O `x86_64`, and both have `i386-windows`, `x86_64-windows`, and `x86_64-unix` runtime directories in the complete file lists.

Top-level/runtime-shape comparison:

| Surface | Working Wine 11.5 install | Wine 11.9 bundle asset | Rebuild requirement |
| --- | --- | --- | --- |
| Archive/install root | installed as `~/.metalsharp/runtime/wine` | archive root is `wine-11.9` | installer may extract from `wine-11.9`, but final runtime root must still be `~/.metalsharp/runtime/wine` unless all code is updated and tested |
| `bin/wine` | present, SHA256 `e621bf88dd07872b391198aee50bf1503fe18d43b7a9c0183fa23075efc61395` | present, SHA256 `8ac73223a37653b39212545a4d97c6302d5ea4e516f7b1ea5991dfc6848a90a4` | expected version change |
| `bin/wineserver` | present, SHA256 `fbd59b533e8db0e05015d623dca24919ec65a8a4a7e1487d8725b1efce1e7ee5` | present, SHA256 `a970f075a950f413a7b01b9cca7a4a21e473bc78111f893640a28374b04c2bb2` | expected version change |
| `bin/metalsharp-wine` | present shell wrapper, SHA256 `6873d7f19ff16c707b76806732f531f06cd2b5305231f7142d12b3ebc8ad9cd0` | missing | must be regenerated or carried forward; launcher prefers this wrapper when present |
| `etc/dxmt.conf` | present, SHA256 `166e2fe829de03d64b225c65fa1682162e198852df51e9520adc3f3b37244621` | present, same SHA256 | preserve path and visibility for M9/M11/M12 |
| `etc/mscompatdb_rules.toml` | present, SHA256 `7b17e2feafa3453e79a43ab0cfe246faecc70ec8c3427a1128eb8bd1fd375e87` | present, same SHA256 | not a differentiator by itself |
| `etc/vulkan/icd.d/MoltenVK_icd.json` | present, SHA256 `61d212ba97b02155cd8a16ef3e1be0ef7bf7a8c93e9f20c30506550f69a673ae` | raw bundle is same SHA256 | candidate preparation must rewrite `library_path` to the candidate runtime so isolated proof does not load MoltenVK from the installed 11.5 home |
| `etc/vulkan/icd.d/MoltenVK_x86_64_icd.json` | present, SHA256 `7ce881b47fd3adbc29855703c147566e7ffebf514bf7191e6f7903b4eb433af8` | raw bundle is same SHA256 | candidate preparation must rewrite `library_path` to the candidate runtime so isolated proof does not load MoltenVK from the installed 11.5 home |
| `lib/libMoltenVK.dylib` | missing, while ICD JSON points at this absolute path under the installed home | missing in raw bundle | candidate preparation must create a candidate-local binding to `lib/wine/x86_64-unix/libMoltenVK.1.dylib` and rewrite ICD JSON to that candidate-local path |
| Wine version marker | `share/wine/wine.inf` says Wine 11.5, SHA256 `14f55c9a641549b253445ffabc2fccf4ede4852f2ffc5aaea780f2c5da19c763` | says Wine 11.9, SHA256 `a4db2eb9c9746ec670dd982e306e419dda2fcb5625de577fddc109d8e6e888b6` | expected version change |

DXMT/WineMetal internal comparison:

| Surface | Working Wine 11.5 install | Wine 11.9 bundle asset | Rebuild requirement |
| --- | --- | --- | --- |
| `lib/dxmt/x86_64-windows/d3d11.dll` | present, SHA256 `15785d6c75d4ae5f0e3ab9b61f56b534d933ab6b691ffbc02eae30cee25b5bbc` | present, same SHA256 | unchanged |
| `lib/dxmt/x86_64-windows/winemetal.dll` | present, SHA256 `d357ed7a83df000f98f7e4fe5e2da99f0e36561457d2dfc30e90f09504880fc8` | present, same SHA256 | unchanged |
| `lib/dxmt/x86_64-windows/dxgi.dll` | present, SHA256 `4c86d66ac32248f80f42c2e0b86bb245e1295ef6f1b67ad522562b9fa81eb05d` | present, but SHA256 `dd2b6544c51d5e6b97df5ba9dc85863e07963962474f4ba2af5794cf71997c71` | audit DXGI behavior before parity claim |
| `lib/dxmt/x86_64-windows/d3d12.dll` | present, SHA256 `5a512873e328e7100c59968441bcea6a22a616c1653cb632bb221d215308896d` | present, SHA256 `388b4aa4418d54ed9efd3a6c8d853dc3c0e948990de80b368cdd60c43535003a` | expected D3D12 change; isolate from M9/M11 parity |
| `lib/dxmt/x86_64-unix/winemetal.so` | present, SHA256 `f01cfe25619faf85f3b840d527f6724c9efc444a9137882e9b22daa78a8a2c9c` | present, SHA256 `77f4d0b4b0b7ce867c1091ab6ae6951ff993489c84f26e7139886560a4814915` | expected WineMetal bridge change; must be tested directly |
| `lib/wine/x86_64-unix/winemetal.so` | present, SHA256 `2e80f62162ed3b66d905463ba0b7a2737e1c85cd740febabe3ceae87db24d723` | missing | must copy/bind from `lib/dxmt/x86_64-unix/winemetal.so` if preserving 11.5 route contract |
| `lib/wine/x86_64-windows/winemetal.dll` | present, SHA256 `9bf08a8d583a2b13f23765221238b6d4406467e8bae23b5bde4f5eed717f6e5b` | missing | must copy/bind from `lib/dxmt/x86_64-windows/winemetal.dll` or keep game-local deployment |
| `lib/wine/i386-windows/winemetal.dll` | present, SHA256 `20a6865facebdaac92b6c06fadf37d2efb5a242b22ca1c349bb57d7ad43df8e3` | missing | high-risk for Nidhogg 2; must restore or prove equivalent 32-bit route |
| `lib/wine/x86_64-windows/d3d11.dll` | present, SHA256 `c332d17219b5119ff16658950a4138e916ae024409e15331e341dbe600ae976d` | present, SHA256 `8bebdfd74251bcfbc4bb8f56f2e7e3db9c49ad81d0c798a64cd3e93175d3d64c` | expected Wine builtin change; do not switch override policy at the same time |
| `lib/wine/i386-windows/d3d11.dll` | present, SHA256 `9afc2b3419818618c4c87274435a28935b0df002caa4b9a8d3a88d3dc846b17d` | present, SHA256 `f8db6043b772cef94794524a247617800d3331dbae5f47a5fc283330e03669d5` | expected Wine builtin change; must be tested by Nidhogg 2 |
| `lib/wine/x86_64-windows/dxgi.dll` | present, SHA256 `69e0aa0ed177aa2b0e6ccca01b5d4bdee8317082b3d6dd42fba47d288c219bbf` | present, SHA256 `05e3959f3075fa4f3e22a54f6d659bbe8e07e0aa491a1b849cf6a7740a6b333c` | expected Wine builtin change; do not combine with native-only override narrowing |
| `lib/wine/i386-windows/dxgi.dll` | present, SHA256 `7df8cdf66e12a108410002abc77b01f70aa59b8a36a70fa0bc6ae3b056841319` | present, SHA256 `1bcba37120961191e27c5aa6f0c104dba2cd84dc08dd06e6ff3bb2436403606e` | expected Wine builtin change; Nidhogg 2 gate |

Anti-cheat/mscompatdb internal comparison:

| Surface | Working Wine 11.5 install | Wine 11.9 bundle asset | Rebuild requirement |
| --- | --- | --- | --- |
| `lib/wine/x86_64-unix/mscompatdb.so` | present, SHA256 `3d08163039ec14b6acc5ebd6aa02e5c5776e3dcd38e13f2ac512611f4025be84` | present, SHA256 `a90138f3b1c5ac7cc08fdd7f85aac8afe435423db9f98efd9171f6c303cd4739` | expected anti-cheat surface change; must be symbol-probed |
| `lib/wine/x86_64-unix/mscompatdb.dylib` | missing | present, SHA256 `5dda648f35863e3029f5d9ce550f1904a659771ef4b8a82c9a0dffa4393b18a5` | new 11.9 artifact; packaging must define whether `.so`, `.dylib`, or both are loaded |
| `nm -gU` hook evidence | 11.5 `ntdll.so` does not expose the MetalSharp hook contract; live logs still show `couldn't find KeServiceDescriptorTable` while games work | 11.9 `ntdll.so` exposes `_MetalSharpGetMscompatdbHookContract` and `_MetalSharpGetMscompatdbHookContractVersion`; selected `mscompatdb.so` and `mscompatdb.dylib` expose no public symbols under this probe | hook readiness must check `ntdll.so` contract plus runtime load behavior; do not treat mscompatdb file presence as success |
| Runtime log behavior | Nidhogg/Schedule/Subnautica all log `mscompatdb:error: couldn't find KeServiceDescriptorTable` and still work | not yet parity tested | hook readiness must be a separate probe/gate from game launch parity |

Critical internal mismatches found by file-list comparison:

- Wine 11.9 asset is missing `wine/bin/metalsharp-wine`.
- Wine 11.9 asset is missing `wine/lib/wine/x86_64-unix/winemetal.so`.
- Wine 11.9 asset is missing `wine/lib/wine/x86_64-windows/winemetal.dll`.
- Wine 11.9 asset is missing `wine/lib/wine/i386-windows/winemetal.dll`.
- Wine 11.9 asset is missing the working install's `.dxvk` and `.wine-builtin` backup filenames under `lib/wine`, including `d3d11.dll.dxvk`, `d3d11.dll.wine-builtin`, and `winevulkan.dll.115`.
- Wine 11.9 asset adds `wine/lib/wine/x86_64-unix/mscompatdb.dylib`.
- Both the working 11.5 install and raw 11.9 asset carry Vulkan ICD JSON that points at `/Users/alexmondello/.metalsharp/runtime/wine/lib/libMoltenVK.dylib`, but that file is absent in the inspected roots; isolated 11.9 candidates must localize the ICD path before live proof, otherwise Subnautica/Schedule evidence can accidentally borrow the installed runtime.
- Wine 11.9 asset adds headers under `include/wine/...`, which are useful for building but do not prove runtime parity.

Internal rebuild contract:

1. Build or package Wine 11.9 into the same final runtime root shape as 11.5.
2. Restore `bin/metalsharp-wine` or update and test every code path that chooses it.
3. Pre-bind or deploy `winemetal.so`/`winemetal.dll` into the locations the 0.33.27 live games actually loaded.
4. Keep `lib/dxmt` as the source-of-truth payload, but do not assume route loaders search it directly.
5. Preserve `etc/dxmt.conf`, Vulkan ICD paths, and `mscompatdb_rules.toml`.
6. Treat `mscompatdb.dylib` as a new 11.9 anti-cheat artifact that needs an explicit load/symbol contract.
7. Only after the internal runtime map matches the 11.5 contract should Wine 11.9 be tested against Nidhogg 2, Schedule I, and Subnautica BZ.

## Regression Map

### 1. Wine Steam Attach vs Relaunch Changed First

Commit `c95fd904812190809748feae0dacd8fcd8584c79` introduced protected handoff routing. It changed `/steam/launch-game` so anti-cheat-detected games route through `launch_game_via_steam_with_protected_env`.

Initial behavior stopped and relaunched Wine Steam to inherit env. That explains the observed regression where the app relaunched Steam instead of attaching to the existing Wine Steam process.

Commit `10aeada4004fc9918e55f5289896cdb4d3c01634` removed the destructive stop/restart path, but left a split:

- If Wine Steam is not running, Steam can inherit route env.
- If Wine Steam is already running, only the transient `steam://run/<appid>` helper gets env.

Any pipeline requiring route env to be inherited by the already-running Steam client remains suspect.

### 2. Runtime Migration Switched the Substrate Too Early

Commit `6e235de53e717f106d4b61e2f2e29f00e21bcba3` changed installer/runtime language toward Wine 11.9 and mscompatdb hook support.

Commit `14a5cb5a3c70a8a9cf1eb947427aff2f89b14e04` bumped migration schema and forced runtime repair/migration for Wine 11.9.

This changed too much at once:

- Wine version
- runtime install source selection
- prefix repair/migration behavior
- mscompatdb hook expectations
- launch handoff evidence

Wine 11.9 should have been introduced as a 1:1 Wine 11.5 runtime replacement first, before anti-cheat hooks or route semantics changed.

### 3. DXMT Winedll/Unix Bridge Model Changed Under Working Games

Commit `e2dff524f35604164ca5ab7f99bcd7b31a8f732e` introduced the most suspicious DXMT contract change.

`v0.33.27` deploys `winemetal.dll` beside games for M10/M11/M12 and uses `=n,b` overrides. Current `v0.33.27` pipeline definitions show this in `app/src-rust/src/mtsp/engine.rs`:

- M12 deploys `d3d12.dll`, `d3d11.dll`, `dxgi.dll`, `d3d10core.dll`, `winemetal.dll`
- M11 deploys `d3d11.dll`, `dxgi.dll`, `d3d10core.dll`, `winemetal.dll`
- M10 deploys Wine D3D10 DLLs plus DXMT `d3d11.dll`, `dxgi.dll`, `d3d10core.dll`, `winemetal.dll`
- M9 deploys Wine runtime `d3d9.dll` for x86_64 and i386

The post-11.9 branch changed M10/M11/M12 to remove game-local `winemetal.dll`, use `winemetal=n,b`, copy `winemetal.dll` into prefix `system32`, copy `winemetal.so` into Wine `x86_64-unix`, and remove game-local `winemetal.*`.

That is a real route contract change, not just a Wine update. It is high risk for the exact symptoms we saw: black/no-window Nidhogg, DXMT uncertainty, and launch paths that appear to render through the wrong split process.

### 4. Native-Only DirectX Overrides Narrowed the Loader Path

Commit `c1dd81285c32fd6bdedf9b6946f3c2f2c6c6aba2` moved DXMT routes toward native-only DirectX overrides:

- Post branch M12: `d3d12,dxgi,d3d11,d3d10core=n;winemetal=n,b`
- Post branch M11: `dxgi,d3d11,d3d10core=n;winemetal=n,b`
- v0.33.27 M12: `d3d12,dxgi,d3d11,d3d10core=n,b`
- v0.33.27 M11: `dxgi,d3d11,d3d10core=n,b`

The working Nidhogg process loaded builtin i386 Wine/DXMT DLLs from `lib/wine/i386-windows`, so native-only narrowing must be treated as a possible break in 32-bit routing and WoW64 thunk loading.

### 5. M9/M10 DXMT Config Was Narrowed Away

Commits `230c2ec517937945849257646d3b5deeb3f7b6d5` and `c95024d9c03552035750c2f861f57d7e0c44efba` scoped DXMT config to M11/M12.

The live Nidhogg M9 control log proves M9 still finds and uses `~/.metalsharp/runtime/wine/etc/dxmt.conf`. Removing or narrowing `DXMT_CONFIG_FILE`/DXMT config behavior from M9/M10 is a direct suspect for Nidhogg 2 and other 32-bit/D3D9-family regressions.

### 6. Unity Launch Args Churned During Schedule I/Subnautica Work

Commits `f548c5eac3258dc1d24852799a6afadfb8b70522`, `4e3afdea6895340d1cb46a123f7ba560a1cc3557`, and `610411d297140e15718c4639a0514d51d5fee4c1` changed Unity renderer args:

- add `-force-d3d12` for explicit M12
- broaden to M12 Unity launches
- later gate away from configured M10/M11

This is likely related to Schedule I and Subnautica BZ ambiguity. Wine 11.9 rebuild must preserve configured route authority: M11 must not accidentally force D3D12, and M12 must not be inferred from Unity alone.

## Code Yin/Yang Map

`git diff v0.33.27..main` is empty after the rollback, so the authoritative code comparison is:

- Yin: current `main`, tree-equivalent to `v0.33.27`
- Yang: `backup/main-before-v0.33.28-reset-20260521T223249Z` at `797db720c852b7f46356fa2541fb4dac81afcc69`

There is a second backup ref, `backup/main-before-v0.33.27-reset-20260521T225540Z`, at `e16b754eb770fdabc4810b371839d1d42c11cf70`. That ref is not the full regression tip. Its tree is identical to `c2eb975907cc5e285463ce08989a36d8b55f9412` (`Bump version to 0.33.28`), which is already after the Wine 11.9 promotion commit `6e235de53e717f106d4b61e2f2e29f00e21bcba3`. Treat `e16b754` as an intermediate rollback checkpoint, not as "pre-Wine-11.9" evidence.

Code surfaces that changed between yin and yang:

| Area | Yin behavior | Yang behavior | Evidence | Rebuild rule |
| --- | --- | --- | --- | --- |
| Bundle source selection | installer can install the 11.5-shaped runtime | installer prefers `wine-11.9`, then `wine-11.5`, then `wine` | commit `6e235de`, `app/src-rust/src/installer.rs::install_metalsharp_bundle` | keep 11.9 source selection, but only if final extracted shape matches the 11.5 runtime contract |
| Setup dependency contract | setup language describes Wine 11.5 runtime | setup language describes Wine 11.9, DXMT/DXVK, WoW64, and mscompatdb hook contract | commit `6e235de`, `app/src-rust/src/setup.rs::dependencies` | update text only after internal layout and hook readiness are true |
| Runtime migration | schema version `2` | schema version `3`; migration forced when current schema is older | commit `14a5cb5`, `app/src-rust/src/migrate.rs::MIGRATE_SCHEMA_VERSION` and `migration_needed_for_setup` | do not force prefix/runtime migration until 11.9 parity is proven |
| Migration restore | narrower restore behavior | panic handling and restore behavior changed for `prefix-steam`, `games`, `sharp-library`, `bottles`, and `compatdata` | commit `10aeada`, `app/src-rust/src/migrate.rs` | test restore behavior separately from Wine bump |
| Steam handoff route | `/steam/launch-game` launches through normal bottle/pipeline path | anti-cheat recipes route through protected Steam handoff | commit `c95fd90`, `app/src-rust/src/main.rs::route`, `/steam/launch-game` | keep protected handoff anti-cheat-only and off the parity path |
| Steam attach/relaunch | Wine Steam remains the background identity process | early yang stopped/relaunched Wine Steam for env inheritance; later yang stopped relaunching but env only reaches transient helper if Steam is already running | commits `c95fd90`, `10aeada`, `app/src-rust/src/steam.rs::launch_game_via_steam_with_env_options` | preserve 0.33.27 attach semantics; never rely on already-running Steam inheriting new env |
| Game pid accounting | launch state records process directly | delegated Steam URL helper no longer counted as game pid | commit `63867b1`, `app/src-rust/src/bottles.rs::set_launch_delegated` | keep this diagnostic distinction, but do not let it mask missing real game pids |
| DXMT config env | DXMT launcher sets `DXMT_CONFIG_FILE` for DXMT routes | cache/env contract expanded with `DXMT_CONFIG`, `DXMT_SHADER_CACHE_PATH`, `DXMT_PIPELINE_CACHE_PATH`, `MTL_SHADER_CACHE_DIR`, and logs | commit `c01180f`, `app/src-rust/src/mtsp/launcher.rs` | keep added diagnostics/cache env only if M9/M10 visibility remains intact |
| M9/M10 config visibility | M9 can still find `~/.metalsharp/runtime/wine/etc/dxmt.conf` during live Nidhogg 2 run | `dxmt_config_applies` narrowed to `M11 | M12`; recipe asset requirement removed from M9/M10 | commits `230c2ec`, `c95024d`, `app/src-rust/src/mtsp/launcher.rs`, `app/src-rust/src/mtsp/recipe.rs` | restore/keep M9 config visibility for parity |
| Winemetal deployment | M10/M11/M12 deploy `winemetal.dll` game-local from `lib/dxmt/x86_64-windows` | game-local `winemetal.dll` removed; runtime/prefix binding added | commit `e2dff52`, `app/src-rust/src/mtsp/engine.rs`, `app/src-rust/src/mtsp/launcher.rs::ensure_dxmt_winemetal_runtime` | keep game-local deployment during 11.9 parity; test relocation later |
| Unix WineMetal bridge | route depends on existing runtime/game-local resolution | copies `lib/dxmt/x86_64-unix/winemetal.so` into `lib/wine/x86_64-unix` and sets `DXMT_WINEMETAL_UNIXLIB=winemetal.so` | commits `e2dff52`, `2f0d9c6`, `app/src-rust/src/mtsp/launcher.rs` | make binding explicit in logs and prove with `lsof` |
| DLL overrides | M12/M11/M10 use DirectX `=n,b` overrides | DirectX overrides narrowed to native-only `=n`; `winemetal=n,b` added | commit `c1dd812`, `app/src-rust/src/mtsp/engine.rs` | keep `=n,b` for first 11.9 parity; native-only is a later experiment |
| WINEDLLPATH | no central DXMT dll path injection for this change | `mtsp/launcher.rs::dxmt_wine_dll_path` adds `WINEDLLPATH` | commit `c1dd812`, `app/src-rust/src/mtsp/launcher.rs` | useful diagnostic/control only after route parity |
| Route selection | D3D12 PE routes to M12 if 64-bit; D3D11 routes to M11; D3D9 routes to M9; Unity/directory heuristics prefer M11 | mostly stable route detection, but configured pipeline is consulted for launch args | `app/src-rust/src/mtsp/rules.rs`, `app/src-rust/src/mtsp/recipe.rs::launch_args_for_recipe` | preserve route authority; do not infer M12 from Unity alone |
| Unity args | no broad force-D3D churn in first working contract | screen defaults, `-force-d3d11`, and `-force-d3d12` churn across M10/M11/M12 | commits `f548c5e`, `03c0826`, `4e3afde`, `610411d` | keep Unity arg changes out of first Wine 11.9 parity build |
| Auto launch args | auto route launches selected pipeline | `/game/launch-auto` can pass request-supplied args through `launch_with_pipeline_and_args` | `app/src-rust/src/main.rs` | useful later, but parity runs should use empty args unless game-specific evidence requires otherwise |

The code map matches the runtime evidence: yang moved several loader and launch contracts at the same time as the Wine version. The rebuild must therefore split "Wine 11.9 internals" from "MetalSharp route behavior changes" and prove each layer separately.

Commit replay guidance:

- Do not replay merge commit `797db720c852b7f46356fa2541fb4dac81afcc69` as a unit.
- Do not cherry-pick `e2dff524f35604164ca5ab7f99bcd7b31a8f732e` as-is. It mixed WineMetal deployment model changes into the same regression cluster.
- Do not cherry-pick the mscompatdb/protected-handoff cluster as-is. Commits `c95fd904812190809748feae0dacd8fcd8584c79`, `6e235de`, `8e83923`, `ef3377b`, and `10aeada` must be rebuilt as separately gated behavior.
- Replay only the clean observability subset directly: `65032f7`, `168d235`, `ce2c008`, `1148f57`, `cfade4e`, `f654c10`, `1c0b788`, `38fed0a`, `d269d74`, `e796968`, `1e54117`, `ddba26f`, `e3c2a63`, `557cb14`, `05d7fd9`, `a514813`, `5e9a23c`, and `1683b79`.
- Reimplement mixed evidence/behavior commits manually instead of replaying them: `df728e3`, `d86fd03`, `2644948`, `63867b1`, `ab719e3`, `b5e4e28`, `f9aa3bd`, and `610411d`.
- Rebuild route parsing and custom launch args next, but preserve configured route authority.
- Rebuild DXMT file deployment for direct/non-Steam launches before touching Steam handoff.
- Rebuild cache/config next, and keep the live-proven M9 config visibility intact.
- Add Steam env propagation only after direct launch proof.
- Delay protected Steam handoff and syscall mutation until all normal M9/M11/M12 controls pass.
- Replay native D3D/DXGI compatibility patches only after launch plumbing is stable, one proof loop per route.

## Commit Replay Ledger

This ledger is intentionally stricter than normal cherry-pick guidance. The next Wine 11.9 branch should not replay the post-0.33.27 history in chronological order. Each commit must be either replayed as diagnostics, rebuilt manually in a smaller pass, quarantined until live parity passes, or ignored as version-only release metadata.

Classification meanings:

- `safe-diagnostic`: may be replayed early if it does not change route, runtime, migration, or Steam handoff behavior.
- `rebuild-manually`: useful intent, but must be reimplemented in a narrower patch because the original commit couples too many behaviors.
- `quarantine-until-parity`: do not apply until the three Wine 11.9 control games pass with 0.33.27 semantics.
- `never-as-is`: known regression cluster; only salvage small pieces after new tests and proof exist.
- `version-only`: do not replay while rebuilding; create a fresh version/tag only after gates pass.

| Commit | Subject | Classification | Reason |
| --- | --- | --- | --- |
| `df728e3` | Diagnose D3D12 ordinal and EAC evidence targets | `rebuild-manually` | Useful PE/export probing, but also touches route recipe behavior; split evidence from route logic. |
| `65032f7` | Track direct game anti-cheat crash evidence | `safe-diagnostic` | Evidence endpoint/log parsing only; good early observability. |
| `c95fd90` | Add protected Steam handoff probes | `never-as-is` | Couples protected Steam handoff, PID tracking, backend routes, and Steam process behavior. |
| `168d235` | Add mscompatdb hook surface diagnostics | `safe-diagnostic` | Diagnostic surface only; keep behind read-only evidence endpoints. |
| `d86fd03` | Prepare mscompatdb dylib parity | `rebuild-manually` | Good parity idea, but runtime/dylib assumptions must be rebuilt against actual 11.9 assets. |
| `2644948` | Define mscompatdb hook contract target | `rebuild-manually` | Keep the contract design, but revalidate ABI against actual Wine 11.9 before hook phase. |
| `070b27e` | Seed Wine mscompatdb contract export | `quarantine-until-parity` | Wine source hook belongs after runtime parity and symbol proof. |
| `6e235de` | Promote Wine 11.9 runtime hook surface | `never-as-is` | Mixes installer/setup/migration/runtime promotion with hook source changes. |
| `ce2c008` | Format mscompatdb hook sources | `safe-diagnostic` | Formatting-only for hook sources; harmless if retained with later hook sources. |
| `1148f57` | Tighten mscompatdb readiness checks | `safe-diagnostic` | Readiness checks are useful if they stay non-mutating and distinguish file presence from hook readiness. |
| `63867b1` | Avoid recording Steam handoff pid as game pid | `rebuild-manually` | Correct idea, but tied to Steam handoff semantics; replay only after attach behavior is redesigned. |
| `8e83923` | Restore mscompatdb syscall table protections | `quarantine-until-parity` | Low-level hook mutation; needs isolated Wine 11.9 proof first. |
| `14a5cb5` | Force migration for Wine 11.9 runtime schema | `never-as-is` | Forced migration is exactly the state disruption that regressed working installs. |
| `ab719e3` | Polish migration window and handoff evidence | `rebuild-manually` | UI/evidence pieces may help, but migration/handoff coupling needs separation. |
| `ef3377b` | Gate mscompatdb mutation and exported contract checks | `never-as-is` | Still introduces gated mutation into runtime path before parity is proven. |
| `cfade4e` | Read PE export and import directories separately | `safe-diagnostic` | Parser improvement; useful for evidence and patch validation. |
| `c2eb975` | Bump version to 0.33.28 | `version-only` | Release metadata must be regenerated after gates, not replayed. |
| `875004e` | Merge pull request #98 from aaf2tbz/codex/eac-proof-target-filtering | `never-as-is` | Merge commit; replay selected children only. |
| `10aeada` | Fix postinstall Steam handoff and migration restore | `never-as-is` | Postinstall Steam relaunch/restore logic is implicated in broken Steam attachment behavior. |
| `3eff2f4` | Bump version to 0.33.29 | `version-only` | Release metadata must be regenerated after gates, not replayed. |
| `3dacbe7` | Merge pull request #103 from aaf2tbz/codex/fix-postinstall-steam-migration | `never-as-is` | Merge commit; replay selected children only. |
| `e2dff52` | Bind DXMT winemetal through Wine runtime | `never-as-is` | Core DXMT/WineMetal binding changed too much at once and hit M9/M11 regressions. |
| `f8dfa3f` | Record DXMT DXGI private device export patch | `quarantine-until-parity` | Patch asset may matter, but only after clean DXMT parity build. |
| `c1dd812` | Force DXMT routes to native DirectX DLLs | `never-as-is` | Forced native overrides can break working M9/M11 routing and cache behavior. |
| `2f0d9c6` | Fix DXMT winemetal Unix bridge loading | `quarantine-until-parity` | Probably needed for 11.9, but must be tested in isolated candidate first. |
| `f548c5e` | Tighten M12 Unity launch probes | `quarantine-until-parity` | M12 launch behavior should not land before M9/M11 parity gates pass. |
| `03c0826` | Harden Unity DXMT launch defaults | `quarantine-until-parity` | Launch default changes are risky until route authority is proven. |
| `f654c10` | Add D3D12 surface extraction diagnostics | `safe-diagnostic` | Standalone diagnostic script/docs; good to keep. |
| `bd3cb30` | Expand D3D12 runtime compatibility | `quarantine-until-parity` | Runtime ABI/surface expansion; test after base Wine/DXMT launch parity. |
| `d34f964` | Improve D3D12 descriptor object behavior | `quarantine-until-parity` | D3D12 implementation change; not part of first Wine 11.9 bootstrapping. |
| `8f3ec06` | Add D3D12 resource allocation info | `quarantine-until-parity` | ABI/runtime expansion; hold until M12 phase. |
| `840f7e7` | Add DXGI2 D3D12 swapchain path | `quarantine-until-parity` | Swapchain implementation risk; hold until D3D12 pass. |
| `5fc0edb` | Return D3D12 swapchain backbuffers | `quarantine-until-parity` | Presentation/runtime behavior; needs live M12 proof. |
| `82e46e0` | Stabilize M12 runtime contract | `quarantine-until-parity` | M12 contract work belongs after M9/M11 parity. |
| `fe3164a` | Make D3D12 descriptor copies overlap-safe | `quarantine-until-parity` | Likely valid fix, but still D3D12 runtime mutation. |
| `7ae7ce7` | Report D3D12 capabilities conservatively | `quarantine-until-parity` | Capability reporting can affect game decisions; prove later. |
| `1c0b788` | Log direct MTSP runtime contracts | `safe-diagnostic` | Logging-only; useful for future rebuild verification. |
| `38fed0a` | Format D3D12 feature checks | `safe-diagnostic` | Test formatting only. |
| `d269d74` | Log D3D12 compute pipeline evidence | `safe-diagnostic` | Logging-only in D3D12 path; acceptable as diagnostics. |
| `e796968` | Log D3D12 compute dispatch evidence | `safe-diagnostic` | Logging-only. |
| `1e54117` | Log D3D12 draw execution evidence | `safe-diagnostic` | Logging-only. |
| `194c66f` | Preserve swapchain drawable presentation | `quarantine-until-parity` | Presentation behavior change; validate in M12 pass. |
| `ddba26f` | Log D3D12 feature query decisions | `safe-diagnostic` | Logging-only. |
| `e3c2a63` | Track DXGI present count | `safe-diagnostic` | Counter/test evidence; useful for live proof. |
| `4e3afde` | Make explicit M12 Unity launches force D3D12 | `quarantine-until-parity` | Route-forcing must wait until base M11/M12 split is proven. |
| `557cb14` | Route DXMT D3D12 traces per launch | `safe-diagnostic` | Trace file routing is good observability if non-invasive. |
| `05d7fd9` | Harden DXMT patch preflight | `safe-diagnostic` | Patch preflight/docs are useful before applying anything. |
| `c01180f` | Harden DXMT launch cache contract | `never-as-is` | Cache contract changes are implicated in shader-cache regression; rebuild in a smaller route-cache pass. |
| `b5e4e28` | Capture launcher installer evidence | `rebuild-manually` | Evidence is useful, but original touches launcher/install surfaces too broadly. |
| `a514813` | Fix EAC Proton asset evidence detection | `safe-diagnostic` | Evidence detection fix; safe if read-only. |
| `f9aa3bd` | Preserve DXMT config during contract repair | `rebuild-manually` | Correct direction, but should be rebuilt around explicit M9/M11/M12 config ownership. |
| `610411d` | Gate Unity D3D12 launch arg by configured route | `rebuild-manually` | Good principle, but reimplement after route model is cleaned up. |
| `e80ee5b` | Apply Steam identity to Wine fallback launches | `quarantine-until-parity` | Steam identity/env propagation can affect attach vs relaunch behavior. |
| `5e9a23c` | Cover expanded DXGI factory surface | `safe-diagnostic` | Test coverage only. |
| `230c2ec` | Scope DXMT config contract to M11 M12 | `never-as-is` | Explicitly narrows DXMT config away from M9, conflicting with working Nidhogg 2 evidence. |
| `51e1417` | Preserve D3D12 device ABI method order | `quarantine-until-parity` | ABI fix may be valid, but belongs in later D3D12 pass. |
| `c95024d` | Scope DXMT config recipe asset to M11 M12 | `never-as-is` | Same M9 exclusion risk as `230c2ec`; likely breaks M9 DXMT-family path. |
| `1683b79` | Cover DXGI factory implementation surface | `safe-diagnostic` | Test/coverage around DXGI surface; safe as evidence. |
| `79cc6fb` | Bump version to 0.33.30 | `version-only` | Release metadata only. |
| `797db72` | Bind DXMT winemetal through Wine runtime | `never-as-is` | PR merge/rollup with mixed runtime, launcher, D3D12, patch, and version changes. |

The first implementation branch should start with only the `safe-diagnostic` subset plus the new candidate preparation/audit scripts in this document. The `rebuild-manually` set should be applied as small new patches in the pass order below. The `quarantine-until-parity` set is allowed only after the readiness audit can point to a passing live suite. The `never-as-is` set should never be cherry-picked directly.

The concrete branch execution runbook is tracked separately in `docs/wine-119-implementation-runbook.md`. Use that file for pass-by-pass commands, allowed write scopes, final release checklist, and the exact point where live controls must be run.

## Wine 11.9 Rebuild Design

### Phase 0: Freeze the Working 11.5 Contract

Before changing Wine, create a machine-readable contract from the live 0.33.27 runtime:

- runtime tree manifest for `~/.metalsharp/runtime/wine`
- hashes for `bin/wine`, `bin/wineserver`, `bin/metalsharp-wine`
- hashes for `lib/wine/{x86_64-unix,x86_64-windows,i386-windows}`
- hashes for `lib/dxmt/{x86_64-unix,x86_64-windows,i386-windows}` if present
- `file` and `otool -L` output for `wine`, `wineserver`, `ntdll.so`, `win32u.so`, `mscompatdb.so`, `winemetal.so`, `libMoltenVK.1.dylib`
- launch contract logs for Nidhogg 2, Schedule I, and Subnautica BZ

This is the yin baseline.

### Phase 1: Build Wine 11.9 as a Layout-Compatible Runtime

Use the recovered `wine-11.9` bundle as the candidate, but rebuild/package it to match Wine 11.5 structure:

- `bin/wine`
- `bin/wineserver`
- `bin/metalsharp-wine`
- `lib/wine/x86_64-unix`
- `lib/wine/x86_64-windows`
- `lib/wine/i386-windows`
- `lib/dxmt/x86_64-unix`
- `lib/dxmt/x86_64-windows`
- `lib/dxmt/i386-windows` when 32-bit DXMT assets exist
- `etc/dxmt.conf`
- `etc/vulkan/icd.d/MoltenVK_icd.json`
- Gecko/Mono redists where installer expects them

The installer may prefer `wine-11.9`, but the extracted contents copied into `~/.metalsharp/runtime/wine` must present the same final shape as 11.5.

### Phase 2: Parity Launches Before Anti-Cheat Hooks

Do not enable mscompatdb hook mutation yet.

Run Wine 11.9 with 0.33.27 launch semantics:

- Keep M9 config visible.
- Keep M9 as `d3d9=n,b`.
- Keep M11/M12 `=n,b` DirectX overrides until parity is proven.
- Keep game-local `winemetal.dll` deployment for routes that previously used it.
- Keep per-route cache paths:
  - `~/.metalsharp/shader-cache/<route>/<appid>/`
  - `~/.metalsharp/pipeline-cache/<route>/<appid>/`
- Keep `SteamAppId` and `SteamGameId` in Wine Steam handoff env.
- Keep Wine Steam attach behavior: do not stop/relaunch Steam for game launch env.

Required control games:

- Nidhogg 2: must run under M9 and load i386 `d3d11.dll`, `dxgi.dll`, `winemetal.dll`, Unix `winemetal.so`, and shader cache under `m9/535520`.
- Schedule I: must run under M11, load Unix `winemetal.so`, `libMoltenVK.1.dylib`, use `dxmt.conf`, and keep shader cache under `m11/3164500`.
- Subnautica Below Zero: must run under M11 and prove actual DX11 to DXMT runtime use by loading `d3d11.dll`, `dxgi.dll`, `winemetal.dll`, Unix `winemetal.so`, `libMoltenVK.1.dylib`, and shader cache under `m11/848450`.

### Phase 3: Add Anti-Cheat Hook Surface Behind Gates

Only after parity:

- Add `MscompatdbHookContract.h`.
- Add the ntdll/mscompatdb hook exports.
- Require `nm`/symbol evidence that `ntdll.so` exports the expected contract.
- Require a runtime probe endpoint to report `hook_surface_ready`.
- Keep mutation and dylib prep gated to setup/migration context.
- Do not treat `mscompatdb.so loaded` as success. Current live 0.33.27 logs show `mscompatdb:error: couldn't find KeServiceDescriptorTable` while Nidhogg still works; presence alone is not proof.

### Phase 4: Revisit the Safer Winedll Binding Model Separately

The later winemetal relocation may still be desirable, but it must be a separate PR/phase after Wine 11.9 parity:

- Add prefix `system32/winemetal.dll` binding only behind a feature flag or route-level experiment.
- Keep game-local deployment available as fallback.
- Log which model is active for every launch.
- Test Nidhogg 2, Schedule I, and Subnautica BZ under both models.

## Required Tests and Gates

Repo-level tests:

- MTSP route tests for M9/M10/M11/M12 overrides.
- M9 test must assert DXMT config remains visible.
- M11/M12 tests must assert DXMT config, cache env, and DirectX overrides.
- Steam handoff tests must assert no Steam relaunch when Wine Steam is already running.
- Migration tests must not destroy prefix/game installs without an explicit runtime schema transition.
- mscompatdb tests must distinguish loaded vs hook-ready.

Runtime probes:

- `wine --version`
- `wineserver --version`
- `file` on Wine binaries and DXMT binaries.
- `otool -L` on Unix dylibs/so files.
- `nm -gU` on `ntdll.so`, `mscompatdb.so`, and host runtime dylib.
- `lsof -p <game_pid>` for loaded D3D/DXMT/MoltenVK/Metal/cache files.
- Launch log contract emitted before Wine output:
  - pipeline
  - prefix
  - runtime root
  - working directory
  - exe
  - args
  - `WINEDLLOVERRIDES`
  - runtime library env
  - `DXMT_CONFIG_FILE`
  - shader/pipeline cache paths
  - winemetal binding model
  - Steam identity mode

Release gates:

- Do not bump/tag until Nidhogg 2, Schedule I, and Subnautica BZ route evidence is captured.
- Do not publish a Wine 11.9 release whose only proof is installer success.
- Do not combine Wine 11.9, mscompatdb mutation, winemetal relocation, Steam handoff changes, and Unity renderer changes in one release again.

## Immediate Next Implementation Path

1. Create a Wine 11.9 parity branch from `main`/`v0.33.27`.
2. Add diagnostics/manifests first, without changing runtime behavior.
3. Package Wine 11.9 into the 11.5-compatible runtime layout.
4. Prepare two M9 candidate variants:
   - 11.9 bundle plus `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll`
   - 11.9 bundle plus borrowed 11.5 i386 `winemetal.dll` as a compatibility fallback experiment
5. Install candidates only into a disposable `~/.metalsharp-119-parity` or alternate runtime root first.
6. Run the three control games with the exact 0.33.27 route semantics.
7. Only after parity, cherry-pick or reimplement anti-cheat hook commits in isolation.
8. Leave winemetal relocation and native-only override narrowing for a later isolated experiment.

## Pass-by-Pass Rebuild Matrix

The next Wine 11.9 attempt should be built as multiple small passes. Each pass has one allowed behavior change family and one proof bundle. If a pass fails, revert only that pass instead of chasing all regressions together.

| Pass | Allowed changes | Explicitly not allowed | Proof required before next pass |
| --- | --- | --- | --- |
| A: Baseline diagnostics on 11.5 | add manifest generation, launch contract logging, route/cache/env introspection, and `lsof` capture helpers | no Wine version change, no route behavior change, no migration bump | 11.5 controls still launch; logs include pipeline, runtime root, `WINEDLLOVERRIDES`, config/cache paths, winemetal binding model, and Steam identity mode |
| B: Wine 11.9 asset packaging | install from `bundles/metalsharp_bundle.tar.zst`; normalize archive root `wine-11.9` into final runtime root `~/.metalsharp/runtime/wine`; regenerate/carry `bin/metalsharp-wine` | no protected Steam handoff, no winemetal relocation, no native-only DirectX overrides, no Unity arg changes | runtime manifest shows complete 11.9 file tree plus 11.5-compatible final paths; `wine --version` and `wineserver --version` return 11.9; `bin/metalsharp-wine` exists or all callers are updated |
| C1: 11.9 parity with explicit DXMT i386 WineMetal | run M9/M11/M12 routes using 0.33.27 overrides, config visibility, game-local deployment, and Steam attach semantics; source i386 WineMetal from `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll` | no mscompatdb hook mutation, no schema-forced prefix migration, no Steam handoff changes | Nidhogg 2, Schedule I, and Subnautica BZ match the control matrix below under Wine 11.9; i386 artifact provenance is logged |
| C2: 11.9 parity with borrowed 11.5 i386 fallback | only if C1 fails specifically on i386 WineMetal compatibility, test the borrowed 11.5 i386 `winemetal.dll` as a compatibility artifact | no release blessing without live M9 proof and an explicit compatibility rationale | Nidhogg 2 proves whether the 11.5 i386 bridge can safely pair with Wine 11.9, or the path is rejected |
| D: Anti-cheat hook surface | add hook headers/exports/probes and mscompatdb readiness reporting | no route behavior changes; no Steam relaunch for normal routes | symbol probe proves hook contract; runtime endpoint reports hook readiness; non-anti-cheat control games still match pass C |
| E: Protected Steam handoff | enable protected handoff only for explicit anti-cheat recipes | no global Steam launch path replacement | logs prove protected handoff is used only for opted-in games; already-running Wine Steam is not killed for normal routes |
| F: Winemetal relocation experiment | test prefix/system32 and runtime-bound `winemetal` model behind a feature flag | no removal of fallback game-local deployment | both binding models are logged; controls pass or fallback remains automatic |
| G: Native-only override experiment | test `=n` DirectX overrides and `winemetal=n,b` separately | no Unity arg inference or route remapping in same pass | controls pass under native-only, or experiment remains disabled |
| H: Unity launch args | add game/route-specific Unity args only with per-game proof | no global `-force-d3d12` based on Unity alone | Schedule I and Subnautica BZ remain M11 unless explicitly configured otherwise |

## Control Game Acceptance Matrix

These are hard gates for any release candidate that includes Wine 11.9. The evidence must come from fresh launch logs and fresh `lsof` captures from the candidate build.

Capture command:

- `scripts/capture-steam-game-proof.sh 535520 Nidhogg_2 /tmp/metalsharp-proof-nidhogg2-candidate`
- `scripts/capture-steam-game-proof.sh 3164500 'Schedule I' /tmp/metalsharp-proof-schedule-i-candidate`
- `scripts/capture-steam-game-proof.sh 848450 SubnauticaZero /tmp/metalsharp-proof-subnautica-bz-candidate`

The proof script writes:

- `summary.txt`
- `all-processes.txt`
- `matching-processes.txt`
- `game-pids.txt`
- latest copied `launch-*.log`
- `metalsharp-compatdata.json`
- `bottle.json`
- shader/pipeline cache listings
- `lsof-<pid>.txt` for each matching live game PID

If `game-pids.txt` is empty, the proof is log-only and does not satisfy a release gate. Candidate parity requires a live game PID plus `lsof` loaded-module evidence.

| Game | Route | Must prove | Must not happen |
| --- | --- | --- | --- |
| Nidhogg 2 `535520` | M9 / `d3d9_metal` | process `Nidhogg_2.exe`; `pipeline=M9`; `dxmt.conf` visible; i386 `d3d11.dll`, `dxgi.dll`, `winemetal.dll` loaded where the 0.33.27 control loaded them; Unix `winemetal.so`; shader cache under `~/.metalsharp/shader-cache/m9/535520` | black/no-window-with-music; missing real game pid; Steam relaunch; M9 config stripped; native-only override narrowing without proof |
| Schedule I `3164500` | M11 / `dxmt_metal` | process `Schedule I.exe`; `pipeline=M11`; `d3d11`/`dxgi` route active; Unix `winemetal.so`; `libMoltenVK.1.dylib`; shader cache under `~/.metalsharp/shader-cache/m11/3164500`; Unity crash handler can exist without counting as launch failure by itself | crash-to-dust after handoff; M12 forced by Unity; Steam relaunch; game pid replaced by Steam URL helper |
| Subnautica BZ `848450` | M11 / `dxmt_metal` | process `SubnauticaZero.exe`; `pipeline=M11`; `d3d11.dll`, `dxgi.dll`, `winemetal.dll`, Unix `winemetal.so`, `libMoltenVK.1.dylib`; shader cache under `~/.metalsharp/shader-cache/m11/848450` | ambiguous fallback rendering; split process with no loaded DXMT proof; M12 inference from Unity; missing cache activity |

Current log-only control captures from the 0.33.27 runtime:

- `/tmp/metalsharp-proof-nidhogg2-control/summary.txt`
- `/tmp/metalsharp-proof-schedule-i-control/summary.txt`
- `/tmp/metalsharp-proof-subnautica-bz-control/summary.txt`

These confirm the latest launch contracts from logs, but because the games were no longer running when captured, they correctly report `none; live lsof proof still required`. The earlier live `lsof` snapshots remain the stronger control evidence for this investigation.

## Runtime Manifest Requirements

Every Wine 11.9 build candidate should emit or archive a manifest with:

- command:
  - `scripts/runtime-manifest.sh ~/.metalsharp/runtime/wine /tmp/metalsharp-runtime-11.5-current`
  - `scripts/runtime-manifest.sh <candidate-runtime-root>/wine /tmp/metalsharp-runtime-11.9-candidate`
- normalized file list for final runtime root
- SHA256 for route-critical binaries:
  - `bin/wine`
  - `bin/wineserver`
  - `bin/metalsharp-wine`
  - `share/wine/wine.inf`
  - `etc/dxmt.conf`
  - `etc/mscompatdb_rules.toml`
  - `etc/vulkan/icd.d/MoltenVK_icd.json`
  - `etc/vulkan/icd.d/MoltenVK_x86_64_icd.json`
  - `lib/libMoltenVK.dylib`
  - `lib/dxmt/x86_64-windows/{d3d10core.dll,d3d11.dll,d3d12.dll,dxgi.dll,winemetal.dll}`
  - `lib/dxmt/x86_64-unix/winemetal.so`
  - `lib/wine/x86_64-unix/{winemetal.so,mscompatdb.so,mscompatdb.dylib,libMoltenVK.1.dylib}`
  - `lib/wine/x86_64-windows/{d3d11.dll,dxgi.dll,winemetal.dll}`
  - `lib/wine/i386-windows/{d3d11.dll,dxgi.dll,winemetal.dll}`
- `file` output for Wine executables and Mach-O dylibs
- `otool -L` output for `winemetal.so`, `mscompatdb.so`, `mscompatdb.dylib`, and `libMoltenVK.1.dylib`
- `otool -l` load-command output for Unix-side probes, so install names, `LC_RPATH`, and `@rpath` loader contracts are captured instead of inferred
- `nm -gU` hook probe output for `ntdll.so`, `mscompatdb.so`, and `mscompatdb.dylib`
- final installer source URL or artifact digest

The manifest script is `scripts/runtime-manifest.sh`. It is read-only against the runtime root and writes:

- `versions.txt`
- `files.txt`
- `file-count.txt`
- `critical-sha256.txt`
- `file.txt`
- `otool-L.txt`
- `otool-l.txt`
- `nm-gU.txt`
- `manifest.json`

Manifest comparison is handled by `scripts/compare-runtime-manifests.sh`:

- command:
  - `scripts/compare-runtime-manifests.sh /tmp/metalsharp-runtime-11.5-current /tmp/metalsharp-runtime-11.9-candidate /tmp/metalsharp-runtime-11.9-candidate/classified-diff.md`
- behavior:
  - classifies route-critical deltas instead of requiring byte-for-byte identity
  - allows expected Wine version changes
  - allows expected DXMT D3D12/DXGI/WineMetal bridge changes
  - allows expected anti-cheat hook-surface changes
  - requires wrapper/config/MoltenVK/shared DXMT files to match
  - allows Vulkan ICD JSON and `lib/libMoltenVK.dylib` to differ only as candidate-local binding repairs
  - marks unknown or route-critical missing files as release blockers

Candidate orchestration is handled by `scripts/prepare-wine119-parity-candidates.sh`:

- command:
  - `scripts/prepare-wine119-parity-candidates.sh /tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst /tmp/metalsharp-wine119-parity`
- behavior:
  - creates a fresh 11.5 baseline manifest
  - prepares `clean`, `dxmt32`, and `borrowed` Wine 11.9 runtime candidates under `/tmp/metalsharp-wine119-parity/candidates`
  - writes manifests under `/tmp/metalsharp-wine119-parity/manifests`
  - compares each candidate against the 11.5 baseline contract
  - writes `/tmp/metalsharp-wine119-parity/summary.md`
- default explicit i386 DXMT source:
  - `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll`
- override:
  - `METALSHARP_I386_WINEMETAL_SOURCE=/path/to/winemetal.dll`

Latest orchestration result:

| Candidate | Root | Manifest gate | Meaning |
| --- | --- | --- | --- |
| `clean` | `/tmp/metalsharp-wine119-parity/candidates/clean/wine` | fail | release 11.9 bundle lacks i386 `winemetal.dll` |
| `dxmt32` | `/tmp/metalsharp-wine119-parity/candidates/dxmt32/wine` | fail | i386 WineMetal exists but differs from 11.5 control; requires live M9 proof |
| `borrowed` | `/tmp/metalsharp-wine119-parity/candidates/borrowed/wine` | pass | manifest-complete by borrowing 11.5 i386 WineMetal; still requires live M9 proof before any release blessing |

Manifest pass is not a release gate. The orchestrator only proves runtime-shape readiness. Live control games remain mandatory.

Isolated parity-home installation is handled by `scripts/install-wine119-parity-home.sh`:

- command:
  - `scripts/install-wine119-parity-home.sh /tmp/metalsharp-wine119-parity/candidates/dxmt32/wine /tmp/metalsharp-home-wine119-dxmt32`
- behavior:
  - copies the selected candidate runtime into `<parity-home>/.metalsharp/runtime/wine`
  - writes `<parity-home>/activate-parity-env.sh`
  - writes `<parity-home>/proof-commands.sh`
  - writes `<parity-home>/runtime-manifest`
  - does not modify the working `~/.metalsharp`
- default state behavior:
  - runtime only; no Steam prefix, compatdata, bottles, games, or caches are copied
- optional state clone:
  - `METALSHARP_CLONE_USER_STATE=1 scripts/install-wine119-parity-home.sh ...`
  - copies `prefix-steam`, `compatdata`, `bottles`, `games`, `configs`, `shader-cache`, and `pipeline-cache` from the source MetalSharp home into the isolated parity home
  - rewrites active cloned bottle/compatdata/config manifests from the source MetalSharp home path to the parity MetalSharp home path
  - fails if any active cloned manifest still points back at the source MetalSharp home
- copy mode:
  - `METALSHARP_COPY_MODE=auto` prefers APFS clone-copy on macOS and falls back to ordinary copy
  - `METALSHARP_COPY_MODE=clone` requires APFS clone-copy and fails instead of silently making a full copy
  - this matters because the current `prefix-steam` source is about `20G` while the internal disk has about `8.4Gi` free
- size/copy evidence:
  - writes `<parity-home>/state-sizes.txt`
  - writes `<parity-home>/copy.log`
  - writes `<parity-home>/state-path-rewrite.log`
- safety rails:
  - the installer refuses to target the real `HOME` or the source MetalSharp home
  - parity homes must live under `/tmp` or `/private/tmp` unless `METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1` is explicitly set
  - copied `compatdata/*/logs` files are pruned so old 11.5 launch logs cannot contaminate Wine 11.9 proof summaries; empty log directories created by backend scans are harmless

Latest isolated install result:

- parity home: `/private/tmp/metalsharp-home-wine119-dxmt32`
- runtime: `/private/tmp/metalsharp-home-wine119-dxmt32/.metalsharp/runtime/wine`
- `wine --version`: `wine-11.9`
- `wineserver --version`: `Wine 11.9`
- cloned user state: `0`
- activation file: `/private/tmp/metalsharp-home-wine119-dxmt32/activate-parity-env.sh`
- proof commands: `/private/tmp/metalsharp-home-wine119-dxmt32/proof-commands.sh`

Latest cloned-state install result:

- command:
  - `METALSHARP_CLONE_USER_STATE=1 METALSHARP_COPY_MODE=clone scripts/install-wine119-parity-home.sh /tmp/metalsharp-wine119-parity/candidates/dxmt32/wine /tmp/metalsharp-home-wine119-dxmt32-state`
- parity home: `/private/tmp/metalsharp-home-wine119-dxmt32-state`
- runtime: `/private/tmp/metalsharp-home-wine119-dxmt32-state/.metalsharp/runtime/wine`
- cloned user state: `1`
- copy method: `clone`
- `wine --version`: `wine-11.9`
- `wineserver --version`: `Wine 11.9`
- active state path rewrite:
  - source MetalSharp home: `/Users/alexmondello/.metalsharp`
  - parity MetalSharp home: `/private/tmp/metalsharp-home-wine119-dxmt32-state/.metalsharp`
  - active stale manifest count before rewrite: `8`
  - active stale manifest count after rewrite: `0`
  - verified by `state-path-rewrite.log`
  - copied launch log directories after hardening: `0`
- source state sizes:
  - `prefix-steam`: `20G`
  - `compatdata`: `96K`
  - `bottles`: `64K`
  - `games`: `0B`
  - `shader-cache`: `12M`
- destination state sizes are recorded in `/private/tmp/metalsharp-home-wine119-dxmt32-state/state-sizes.txt`
- copy log: `/private/tmp/metalsharp-home-wine119-dxmt32-state/copy.log`

To test a candidate live without mutating the working runtime, launch the backend/app from a shell that has sourced the activation file. The current app code resolves most MetalSharp paths through `dirs::home_dir()/.metalsharp`, so the isolated run depends on `HOME=<parity-home>`.

Backend parity preflight is handled by `scripts/probe-wine119-parity-backend.sh`:

- command:
  - `scripts/probe-wine119-parity-backend.sh /private/tmp/metalsharp-home-wine119-dxmt32 /private/tmp/metalsharp-home-wine119-dxmt32/backend-probe-main03327`
- behavior:
  - starts `metalsharp-backend` with `HOME=<parity-home>`
  - uses a non-default port, default `9374`
  - probes `/status`, `/setup/dependencies`, `/runtime/host-abi`, and `/bottles/profiles`
  - records direct `wine --version` and `wineserver --version` from the parity runtime
  - stops the backend before exiting
- safety rails:
  - refuses the real `HOME` or source MetalSharp home as a parity target
  - refuses non-tmp parity homes unless `METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1` is set
- latest verified result after rebuilding the Rust backend from current `main`:
  - backend binary: `/Users/alexmondello/Dev/metalsharp/app/src-rust/target/release/metalsharp-backend`
  - backend `/status` version: `0.33.27`
  - parity runtime: `/private/tmp/metalsharp-home-wine119-dxmt32/.metalsharp/runtime/wine`
  - parity `wine --version`: `wine-11.9`
  - parity `wineserver --version`: `Wine 11.9`
  - probe summary: `/private/tmp/metalsharp-home-wine119-dxmt32/backend-probe-main03327/summary.txt`
- stale-binary warning:
  - before rebuilding, the existing release backend binary reported `0.33.30`
  - backend preflight evidence must therefore include `/status` version and should rebuild `app/src-rust` from current `main` before any release decision
- expected setup text caveat:
  - current `main` setup copy still describes Wine 11.5 even when the isolated runtime is Wine 11.9
  - this is not a runtime identity failure; setup text should only be updated after 11.9 parity passes
- latest cloned-state backend preflight:
  - probe summary: `/private/tmp/metalsharp-home-wine119-dxmt32-state/backend-probe-main03327-hardened/summary.txt`
  - backend `/status` version: `0.33.27`
  - parity `wine --version`: `wine-11.9`
  - parity `wineserver --version`: `Wine 11.9`
  - `/bottles/profiles` returns M9, M10, M11, and M12 profiles from the cloned-state parity home

Live control suite orchestration is handled by `scripts/run-wine119-live-control-suite.sh`:

- guarded command:
  - `METALSHARP_RUN_LIVE_GAMES=1 scripts/run-wine119-live-control-suite.sh /tmp/metalsharp-home-wine119-dxmt32 /tmp/metalsharp-live-controls-dxmt32`
- hard guard:
  - the script refuses to launch anything unless `METALSHARP_RUN_LIVE_GAMES=1` is set
- active-state guard:
  - before launching, the script reads the parity install report or `METALSHARP_PARITY_SOURCE_HOME`
  - source-home metadata is mandatory for cloned-state live runs
  - it refuses to continue if active cloned JSON/TOML/CONF/ENV manifests under bottles, compatdata, or configs still reference the source MetalSharp home
  - it requires the three control-game bottle and compatdata manifests to reference the parity MetalSharp home
  - it refuses to run if copied historical `compatdata/*/logs` directories are present
  - it refuses the real `HOME` or source MetalSharp home as a parity target
  - it refuses non-tmp parity homes unless `METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1` is set
- required parity-home state:
  - prepare the parity home with `METALSHARP_CLONE_USER_STATE=1 scripts/install-wine119-parity-home.sh ...`
  - the script refuses to run if `prefix-steam`, `compatdata`, `bottles`, or `games` are missing from the parity home
- launch sequence:
  - `535520` Nidhogg 2 with `launchMethod: "m9"`
  - `3164500` Schedule I with `launchMethod: "m11"`
  - `848450` Subnautica Below Zero with `launchMethod: "m11"`
- proof sequence:
  - waits after each launch, default `METALSHARP_WAIT_SECS=20`
  - runs `scripts/capture-steam-game-proof.sh` against the same isolated `METALSHARP_HOME` with `METALSHARP_REQUIRE_PARITY_HOME=1`
  - writes `summary.md`, launch JSON responses, backend logs, and per-game proof directories
- latest guard validation:
  - running without `METALSHARP_RUN_LIVE_GAMES=1` exits with code `2` and launches nothing
  - a fake stale parity home with active manifests pointing at `/Users/alexmondello/.metalsharp` is refused before backend/game launch
  - a fake parity home with no source metadata is refused before backend/game launch
  - `scripts/install-wine119-parity-home.sh ... /Users/alexmondello` is refused before runtime copy
  - `METALSHARP_REQUIRE_PARITY_HOME=1 scripts/capture-steam-game-proof.sh ...` without explicit `METALSHARP_HOME` is refused

This script is the release-candidate gate runner. It should be run first against the `dxmt32` parity home. The borrowed candidate should only be tested if `dxmt32` fails in a way that points specifically at the explicit DXMT i386 WineMetal artifact.

Live control verification is handled by `scripts/verify-wine119-live-control-suite.sh`:

- command:
  - `scripts/verify-wine119-live-control-suite.sh /tmp/metalsharp-live-controls-dxmt32`
- behavior:
  - requires parity runtime identity to report Wine 11.9
  - requires backend `/status` to report current-main version `0.33.27`
  - requires every launch JSON to report `ok: true` and the expected pipeline
  - requires each proof directory to contain a live game PID
  - requires Wine Steam PID snapshots before/after each launch and fails if a pre-existing `Steam.exe` PID disappears
  - when `METALSHARP_REQUIRE_PREEXISTING_WINE_STEAM=1` is used, starts Wine Steam before the controls and fails verification unless each control had a pre-existing Wine Steam PID to attach to
  - requires loaded-module/cache evidence for each control game from `summary.txt`
- expected gate:
  - `pass` only when all three control games satisfy the acceptance matrix
  - `fail` for log-only captures, wrong route, missing DXMT/WineMetal/MoltenVK evidence, missing cache evidence, or failed launch JSON
  - `fail` if an already-running Wine Steam process is killed/relaunched during a control-game launch

This verifier is the final local release gate before any Wine 11.9 tag bump. A green manifest or backend preflight is not enough.

App-facing route verification is handled by `scripts/audit-electron-launch-routes.mjs`. The Electron path is `LibraryView.vue` -> `window.metalsharpAPI.backendRequest` -> `RustBridge.request` -> backend HTTP. The audit mirrors the Library view routing decision and proves the control games resolve to `/steam/launch-game` from the renderer-facing model, including `auto` fallback behavior when library route metadata is missing.

- command:
  - `node scripts/audit-electron-launch-routes.mjs --base-url http://127.0.0.1:<port> --out-dir <probe>/electron-launch-routes`
- behavior:
  - reads `/steam/library`
  - probes `/mtsp/pipelines?appid=<control>` for backend recommended route context
  - checks Nidhogg 2, Schedule I, and Subnautica BZ
  - requires both renderer `auto` and explicit M9/M11 selections to resolve to `/steam/launch-game`

This plugs the previous proof gap where backend direct launches were covered but Electron's route selection could still fall through to `/game/launch-auto`.

Current M12 limitation: the first live gate intentionally targets the reported regressions: Nidhogg 2 M9, Schedule I M11, and Subnautica Below Zero M11. M12 remains a mapped rebuild surface, not a passed live surface. Add a dedicated M12 control before enabling D3D12/M12 behavior changes beyond this parity plan.

Non-live readiness auditing is handled by `scripts/audit-wine119-readiness.sh`:

- command:
  - `scripts/audit-wine119-readiness.sh /private/tmp/metalsharp-home-wine119-dxmt32-state /tmp/metalsharp-wine119-readiness-current`
- behavior:
  - checks that current source is still on the v0.33.27 rollback commit
  - verifies the installed 11.5 baseline still reports `wine-11.5` and still has i386 WineMetal
  - verifies the prepared `clean`, `dxmt32`, and `borrowed` Wine 11.9 candidates still report `wine-11.9`
  - checks the parity home for Wine 11.9 identity, source-home metadata, no active source-home references, no copied historical logs, and control manifests bound to the parity home
  - checks the latest guarded backend preflight status for backend version `0.33.27`
  - fails until a passing live control suite is supplied through `METALSHARP_LIVE_SUITE_DIR`
- latest result:
  - report: `/tmp/metalsharp-wine119-readiness-current/readiness.md`
  - failures: `1`
  - warning: `1`
  - only failure: no passing live control suite has been supplied
  - warning: HEAD is not exactly tagged `v0.33.27`, because `main` is on the rollback commit even though it is the intended v0.33.27 tree line

This audit is intentionally non-live. It proves the staged 11.9 testbed is clean enough to run controlled live tests, but it cannot bless a release candidate.

Candidate preparation for Pass B is handled by `scripts/prepare-wine119-candidate.sh`:

- command:
  - `scripts/prepare-wine119-candidate.sh /tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst /tmp/metalsharp-wine119-candidate/wine`
- behavior:
  - normalizes archive root `wine-11.9` into final runtime root shape
  - carries the current `bin/metalsharp-wine` wrapper when the bundle lacks one
  - binds x86_64 `winemetal.so` into `lib/wine/x86_64-unix/winemetal.so`
  - binds x86_64 `winemetal.dll` into `lib/wine/x86_64-windows/winemetal.dll`
  - writes `prepare-report.txt` and `missing-critical.txt` beside the candidate runtime root

Current Pass B dry run result:

- candidate root: `/tmp/metalsharp-wine119-candidate/wine`
- source bundle: `/tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst`
- `metalsharp-wine --version`: `wine-11.9`
- `wine --version`: `wine-11.9`
- `wineserver --version`: `Wine 11.9`
- candidate manifest: `/tmp/metalsharp-runtime-11.9-candidate`
- candidate file count after normalization: `3922`
- remaining critical missing file: `lib/wine/i386-windows/winemetal.dll`
- classified diff: `/tmp/metalsharp-runtime-11.9-candidate/classified-diff.md`
- classified diff gate: `fail`
- classified release blockers: `1`
- 11.9 `ntdll.so` hook symbols found:
  - `_MetalSharpGetMscompatdbHookContract`
  - `_MetalSharpGetMscompatdbHookContractVersion`

That means the recovered 11.9 bundle can be shaped into a runnable candidate root, but it is not release-ready for Nidhogg 2/M9 parity until the 32-bit `winemetal.dll` gap is resolved or proven unnecessary with a live control launch.

Current i386 WineMetal investigation:

- The release bundle contains no `wine-11.9` i386 `winemetal.dll` under either `lib/wine/i386-windows` or `lib/dxmt/i386-windows`.
- The 11.5 control file is `~/.metalsharp/runtime/wine/lib/wine/i386-windows/winemetal.dll`.
- `file` identifies it as `PE32 executable (DLL) (console) Intel 80386`.
- SHA256: `20a6865facebdaac92b6c06fadf37d2efb5a242b22ca1c349bb57d7ad43df8e3`.
- Size: `68K`.
- `i686-w64-mingw32-objdump -p` shows imports from:
  - `KERNEL32.dll`
  - CRT API-set DLLs
  - `ntdll.dll`
- The export table has `0x83` entries.

Build-surface audit:

- The checkout contains MetalSharp D3D9 shim sources and build scripts:
  - `scripts/build-d3d9-metal.sh`
  - `src/d3d9/d3d9_pe.cpp`
  - `src/d3d9/d3d9_unix.mm`
  - `src/wine/d3d9_pe.cpp`
  - `src/wine/d3d9_unix.mm`
- Those scripts build or deploy `d3d9.dll`/`d3d9.so`; they do not build DXMT's `winemetal.dll`.
- No DXMT source tree for rebuilding `winemetal.dll` is present in this checkout. Only DXMT license files and packaged runtime DLLs are present.
- The only local i386 `winemetal.dll` found by path scan is the 11.5 control artifact at `~/.metalsharp/runtime/wine/lib/wine/i386-windows/winemetal.dll`.
- The 11.9 x86_64 DXMT `winemetal.dll` is `PE32+ x86-64`, SHA256 `d357ed7a83df000f98f7e4fe5e2da99f0e36561457d2dfc30e90f09504880fc8`, and has `0x84` exports.
- The 11.5 i386 WineMetal file is `PE32 Intel 80386`, SHA256 `20a6865facebdaac92b6c06fadf37d2efb5a242b22ca1c349bb57d7ad43df8e3`, and has `0x83` exports.
- Both WineMetal PE files import the same high-level dependency family: `KERNEL32.dll`, CRT API-set DLLs, and `ntdll.dll`.

This means the clean 11.9 candidate cannot be completed from checked-in MetalSharp source alone. A release-quality fix must either recover/build a true 11.9 i386 `winemetal.dll` from the DXMT build source that produced the x86_64 payload, or explicitly promote the 11.5 i386 bridge as a compatibility artifact after it passes live Nidhogg 2/M9 proof under Wine 11.9.

External DXMT source/build provenance found on AverySSD:

- Source checkout: `/Volumes/AverySSD/metalsharp/dxmt-src`
- Current branch: `codex/d3d12-ordinal-compat`
- HEAD: `f520cb8 Stabilize Elden Ring D3D12 startup probes`
- Dirty files exist in D3D12, DXGI, and WineMetal sources, so the checkout must be treated as a local build workspace, not a clean upstream tag.
- Remotes:
  - fork: `https://github.com/aaf2tbz/dxmt.git`
  - origin: `https://github.com/3Shain/dxmt.git`
- `build32/meson-logs/meson-setup.txt` says the i386 build used:
  - cross file: `/Volumes/AverySSD/metalsharp/dxmt-src/build-win32.txt`
  - compiler: `i686-w64-mingw32-gcc/g++` `15.2.0`
  - build type: `release`
  - `wine_install_path`: `/Volumes/AverySSD/metalsharp/runtime/wine-11.5`
- `build64/meson-logs/meson-setup.txt` says the x86_64 build used:
  - cross file: `build-win64.txt`
  - compiler: `x86_64-w64-mingw32-gcc/g++` `15.2.0`
  - `wine_builtin_dll`: `true`
  - `wine_install_path`: `/Users/alexmondello/.metalsharp/runtime/wine`

DXMT build outputs found:

| Path | Type | SHA256 | Notes |
| --- | --- | --- | --- |
| `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll` | PE32 i386 | `30dc23b57f6498df50217f71ee68029c2ff8ed1abdefa178574a07d9095c077e` | true i386 DXMT WineMetal candidate; built against Wine 11.5 path |
| `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/d3d11/d3d11.dll` | PE32 i386 | `565d442115a3bddd869c950f75a620b0eef6014459a93e1e8131cf2eaa5878dd` | i386 DXMT D3D11 build output |
| `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/dxgi/dxgi.dll` | PE32 i386 | `93bdab58be082fdf31caf2becd7b2caec5ccf526cdc7ce348cb0ac98935fef71` | i386 DXMT DXGI build output |
| `/Volumes/AverySSD/metalsharp/dxmt-src/build64/src/winemetal/winemetal.dll` | PE32+ x86_64 | `ab235e49844dc3ee2b0313d7812f3c90128d698e4aa20803f66f846c1fd7da70` | local build output, does not match release bundle x86_64 WineMetal |
| `/Volumes/AverySSD/metalsharp/dxmt-src/build64/src/winemetal/unix/winemetal.so` | Mach-O x86_64 | `cdb05c6737c4b1dc1792f162d098d35357d60ef1f0229cb35c7c09c4de5d048b` | local build output, does not match release bundle Unix WineMetal |

The AverySSD i386 WineMetal artifact is a better first 11.9/M9 candidate than borrowing the 11.5 installed `winemetal.dll`, because it is a true DXMT i386 build output. It is still not release-proven: it was built against a Wine 11.5 path, from a dirty DXMT workspace, and its x86_64 siblings do not match the released 11.9 bundle payload.

A third disposable candidate was prepared from the 11.9 bundle plus that explicit i386 artifact:

- command:
  - `METALSHARP_I386_WINEMETAL_SOURCE=/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll scripts/prepare-wine119-candidate.sh /tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst /tmp/metalsharp-wine119-candidate-dxmt32/wine`
- prepare report: `/tmp/metalsharp-wine119-candidate-dxmt32/prepare-report.txt`
- candidate manifest: `/tmp/metalsharp-runtime-11.9-candidate-dxmt32`
- classified diff: `/tmp/metalsharp-runtime-11.9-candidate-dxmt32/classified-diff.md`
- missing critical files: `0`
- classified diff gate: `fail`
- gate reason: i386 `winemetal.dll` exists, but it differs from the 11.5 control and therefore remains `release_blocker_until_live_m9_proves_unneeded`

This is the current best M9 test candidate, not a release candidate.

Borrowed-i386 experiment:

- command:
  - `METALSHARP_BORROW_BASELINE_I386_WINEMETAL=1 scripts/prepare-wine119-candidate.sh /tmp/metalsharp-wine-assets-IjmErR/metalsharp_bundle.tar.zst /tmp/metalsharp-wine119-candidate-borrow/wine`
- candidate root: `/tmp/metalsharp-wine119-candidate-borrow/wine`
- prepare report: `/tmp/metalsharp-wine119-candidate-borrow/prepare-report.txt`
- candidate manifest: `/tmp/metalsharp-runtime-11.9-candidate-borrow`
- classified diff: `/tmp/metalsharp-runtime-11.9-candidate-borrow/classified-diff.md`
- `borrowed_baseline_i386_winemetal=1`
- missing critical files: `0`
- classified diff gate: `pass`
- i386 `winemetal.dll` gate: `ok_manifest_only_still_requires_live_m9`

This borrowed-i386 candidate is only an experiment. It may be useful for the first Nidhogg 2/M9 Wine 11.9 parity attempt, but it is not a final release answer unless live M9 proves the 11.5 i386 bridge is compatible with the 11.9 runtime or a true 11.9-built i386 `winemetal.dll` is produced.

The manifest should be compared against the 11.5 control manifest. Differences are allowed only when classified as:

- expected Wine version delta
- expected DXMT/DXGI/D3D12 delta
- expected anti-cheat hook delta
- packaging defect
- unknown and release-blocking

## Why the Previous 11.9 Attempt Failed

The failure pattern is now specific enough to explain without guessing:

1. Wine changed from 11.5 to 11.9.
2. The runtime package shape changed at the same time, including missing `bin/metalsharp-wine` and missing pre-bound `lib/wine/.../winemetal.*` paths unless later code repaired them.
3. Migration/schema behavior changed at the same time, so users were not just switching binaries; their runtime/prefix state was being forced through a new repair/migration path.
4. Steam launch semantics changed for protected handoff, first relaunching Wine Steam and later still splitting route env from an already-running Steam client.
5. DXMT config/cache handling improved in some ways, but M9/M10 config visibility was narrowed even though the live Nidhogg 2 M9 control proves `dxmt.conf` is part of the working path.
6. Winemetal moved from game-local deployment to prefix/runtime binding at the same time as the Wine bump.
7. DirectX overrides narrowed from `=n,b` to native-only `=n`, altering loader fallback semantics.
8. Unity launch args churned while Schedule I and Subnautica BZ needed route authority to stay M11 unless explicitly configured.

The core mistake was not choosing Wine 11.9 for anti-cheat. The mistake was coupling the anti-cheat Wine bump with loader model changes, Steam handoff changes, migration changes, override changes, and Unity argument changes. The rebuild should prove Wine 11.9 as a layout-compatible substrate first, then add anti-cheat behavior as a separately gated layer.

## Completion Audit For This Investigation

Current investigation status:

- Done: rollback source-of-truth identified (`main` tree-equivalent to `v0.33.27`).
- Done: three live 11.5 control samples captured and documented.
- Done: release asset source for current Wine 11.9 identified (`bundles/metalsharp_bundle.tar.zst`, also embedded in the recovered `v0.33.30` Actions DMG artifact).
- Done: reproducible GitHub release asset fetch/verify script added for `bundles/metalsharp_bundle.tar.zst`.
- Done: release/upload evidence checked; the Wine 11.9 release asset does not contain an i386 `winemetal.dll`.
- Done: external AverySSD DXMT source/build workspace found, including a true PE32 i386 WineMetal candidate at `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll`.
- Done: Wine 11.5 vs Wine 11.9 internal runtime map added.
- Done: WineMetal build-surface audit completed; this checkout can build the MetalSharp D3D9 shim but does not contain the DXMT source/build path needed to produce a fresh i386 `winemetal.dll`.
- Done: code yin/yang map added against `backup/main-before-v0.33.28-reset-20260521T223249Z`.
- Done: rebuild passes, hard gates, and acceptance matrix defined.
- Done: strict commit replay ledger added; the post-0.33.27 branch is source material only, not a linear replay plan.
- Done: branch execution runbook added at `docs/wine-119-implementation-runbook.md`.
- Done: non-live readiness audit added and currently fails only because no passing live control suite has been supplied.
- Not done: no Wine 11.9 parity branch has been implemented yet.
- Not done: no candidate Wine 11.9 runtime has passed the three control games.
- Not done: no release-proven 11.9-built i386 `winemetal.dll` has been recovered or produced; the AverySSD DXMT i386 artifact and borrowed 11.5 i386 bridge both remain experiments until live M9 proof says otherwise.
- Not done: anti-cheat hook readiness has not been proven beyond file presence.

Therefore the investigation/design artifact is ready to guide implementation, but the overall Wine 11.9 rebuild goal is not complete until a candidate build passes the matrix above.
