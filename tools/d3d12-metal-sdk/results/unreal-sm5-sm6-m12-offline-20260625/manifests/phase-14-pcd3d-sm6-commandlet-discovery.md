# Phase 14 PCD3D_SM6 Commandlet Discovery

Date: 2026-06-25

## Purpose

Determine whether the current Mac-host Unreal checkout can run an offline Unreal-native `PCD3D_SM6` shader compile/debug-dump lane with no game launch.

## Result directory

`06-results/in-progress/phase14-pcd3d-sm6-commandlet-discovery-20260625-141306`

## Commands attempted

All commands used `UnrealEditor-Cmd` from:

`02-unreal-full/release/UnrealEngine/Engine/Binaries/Mac/UnrealEditor-Cmd`

All were offline commandlet probes with `-NullRHI`, `-unattended`, `-nop4`, `-NoSplash`, and no game launch.

Target-platform help/discovery attempts:

- `-run=CompileShadersTestBed -targetplatform=Windows -help`
- `-run=CompileShadersTestBed -targetplatform=Win64 -help`
- `-run=CompileShadersTestBed -targetplatform=WindowsEditor -help`
- `-run=CompileShadersTestBed -targetplatform=WindowsClient -help`

Minimal material attempt:

- `-run=CompileShadersTestBed -targetplatform=Windows -ExcludeGlobalShaders -ExcludeDefaultMaterials -materials=/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial`

## Outcome

All Windows/Win64 target-platform attempts failed before useful shader compilation.

Common signals:

- exit code: `1`
- `##PlatformValidate: Win64 INVALID <UNKNOWN>`
- `Unable to find required SDK version 'MainVersion' for platform Win64. Check your SDK.json files`
- Windows ini files were loaded, but the Mac editor then crashed in target-platform/shader-platform initialization:
  - `SIGSEGV: invalid attempt to access memory at address 0x3`
  - stack includes `GetTargetPlatformManager(bool)`
  - stack includes `UE::RHIShaderPlatformConfig::GetConfigSectionForShaderPlatform`

Static discovery confirms the Mac editor build does not include D3D/Windows target modules:

- no `libUnrealEditor-ShaderFormatD3D.dylib`
- no `libUnrealEditor-WindowsTargetPlatform*.dylib`
- no Mac `ShaderCompileWorker`/`ShaderBuildWorker` D3D shader-format module
- `UEBuildMac.cs` explicitly comments out `ShaderFormatD3D` in the Mac target-platform shader-format list:
  - `// Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatD3D");`
- `TargetPlatform.Build.cs` only adds `ShaderFormatD3D` automatically for `Target.Platform == UnrealTargetPlatform.Win64`.

## Interpretation

The current Mac-host UnrealEditor commandlet lane cannot produce authoritative `PCD3D_SM6` jobs as-is. This is a host/toolchain/module availability blocker, not an M12 shader translation failure.

The blocker is precise enough to avoid repeating raw standalone `.usf` DXC attempts:

1. Windows target platform is invalid on this host because the Win64 SDK is unavailable.
2. The Mac editor build does not load/build `ShaderFormatD3D` or Windows target-platform modules.
3. `PCD3D_SM6` exists in source/config, but there is no Mac-host commandlet path to compile it without adding/building missing modules or using a Windows-host lane.

## Selected fallback path

Proceed with the roadmap fallback hierarchy:

1. Use existing Unreal-native generated debug artifacts and DXIL from the Mac `METAL_SM6` material lane as a source-backed generated-environment corpus.
2. Search for any already-built Windows/Win64 Unreal or shader worker binaries before ruling out Wine-hosted Unreal-native PCD3D.
3. If Windows binaries remain unavailable, use compiled bytecode/debug artifact extraction rather than raw `.usf` source as the authoritative offline input.
4. For raw `.usf` failures, classify them by top-level Unreal owner/material/permutation lane and link them to generated debug artifacts or expected-denial classifications.
5. Continue M12 validation through DXIL/MSC/M12 replay and root-binding reconciliation.

## Key evidence files

- `01-static-discovery.txt`
- `02-help-targetplatform-windows.stdout.txt`
- `03-min-material-targetplatform-windows.stdout.txt`
- `04-commandlet-summary.md`
- `05-targetplatform-variant-summary.md`
- `00-disk-guard-before.txt`
- `99-disk-guard-after.txt`

## Exit criteria status

Phase 14 exits with a precise blocker and fallback selection. No live game launch occurred.
