# D3DMetal extracted metallib inventory

- Reference root: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`
- Extract root: `<manifest-only>`

| Game | MTLB occurrences | Unique metallibs | Unique bytes |
|---|---:|---:|---:|
| elden-ring | 1725 | 1271 | 20749448 |
| armored-core-vi | 2107 | 1659 | 27535268 |
| subnautica-2 | 11500 | 10963 | 229395468 |

## elden-ring

- Cache: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155/elden-ring/shaders.cache`
- Occurrences: `1725`
- Unique metallibs: `1271`
- Unique bytes: `20749448`
- Function-name histogram:
  - `SATPSMain`: `499`
  - `SATVSMain`: `224`
  - `irconverter_stage_in_shader`: `73`
  - `PixelMain`: `35`
  - `main`: `26`
  - `postfixPrimaryAccumulate_4xf`: `23`
  - `postfixPrimaryAccumulate_f`: `23`
  - `postfixPrimary_4xf`: `23`
  - `postfixPrimary_f`: `23`
  - `prefixPrimary_4xf`: `23`
  - `prefixPrimary_f`: `23`
  - `irconverter_hull_shader`: `22`
  - `middlefixPrimary_4xf`: `22`
  - `middlefixPrimary_f`: `22`
  - `irconverter_domain_shader_triangle_passthrough`: `20`
  - `irconverter_dxil_domain_shader`: `20`
  - `prefixSecondary_4xf`: `15`
  - `prefixSecondary_f`: `15`
  - `irconverter_tessellator`: `11`
  - `VertexMain`: `10`
- First 20 unique blobs:
  - `0c9b13bd625a894fe6426dd00ee9949dd5735ce1dad99a59cb171ac19885f8f1` size=9328 funcs=`BclrA3Xhfu` first=`shaders.cache/32024/libraries.data@64` occurrences=1
  - `75c6f40c66b52450d3d70c44dded0ce00fcf764fcf8dc54d4e9a585d7f9cb2d9` size=5184 funcs=`VfxXh` first=`shaders.cache/32024/libraries.data@9472` occurrences=1
  - `cc72bf8aba0874fc6f76e3abc880e97f9ae7a410dbcff67aaee5434e3e3ff92b` size=11908 funcs=`TimgA3Xhfu_IxrgN3Oc3mtc0nlnlnlXq` first=`shaders.cache/32024/libraries.data@14720` occurrences=1
  - `5d8527c2f203d408bc679d4e714362f6cba2fdbcf095f968cff954c9b13ad20a` size=5620 funcs=`VfxU10Xh` first=`shaders.cache/32024/libraries.data@26752` occurrences=1
  - `4fecc15875d8ae6d1af20a6fd4bb517ae5d5e8d86fe8dc61f7fa444e0c0aafeb` size=8452 funcs=`VfxXghb` first=`shaders.cache/32024/libraries.data@32448` occurrences=1
  - `1146f6bce067bf72e67ae6a70644f7445059ff075a621a087115ec099bf95779` size=7216 funcs=`luma_log_sum_to_exposure` first=`shaders.cache/32024/libraries.data@41024` occurrences=1
  - `9f52e8f949c229e4d5096cdee842a07478f7351fd2a2016eabf1d4de64bb2bfe` size=10684 funcs=`brnetv3_build_tensors_low_resolution` first=`shaders.cache/32024/libraries.data@48320` occurrences=1
  - `aeacb622d78fdf320fa839eb56bd96ba01874cb08ef82e910ca80365a5311059` size=5716 funcs=`brnetv3_output_temporal` first=`shaders.cache/32024/libraries.data@59072` occurrences=1
  - `ab580da17cfcb758f01a2721899250267b6d434133fd57b5a5b695b968caef50` size=100732 funcs=`A3Xghfc` first=`shaders.cache/32024/libraries.data@65600` occurrences=1
  - `3ac840c6febced2ce49350aa0943fac32106a59fcdb6cfd2c80c7eda770f798a` size=15744 funcs=`brnetv3_lowres_signals` first=`shaders.cache/32024/libraries.data@168000` occurrences=1
  - `843fd4b416aa802d1289b9e0ef5eb8a06e0ddec6ce4651f40020cea8a478a597` size=15312 funcs=`brnetv3_lowres_signals` first=`shaders.cache/32024/libraries.data@183808` occurrences=1
  - `7db4b2d2d9f9488782e309044f4cc8d167d98489906937e6f943a00acb40a63a` size=11272 funcs=`brnetv3_warp_history_high_resolution` first=`shaders.cache/32024/libraries.data@199232` occurrences=1
  - `98a7e60ced3bde823897f2c6257f34e03ba3e15e69d604290f85fa1a582cc816` size=14624 funcs=`brnetv3_anisotropic_gaussian_filter` first=`shaders.cache/32024/libraries.data@210624` occurrences=1
  - `1bffe7e83c4ee3ec44fee7bc0930148ba2ea6f7c95884146caf9ed5a836c7f74` size=5756 funcs=`brnetv3_output_spatial_lr` first=`shaders.cache/32024/libraries.data@225344` occurrences=1
  - `23d4d2ba71b2bf7aa3b0523f8a047cef558ae685403d237e3bdffeefff77f88a` size=9324 funcs=`brnetv3_flow_splat` first=`shaders.cache/32024/libraries.data@233536` occurrences=1
  - `2097a8791aeaa7fe19f701baadaa4f8ed7a773e825f9510fd1c227b9284637a7` size=9324 funcs=`brnetv3_flow_splat` first=`shaders.cache/32024/libraries.data@242944` occurrences=1
  - `cbba259b0e46d7d0957f4b4ca2dd15670a7af7518bd69dc9d8a57417c6df0524` size=7608 funcs=`brnetv3_flow_diff_texture` first=`shaders.cache/32024/libraries.data@252352` occurrences=1
  - `1f6e732af38bf3dfe430692fb93b2ba10bf47495f63858ac33cf8299acfee826` size=6220 funcs=`depthToSpace2d_0_1_2_4_s_` first=`shaders.cache/32024/libraries.data@260032` occurrences=1
  - `aabf794ce2522631b12ecae59b5c7fa84dac222415db4916a7ee9a4e234d9628` size=12180 funcs=`read_0_8__u8` first=`shaders.cache/32024/libraries.data@266368` occurrences=1
  - `a4cd12d4b7185db37aeb9ce2d30075ec6d30626a3c07c8af8b35c020f5bcc00b` size=7084 funcs=`constant_0000000000000000_f` first=`shaders.cache/32024/libraries.data@278656` occurrences=1

