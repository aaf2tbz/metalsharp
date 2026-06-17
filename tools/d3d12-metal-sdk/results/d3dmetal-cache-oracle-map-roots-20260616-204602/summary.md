# D3DMetal cache oracle map

- D3DMetal reference root: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`
- M12 cache root: `/Users/alexmondello/.metalsharp/shader-cache/m12`

| Game | Bytecode records | Stage metallibs | Pipeline records | Root signatures | Linked stages | Linked source bytecodes | M12 raw bytecode overlap | Unlinked stages |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| elden-ring | 1410 | 1503 | 1134 | 8 | 1416 | 1026 | 717 | 87 |
| armored-core-vi | 1714 | 1854 | 1649 | 9 | 1769 | 1368 | 1087 | 85 |
| subnautica-2 | 10453 | 11486 | 11229 | 234 | 10138 | 9446 | 485 | 1348 |

## elden-ring

- bytecode_records: `1410`
- stage_metallibs: `1503`
- pipeline_records: `1134`
- root_signature_records: `8`
- pipeline_records_with_bytecode_keys: `1134`
- pipeline_bytecode_key_refs: `2280`
- linked_stage_records: `1416`
- linked_bytecode_records: `1026`
- m12_raw_bytecode_overlap: `717`
- unlinked_stage_records: `87`
- shader_kind_counts: `{'domain': 21, 'geometry': 2, 'hull': 17, 'pixel': 844, 'vertex': 526}`
- top_function_names: `{'SATPSMain': 688, 'SATVSMain': 469, 'irconverter_stage_in_shader': 77, 'PixelMain': 35, 'main': 26, 'irconverter_domain_shader_triangle_passthrough': 21, 'irconverter_dxil_domain_shader': 21, 'irconverter_hull_shader': 17, 'irconverter_tessellator': 17, 'VertexMain': 11, 'ps_main': 11, 'VertexMain.dxil_irconverter_object_shader': 10, 'PS': 5, 'PS_EnvMapRegion': 5, 'PixelMain_2Sampler': 4, 'vs_main': 3, 'Decal_PS': 2, 'Decal_VS': 2, 'GS_Main': 2, 'PS_DeferredAO': 2}`
- First pipeline records with bytecode keys:
  - pipeline=`2ebc18d7906a3e43b2865d65b3143110` offset=`0` refs=`vertex:f3d00b65be673710baef67fd62849927, pixel:3fa3d9a9a8c9f2302ebb87913cb36afd`
  - pipeline=`ab6c832284cd1ce0070edabccbc05832` offset=`160` refs=`vertex:4bda91bcc6b35e327cd0f656bea55bab, pixel:d9d733394953d3c6a92f3314531b9e1a`
  - pipeline=`82fdce488ad1941a1a87e33fe9f2b73c` offset=`320` refs=`vertex:9c638f3fe481b13c50f594525d591ab9, pixel:b03bc2ad61bd10293583cb9f2e5c6ca7`
  - pipeline=`0ca0e1a978797b7d6dc3b0b3f7ba2cdc` offset=`480` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:0e6d100abaa517f33aa0c47149e8e912`
  - pipeline=`41a5cb86d4acaeeae5cfc864e4b26354` offset=`640` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:3efeeeb3d286c03c455194f1f85f6bbe`
  - pipeline=`923293b86d8ec448be54e364e4b26354` offset=`800` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:e36c236a90ba3818dc4e0ce48052082b`
  - pipeline=`41c82d66db196772ea4582b3f7ba2cdc` offset=`960` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:62017073d4923494d5c3f8a202114f64`
  - pipeline=`90b7b019f8f314679a455264e4b26354` offset=`1120` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:af8b42fe743a8d1dfba06e29860a6a7f`
  - pipeline=`5d60d2de0012c69e5a56d964e4b26354` offset=`1280` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:4b5474e0c16081df080ff03d571c0c26`
  - pipeline=`2ffed9fbf2087dd91264da64e4b26354` offset=`1440` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:9ec5ea5148b62ca8db8190646bf15da4`
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
- pipeline_records: `1649`
- root_signature_records: `9`
- pipeline_records_with_bytecode_keys: `1649`
- pipeline_bytecode_key_refs: `3371`
- linked_stage_records: `1769`
- linked_bytecode_records: `1368`
- m12_raw_bytecode_overlap: `1087`
- unlinked_stage_records: `85`
- shader_kind_counts: `{'compute': 24, 'domain': 30, 'geometry': 5, 'hull': 20, 'pixel': 1101, 'vertex': 534}`
- top_function_names: `{'SATPSMain': 948, 'SATVSMain': 473, 'irconverter_stage_in_shader': 74, 'PixelMain': 49, 'main': 36, 'irconverter_domain_shader_triangle_passthrough': 30, 'irconverter_dxil_domain_shader': 30, 'irconverter_hull_shader': 20, 'irconverter_tessellator': 20, 'PS_SingleTexture': 9, 'SATVSMain.dxil_irconverter_object_shader': 9, 'VertexMain': 8, 'Decal_PS': 6, 'PS': 6, 'CS_Main': 5, 'GS_Main': 5, 'VertexMain.dxil_irconverter_object_shader': 5, 'ps_main': 5, 'CS_Emit': 3, 'PS_LightAcc_DirLight1': 3}`
- First pipeline records with bytecode keys:
  - pipeline=`d5a90b1f2dfad5cf0c9a4fc9d56c24c5` offset=`0` refs=`vertex:76e608c9d81f22c51bf40c67efb27d71, pixel:6ba340d621a406e266e7ba0e273f5ba1`
  - pipeline=`ab30502e4424608fb2865d65b3143110` offset=`160` refs=`vertex:f3d00b65be673710baef67fd62849927, pixel:13646f90b532e6a1f676471fd2a0639f`
  - pipeline=`4a52e71bc8e15832070edabccbc05832` offset=`320` refs=`vertex:4bda91bcc6b35e327cd0f656bea55bab, pixel:d9d733394953d3c6a92f3314531b9e1a`
  - pipeline=`8d12d1981ef20f257693e33fe9f2b73c` offset=`480` refs=`vertex:9c638f3fe481b13c50f594525d591ab9, pixel:b03bc2ad61bd10293583cb9f2e5c6ca7`
  - pipeline=`a972fbb38a658bb4e5cfc864e4b26354` offset=`640` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:3efeeeb3d286c03c455194f1f85f6bbe`
  - pipeline=`b6177bfd666f83cbd1ffb0b3f7ba2cdc` offset=`800` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:0e6d100abaa517f33aa0c47149e8e912`
  - pipeline=`f7b3f208e9cf1b34629781b3f7ba2cdc` offset=`960` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:09509e0c578b6e40b21baf683a2ed4c1`
  - pipeline=`945ae9488ba32120120ae164e4b26354` offset=`1120` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:db9cedd7d57a401ccc98b5499157ce04`
  - pipeline=`71b5755fdf3d791d14f4772f45ee1c0c` offset=`1280` refs=`vertex:2858ee2f489d1a0c9251fcd4a60b8db7, pixel:0f8671f1ff03eb897b65ebe0acd4c675`
  - pipeline=`6f5bccc0d1ec5ffee6fd5f64e4b26354` offset=`1440` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:6946c3990de16dda18367d722c517f77`
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
- pipeline_records: `11229`
- root_signature_records: `234`
- pipeline_records_with_bytecode_keys: `11229`
- pipeline_bytecode_key_refs: `16226`
- linked_stage_records: `10138`
- linked_bytecode_records: `9446`
- m12_raw_bytecode_overlap: `485`
- unlinked_stage_records: `1348`
- shader_kind_counts: `{'compute': 6485, 'geometry': 5, 'pixel': 2945, 'unknown': 257, 'vertex': 761}`
- top_function_names: `{'Main': 2254, 'MainCS': 1646, 'MainPS': 1121, 'SimulateMainComputeCS': 702, 'DeferredLightPixelMain': 512, 'MicropolyRasterize': 346, 'InjectMainPS': 312, 'VirtualShadowMapProjection': 296, 'HWRasterizeMS': 269, 'Main__OPTIMIZED': 264, 'InstanceCull': 256, 'MainVS': 157, 'MainPixelShader__OPTIMIZED': 128, 'ScreenProbeTemporalReprojectionCS': 120, 'main': 120, 'HWRasterizePS': 114, 'PatchSplit': 112, 'MainVertexShader__OPTIMIZED': 105, 'HZBBuildCS': 96, 'LightGridInjectionCS': 96}`
- First pipeline records with bytecode keys:
  - pipeline=`fb6b4a633aca65a481cda42ee305ae81` offset=`0` refs=`compute:b183a62ee32fae8184601c90249dea94`
  - pipeline=`ea8520e2ccfab60d14221a7a6e847ad7` offset=`128` refs=`compute:34631b7a6eae7ad71770ef07cec564ca`
  - pipeline=`fbd58f026d64afda54ad5ea1347594b3` offset=`256` refs=`compute:d4035fa1345f94b3ef430296979118df`
  - pipeline=`5c42b750b049eff5d8b3a20a9eccce34` offset=`384` refs=`compute:4847a20a9ee6ce34d3d1821618334a7b`
  - pipeline=`b223159aa3755f2264bbcb4981944171` offset=`512` refs=`compute:b4bcca4981be41718937e4c8bef991b4`
  - pipeline=`53ba2cb749b14da21d712af8fed54135` offset=`640` refs=`compute:1d452bf8feff413555459f8ab5724e12`
  - pipeline=`442a88397ac27f2f4e5470f53f69d7b0` offset=`768` refs=`compute:4e6071f53f43d7b0bb27d84faa2a75fd`
  - pipeline=`30801608a26bb4c99c78306f1f99c00b` offset=`896` refs=`compute:2cc8316f1fb3c00b1abd9b3b45f5d587`
  - pipeline=`2e44469055dd783beda219195490474a` offset=`1024` refs=`compute:bd1c191954ba474a4f01311f0570b9fa`
  - pipeline=`7cf4412d5c07817c9fa83827aa4ce2fb` offset=`1152` refs=`compute:ff993927aa66e2fb575299ab632d9445`
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
