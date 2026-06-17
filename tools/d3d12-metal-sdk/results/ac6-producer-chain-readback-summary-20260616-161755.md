# AC6 producer-chain readback summary

- run: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-161755/`
- launch log: `/Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781648302.log`
- expected_d3d12_sha: `4dcc067e3241f8efb5ab8dc0f3aa30534608ad432d61bb01a315f1e330ccb73e`
- expected_dxgi_sha: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- expected_dxgi_dxmt_sha: `66b1265d52bc46541488ede3676075a5ed546d7886e51ed5e97dd43b0fc98241`
- expected_winemetal_dll_sha: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- expected_winemetal_so_sha: `43155b9a9e337868d906a2d2377beae5223e8eb1b131e9717b06c4c3ae7e5a43`
- launch_ok: `True` pid: `54451` seconds: `60`
- metrics: `{"compute_pso_compiled": 0, "compute_pso_failed": 0, "drawn_present_count": 22, "dxil_msl_compile_failed": 3, "graphics_pso_compiled": 252, "present_count": 22, "render_pso_failed": 0, "sm50_compile_failed": 0, "tessellation_fallback": 5, "unix_call_failed": 0, "unsafe_draw_skips": 0, "vertex_descriptor_missing": 0, "vs_ps_varying_mismatch": 0}`

## Counts
- `M12 AC6 candidate OMSetRenderTargets`: `447`
- `M12 AC6 candidate ClearRTV`: `65`
- `M12 AC6 producer candidate`: `53`
- `M12 AC6 producer diagnostic`: `75`
- `M12 AC6 producer cbv readback`: `375`
- `final SRV readback slot=1`: `74`
- `swapchain render readback`: `18`

## Representative evidence
### producer_diag

`M12 AC6 producer diagnostic draw=DrawInstanced elems=4 res=0x58322d0 dim=3 fmt=28 size=1920x1080x1 mips=1 samples=1 tex=140331288147872 tex_id=8 tex_array=1 buf=0 gpu=0x35184372416512 bytes=0 swapchain=0 bb=0 force_white=0 pso=0x13ab0860 vs=ca33abe9a2d27ce9 ps=58539be4844b1dd9 gs= vs_args=0 vs_cb=0 vs_qwords=0 vs_cb_bind=0 vs_arg_bind=0 ps_args=0 ps_cb=0 ps_qwords=0 ps_cb_bind=0 ps_arg_bind=0 stage_in=0 geom_mesh=0 tess_fallback=0`

### producer_state

`M12 AC6 producer state vp_count=1 vp=0,0 1920x1080 depth=0-1 sc_count=1 sc=0,0-1920,1080 topology=5 primitive=4 cull=1 front_ccw=1 depth_enable=0 depth_write=0 depth_func=8 stencil_enable=0 blend0=0 write_mask0=0xf src0=2 dst0=1 op0=1`

### producer_cbv

`M12 AC6 producer cbv0 desc=0x91b0000 heap_type=0 range=CBV res=0x11ba14f0 dim=3 fmt=70 size=16x16x1 mips=5 samples=1 tex=140330753614784 tex_id=283 tex_array=1 buf=0 gpu=0x35184381460480 bytes=0 swapchain=0 bb=0 cbv_gpu=0x1100315574272 cbv_size=256`

### producer_cbv_queued

`M12 AC6 producer cbv readback queued=1 gpu=0x1002feb4000 off=0 bytes=256 available=256 res=0x13a6bab0 dim=1 fmt=0 size=256x1x1 mips=1 samples=1 tex=0 tex_id=0 tex_array=1 buf=140330750126048 gpu=0x1100315574272 bytes=256 swapchain=0 bb=0`

### producer_row5

`M12 AC6 producer cbv readback row=5 off=80 f=0,0,0,1 bytes=[00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3f]`

### producer_row9

`M12 AC6 producer cbv readback row=9 off=144 f=7.5,4.21875,0.737681,0.738277 bytes=[00 00 f0 40 00 00 87 40 b0 d8 3c 3f c0 ff 3c 3f]`

### producer_row10

`M12 AC6 producer cbv readback row=10 off=160 f=-0,-0,0,0 bytes=[00 00 00 80 00 00 00 80 00 00 00 00 00 00 00 00]`

### producer_row11

`M12 AC6 producer cbv readback row=11 off=176 f=-0,-0,0,0 bytes=[00 00 00 80 00 00 00 80 00 00 00 00 00 00 00 00]`

### producer_rtv

`M12 AC6 producer readback rtv slot=0 tex=140331288147872 fmt=28 sample=1920x1080 nonzero_pixels=0 nonzero_bytes=0 ch_pixels=0,0,0,0 ch_bytes=0,0,0,0 max_byte=0 ch_max=0,0,0,0 checksum=0xf7752776a1bc383`

### producer_srv0

`M12 AC6 producer readback srv0 slot=0 tex=140331288279040 fmt=28 sample=1920x1080 nonzero_pixels=2073600 nonzero_bytes=2073600 ch_pixels=0,0,0,2073600 ch_bytes=0,0,0,2073600 max_byte=255 ch_max=0,0,0,255 checksum=0x792bed5a3f02a383`

### upstream_candidate

`M12 AC6 producer candidate draw=DrawIndexedInstanced elems=60 res=0x13a6bbd0 dim=3 fmt=28 size=1920x1080x1 mips=1 samples=1 tex=140331288279040 tex_id=60 tex_array=1 buf=0 gpu=0x35184403808256 bytes=0 swapchain=0 bb=0 pso=0x1c05b560 vs=42dbf5610021bd23 ps=6aaa91c23c794ed8 gs= vs_args=0 vs_cb=1 vs_qwords=0 vs_cb_bind=29 vs_arg_bind=4294967295 ps_args=0 ps_cb=0 ps_qwords=0 ps_cb_bind=4294967295 ps_arg_bind=4294967295 stage_in=0 geom_mesh=0 tess_fallback=0`

### upstream_draw

`M12 offscreen DrawIndexedInstanced encoded idx=60 inst=1 start=0 base=0 start_inst=0 primitive=3 ib_fmt=57 ib_gpu=0x10047e38000 ib_res=0x13ac32c0 ib_handle=140331277845968 ib_off=0 vb_summary=vb_table mask=0x1 entries=1 vb_bound=1 frag buffers=31+29/31 textures=16+16/16 samplers=4+4/4 pso=0x1c05b560 vs=42dbf5610021bd23 ps=6aaa91c23c794ed8 gs= vs_args=0 vs_cb=1 vs_qwords=0 vs_cb_bind=29 vs_arg_bind=4294967295 ps_args=0 ps_cb=0 ps_qwords=0 ps_cb_bind=4294967295 ps_arg_bind=4294967295 stage_in=0 geom_mesh=0 tess_fallback=0 rt0={res=0x13a6bbd0 dim=3 fmt=28 size=1920x1080x1 mips=1 samples=1 tex=140331288279040 tex_id=60 tex_array=1 buf=0 gpu=0x35184403808256 bytes=0 swapchain=0 bb=0} dsv={res=0x5832090 dim=3 fmt=19 size=1920x1080x1 mips=1 samples=1 tex=140330750073792 tex_id=5 tex_array=1 buf=0 gpu=0x35184372285440 bytes=0 swapchain=0 bb=0 stencil=1}`

### final_t1

`M12 final SRV readback slot=1 reg=1 tex=140331288147872 fmt=28 sample=1920x1080 nonzero_pixels=0 nonzero_bytes=0 ch_pixels=0,0,0,0 ch_bytes=0,0,0,0 max_byte=0 ch_max=0,0,0,0 checksum=0xf7752776a1bc383`

### final_t0

`M12 final SRV readback slot=0 reg=0 tex=140331009070832 fmt=28 sample=1920x1080 nonzero_pixels=2073600 nonzero_bytes=2073600 ch_pixels=0,0,0,2073600 ch_bytes=0,0,0,2073600 max_byte=255 ch_max=0,0,0,255 checksum=0x792bed5a3f02a383`

## Interpretation

- The final composite path remains healthy enough to read back deterministic inputs: final `t0` is alpha-only, final `t1` is zero, and swapchain readback is zero.
- The exact AC6 final `t1` producer (`VS ca33abe9a2d27ce9`, `PS 58539be4844b1dd9`) now fires under the widened 1920x1080 predicate.
- That producer has valid-looking state/geometry and its CBV readback queues successfully; row 5 contains nonzero values while rows 10/11 are zero in this run.
- The producer output RTV is zero, and its `srv0` input is either alpha-only (`RGBA = 0,0,0,255`) in earlier frames or fully zero in later frames.
- The `srv0` resource (`res=0x13a6bbd0`, `tex_id=60`) is repeatedly cleared to black and then written by an upstream 60-index draw with `VS 42dbf5610021bd23`, `PS 6aaa91c23c794ed8`. This moves the black-output root cause one pass upstream of `ChromaticAberrationAndNoisePs`.

## Next root-cause target

Trace/read back the upstream `42dbf5610021bd23` / `6aaa91c23c794ed8` draw: its CBV/SRV inputs, RTV readback after draw, and whether depth/stencil or state prevents RGB writes into `tex_id=60`.
