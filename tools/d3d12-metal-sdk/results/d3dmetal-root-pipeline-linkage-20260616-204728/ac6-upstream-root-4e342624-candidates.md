# AC6 D3DMetal upstream-root pipeline candidates

- source linkage manifest: `tools/d3d12-metal-sdk/results/d3dmetal-root-pipeline-linkage-20260616-204728/manifest.json`
- target root key: `4e3426240db34ed78814503de9ddc482`
- structural signature: `900ad54a33337bbf7c47a50da7a0da77d215ce12ddc79692b5d26dc26a7a194f`
- candidate pipeline records: `27`

## Root layout

- root[0] cbv vis=vertex reg=0 space=0
- root[1] cbv vis=pixel reg=1 space=0
- root[2] table vis=pixel: srv base=0 space=0 count=4 off=0
- root[3] table vis=pixel: sampler base=0 space=0 count=4 off=0

## Candidate D3DMetal pipelines

- pipeline ordinal `50` offset `7952` key `aba6e1e049738eaa62d62fa373a810d7` status `unique`
  - vertex: key `ded829a37edb16d7068e6844498f14ec` bytecode_ordinal `66` size `2188` sha256 `7ef2fcd687fcec90062f20a5efb36244d5cedfe1e0ed6339fe33ce41bd0c7187` funcs `main`
  - pixel: key `c6f17799fda0e90e73c7a95d2103eb78` bytecode_ordinal `64` size `1468` sha256 `feaf39ac6fd5b4ffab2bf813649e8b8073cabba8dd391a90044a48a108237a58` funcs `main`
- pipeline ordinal `151` offset `24016` key `b439ce801dbe3c4c920359082f38ad78` status `unique`
  - vertex: key `caae5e08224bab782670c15bdf7fd5f6` bytecode_ordinal `210` size `2468` sha256 `b30612f8397ccacef5c7730348fd988fa30055b7cd4b04536646707466caa2ca` funcs `main`
  - pixel: key `ac1dfab60d7a8850957b46ce0cdef2c2` bytecode_ordinal `209` size `1908` sha256 `e0409e1f076d5116282a1ea6f3de4322c90ed6d2c93c6ce2fc7a589662646843` funcs `main`
- pipeline ordinal `177` offset `28128` key `3b75536622b0cbffeb9be85772c42fc6` status `unique`
  - vertex: key `a315e0577fb729c620606d8a3a6c726b` bytecode_ordinal `241` size `2588` sha256 `ae01b46cccff361105af9f32ddb9b2e0d9e1378c3f629e9442fba61026cb08db` funcs `main`
  - pixel: key `6416d5c294dff30498245e4f74c62961` bytecode_ordinal `240` size `2248` sha256 `170ca148451e7002ff696300282a60c73731d89aa6c823355cfc684b1121c696` funcs `main`
- pipeline ordinal `198` offset `31488` key `9b3a7c8f447d3640a04a7f112f369fd4` status `unique`
  - vertex: key `84697811224599d4ff670a51172a17b2` bytecode_ordinal `268` size `2332` sha256 `295a9a14472fa5cf0456ab97839d173ef702d0e33c465452b6d59d8be2cdf342` funcs `main`
  - pixel: key `a87d24b510ca614c6b80f7eff549c770` bytecode_ordinal `267` size `1864` sha256 `7eb25f4cd175b24efb4ec18466b3762ccf88d7ccf3248cfd25935540f547a959` funcs `main`
- pipeline ordinal `200` offset `31840` key `abc07a4c2c096015e8a33fc9d56c24c5` status `unique_psv_resources`
  - vertex: key `76e608c9d81f22c51bf40c67efb27d71` bytecode_ordinal `1` size `3950` sha256 `ad10450a3829a63810f36e86092e59def1a4f717af54e09a4bdb7a2ef1eaf96c` funcs `TitleTexVs`
  - pixel: key `e1ce28f3f52bc6c8d0796ec11095bc8e` bytecode_ordinal `270` size `4264` sha256 `2c28a0a453572afeb738faa5125097685ebddcc1fa5d060f4834250c329481e7` funcs `WriteTexAlphaMergePs`
  - PSV resources: sampler[pixel:0:0-0], sampler[pixel:0:1-1], srv[pixel:0:0-0], srv[pixel:0:1-1]; best_score=[1, 4, 10]
- pipeline ordinal `202` offset `32160` key `1af67b0c7889b22e6c0931c9d56c24c5` status `unique_psv_resources`
  - vertex: key `76e608c9d81f22c51bf40c67efb27d71` bytecode_ordinal `1` size `3950` sha256 `ad10450a3829a63810f36e86092e59def1a4f717af54e09a4bdb7a2ef1eaf96c` funcs `TitleTexVs`
  - pixel: key `900265d8b7fabeb277b0b8a3c8aed4a0` bytecode_ordinal `271` size `4484` sha256 `1d04b9ad658b03d4f5a26f25fc35a051a66834f6fb48869b1be25622aa45b705` funcs `WriteTexMergePs`
  - PSV resources: sampler[pixel:0:0-0], sampler[pixel:0:1-1], srv[pixel:0:0-0], srv[pixel:0:1-1]; best_score=[1, 4, 10]
