# D3DMetal oracle pack vs M12 bindings

- pack: `tools/d3d12-metal-sdk/results/d3dmetal-oracle-pack-ac6-final-chain-20260616-200351/manifest.json`
- m12 cache: `/Users/alexmondello/.metalsharp/shader-cache/m12/1888160`
- records: `4`

| M12 shader | Kind | D3DMetal function | Reflection function | Active/total D3DMetal args | M12 bindings |
|---|---|---|---|---:|---:|
| `58539be4844b1dd9` | `pixel` | `ChromaticAberrationAndNoisePs` | `ChromaticAberrationAndNoisePs` | 3/8 | 35 |
| `ca33abe9a2d27ce9` | `vertex` | `TitleTexVs` | `TitleTexVs` | 5/11 | 43 |
| `8fc08bbc2900719b` | `pixel` | `BlendUI_SDR2SDR` | `BlendUI_SDR2SDR` | 3/8 | 35 |
| `237a203b2ac9964b` | `vertex` | `SimpleVS_Single2DTexture` | `SimpleVS_Single2DTexture` | 5/11 | 43 |

## 58539be4844b1dd9

- D3DMetal metallib: `tools/d3d12-metal-sdk/results/d3dmetal-oracle-pack-ac6-final-chain-20260616-200351/armored-core-vi/58539be4844b1dd9/eb57a31d9d8abb60c9d21ae00946897a61979f594ce5de42a5e9313016ea8299.metallib`
- D3DMetal function: `ChromaticAberrationAndNoisePs`
- M12 function: `ps_main`

### Active D3DMetal reflection args

- FragmentInputArg loc=None count=None name=`texcoord1` type=`float4` access=`None` addr=`None`
- IndirectBufferArg loc=2 count=1 name=`struct.top_level_global_ab` type=`struct.top_level_global_ab` access=`Read` addr=`Constant`
- BufferArg loc=5 count=1 name=`bufROG` type=`float` access=`ReadWrite` addr=`Global`

### M12 MSL bindings

- buffer(0) `buf0`: `device char*`
- buffer(1) `buf1`: `device char*`
- buffer(2) `buf2`: `device char*`
- buffer(3) `buf3`: `device char*`
- buffer(4) `buf4`: `device char*`
- buffer(5) `buf5`: `device char*`
- buffer(6) `buf6`: `device char*`
- buffer(7) `buf7`: `device char*`
- buffer(8) `buf8`: `device char*`
- buffer(9) `buf9`: `device char*`
- buffer(10) `buf10`: `device char*`
- buffer(11) `buf11`: `device char*`
- buffer(12) `buf12`: `device char*`
- buffer(13) `buf13`: `device char*`
- buffer(14) `buf14`: `device char*`
- buffer(15) `buf15`: `device char*`
- buffer(16) `buf16`: `device char*`
- buffer(17) `buf17`: `device char*`
- buffer(18) `buf18`: `device char*`
- buffer(19) `buf19`: `device char*`
- buffer(20) `buf20`: `device char*`
- buffer(21) `buf21`: `device char*`
- buffer(22) `buf22`: `device char*`
- buffer(23) `buf23`: `device char*`
- buffer(24) `buf24`: `device char*`
- buffer(25) `buf25`: `device char*`
- buffer(26) `buf26`: `device char*`
- buffer(27) `buf27`: `device char*`
- buffer(28) `buf28`: `device char*`
- buffer(29) `buf29`: `device char*`
- buffer(30) `buf30`: `device char*`
- texture(0) `tex0`: `texture2d<float, access::sample>`
- texture(1) `tex1`: `texture2d<float, access::sample>`
- sampler(0) `samp0`: `sampler`
- sampler(1) `samp1`: `sampler`

### M12 binding manifest comments

- metalsharp.binding_manifest.v1
- direct_buffers=31 direct_textures=2 direct_samplers=2
- range kind=srv space=0 lower=1 count=1
- range kind=srv space=0 lower=0 count=1
- range kind=sampler space=0 lower=1 count=1
- range kind=sampler space=0 lower=0 count=1
- range kind=cbv space=0 lower=0 count=1

## ca33abe9a2d27ce9

- D3DMetal metallib: `tools/d3d12-metal-sdk/results/d3dmetal-oracle-pack-ac6-final-chain-20260616-200351/armored-core-vi/ca33abe9a2d27ce9/933ee76170bfcf0715aa38dcf0a0e39645b24c993dcf158a2366f7faf734d3b5.metallib`
- D3DMetal function: `TitleTexVs`
- M12 function: `vs_main`

### Active D3DMetal reflection args

