# D3DMetal cache oracle map

- D3DMetal reference root: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`
- M12 cache root: `/Users/alexmondello/.metalsharp/shader-cache/m12`

| Game | Bytecode records | Stage metallibs | Linked stages | Linked source bytecodes | M12 raw bytecode overlap | Unlinked stages |
|---|---:|---:|---:|---:|---:|---:|
| elden-ring | 1410 | 1503 | 1416 | 1026 | 717 | 87 |
| armored-core-vi | 1714 | 1854 | 1769 | 1368 | 377 | 85 |
| subnautica-2 | 10453 | 11486 | 10138 | 9446 | 485 | 1348 |

## elden-ring

- bytecode_records: `1410`
- stage_metallibs: `1503`
- linked_stage_records: `1416`
- linked_bytecode_records: `1026`
- m12_raw_bytecode_overlap: `717`
- unlinked_stage_records: `87`
- shader_kind_counts: `{'domain': 21, 'geometry': 2, 'hull': 17, 'pixel': 844, 'vertex': 526}`
- top_function_names: `{'SATPSMain': 688, 'SATVSMain': 469, 'irconverter_stage_in_shader': 77, 'PixelMain': 35, 'main': 26, 'irconverter_domain_shader_triangle_passthrough': 21, 'irconverter_dxil_domain_shader': 21, 'irconverter_hull_shader': 17, 'irconverter_tessellator': 17, 'VertexMain': 11, 'ps_main': 11, 'VertexMain.dxil_irconverter_object_shader': 10, 'PS': 5, 'PS_EnvMapRegion': 5, 'PixelMain_2Sampler': 4, 'vs_main': 3, 'Decal_PS': 2, 'Decal_VS': 2, 'GS_Main': 2, 'PS_DeferredAO': 2}`
- First linked records:
  - bytecode_key=`3fa3d9a9a8c9f2302ebb87913cb36afd` stage=`925c6cfad6917e7d` funcs=`BlendUI_SDR2SDR` dxil=`27bc6d5a9de9046b` kind=`pixel` m12=`6f0e7d2f3cfff83c.dxbc`
  - bytecode_key=`f3d00b65be673710baef67fd62849927` stage=`b5e2cc64ea41f5ac` funcs=`SimpleVS_Single2DTexture` dxil=`a4d1c51b36169304` kind=`vertex` m12=`9211ad3b955bc80f.dxbc`
  - bytecode_key=`d9d733394953d3c6a92f3314531b9e1a` stage=`f529905bd1d68be6` funcs=`PixelMain` dxil=`567a318796856d9f` kind=`pixel` m12=`8ad248bf4baceb18.dxbc`
  - bytecode_key=`4bda91bcc6b35e327cd0f656bea55bab` stage=`1548718784c795ce` funcs=`VertexMain` dxil=`d7d5ede1474a779f` kind=`vertex` m12=`891837a8d7181944.dxbc`
  - bytecode_key=`b03bc2ad61bd10293583cb9f2e5c6ca7` stage=`2397045780005e83` funcs=`SampleBilateralDepthBlurTwoOptPS` dxil=`7b43ab8a49e52b41` kind=`pixel` m12=`c91ab161fe896cf7.dxbc`
  - bytecode_key=`9c638f3fe481b13c50f594525d591ab9` stage=`c05603760703a96b` funcs=`ScreenSpaceQuadScaleShaderVS` dxil=`5381c58b16429f0b` kind=`vertex` m12=`6a9e5c703a1c2930.dxbc`
  - bytecode_key=`0e6d100abaa517f33aa0c47149e8e912` stage=`a3db6c23a54c3f62` funcs=`DepthScale2x2OutputDepthPS` dxil=`e1ae3b173b51cb88` kind=`pixel` m12=`af8026a0f907213b.dxbc`
  - bytecode_key=`00dbffb3fac92adc7d94cb72f07f1ac5` stage=`f6a6e34705a4b0b1` funcs=`ScreenSpaceQuadShaderVS` dxil=`921d4464c738a01c` kind=`vertex` m12=`ed51a0e2d1a25780.dxbc`
  - bytecode_key=`3efeeeb3d286c03c455194f1f85f6bbe` stage=`77d9e00fc4f2ab84` funcs=`PS_CopyShadowMapDepth` dxil=`d5b18abf24a51100` kind=`pixel` m12=`54f77fb39a95460b.dxbc`
  - bytecode_key=`62ec9664e9c16554ff1874d8989896ce` stage=`20c74f486e5ed6d5` funcs=`VS_LightAcc` dxil=`9a7dd8ee38eef8ae` kind=`vertex` m12=`a21b32ee20326307.dxbc`

## armored-core-vi

- bytecode_records: `1714`
- stage_metallibs: `1854`
- linked_stage_records: `1769`
- linked_bytecode_records: `1368`
- m12_raw_bytecode_overlap: `377`
- unlinked_stage_records: `85`
- shader_kind_counts: `{'compute': 24, 'domain': 30, 'geometry': 5, 'hull': 20, 'pixel': 1101, 'vertex': 534}`
- top_function_names: `{'SATPSMain': 948, 'SATVSMain': 473, 'irconverter_stage_in_shader': 74, 'PixelMain': 49, 'main': 36, 'irconverter_domain_shader_triangle_passthrough': 30, 'irconverter_dxil_domain_shader': 30, 'irconverter_hull_shader': 20, 'irconverter_tessellator': 20, 'PS_SingleTexture': 9, 'SATVSMain.dxil_irconverter_object_shader': 9, 'VertexMain': 8, 'Decal_PS': 6, 'PS': 6, 'CS_Main': 5, 'GS_Main': 5, 'VertexMain.dxil_irconverter_object_shader': 5, 'ps_main': 5, 'CS_Emit': 3, 'PS_LightAcc_DirLight1': 3}`
- First linked records:
  - bytecode_key=`6ba340d621a406e266e7ba0e273f5ba1` stage=`eb57a31d9d8abb60` funcs=`ChromaticAberrationAndNoisePs` dxil=`0baf2004acb015be` kind=`pixel` m12=`58539be4844b1dd9.dxbc`
  - bytecode_key=`76e608c9d81f22c51bf40c67efb27d71` stage=`933ee76170bfcf07` funcs=`TitleTexVs` dxil=`ad10450a3829a638` kind=`vertex` m12=`ca33abe9a2d27ce9.dxbc`
  - bytecode_key=`13646f90b532e6a1f676471fd2a0639f` stage=`3d079e7955d211d6` funcs=`BlendUI_SDR2SDR` dxil=`16646b1b60deda90` kind=`pixel` m12=`8fc08bbc2900719b.dxbc`
  - bytecode_key=`f3d00b65be673710baef67fd62849927` stage=`b8ef4a6c742f8ef0` funcs=`SimpleVS_Single2DTexture` dxil=`7d42e3197e5a07e3` kind=`vertex` m12=`237a203b2ac9964b.dxbc`
  - bytecode_key=`d9d733394953d3c6a92f3314531b9e1a` stage=`51476ccd22750985` funcs=`PixelMain` dxil=`e78ecb2754eef366` kind=`pixel` m12=`dcfb1c1fdead360a.dxbc`
  - bytecode_key=`4bda91bcc6b35e327cd0f656bea55bab` stage=`5ff3f647ffc94539` funcs=`VertexMain` dxil=`009765274ab2b81a` kind=`vertex` m12=`e4c66c932853d01b.dxbc`
  - bytecode_key=`b03bc2ad61bd10293583cb9f2e5c6ca7` stage=`8cff7e4d85c70cfa` funcs=`SampleBilateralDepthBlurTwoOptPS` dxil=`4f22037f34aed73e` kind=`pixel` m12=`47d8acd459bb374b.dxbc`
  - bytecode_key=`9c638f3fe481b13c50f594525d591ab9` stage=`45f41017636b5d24` funcs=`ScreenSpaceQuadScaleShaderVS` dxil=`43a1d59f809f479a` kind=`vertex` m12=`8f1aeda1e31af397.dxbc`
  - bytecode_key=`3efeeeb3d286c03c455194f1f85f6bbe` stage=`86d24cd9bccd872e` funcs=`PS_CopyShadowMapDepth` dxil=`0727f155584b87c4` kind=`pixel` m12=`47e9df9d5da4844f.dxbc`
  - bytecode_key=`62ec9664e9c16554ff1874d8989896ce` stage=`4048a9e6dda361c9` funcs=`VS_LightAcc` dxil=`2ae6b88e36628a0e` kind=`vertex` m12=`0eb989d777dbdad2.dxbc`

## subnautica-2

- bytecode_records: `10453`
- stage_metallibs: `11486`
- linked_stage_records: `10138`
- linked_bytecode_records: `9446`
- m12_raw_bytecode_overlap: `485`
- unlinked_stage_records: `1348`
- shader_kind_counts: `{'compute': 6485, 'geometry': 5, 'pixel': 2945, 'unknown': 257, 'vertex': 761}`
- top_function_names: `{'Main': 2254, 'MainCS': 1646, 'MainPS': 1121, 'SimulateMainComputeCS': 702, 'DeferredLightPixelMain': 512, 'MicropolyRasterize': 346, 'InjectMainPS': 312, 'VirtualShadowMapProjection': 296, 'HWRasterizeMS': 269, 'Main__OPTIMIZED': 264, 'InstanceCull': 256, 'MainVS': 157, 'MainPixelShader__OPTIMIZED': 128, 'ScreenProbeTemporalReprojectionCS': 120, 'main': 120, 'HWRasterizePS': 114, 'PatchSplit': 112, 'MainVertexShader__OPTIMIZED': 105, 'HZBBuildCS': 96, 'LightGridInjectionCS': 96}`
- First linked records:
  - bytecode_key=`b183a62ee32fae8184601c90249dea94` stage=`c2f496a6e5f8652d` funcs=`MainCS` dxil=`c8b8fadb23dcc6e4` kind=`compute` m12=`7ee74679203976c5.dxbc`
  - bytecode_key=`34631b7a6eae7ad71770ef07cec564ca` stage=`7d86874e39506443` funcs=`MainCS` dxil=`efea231ab5732b51` kind=`compute` m12=`1f466afc875459a9.dxbc`
  - bytecode_key=`d4035fa1345f94b3ef430296979118df` stage=`0a7cbdc2e33fca45` funcs=`MainCS` dxil=`5d676a73cb26cdb9` kind=`compute` m12=`1285001a93fce9ca.dxbc`
  - bytecode_key=`4847a20a9ee6ce34d3d1821618334a7b` stage=`122d8724c984f73e` funcs=`MainCS` dxil=`5eabb639eff81013` kind=`compute` m12=`61223ad2026a88fb.dxbc`
  - bytecode_key=`b4bcca4981be41718937e4c8bef991b4` stage=`3a20875e9c1fcb7a` funcs=`MainCS` dxil=`d875150b60862088` kind=`compute` m12=`1b351613178f66de.dxbc`
  - bytecode_key=`1d452bf8feff413555459f8ab5724e12` stage=`a391fbca1ea379cd` funcs=`MainCS` dxil=`28a857d22177cd33` kind=`compute` m12=`075f7e5339f6d495.dxbc`
  - bytecode_key=`4e6071f53f43d7b0bb27d84faa2a75fd` stage=`8ad3122ee2beed5a` funcs=`MainCS` dxil=`cb5adb482b823199` kind=`compute` m12=`ea7212c597dd84a2.dxbc`
  - bytecode_key=`2cc8316f1fb3c00b1abd9b3b45f5d587` stage=`1e54c69b6ff2d2d9` funcs=`MainCS` dxil=`264c6b23d83651db` kind=`compute` m12=`f11fbdf3cbc5775c.dxbc`
  - bytecode_key=`bd1c191954ba474a4f01311f0570b9fa` stage=`7397953292841cf2` funcs=`MainCS` dxil=`10d440b1ff5e9d4d` kind=`compute` m12=`dd136921f7da3fa3.dxbc`
  - bytecode_key=`ff993927aa66e2fb575299ab632d9445` stage=`dc393e711652165f` funcs=`MainCS` dxil=`853b2a311bb3f241` kind=`compute` m12=`05b1c1933b3d752f.dxbc`

