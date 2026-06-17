# D3DMetal vs M12 shader cache inventory

- D3DMetal reference root: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`
- M12 cache root: `/Users/alexmondello/.metalsharp/shader-cache/m12`

This is phase 1: topology/count/hash inventory. It is not yet proof of metallib ABI compatibility or semantic equivalence.

## Summary

| Game | D3DM stage | M12 MSL | D3DM bytecode | M12 DXBC | D3DM pipelines | M12 PSO manifests | D3DM bytes | M12 bytes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| elden-ring | 354 | 795 | 375 | 795 | 3 | 585 | 68160188 | 58900022 |
| armored-core-vi | 444 | 419 | 479 | 420 | 5 | 375 | 109253580 | 22439568 |
| subnautica-2 | 3752 | 488 | 2574 | 489 | 25 | 483 | 644718164 | 35252525 |

## elden-ring

### D3DMetal observed counts
- `MTLGPUFamilyApple9_0/stage_cache.bin`: `354`
- `MTLGPUFamilyApple9_0/bytecode_cache.bin`: `375`
- `MTLGPUFamilyApple9_0/pipeline_cache.bin`: `3`
- `MTLGPUFamilyApple9_0/rootsignature_cache.bin`: `1`

### M12 suffix counts
- `dxbc`: `795`
- `metallib`: `2`
- `msl`: `795`
- `pso-render.json`: `585`
- `tsv`: `1`
- `txt`: `1590`

## armored-core-vi

### D3DMetal observed counts
- `MTLGPUFamilyApple9_0/stage_cache.bin`: `444`
- `MTLGPUFamilyApple9_0/bytecode_cache.bin`: `479`
- `MTLGPUFamilyApple9_0/pipeline_cache.bin`: `5`
- `MTLGPUFamilyApple9_0/rootsignature_cache.bin`: `1`

### M12 suffix counts
- `dxbc`: `420`
- `metallib`: `2`
- `msl`: `419`
- `pso-render.json`: `375`
- `tsv`: `1`
- `txt`: `838`

## subnautica-2

### D3DMetal observed counts
- `MTLGPUFamilyApple9_0/stage_cache.bin`: `3752`
- `MTLGPUFamilyApple9_0/bytecode_cache.bin`: `2574`
- `MTLGPUFamilyApple9_0/pipeline_cache.bin`: `25`
- `MTLGPUFamilyApple9_0/rootsignature_cache.bin`: `3`

### M12 suffix counts
- `dxbc`: `489`
- `msl`: `488`
- `pso-compute.json`: `477`
- `pso-render.json`: `6`
- `tsv`: `1`
- `txt`: `977`