- BufferArg loc=6 count=1 name=`vertex_buffers_ab` type=`uchar` access=`Read` addr=`Constant`
- VertexIDArg loc=None count=None name=`vid` type=`uint` access=`None` addr=`None`
- InstanceIDArg loc=None count=None name=`iid` type=`uint` access=`None` addr=`None`
- BaseInstanceArg loc=None count=None name=`bi` type=`uint` access=`None` addr=`None`
- IndirectConstantArg loc=0 count=1 name=`index_type` type=`ushort` access=`None` addr=`None`

### M12 MSL bindings

- buffer(0) `buf0`: `device char*`
- buffer(1) `buf1`: `device char*`
- buffer(2) `buf2`: `device char*`
- buffer(3) `buf3`: `device char*`
- buffer(4) `buf4`: `device char*`
- buffer(5) `buf5`: `device char*`
- buffer(6) `buf6`: `device char*`
- buffer(7) `buf7`: `device char*`
- buffer(8) `buf8`: `device char*`
- buffer(9) `buf9`: `device char*`
- buffer(10) `buf10`: `device char*`
- buffer(11) `buf11`: `device char*`
- buffer(12) `buf12`: `device char*`
- buffer(13) `buf13`: `device char*`
- buffer(14) `buf14`: `device char*`
- buffer(15) `buf15`: `device char*`
- buffer(16) `buf16`: `device char*`
- buffer(17) `buf17`: `device char*`
- buffer(18) `buf18`: `device char*`
- buffer(19) `buf19`: `device char*`
- buffer(20) `buf20`: `device char*`
- buffer(21) `buf21`: `device char*`
- buffer(22) `buf22`: `device char*`
- buffer(23) `buf23`: `device char*`
- buffer(24) `buf24`: `device char*`
- buffer(25) `buf25`: `device char*`
- buffer(26) `buf26`: `device char*`
- buffer(27) `buf27`: `device char*`
- buffer(28) `buf28`: `device char*`
- buffer(29) `buf29`: `device char*`
- buffer(30) `buf30`: `device char*`
- texture(0) `tex0`: `texture2d<float, access::sample>`
- texture(1) `tex1`: `texture2d<float, access::sample>`
- texture(2) `tex2`: `texture2d<float, access::sample>`
- texture(3) `tex3`: `texture2d<float, access::sample>`
- texture(4) `tex4`: `texture2d<float, access::sample>`
- texture(5) `tex5`: `texture2d<float, access::sample>`
- texture(6) `tex6`: `texture2d<float, access::sample>`
- texture(7) `tex7`: `texture2d<float, access::sample>`
- sampler(0) `samp0`: `sampler`
- sampler(1) `samp1`: `sampler`
- sampler(2) `samp2`: `sampler`
- sampler(3) `samp3`: `sampler`

### M12 binding manifest comments

- metalsharp.binding_manifest.v1
- direct_buffers=31 direct_textures=8 direct_samplers=4
- range none

## 8fc08bbc2900719b

- D3DMetal metallib: `tools/d3d12-metal-sdk/results/d3dmetal-oracle-pack-ac6-final-chain-20260616-200351/armored-core-vi/8fc08bbc2900719b/3d079e7955d211d6f1a3a2a9c25e5b3ab830a70c5455c6edac0a1b43640b6626.metallib`
- D3DMetal function: `BlendUI_SDR2SDR`
- M12 function: `ps_main`

### Active D3DMetal reflection args

- FragmentInputArg loc=None count=None name=`texcoord1` type=`float2` access=`None` addr=`None`
- IndirectBufferArg loc=2 count=1 name=`struct.top_level_global_ab` type=`struct.top_level_global_ab` access=`Read` addr=`Constant`
- BufferArg loc=5 count=1 name=`bufROG` type=`float` access=`ReadWrite` addr=`Global`

### M12 MSL bindings

