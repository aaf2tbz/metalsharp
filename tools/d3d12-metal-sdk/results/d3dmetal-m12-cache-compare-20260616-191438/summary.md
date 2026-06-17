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

### D3DMetal raw embedded marker hits

These counts are raw byte hits used as provenance hints; only the metallib extractor performs bounded `MTLB` blob validation.
- `MTLGPUFamilyApple9_0/stage_cache.bin`: AIR=1633, HASH=1549, MTLB=1503, NAME=3052, OFFT=1549, TYPE=1549
- `MTLGPUFamilyApple9_0/bytecode_cache.bin`: HASH=1384
- `32024/libraries.data`: AIR=426, HASH=415, MTLB=222, NAME=637, OFFT=415, TYPE=415

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

### D3DMetal raw embedded marker hits

These counts are raw byte hits used as provenance hints; only the metallib extractor performs bounded `MTLB` blob validation.
- `MTLGPUFamilyApple9_0/stage_cache.bin`: AIR=2028, HASH=1921, MTLB=1854, NAME=3775, OFFT=1921, TYPE=1921
- `MTLGPUFamilyApple9_0/bytecode_cache.bin`: HASH=1678
- `32024/libraries.data`: AIR=476, HASH=446, MTLB=253, NAME=699, OFFT=446, TYPE=446

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

### D3DMetal raw embedded marker hits

These counts are raw byte hits used as provenance hints; only the metallib extractor performs bounded `MTLB` blob validation.
- `MTLGPUFamilyApple9_0/stage_cache.bin`: AIR=11487, HASH=11486, MTLB=11486, NAME=22972, OFFT=11486, TYPE=11486
- `MTLGPUFamilyApple9_0/bytecode_cache.bin`: AIR=152, BCGI=1, CPCH=3, HASH=10453
- `32024/libraries.data`: AIR=17, HASH=17, MTLB=14, NAME=31, OFFT=17, TYPE=17

### M12 suffix counts
- `dxbc`: `489`
- `msl`: `488`
- `pso-compute.json`: `477`
- `pso-render.json`: `6`
- `tsv`: `1`
- `txt`: `977`