- pipeline ordinal `522` offset `84056` key `c70e46ef9cbebb41470961313abc55ee` status `unique`
  - vertex: key `9b21663137cf53eecf9a656da26949c9` bytecode_ordinal `623` size `2364` sha256 `e3dda3206a95e92fbfec15719cf8c0213982d825ed705df9825c51eaad345464` funcs `main`
  - pixel: key `1837524176a3699a3ee903405259c7f7` bytecode_ordinal `622` size `1896` sha256 `9b83fc736658e6dd725f7b898696ecc9343fec976441bd8e90e03c21b4131bc1` funcs `main`
- pipeline ordinal `769` offset `123688` key `6c32bf6094096ee9cd20373697320b4a` status `unique`
  - vertex: key `c59c3f369a410d4a29b05a89c4547e8e` bytecode_ordinal `897` size `3068` sha256 `d466259deb9031c033d0e92cc5bdb2d745928e310c3fcb7c49d28d0d35bdd2bb` funcs `main`
  - pixel: key `6416d5c294dff30498245e4f74c62961` bytecode_ordinal `240` size `2248` sha256 `170ca148451e7002ff696300282a60c73731d89aa6c823355cfc684b1121c696` funcs `main`
- pipeline ordinal `770` offset `123848` key `4227524b54e9acaa8e2db67b8442fa2a` status `unique`
  - vertex: key `0adbb27b8931fc2af9b83f8218782ea8` bytecode_ordinal `899` size `2220` sha256 `7981555804fdf0a291ffbec2f1157b73f5177dac312486542337fb3782eb9f1a` funcs `main`
  - pixel: key `12c5207bfa0bbd86e235f76b834af475` bytecode_ordinal `898` size `1512` sha256 `8eb8e56dd5ec379583bdee0abef5a83708b0377bb5b5a021145ce8cf8c08f367` funcs `main`
- pipeline ordinal `771` offset `124008` key `f31a0d430bfd9d390f95ebb5f89c21fc` status `unique`
  - vertex: key `735befb5f5ef27fc21dc7afbac6f3dad` bytecode_ordinal `901` size `1820` sha256 `707ddceb71618412da3c5768fe5a5f1b46992dc02bca75f28ed1d045f2b7dc93` funcs `main`
  - pixel: key `12c5207bfa0bbd86e235f76b834af475` bytecode_ordinal `898` size `1512` sha256 `8eb8e56dd5ec379583bdee0abef5a83708b0377bb5b5a021145ce8cf8c08f367` funcs `main`
- pipeline ordinal `772` offset `124168` key `28b245396a2b58d9c18828f6e74fc2ad` status `unique`
  - vertex: key `1ded2ff6ea3cc4ad06c61aee8e021283` bytecode_ordinal `903` size `2260` sha256 `a302f7616e28a1009dc1577dc36c75dd595776373211db404f5fb05045d35003` funcs `main`
  - pixel: key `6e9c3c46213492fe9b2729e03c49b3fb` bytecode_ordinal `902` size `2172` sha256 `9b3b53943c86e68ad79fe9d3b538d376759fddedce580d771048e5ab4d2ded99` funcs `main`
- pipeline ordinal `774` offset `124488` key `1fc0bbc5bbcbcb087999988fbf0d17b2` status `unique`
  - vertex: key `0d659f8fb27e11b26b1ec9351c23ed83` bytecode_ordinal `904` size `3000` sha256 `255a109b256d1cc47e17dfeb18b777b311492923b09a9aa71549af8e0aa8aa0a` funcs `main`
  - pixel: key `ac1dfab60d7a8850957b46ce0cdef2c2` bytecode_ordinal `209` size `1908` sha256 `e0409e1f076d5116282a1ea6f3de4322c90ed6d2c93c6ce2fc7a589662646843` funcs `main`
- pipeline ordinal `775` offset `124648` key `3f783055da5a36fb4cf2c7b6e5f112d7` status `unique`
  - vertex: key `289dc0b6e88214d7ba6d3de1356a455d` bytecode_ordinal `905` size `2868` sha256 `81904c43bedb2b61e630e274b4fb37499f1a4c76d103a90504c39927ef4fe54d` funcs `main`
  - pixel: key `1837524176a3699a3ee903405259c7f7` bytecode_ordinal `622` size `1896` sha256 `9b83fc736658e6dd725f7b898696ecc9343fec976441bd8e90e03c21b4131bc1` funcs `main`
