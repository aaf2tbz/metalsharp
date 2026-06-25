# Phase 3/4 smoke: Unreal-native SM5/SM6 commandlet proof

Date: 2026-06-24 / 2026-06-25 UTC

Result directory:

- `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/06-results/completed/20260624-unreal-native-sm5-sm6-commandlet-smoke`

Validated commandlet path:

- `UnrealEditor-Cmd -run=CompileShadersTestBed -targetplatform=Mac -ExcludeMaterials -unattended -nop4 -NoSplash -NullRHI`

Outcome:

- Exit status: 0
- Shader formats observed: `SF_METAL_SM5`, `SF_METAL_SM6`
- Shaders compiled: `17,104`
- Jobs completed: `17,104 / 17,104`
- Commandlet reported: `Success - 0 error(s), 133 warning(s)`
- No live game launch performed
- Disk guard after run: >400 GiB free, above 50 GiB floor

Scope note:

This is an authoritative Unreal-native commandlet smoke/proof for the built Mac shader compiler stack and Metal SM5/SM6 global/default shader coverage. It does not yet complete the direct D3D `PCD3D_SM5`/`PCD3D_SM6` shader corpus or M12 replay phases.
