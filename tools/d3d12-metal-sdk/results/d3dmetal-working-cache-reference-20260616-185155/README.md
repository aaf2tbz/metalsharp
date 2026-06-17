# D3DMetal/GPTK working shader cache references

Reference dir: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`

These are copied from macOS D3DMetal cache roots under /var/folders and are intended as an oracle/reference against our DXMT M12 cache output. They were not inserted into live DXMT caches.

| Game | Source | Copy size | Files |
|---|---|---:|---:|
| elden-ring | `/var/folders/cj/fy0x7bzj727_ltlshb87_bpr0000gn/C/d3dm/start_protected_game.exe/shaders.cache` | 65M | 9 |
| armored-core-vi | `/var/folders/cj/fy0x7bzj727_ltlshb87_bpr0000gn/C/d3dm/armoredcore6.exe/shaders.cache` | 104M | 9 |
| subnautica-2 | `/var/folders/cj/fy0x7bzj727_ltlshb87_bpr0000gn/C/d3dm/Subnautica2-Win64-Shipping.exe/shaders.cache` | 615M | 14 |

Next useful step: build an extractor/comparator that inventories D3DMetal function blobs/pipeline keys and compares counts, formats, and entry-point signatures against `~/.metalsharp/shader-cache/m12/<appid>` MSL/metallib/PSO manifests.
