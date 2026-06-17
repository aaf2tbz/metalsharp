# AC6 descriptor-table diagnostic: local branch backend

- run root: `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105`
- bounded run dir: `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105`
- launch_ok: `True`
- pid: `6107`
- requested_seconds: `None`
- kill artifact: `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105/kill.json`

## Backend/runtime gate

- backend: local built branch backend on `127.0.0.1:9277`, separate from normal app backend/PID.
- env_handoff includes producer diagnostic: `True` (`METALSHARP_M12_AC6_PRODUCER_DIAGNOSTIC` was accepted as an override and mapped to `DXMT_D3D12_AC6_PRODUCER_DIAGNOSTIC` in the game process handoff).

## Key findings

- The target upstream PSO was captured repeatedly: VS `42dbf5610021bd23`, PS `6aaa91c23c794ed8`, indexed draws of `60` and `30`.
- Upstream root signature at capture time has 4 params: VS root CBV b0, PS root CBV b1, PS SRV descriptor table t0..t3, PS sampler table s0..s3.
- For the target upstream pass, PS SRV table root[2] has only `t0` populated (`fmt=98`, `4096x4096`); `t1`-`t3` are null.
- Upstream target RTV readback remains zero for the inspected immediate draw, but the downstream final-producer input (`srv0`) later reads RGB-nonzero with alpha exactly zero.
- The final producer (`ca33abe9a2d27ce9`/`58539be4844b1dd9`) writes a zero RTV while sampling that RGB-nonzero/alpha-zero `srv0`, keeping final `t1` black/empty.
- Current M12 cache manifests reference `42dbf5610021bd23`/`6aaa91c23c794ed8` metallibs, but raw `.dxbc`/`.msl` source artifacts for those stems are absent in the live cache; mapping to D3DMetal still needs PSO/root/oracle linkage or bytecode recovery.

## Evidence files

- `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105/ac6-descriptor-diagnostic-lines.txt`
- `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105/summary.md`
- `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105/summary.json`
- `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105/launch.json`
- `tools/d3d12-metal-sdk/results/perf-runs/ac6-descriptor-table-local-backend-20260616-202105/armored-core-vi-smoke-20260616-202105/bounded/armored-core-vi-20260616-202105/runtime-preflight-armored-core-vi.json`
