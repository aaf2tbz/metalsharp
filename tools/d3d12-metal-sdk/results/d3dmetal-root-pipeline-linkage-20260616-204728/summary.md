# D3DMetal root ↔ bytecode ↔ pipeline linkage

- D3DMetal reference root: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`

| Game | Roots | Bytecode | Bytecode RTS0 | Bytecode matched to root cache | Pipelines | Unique root pipelines | Ambiguous root pipelines |
|---|---:|---:|---:|---:|---:|---:|---:|
| elden-ring | 8 | 1410 | 1026 | 1026 | 1134 | 1134 | 0 |
| armored-core-vi | 9 | 1714 | 1362 | 1362 | 1649 | 1649 | 0 |
| subnautica-2 | 234 | 10453 | 0 | 0 | 11229 | 9417 | 0 |

## elden-ring

- root inference status counts: `{'unique': 1134}`
- First uniquely root-linked pipelines:
  - pipeline=`2ebc18d7906a3e43b2865d65b3143110` offset=`0` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:f3d00b65be673710baef67fd62849927, pixel:3fa3d9a9a8c9f2302ebb87913cb36afd` status=`unique`
  - pipeline=`ab6c832284cd1ce0070edabccbc05832` offset=`160` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:4bda91bcc6b35e327cd0f656bea55bab, pixel:d9d733394953d3c6a92f3314531b9e1a` status=`unique`
  - pipeline=`82fdce488ad1941a1a87e33fe9f2b73c` offset=`320` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:9c638f3fe481b13c50f594525d591ab9, pixel:b03bc2ad61bd10293583cb9f2e5c6ca7` status=`unique`
  - pipeline=`0ca0e1a978797b7d6dc3b0b3f7ba2cdc` offset=`480` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:0e6d100abaa517f33aa0c47149e8e912` status=`unique`
  - pipeline=`41a5cb86d4acaeeae5cfc864e4b26354` offset=`640` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:3efeeeb3d286c03c455194f1f85f6bbe` status=`unique`
  - pipeline=`923293b86d8ec448be54e364e4b26354` offset=`800` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:e36c236a90ba3818dc4e0ce48052082b` status=`unique`
  - pipeline=`41c82d66db196772ea4582b3f7ba2cdc` offset=`960` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:62017073d4923494d5c3f8a202114f64` status=`unique`
  - pipeline=`90b7b019f8f314679a455264e4b26354` offset=`1120` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:af8b42fe743a8d1dfba06e29860a6a7f` status=`unique`
  - pipeline=`5d60d2de0012c69e5a56d964e4b26354` offset=`1280` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:4b5474e0c16081df080ff03d571c0c26` status=`unique`
  - pipeline=`2ffed9fbf2087dd91264da64e4b26354` offset=`1440` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:9ec5ea5148b62ca8db8190646bf15da4` status=`unique`
  - pipeline=`eadf6ac752c26936c83d792f45ee1c0c` offset=`1600` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:2858ee2f489d1a0c9251fcd4a60b8db7, pixel:839778188a9440e303e438217262e1ea` status=`unique`
  - pipeline=`406a9c37341e63755cfdecf12c226271` offset=`1760` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:3607acf121516471bc3d1e3df779f51a, pixel:2fa907ce9d85610929b05b1db25815f0` status=`unique`

## armored-core-vi

- root inference status counts: `{'unique_psv_resources': 5, 'unique': 1644}`
- First uniquely root-linked pipelines:
  - pipeline=`d5a90b1f2dfad5cf0c9a4fc9d56c24c5` offset=`0` roots=`99050a109bdb87e3bde1d89affae0045` refs=`vertex:76e608c9d81f22c51bf40c67efb27d71, pixel:6ba340d621a406e266e7ba0e273f5ba1` status=`unique_psv_resources` psv_score=[1, 4, 104]
  - pipeline=`ab30502e4424608fb2865d65b3143110` offset=`160` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:f3d00b65be673710baef67fd62849927, pixel:13646f90b532e6a1f676471fd2a0639f` status=`unique`
  - pipeline=`4a52e71bc8e15832070edabccbc05832` offset=`320` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:4bda91bcc6b35e327cd0f656bea55bab, pixel:d9d733394953d3c6a92f3314531b9e1a` status=`unique`
  - pipeline=`8d12d1981ef20f257693e33fe9f2b73c` offset=`480` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:9c638f3fe481b13c50f594525d591ab9, pixel:b03bc2ad61bd10293583cb9f2e5c6ca7` status=`unique`
  - pipeline=`a972fbb38a658bb4e5cfc864e4b26354` offset=`640` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:3efeeeb3d286c03c455194f1f85f6bbe` status=`unique`
  - pipeline=`b6177bfd666f83cbd1ffb0b3f7ba2cdc` offset=`800` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:0e6d100abaa517f33aa0c47149e8e912` status=`unique`
  - pipeline=`f7b3f208e9cf1b34629781b3f7ba2cdc` offset=`960` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:00dbffb3fac92adc7d94cb72f07f1ac5, pixel:09509e0c578b6e40b21baf683a2ed4c1` status=`unique`
  - pipeline=`945ae9488ba32120120ae164e4b26354` offset=`1120` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:db9cedd7d57a401ccc98b5499157ce04` status=`unique`
  - pipeline=`71b5755fdf3d791d14f4772f45ee1c0c` offset=`1280` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:2858ee2f489d1a0c9251fcd4a60b8db7, pixel:0f8671f1ff03eb897b65ebe0acd4c675` status=`unique`
  - pipeline=`6f5bccc0d1ec5ffee6fd5f64e4b26354` offset=`1440` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:6946c3990de16dda18367d722c517f77` status=`unique`
  - pipeline=`24aa2310b887e350b678da64e4b26354` offset=`1600` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:9ec5ea5148b62ca8db8190646bf15da4` status=`unique`
  - pipeline=`e5905d62d39309a25e23c264e4b26354` offset=`1760` roots=`58eddcc40689df70e8af0a5d9483c4ef` refs=`vertex:62ec9664e9c16554ff1874d8989896ce, pixel:7e3620fc5504f62fb0a73e16840565d9` status=`unique`

## subnautica-2

- root inference status counts: `{'unique_psv_resources': 9417, 'none': 1812}`
- First uniquely root-linked pipelines:
  - pipeline=`fb6b4a633aca65a481cda42ee305ae81` offset=`0` roots=`4efd6756f67b56bdb4cb4b9aada0366c` refs=`compute:b183a62ee32fae8184601c90249dea94` status=`unique_psv_resources` psv_score=[0, 3, 81]
  - pipeline=`ea8520e2ccfab60d14221a7a6e847ad7` offset=`128` roots=`4efd6756f67b56bdb4cb4b9aada0366c` refs=`compute:34631b7a6eae7ad71770ef07cec564ca` status=`unique_psv_resources` psv_score=[0, 3, 81]
  - pipeline=`fbd58f026d64afda54ad5ea1347594b3` offset=`256` roots=`4efd6756f67b56bdb4cb4b9aada0366c` refs=`compute:d4035fa1345f94b3ef430296979118df` status=`unique_psv_resources` psv_score=[0, 3, 81]
  - pipeline=`5c42b750b049eff5d8b3a20a9eccce34` offset=`384` roots=`4efd6756f67b56bdb4cb4b9aada0366c` refs=`compute:4847a20a9ee6ce34d3d1821618334a7b` status=`unique_psv_resources` psv_score=[0, 3, 81]
  - pipeline=`b223159aa3755f2264bbcb4981944171` offset=`512` roots=`b3b19d3d72cf76c0a90265cf780b4229` refs=`compute:b4bcca4981be41718937e4c8bef991b4` status=`unique_psv_resources` psv_score=[0, 4, 113]
  - pipeline=`53ba2cb749b14da21d712af8fed54135` offset=`640` roots=`b3b19d3d72cf76c0a90265cf780b4229` refs=`compute:1d452bf8feff413555459f8ab5724e12` status=`unique_psv_resources` psv_score=[0, 4, 113]
  - pipeline=`442a88397ac27f2f4e5470f53f69d7b0` offset=`768` roots=`b3b19d3d72cf76c0a90265cf780b4229` refs=`compute:4e6071f53f43d7b0bb27d84faa2a75fd` status=`unique_psv_resources` psv_score=[0, 4, 113]
  - pipeline=`30801608a26bb4c99c78306f1f99c00b` offset=`896` roots=`b3b19d3d72cf76c0a90265cf780b4229` refs=`compute:2cc8316f1fb3c00b1abd9b3b45f5d587` status=`unique_psv_resources` psv_score=[0, 4, 113]
  - pipeline=`2e44469055dd783beda219195490474a` offset=`1024` roots=`21ebfdf6f83f461b2cf9c8a4bae94010` refs=`compute:bd1c191954ba474a4f01311f0570b9fa` status=`unique_psv_resources` psv_score=[0, 1, 16]
  - pipeline=`7cf4412d5c07817c9fa83827aa4ce2fb` offset=`1152` roots=`81445e9cf8a401875969c81c6c47a64d` refs=`compute:ff993927aa66e2fb575299ab632d9445` status=`unique_psv_resources` psv_score=[0, 2, 17]
  - pipeline=`9fc9124f851dba3c5444d8785274d1b8` offset=`1280` roots=`81445e9cf8a401875969c81c6c47a64d` refs=`compute:74eed978525ed1b8851112d54929f0bb` status=`unique_psv_resources` psv_score=[0, 2, 17]
  - pipeline=`2c080cebdd37290fe369cb6aa2d334ea` offset=`1408` roots=`997d0f0ec546b70d11a14ce4acc65018` refs=`compute:e363ca6aa2f934ea83436d67fb092f72` status=`unique_psv_resources` psv_score=[0, 3, 112]