- buffer(0) `buf0`: `device char*`
- buffer(1) `buf1`: `device char*`
- buffer(2) `buf2`: `device char*`
- buffer(3) `buf3`: `device char*`
- buffer(4) `buf4`: `device char*`
- buffer(5) `buf5`: `device char*`
- buffer(6) `buf6`: `device char*`
- buffer(7) `buf7`: `device char*`
- buffer(8) `buf8`: `device char*`
- buffer(9) `buf9`: `device char*`
- buffer(10) `buf10`: `device char*`
- buffer(11) `buf11`: `device char*`
- buffer(12) `buf12`: `device char*`
- buffer(13) `buf13`: `device char*`
- buffer(14) `buf14`: `device char*`
- buffer(15) `buf15`: `device char*`
- buffer(16) `buf16`: `device char*`
- buffer(17) `buf17`: `device char*`
- buffer(18) `buf18`: `device char*`
- buffer(19) `buf19`: `device char*`
- buffer(20) `buf20`: `device char*`
- buffer(21) `buf21`: `device char*`
- buffer(22) `buf22`: `device char*`
- buffer(23) `buf23`: `device char*`
- buffer(24) `buf24`: `device char*`
- buffer(25) `buf25`: `device char*`
- buffer(26) `buf26`: `device char*`
- buffer(27) `buf27`: `device char*`
- buffer(28) `buf28`: `device char*`
- buffer(29) `buf29`: `device char*`
- buffer(30) `buf30`: `device char*`
- texture(0) `tex0`: `texture2d<float, access::sample>`
- texture(1) `tex1`: `texture2d<float, access::sample>`
- sampler(0) `samp0`: `sampler`
- sampler(1) `samp1`: `sampler`

### M12 binding manifest comments

- metalsharp.binding_manifest.v1
- direct_buffers=31 direct_textures=2 direct_samplers=2
- range kind=srv space=0 lower=1 count=1
- range kind=srv space=0 lower=0 count=1
- range kind=sampler space=0 lower=1 count=1
- range kind=cbv space=0 lower=0 count=1

## 237a203b2ac9964b

- D3DMetal metallib: `tools/d3d12-metal-sdk/results/d3dmetal-oracle-pack-ac6-final-chain-20260616-200351/armored-core-vi/237a203b2ac9964b/b8ef4a6c742f8ef04c9546b3686a182b3c08befd4fd88e9164938ceeb3c4eb2e.metallib`
- D3DMetal function: `SimpleVS_Single2DTexture`
- M12 function: `vs_main`

### Active D3DMetal reflection args

- BufferArg loc=6 count=1 name=`vertex_buffers_ab` type=`uchar` access=`Read` addr=`Constant`
- VertexIDArg loc=None count=None name=`vid` type=`uint` access=`None` addr=`None`
- InstanceIDArg loc=None count=None name=`iid` type=`uint` access=`None` addr=`None`
- BaseInstanceArg loc=None count=None name=`bi` type=`uint` access=`None` addr=`None`
- IndirectConstantArg loc=0 count=1 name=`index_type` type=`ushort` access=`None` addr=`None`

### M12 MSL bindings

- buffer(0) `buf0`: `device char*`
- buffer(1) `buf1`: `device char*`
- buffer(2) `buf2`: `device char*`
- buffer(3) `buf3`: `device char*`
- buffer(4) `buf4`: `device char*`
- buffer(5) `buf5`: `device char*`
- buffer(6) `buf6`: `device char*`
- buffer(7) `buf7`: `device char*`
- buffer(8) `buf8`: `device char*`
- buffer(9) `buf9`: `device char*`
- buffer(10) `buf10`: `device char*`
- buffer(11) `buf11`: `device char*`
- buffer(12) `buf12`: `device char*`
- buffer(13) `buf13`: `device char*`
- buffer(14) `buf14`: `device char*`
- buffer(15) `buf15`: `device char*`
- buffer(16) `buf16`: `device char*`
- buffer(17) `buf17`: `device char*`
- buffer(18) `buf18`: `device char*`
- buffer(19) `buf19`: `device char*`
- buffer(20) `buf20`: `device char*`
- buffer(21) `buf21`: `device char*`
- buffer(22) `buf22`: `device char*`
- buffer(23) `buf23`: `device char*`
- buffer(24) `buf24`: `device char*`
- buffer(25) `buf25`: `device char*`
- buffer(26) `buf26`: `device char*`
- buffer(27) `buf27`: `device char*`
- buffer(28) `buf28`: `device char*`
- buffer(29) `buf29`: `device char*`
- buffer(30) `buf30`: `device char*`
- texture(0) `tex0`: `texture2d<float, access::sample>`
- texture(1) `tex1`: `texture2d<float, access::sample>`
- texture(2) `tex2`: `texture2d<float, access::sample>`
- texture(3) `tex3`: `texture2d<float, access::sample>`
- texture(4) `tex4`: `texture2d<float, access::sample>`
- texture(5) `tex5`: `texture2d<float, access::sample>`
- texture(6) `tex6`: `texture2d<float, access::sample>`
- texture(7) `tex7`: `texture2d<float, access::sample>`
- sampler(0) `samp0`: `sampler`
- sampler(1) `samp1`: `sampler`
- sampler(2) `samp2`: `sampler`
- sampler(3) `samp3`: `sampler`

### M12 binding manifest comments

- metalsharp.binding_manifest.v1
- direct_buffers=31 direct_textures=8 direct_samplers=4
- range none