## armored-core-vi

- Cache: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155/armored-core-vi/shaders.cache`
- Occurrences: `2107`
- Unique metallibs: `1659`
- Unique bytes: `27535268`
- Function-name histogram:
  - `SATPSMain`: `783`
  - `SATVSMain`: `208`
  - `irconverter_stage_in_shader`: `72`
  - `PixelMain`: `49`
  - `irconverter_hull_shader`: `45`
  - `main`: `36`
  - `irconverter_domain_shader_triangle_passthrough`: `28`
  - `irconverter_dxil_domain_shader`: `28`
  - `postfixPrimaryAccumulate_4xf`: `23`
  - `postfixPrimaryAccumulate_f`: `23`
  - `postfixPrimary_4xf`: `23`
  - `postfixPrimary_f`: `23`
  - `prefixPrimary_4xf`: `23`
  - `prefixPrimary_f`: `23`
  - `middlefixPrimary_4xf`: `22`
  - `middlefixPrimary_f`: `22`
  - `GS_Main`: `15`
  - `irconverter_tessellator`: `15`
  - `prefixSecondary_4xf`: `15`
  - `prefixSecondary_f`: `15`
- First 20 unique blobs:
  - `a23e211370926fd6352b4124cb09f6acbb7151f463f99c05628bedb60b81f8f1` size=9112 funcs=`BclrA3Xhfu` first=`shaders.cache/32024/libraries.data@64` occurrences=1
  - `3cd9b919771cd19bc9ca39be3ae082ab76ab753b69d47ac2335ca2553994bb8e` size=5168 funcs=`VfxXh` first=`shaders.cache/32024/libraries.data@9280` occurrences=1
  - `27c989aaddb8a249180c4b063d566b45f86631ba85d7b22b8a2ff19bc45fe21d` size=12012 funcs=`TimgA3Xhfu_IxrgN3Oc3mtc4nlnlnlXq` first=`shaders.cache/32024/libraries.data@14528` occurrences=1
  - `e351c9295e299a5fdcbc87bf59c334ae1c6daea0d9c31cbf2f628ad65d29762e` size=5620 funcs=`VfxU10Xh` first=`shaders.cache/32024/libraries.data@26624` occurrences=1
  - `0479f004fd54fd00df60f63aec3a21c469660b741ed8bd6047026dfb9dfd1b14` size=8436 funcs=`VfxXghb` first=`shaders.cache/32024/libraries.data@32320` occurrences=1
  - `2c8e4558ca272fe3b5d1383885967096b805f428d78e38c7776da87942574785` size=7528 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@40832` occurrences=1
  - `f4b3898f20fa17c3be4f0e1a283fe0b056d9230745164ee905c5d6e823e18d28` size=7304 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@48448` occurrences=1
  - `fbfbd8bbadf1943506bb1d23600edece575bd325924f3ee1c47bb54f9a89967b` size=9144 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@55872` occurrences=1
  - `808e6be1ffb673d93c50f648399bef9bf96a350f2f30bd7bbf84f8a1c86d6c15` size=97844 funcs=`A3Xghfc` first=`shaders.cache/32024/libraries.data@65600` occurrences=1
  - `139534c17dd162137e431d288f1f0792198439a9dd0b971f05dfd4d69bd6311d` size=19248 funcs=`GS_Main` first=`shaders.cache/32024/libraries.data@163904` occurrences=1
  - `b9c6b3ab8e7e311c84c3169fc222534d4d47548e1e7b087baae75c66cab4b405` size=9864 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@183232` occurrences=1
  - `84a83f40542aa54809af915ec7e16948e466ae175868a85fac205232125d673a` size=7528 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@193216` occurrences=1
  - `48eeea20ee1828566294a997e8bb9885ea1cf2e43b9d35e6bf8c7a03a3375f93` size=7272 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@200832` occurrences=1
  - `8c4c0037e9c72291d17dd4ec8627cbc5b64394f4732785e20d7ea500cd907848` size=9096 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@208192` occurrences=1
  - `3cfe9fc22fe8db8668c28df3c4955fed87e8e4056edbe5d8fe540141f5c14e3f` size=10152 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@217408` occurrences=1
  - `455107c46634a80e53a3355bb7d18d2aae89c0cb2ed94d3ce126315d20b3885d` size=30092 funcs=`GS_Main` first=`shaders.cache/32024/libraries.data@229440` occurrences=1
  - `e53b4f43e808e07053b0c44cf05e0e8cd7040756cccf952a6d14f9d899bf8622` size=7240 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@259648` occurrences=1
  - `7fedf9497f9979519b9058b8bebc410777fe1ee184712f44072cd1707e425bcb` size=9352 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@267008` occurrences=1
  - `a542526904cfa0281d718402b3040bc6e2ab73d4b532645b8de8d45c1cd91fa9` size=7256 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@276480` occurrences=1
  - `d29ebcf5be75fe2880e58afbab4342b86e8bb401c209fa027c8805488ac2e881` size=8056 funcs=`irconverter_hull_shader` first=`shaders.cache/32024/libraries.data@283840` occurrences=1

