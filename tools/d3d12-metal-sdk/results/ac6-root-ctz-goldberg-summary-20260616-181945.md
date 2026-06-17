# AC6 root-binding diagnostic with Goldberg: 20260616-181945

## Run

- Run dir: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-root-ctz-goldberg-20260616-181945/`
- Launch log: `/Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781655615.log`
- Runtime hash gate: `d3d12.dll=9d303582ec525270dc927172138fc8cd1b64d8a2073b5c8d7d6c5c68f7816169` plus stable companion hashes.
- Goldberg deployed before launch:
  - `Game/steam_api.dll` hash `df1959cab89f47129f95b339a9ce4046b9b3e873bafe50a6f5d16ab1a110e245`
  - `Game/steam_api64.dll` hash `4976e711b7312a8bac9ebf8adde98896391dfaab08422c93aa1dcc70c39b74e8`
  - original `steam_api64.dll` backed up as `.orig` hash `4df999c0c8cb12589f0864d52be5d4c775577aeb27fee28b49b188f9ba083eea`
  - `steam_settings/force_steam_appid.txt=1888160`
- No fresh WindowServer reports after the run.

## Metrics

- `launch_ok=True`
- `present_count=22`, `drawn_present_count=22`
- `graphics_pso_compiled=258`
- `render_pso_failed=0`
- `dxil_msl_compile_failed=0` (previous `ctz` failures are gone)
- `new_msl=0`, `new_metallib=0`; this run did not intentionally refresh live shader source/metallib caches.

## Upstream producer evidence

Target upstream producer of final producer `srv0`:

- VS `42dbf5610021bd23`
- PS `6aaa91c23c794ed8`
- PSO manifest: `~/.metalsharp/shader-cache/m12/1888160/pso-render-4242bbe67d38d967.json`
- Draw: `DrawIndexedInstanced elems=60`
- RTV: `tex_id=60`, `rgba8unorm`, `1920x1080`

Root/state diagnostics repeatedly show:

- `vs_cb=1`, descriptor-table `vs_cbv0 desc=null`, but root signature has a VS root CBV:
  - `root[0] type=CBV vis=VS reg=0 root_cbv=...`
  - readback queued and returns small nonzero values in rows 0/1, zeros afterwards.
- `root[1] type=CBV vis=PS reg=1 root_cbv=...`
- `root[2] type=TABLE vis=PS ... table_gpu=0x147e5c00`
- `root[3] type=TABLE vis=PS ... table_gpu=0x1483adc0`
- Current SRV probe reports `srv[0..3] res=null`; because the PS root has two descriptor tables, this likely means the diagnostic is reading the wrong/default table offset rather than proving no PS resources exist.
- OM state is not suppressing writes:
  - `depth_enable=0`, `stencil_enable=0`, `blend0=1`, `write_mask0=0xf`.
- Upstream RTV readback remains all zero across samples:
  - `nonzero_pixels=0`, `nonzero_bytes=0`, checksum `0xf7752776a1bc383`.

## Interpretation

Goldberg did not change the black-output path. The upstream pass still writes zero into the resource later sampled by the final producer. The pass state allows color writes, so the next narrowing step should inspect PS root CBV row data and both PS descriptor tables using root-signature descriptor ranges, not just hard-coded SRV slots 0-3.
