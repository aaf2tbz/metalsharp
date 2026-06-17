# D3DMetal vs M12 metallib ABI/entrypoint comparison

- D3DMetal manifest: `tools/d3d12-metal-sdk/results/d3dmetal-metallib-extract-functions-20260616-190458/manifest.json`
- M12 root: `/Users/alexmondello/.metalsharp/shader-cache/m12`

| Game | D3DMetal unique metallibs | D3DMetal unique names | M12 MSL files | M12 metallibs | Shared entrypoint names | Direct-load verdict |
|---|---:|---:|---:|---:|---:|---|
| elden-ring | 1271 | 312 | 795 | 2 | 2 | oracle-only: only generic names overlap |
| armored-core-vi | 1659 | 356 | 419 | 2 | 2 | oracle-only: only generic names overlap |
| subnautica-2 | 10963 | 551 | 488 | 0 | 0 | oracle-only: entrypoint/function layout differs |

## Conclusion

The extracted D3DMetal blobs are valid Metal libraries and useful as an offline oracle/reference. However, their function naming and cache shape do not match M12's current cache ABI: D3DMetal stores many per-shader/per-pipeline metallibs with names such as `SATVSMain`, `SATPSMain`, `MainCS`, and game-specific Unreal functions, while M12 uses generated MSL plus shared/generic `dxmt_sm50_*` metallibs. Treat direct loading as unsafe until resource/argument layouts and pipeline entrypoints are explicitly bridged.
