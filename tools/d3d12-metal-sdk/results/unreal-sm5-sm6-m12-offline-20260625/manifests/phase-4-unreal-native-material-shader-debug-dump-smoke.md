# Phase 4 Unreal-native Material Shader Debug Dump Smoke

Date: 2026-06-25

## Purpose

Prove a bounded Unreal-native lane for generated material shader artifacts, rather than treating Unreal `.usf` / `.ush` files as standalone HLSL. This is the material/permutation source side needed before DXIL/Metal Shader Converter/M12 binding reconciliation.

## Commandlet

Result root:

`06-results/completed/20260625-unreal-native-material-shader-debug-dump-smoke`

Commandlet:

```sh
UnrealEditor-Cmd -run=CompileShadersTestBed -targetplatform=Mac -ExcludeGlobalShaders -ExcludeDefaultMaterials -materials=/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial -unattended -nop4 -NoSplash -NullRHI
```

Temporary config CVars were injected into `Engine/Config/ConsoleVariables.ini` and restored byte-for-byte afterward:

- `r.ShaderDevelopmentMode=1`
- `r.DumpShaderDebugInfo=1`
- `r.DumpShaderDebugShortNames=1`
- `r.ShaderCompiler.DebugDumpJobInputHashes=1`
- `r.ShaderCompiler.DebugDumpJobDiagnostics=1`
- `r.ShaderCompiler.DebugDumpShaderCode=1`
- `r.ShaderCompiler.DebugDumpShaderCodePlatformHashes=1`
- `r.ShaderCompiler.DebugDumpDetailedShaderSource=1`
- `r.SupportSkyAtmosphere=0` to force a new material shader key and avoid cached no-op success.

## Evidence

- return code: `0`
- elapsed: `39.312s`
- no timeout
- material: `/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial`
- target platform: `Mac`, yielding `METAL_SM5` and `METAL_SM6`
- shaders compiled: `98`
- jobs completed: `98 / 98`
- materials translated: `2`
- changed/new debug files captured: `1591`
- captured bytes: `95,572,361`

Representative captured artifact types:

- generated/debug `.usf`
- `.orig.hlsl`
- `.rewritten.hlsl`
- `.metal`
- `.spv` / `.spvasm`
- `.dxil`
- `ShaderCode.bin`
- `DebugCompile.in`
- `DebugCompileArgs.txt`
- debug hashes / output hashes / diagnostics JSON

Captured extension counts are recorded in:

`06-results/completed/20260625-unreal-native-material-shader-debug-dump-smoke/captured-extension-counts.json`

## Interpretation

This validates the Unreal-native material shader debug-dump path for material/permutation source reconstruction. It is **not** counted as final D3D12/DXIL/M12 conversion success by itself. Acceptance still requires:

1. D3D target/bytecode path or extracted compiled bytecode,
2. root signature/resource binding/reflection preservation,
3. Apple Metal Shader Converter validation where DXIL is available,
4. reconciliation against DXMT/M12 descriptor/root binding behavior.

## Archive / cleanup

Archived bundle:

`07-archives/20260625-010229-phase4-unreal-native-material-shader-debug-dump-smoke.tar.zst`

SHA256:

`b47cb195927dc51ae6d743147ec27e51c0e23d533ebd1823c3b6d744395fc8e0`

After archive, generated Unreal-side `Engine/Saved/ShaderDebugInfo` was removed to avoid contaminating later before/after shader-debug probes. The captured copy remains in the completed result/archive.