## subnautica-2

- Cache: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155/subnautica-2/shaders.cache`
- Occurrences: `11500`
- Unique metallibs: `10963`
- Unique bytes: `229395468`
- Function-name histogram:
  - `Main`: `2202`
  - `MainCS`: `1624`
  - `MainPS`: `1052`
  - `SimulateMainComputeCS`: `702`
  - `DeferredLightPixelMain`: `512`
  - `MicropolyRasterize`: `334`
  - `InjectMainPS`: `312`
  - `HWRasterizeMS`: `269`
  - `Main__OPTIMIZED`: `236`
  - `InstanceCull`: `232`
  - `VirtualShadowMapProjection`: `168`
  - `MainVS`: `151`
  - `MainVertexShader__OPTIMIZED`: `101`
  - `ScreenProbeTemporalReprojectionCS`: `100`
  - `LightGridInjectionCS`: `96`
  - `LightScatteringCS`: `96`
  - `PatchSplit`: `96`
  - `ScreenSpaceShortRangeAOCS`: `96`
  - `main`: `96`
  - `MainPixelShader__OPTIMIZED`: `84`
- First 20 unique blobs:
  - `a23e211370926fd6352b4124cb09f6acbb7151f463f99c05628bedb60b81f8f1` size=9112 funcs=`BclrA3Xhfu` first=`shaders.cache/32024/libraries.data@64` occurrences=1
  - `3cd9b919771cd19bc9ca39be3ae082ab76ab753b69d47ac2335ca2553994bb8e` size=5168 funcs=`VfxXh` first=`shaders.cache/32024/libraries.data@9280` occurrences=1
  - `70c3b6a04379244afb795f588c5ab12df402d8d7aa7ae055b509ab2f9bda6e3b` size=11692 funcs=`TimgA3Xhfu_IxrgN3Oc3mtc0nlnlnlXq` first=`shaders.cache/32024/libraries.data@14528` occurrences=1
  - `e351c9295e299a5fdcbc87bf59c334ae1c6daea0d9c31cbf2f628ad65d29762e` size=5620 funcs=`VfxU10Xh` first=`shaders.cache/32024/libraries.data@26304` occurrences=1
  - `0479f004fd54fd00df60f63aec3a21c469660b741ed8bd6047026dfb9dfd1b14` size=8436 funcs=`VfxXghb` first=`shaders.cache/32024/libraries.data@32000` occurrences=1
  - `2a26d6430055cb25d0e5df142cab776705b6a91ca548e0b82d246137cf9f1864` size=16192 funcs=`libMTLHud_vertexShader,libMTLHud_fragmentShader,libMTLHud_fragmentShader_ms` first=`shaders.cache/32024/libraries.data@40512` occurrences=1
  - `6a96c2a57878fe003e540227ba985c435bbb8c4aca77f1a4bf27c3e8497bb6f2` size=8760 funcs=`libMTLHud_CopyVertex,libMTLHud_CopyFragment` first=`shaders.cache/32024/libraries.data@56768` occurrences=1
  - `808e6be1ffb673d93c50f648399bef9bf96a350f2f30bd7bbf84f8a1c86d6c15` size=97844 funcs=`A3Xghfc` first=`shaders.cache/32024/libraries.data@65600` occurrences=1
  - `0df84abe8f148f13cb041208dcfbb075742530c5f2431dc0d95076c2adee24c6` size=15564 funcs=`VoxelizeGS` first=`shaders.cache/32024/libraries.data@163904` occurrences=1
  - `ee48b853fe85b619d364508a6d44ad47909f1bfe8c3c3ff894bf53d06f510f77` size=16412 funcs=`VoxelizeGS` first=`shaders.cache/32024/libraries.data@179584` occurrences=1
  - `71aa89b8f8fc593641032bc3e8328d895d8041aac2e33a31f1fa049605c9bc1d` size=10600 funcs=`TimgA3Xhfu_Isrc` first=`shaders.cache/32024/libraries.data@196672` occurrences=1
  - `27c989aaddb8a249180c4b063d566b45f86631ba85d7b22b8a2ff19bc45fe21d` size=12012 funcs=`TimgA3Xhfu_IxrgN3Oc3mtc4nlnlnlXq` first=`shaders.cache/32024/libraries.data@208960` occurrences=1
  - `413cd44a641cc4693a19889aa8182211e4de48a22c00944252f019906c56f12e` size=15824 funcs=`VoxelizeGS` first=`shaders.cache/32024/libraries.data@221248` occurrences=1
  - `d2110bac05cd420ece202972bdef13248b7f1ac1de04239b0039fd9677b6118d` size=17072 funcs=`VoxelizeGS` first=`shaders.cache/32024/libraries.data@237184` occurrences=1
  - `c2f496a6e5f8652d80df69a34796be0617390d8cd9c89b61aeaf8d0d36e2839d` size=8540 funcs=`MainCS` first=`shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin@176` occurrences=1
  - `7d86874e3950644308680dddb065885a8f2d9f0d741bc28af09012748fdbe5f2` size=5820 funcs=`MainCS` first=`shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin@9168` occurrences=1
  - `0a7cbdc2e33fca457873651d6fe889ba5f729b1312e94790a142ca319008445d` size=6972 funcs=`MainCS` first=`shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin@15440` occurrences=1
  - `122d8724c984f73ecc710e9f0109c18151cc27d7eab2004c4dbade8f3c537d08` size=4524 funcs=`MainCS` first=`shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin@22864` occurrences=1
  - `3a20875e9c1fcb7a3515d921570505c82e2a055736bebee993937220cc5d005a` size=4764 funcs=`MainCS` first=`shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin@27832` occurrences=1
  - `a391fbca1ea379cda88ec8ade47a17663acc4135ac2f8a1d08da432d059c0d89` size=5260 funcs=`MainCS` first=`shaders.cache/MTLGPUFamilyApple9_0/stage_cache.bin@33088` occurrences=1

