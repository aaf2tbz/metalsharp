# AC6 upstream diagnostic summary

- run: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-175100/`
- launch log: `/Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781653889.log`
- launch_ok: `True` pid: `23926` seconds: `60`
- metrics: `{"compute_pso_compiled": 0, "compute_pso_failed": 0, "drawn_present_count": 22, "dxil_msl_compile_failed": 3, "graphics_pso_compiled": 281, "present_count": 22, "render_pso_failed": 0, "sm50_compile_failed": 0, "tessellation_fallback": 9, "unix_call_failed": 0, "unsafe_draw_skips": 0, "vertex_descriptor_missing": 0, "vs_ps_varying_mismatch": 0}`
- env_overrides: `['METALSHARP_M12_AC6_PRODUCER_DIAGNOSTIC', 'METALSHARP_M12_DIAGNOSTIC_CAPTURE', 'METALSHARP_M12_DUMP_MSL', 'METALSHARP_M12_ENABLE_LIVE_PRESENT']`
- no fresh WindowServer report after run: latest remains `2026-06-16-124916`

## Counts
- `M12 AC6 upstream diagnostic`: `57`
- `M12 AC6 upstream readback rtv`: `57`
- `M12 AC6 upstream srv[0] res=null`: `57`
- `M12 AC6 producer diagnostic`: `79`
- `M12 AC6 producer readback rtv`: `79`
- `M12 AC6 producer readback srv0`: `79`
- `M12 final SRV readback slot=1`: `78`
- `M12 swapchain render readback`: `18`
- `call to 'ctz' is ambiguous`: `12`

## Representative evidence
### upstream_diag

`M12 AC6 upstream diagnostic draw=DrawIndexedInstanced elems=60 res=0x13a63590 dim=3 fmt=28 size=1920x1080x1 mips=1 samples=1 tex=140301718318656 tex_id=60 tex_array=1 buf=0 gpu=0x35184403808256 bytes=0 swapchain=0 bb=0 pso=0x1c0720b0 vs=42dbf5610021bd23 ps=6aaa91c23c794ed8 gs= vs_args=0 vs_cb=1 vs_qwords=0 vs_cb_bind=29 vs_arg_bind=4294967295 ps_args=0 ps_cb=0 ps_qwords=0 ps_cb_bind=4294967295 ps_arg_bind=4294967295 stage_in=0 geom_mesh=0 tess_fallback=0`

### upstream_state

`M12 AC6 upstream state vp_count=1 vp=0,0 1920x1080 depth=0-1 sc_count=1 sc=0,0-1920,1080 topology=4 primitive=3 cull=1 front_ccw=0 depth_enable=0 depth_write=0 depth_func=0 stencil_enable=0 stencil_read=0xff stencil_write=0xff blend0=1 write_mask0=0xf src0=5 dst0=6 op0=1`

### upstream_vs_cbv0

`M12 AC6 upstream vs_cbv0 desc=null`

### upstream_srv0

`M12 AC6 upstream srv[0] res=null`

### upstream_rtv

`M12 AC6 upstream readback rtv slot=0 tex=140301718318656 fmt=28 sample=1920x1080 nonzero_pixels=0 nonzero_bytes=0 ch_pixels=0,0,0,0 ch_bytes=0,0,0,0 max_byte=0 ch_max=0,0,0,0 checksum=0xf7752776a1bc383`

### producer_diag

`M12 AC6 producer diagnostic draw=DrawInstanced elems=4 res=0x5785b80 dim=3 fmt=28 size=1920x1080x1 mips=1 samples=1 tex=140301718150032 tex_id=8 tex_array=1 buf=0 gpu=0x35184372416512 bytes=0 swapchain=0 bb=0 force_white=0 pso=0x13aaa1a0 vs=ca33abe9a2d27ce9 ps=58539be4844b1dd9 gs= vs_args=0 vs_cb=0 vs_qwords=0 vs_cb_bind=0 vs_arg_bind=0 ps_args=0 ps_cb=0 ps_qwords=0 ps_cb_bind=0 ps_arg_bind=0 stage_in=0 geom_mesh=0 tess_fallback=0`

### producer_cbv

`M12 AC6 producer cbv0 desc=0x91b0000 heap_type=0 range=CBV res=0x11b9c960 dim=3 fmt=70 size=16x16x1 mips=5 samples=1 tex=140301206885200 tex_id=283 tex_array=1 buf=0 gpu=0x35184381460480 bytes=0 swapchain=0 bb=0 cbv_gpu=0x1100315574272 cbv_size=256`

### producer_cbv_queued

`M12 AC6 producer cbv readback queued=1 gpu=0x1002feb4000 off=0 bytes=256 available=256 res=0x13a63450 dim=1 fmt=0 size=256x1x1 mips=1 samples=1 tex=0 tex_id=0 tex_array=1 buf=140301171082192 gpu=0x1100315574272 bytes=256 swapchain=0 bb=0`

### producer_rtv

`M12 AC6 producer readback rtv slot=0 tex=140301718150032 fmt=28 sample=1920x1080 nonzero_pixels=0 nonzero_bytes=0 ch_pixels=0,0,0,0 ch_bytes=0,0,0,0 max_byte=0 ch_max=0,0,0,0 checksum=0xf7752776a1bc383`

### producer_srv0

`M12 AC6 producer readback srv0 slot=0 tex=140301718318656 fmt=28 sample=1920x1080 nonzero_pixels=2073600 nonzero_bytes=2073600 ch_pixels=0,0,0,2073600 ch_bytes=0,0,0,2073600 max_byte=255 ch_max=0,0,0,255 checksum=0x792bed5a3f02a383`

### final_t0

`M12 final SRV readback slot=0 reg=0 tex=140301718148768 fmt=28 sample=1920x1080 nonzero_pixels=2073600 nonzero_bytes=2073600 ch_pixels=0,0,0,2073600 ch_bytes=0,0,0,2073600 max_byte=255 ch_max=0,0,0,255 checksum=0x792bed5a3f02a383`

### final_t1

`M12 final SRV readback slot=1 reg=1 tex=140301718150032 fmt=28 sample=1920x1080 nonzero_pixels=0 nonzero_bytes=0 ch_pixels=0,0,0,0 ch_bytes=0,0,0,0 max_byte=0 ch_max=0,0,0,0 checksum=0xf7752776a1bc383`

### swapchain

`M12 swapchain render readback capture=1 backbuffer=0 fmt=28 sample=1920x1080 nonzero_pixels=0 nonzero_bytes=0 ch_pixels=0,0,0,0 ch_bytes=0,0,0,0 max_byte=0 ch_max=0,0,0,0 checksum=0xf7752776a1bc383`

### ctz

`error: call to 'ctz' is ambiguous`

## Interpretation

- The request `envOverrides` path worked: AC6 producer diagnostics and final snapshot/readback were active in the game process.
- The upstream PSO (`VS 42dbf5610021bd23`, `PS 6aaa91c23c794ed8`) was captured 57 times and writes the final-producer `srv0` resource.
- Upstream pass state is not obviously suppressing writes: viewport/scissor cover 1920x1080, depth/stencil disabled, write mask `0xf`, but blend is enabled (`src0=5`, `dst0=6`).
- Upstream RTV readback remains all zero. The diagnostic currently finds no descriptor-table VS CBV or pixel SRVs for this upstream PSO (`desc=null`, `res=null`), even though draw summary says `vs_cb=1`. Next probe must inspect root CBV/root constants/direct bindings, not just descriptor tables.
- Final state remains unchanged: final `t0` alpha-only, final `t1` zero, swapchain zero; final producer output zero follows the upstream RTV zero.
- The run also shows 3 `dxil_msl_compile_failed` metrics with ambiguous `ctz` Metal compile errors, now a separate offline shader-lowering hardening target.