- pipeline ordinal `776` offset `124808` key `44729854c9690c211cfd75c6d80ba177` status `unique`
  - vertex: key `3c3b72c6d578a7777757e31d5f71f026` bytecode_ordinal `906` size `2868` sha256 `b7bc3933a98103986678d7db973bc7ba45c207d5f09edf1c446c0e2246661840` funcs `main`
  - pixel: key `ac1dfab60d7a8850957b46ce0cdef2c2` bytecode_ordinal `209` size `1908` sha256 `e0409e1f076d5116282a1ea6f3de4322c90ed6d2c93c6ce2fc7a589662646843` funcs `main`
- pipeline ordinal `779` offset `125320` key `370d71b89897600bbbcf4c6f88a79285` status `unique`
  - vertex: key `27ea4a6f85d4948550ca10c7fec588ae` bytecode_ordinal `908` size `2540` sha256 `f57af19cf3b67ddd6f07ac93ca2cf464b299ea79edad1cb1bb8e859d0559208f` funcs `main`
  - pixel: key `c6f17799fda0e90e73c7a95d2103eb78` bytecode_ordinal `64` size `1468` sha256 `feaf39ac6fd5b4ffab2bf813649e8b8073cabba8dd391a90044a48a108237a58` funcs `main`
- pipeline ordinal `780` offset `125480` key `169bc32cfb1c811e1b449dfc128bfa34` status `unique`
  - vertex: key `f77f9afc1ff8fc34736537b0156d5c0e` bytecode_ordinal `909` size `2716` sha256 `9efd1feb9900090c579db2d34625f3de87e403246cf8f29ff75409340d4dc439` funcs `main`
  - pixel: key `a87d24b510ca614c6b80f7eff549c770` bytecode_ordinal `267` size `1864` sha256 `7eb25f4cd175b24efb4ec18466b3762ccf88d7ccf3248cfd25935540f547a959` funcs `main`
- pipeline ordinal `781` offset `125640` key `56f743fe70921517242877a4ae2b3a6e` status `unique`
  - vertex: key `d4c972a4a3583c6e4c3b1ae6c4753e6e` bytecode_ordinal `911` size `1972` sha256 `9c114168b83443f85a9b06a6ca0b3d26dcd2e1717eb8b25832f9e2f59cb3ea30` funcs `main`
  - pixel: key `78adbd7b6452a243eec33b552fa6f52f` bytecode_ordinal `910` size `1740` sha256 `61feff43fe1962a46d1442ecf9734296541e02f27985852e668c863cfe1f5f76` funcs `main`
- pipeline ordinal `782` offset `125800` key `c23648a902724ebdcf9bfc97ba27196c` status `unique`
  - vertex: key `63a3fa97b7541f6ce124b005f5a47b0a` bytecode_ordinal `912` size `2608` sha256 `241567158a30a27d36a3c521de4ae63593d1ab3ce3d2c2cc184cb4ea9047ecc5` funcs `main`
  - pixel: key `c6f17799fda0e90e73c7a95d2103eb78` bytecode_ordinal `64` size `1468` sha256 `feaf39ac6fd5b4ffab2bf813649e8b8073cabba8dd391a90044a48a108237a58` funcs `main`
- pipeline ordinal `1462` offset `234904` key `49b5423558aa60766baecd5bba39d95d` status `unique`
  - vertex: key `d309dc5bb74adf5dbe34ed52752ef092` bytecode_ordinal `1544` size `3056` sha256 `2fd53c915c5474608b12ffc524c1a0202a927c576cc15c2b6296c347df9040ac` funcs `main`
  - pixel: key `95bb1992cc2a926296fb9e9404b1c16c` bytecode_ordinal `1543` size `3260` sha256 `1c628e9925ba310c4cee304f9f873668e764a7ce32fabf885314a50044ecfb21` funcs `main`
- pipeline ordinal `1464` offset `235224` key `109553ae8edaa447d7f7d75bba39d95d` status `unique`
  - vertex: key `d309dc5bb74adf5dbe34ed52752ef092` bytecode_ordinal `1544` size `3056` sha256 `2fd53c915c5474608b12ffc524c1a0202a927c576cc15c2b6296c347df9040ac` funcs `main`
  - pixel: key `926746b96f42a653f19e16b095f1e73f` bytecode_ordinal `1546` size `2860` sha256 `e0d2c9c029fa3f78dd21b7d5daf6d18562eef90ff13652724e7fc487b506a9e0` funcs `main`
