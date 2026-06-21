# M12 Phase 8 baseline lock

- timestamp: `20260620-221605`
- profile: `metalsharp`
- root: `/Users/alexmondello/metalsharp-m12-lab`
- build_dir: `/Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/build-metalsharp-x64`
- runtime_dir: `/Users/alexmondello/.metalsharp/runtime/wine/lib/dxmt_m12`
- mscompatdb_path: `/Users/alexmondello/.metalsharp/mscompatdb`
- live_launch: `false`

## Result

- ok: `True`
- preflight_ok: `True`
- failure_count: `0`
- winemetal_abi_ok: `True`
- mscompatdb_absent: `true`

## Atomic runtime parity

| artifact | ok | build sha256 | staged sha256 |
|---|---:|---|---|
| `d3d12.dll` | `True` | `97e7d5224cf343d3ffcf66ea94bf3d61c2104a7b938075854123c7f2bbee85fd` | `97e7d5224cf343d3ffcf66ea94bf3d61c2104a7b938075854123c7f2bbee85fd` |
| `d3d11.dll` | `True` | `63a14b899effa29cb929b3f754b0cbe1940c73e7b68c3c76853e7c9f03174e9d` | `63a14b899effa29cb929b3f754b0cbe1940c73e7b68c3c76853e7c9f03174e9d` |
| `d3d10core.dll` | `True` | `45ced0ef3f7aac6ee4bff8628b09cbd54bd1541771689ff6b905aae65a6f4a45` | `45ced0ef3f7aac6ee4bff8628b09cbd54bd1541771689ff6b905aae65a6f4a45` |
| `dxgi.dll` | `True` | `1f4d4a70ec69c238ad841fea864cdde5ebe6262bcf6e22c03c4b8681f21116e9` | `1f4d4a70ec69c238ad841fea864cdde5ebe6262bcf6e22c03c4b8681f21116e9` |
| `dxgi_dxmt.dll` | `True` | `3666982ab287d45b70fccbe1f00d94e8565244707799b31e344aad464025b229` | `3666982ab287d45b70fccbe1f00d94e8565244707799b31e344aad464025b229` |
| `winemetal.dll` | `True` | `af4027672fc99353f124085ce25e911adb5e1dae88484fde81364f2e8278ec73` | `af4027672fc99353f124085ce25e911adb5e1dae88484fde81364f2e8278ec73` |
| `winemetal.so` | `True` | `29ab057763b5b596998f2d01ae900011b1b5acfbfa4a57f2b2eca2a8eba2e7e5` | `29ab057763b5b596998f2d01ae900011b1b5acfbfa4a57f2b2eca2a8eba2e7e5` |
| `libm12core.dylib` | `True` | `eba79cb0c4691a97ab114a8ae9be0c6f87fdbdf7b1c1a9850439278d112302e8` | `eba79cb0c4691a97ab114a8ae9be0c6f87fdbdf7b1c1a9850439278d112302e8` |

## Safe Phase 8 launch shape

```text
POST /steam/launch-game
METALSHARP_PORT=9277
METALSHARP_M12_BINARY_ARCHIVE=1
METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP=1
# omit METALSHARP_M12_BINARY_ARCHIVE_POPULATE
METALSHARP_M12_LOG_LEVEL=none
METALSHARP_M12_LOG_PATH=none
METALSHARP_M12_TRACE_CAPTURE=0
```