- pipeline ordinal `1465` offset `235384` key `52600ac77453fb30abc1d15bba39d95d` status `unique`
  - vertex: key `d309dc5bb74adf5dbe34ed52752ef092` bytecode_ordinal `1544` size `3056` sha256 `2fd53c915c5474608b12ffc524c1a0202a927c576cc15c2b6296c347df9040ac` funcs `main`
  - pixel: key `d1beca43cb7abe663b68c7778bbc36be` bytecode_ordinal `1547` size `3036` sha256 `d66b8d2bf1248d1ef4854e49361a0efa2808a1e781edc7e6c5258ce98e093893` funcs `main`
- pipeline ordinal `1467` offset `235672` key `2e35af380784035e3d36978fbf0d17b2` status `unique`
  - vertex: key `0d659f8fb27e11b26b1ec9351c23ed83` bytecode_ordinal `904` size `3000` sha256 `255a109b256d1cc47e17dfeb18b777b311492923b09a9aa71549af8e0aa8aa0a` funcs `main`
  - pixel: key `992e92de49fa851b5579a1347ebcf083` bytecode_ordinal `1549` size `1924` sha256 `be620d7fe216b2cdda6cd12f7af3b33cf135fb6feccc80a5a2f259b770fb44c2` funcs `main`
- pipeline ordinal `1468` offset `235832` key `8c6675c472f949b8a7f0e85772c42fc6` status `unique`
  - vertex: key `a315e0577fb729c620606d8a3a6c726b` bytecode_ordinal `241` size `2588` sha256 `ae01b46cccff361105af9f32ddb9b2e0d9e1378c3f629e9442fba61026cb08db` funcs `main`
  - pixel: key `17e1575380994de6caec1042a9fb5258` bytecode_ordinal `1551` size `2264` sha256 `5453c20a86c5a7c33f0674b904090a0fea24cdb9c4f61c903f7e47eb9778e01b` funcs `main`
- pipeline ordinal `1470` offset `236152` key `b89d8dc9bcd60391d7ecd45bba39d95d` status `unique`
  - vertex: key `d309dc5bb74adf5dbe34ed52752ef092` bytecode_ordinal `1544` size `3056` sha256 `2fd53c915c5474608b12ffc524c1a0202a927c576cc15c2b6296c347df9040ac` funcs `main`
  - pixel: key `e1a72cdca4a5996a10f08ae42b2d08a7` bytecode_ordinal `1553` size `2316` sha256 `bcf88165779de0003dad5cbfe637b199e3f41c48fb316d0913b9dc9ef6a98e33` funcs `main`
- pipeline ordinal `1646` offset `263424` key `fbee470af26643e87126c8ddf207c99a` status `unique`
  - vertex: key `9dc0c0ddff74cf9a4e42e1f19451879e` bytecode_ordinal `1711` size `3248` sha256 `219aabfa2ef4d6aec909a532da2ecc3cb373b66eb01b003109e92d770f6415eb` funcs `main`
  - pixel: key `6416d5c294dff30498245e4f74c62961` bytecode_ordinal `240` size `2248` sha256 `170ca148451e7002ff696300282a60c73731d89aa6c823355cfc684b1121c696` funcs `main`
- pipeline ordinal `1647` offset `263584` key `1039dda43b83eb7210fb253b59e32599` status `unique`
  - vertex: key `0c49223b549023993cb00beec23ff59e` bytecode_ordinal `1712` size `2768` sha256 `26690d65c7815cb2b39d31632c9cec7f47d11d8f3b6d8413ed701e0aa486004e` funcs `main`
  - pixel: key `6e9c3c46213492fe9b2729e03c49b3fb` bytecode_ordinal `902` size `2172` sha256 `9b3b53943c86e68ad79fe9d3b538d376759fddedce580d771048e5ab4d2ded99` funcs `main`
- pipeline ordinal `1648` offset `263744` key `7ac3093965e52d6fb4f5eb14f562f724` status `unique`
  - vertex: key `b0fced14f811f124b49d4fa889b0e643` bytecode_ordinal `1713` size `2368` sha256 `d646bf8ada404f65d53dcc0a202ef52d6272a34c15bd9c8daf284d19bf638676` funcs `main`
  - pixel: key `78adbd7b6452a243eec33b552fa6f52f` bytecode_ordinal `910` size `1740` sha256 `61feff43fe1962a46d1442ecf9734296541e02f27985852e668c863cfe1f5f76` funcs `main`

## Interpretation

- This root layout matches the live AC6 upstream diagnostic layout: VS root CBV b0, PS root CBV b1, PS SRV table t0..t3, PS sampler table s0..s3.
- D3DMetal provides root-linked candidate pipeline records for this layout. Runtime M12 PSO hashes `42dbf5610021bd23` / `6aaa91c23c794ed8` still do not directly identify one D3DMetal bytecode pair; bytecode identity recovery or more pipeline-field decoding remains required before using a specific metallib as an oracle fix.
- Direct D3DMetal metallib reuse remains unsafe; the candidate bytecode/stage/metallib records are oracle inputs for repairing M12 ABI/binding translation only.
