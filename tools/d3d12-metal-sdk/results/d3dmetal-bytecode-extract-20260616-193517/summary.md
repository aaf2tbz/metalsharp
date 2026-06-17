# D3DMetal D3D bytecode extraction and M12 comparison

- D3DMetal reference root: `tools/d3d12-metal-sdk/results/d3dmetal-working-cache-reference-20260616-185155`
- M12 cache root: `/Users/alexmondello/.metalsharp/shader-cache/m12`
- Extract root: `<manifest-only>`

This is an offline bytecode-container comparison. A raw SHA match means identical D3D bytecode bytes; a miss does not prove semantic mismatch because M12 cache coverage depends on captured runtime paths and cache-cold state.

| Game | D3DM occurrences | D3DM unique | D3DM DXIL | D3DM legacy DXBC | M12 DXBC files | M12 unique | Raw SHA overlap | D3DM-only | M12-only |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| elden-ring | 1410 | 1410 | 1410 | 0 | 795 | 789 | 717 | 693 | 72 |
| armored-core-vi | 1714 | 1714 | 1714 | 0 | 420 | 412 | 377 | 1337 | 35 |
| subnautica-2 | 10453 | 10431 | 10431 | 0 | 489 | 489 | 485 | 9946 | 4 |

## elden-ring

- D3DMetal bytecode occurrences: `1410`
- D3DMetal unique bytecode containers: `1410`
- D3DMetal kind counts: `{'dxil': 1410}`
- D3DMetal shader-kind counts: `{'domain': 21, 'geometry': 2, 'hull': 17, 'pixel': 844, 'vertex': 526}`
- Top chunk names: `{'DXIL': 1410, 'PSV0': 1410, 'RTS0': 1410, 'SFI0': 1410, 'HASH': 1384, 'ILDN': 1384, 'ISG1': 1384, 'OSG1': 1384, 'STAT': 1384, 'PSG1': 38, 'ISGN': 26, 'OSGN': 26}`
- M12 `.dxbc` files: `795` unique=`789` kinds=`{'dxil': 795}`
- Raw SHA overlap: `717`
- D3DMetal-only unique containers: `693`
- M12-only unique containers: `72`
- Overlap examples:
  - `0027e37685c6fc9d7918e0362e4786c03b540052de9067759ebcd7ba84f54e4e` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@4515660` m12=`c4a2d9363b49ad73.dxbc` kind=`dxil`
  - `00d09a1ca5b8e9bb1b8421d5111a38c0fc066f3c711a8c52fb5f26d6897b49cc` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@908628` m12=`d3456cdad87b521a.dxbc` kind=`dxil`
  - `01108c4c21dd0695b2763a3e9657aea82e366583b1d86f564ce45b8ec5aa2206` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@11062260` m12=`21bba258483bbd65.dxbc` kind=`dxil`
  - `01144306daae525ed7107d340b7b14775b6bbfb0f6dafb799be25f6b63290ef0` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@10740492` m12=`73a4fd6eda72167f.dxbc` kind=`dxil`
  - `01560f1c8791e56dc005c347b13ae873ff8ad943acf739cf0aa5ad971447e370` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@6227276` m12=`2f5772c45e8d9cbd.dxbc` kind=`dxil`
  - `02a54a52edcfbd6306f15c11d12ad496287a46590b5d3ed0912a9bee1b63121e` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@10789052` m12=`aa6cd0f6d67874b8.dxbc` kind=`dxil`
  - `02d508eb0efdb601839f99bea575fee3a1332060f7feb3254ccd0c47356e569e` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@2026476` m12=`7c01bb2cb198ff8c.dxbc` kind=`dxil`
  - `02ea365ee2c7e13bb17182ca96eb64f6b0f1c0acf306c52fb04ca13d931e3d1a` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@4788372` m12=`25714213ee8f7dfc.dxbc` kind=`dxil`
  - `030747f4a1e50d44208cad255bd6939ff1ade6438c382e0b9d54fac31d079eb6` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@1408140` m12=`f3c2071ad0be6137.dxbc` kind=`dxil`
  - `034421af09a540668b7e4115d614bf32cde0a1afd2fc9c5f2b6dbde766f4069c` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@9705620` m12=`d5e02c95f54c3686.dxbc` kind=`dxil`

## armored-core-vi

- D3DMetal bytecode occurrences: `1714`
- D3DMetal unique bytecode containers: `1714`
- D3DMetal kind counts: `{'dxil': 1714}`
- D3DMetal shader-kind counts: `{'compute': 24, 'domain': 30, 'geometry': 5, 'hull': 20, 'pixel': 1101, 'vertex': 534}`
- Top chunk names: `{'DXIL': 1714, 'PSV0': 1714, 'SFI0': 1714, 'RTS0': 1708, 'ISG1': 1680, 'OSG1': 1680, 'HASH': 1678, 'STAT': 1678, 'ILDN': 1672, 'PSG1': 50, 'ISGN': 34, 'OSGN': 34}`
- M12 `.dxbc` files: `420` unique=`412` kinds=`{'dxil': 420}`
- Raw SHA overlap: `377`
- D3DMetal-only unique containers: `1337`
- M12-only unique containers: `35`
- Overlap examples:
  - `00111fd201385714f2c63576970c37b5bc77a53ecf2a6321685730057f3b11ce` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@2084404` m12=`7fb9a3f0e36bbc2c.dxbc` kind=`dxil`
  - `009765274ab2b81a8087aba1fdad905d115a665357e1f8623df9cc5906980ca7` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@28260` m12=`e4c66c932853d01b.dxbc` kind=`dxil`
  - `00c3ac53b04bebe81aa3ec1718cc98ebdd2c16ba43ec2bf390d6e095786569be` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@1380044` m12=`d81ec9a0e02c34e8.dxbc` kind=`dxil`
  - `010928d45df5d10d70c5b992fce4facf7a0f2a33727d70e7efeee7d380fe1797` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@3296420` m12=`b1abb2dd3020c1c7.dxbc` kind=`dxil`
  - `02a1b24bf2d7a04408d8f5ebbc3fae36036b297106d8b055f69b93dbc1579fcc` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@1018244` m12=`ded609e08fd942d9.dxbc` kind=`dxil`
  - `03472d8c28842de4b63cf8a650730eabbf5c7f5f497416b8d75b4fa1d56d86f2` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@1410052` m12=`560dd617e044f8cf.dxbc` kind=`dxil`
  - `03b5e082605dfb232376484cad6bba586613a3d73009bdd5ff89630325868772` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@1965972` m12=`e800c889eed7e0a2.dxbc` kind=`dxil`
  - `040e6b3ade9627eacff9dfe84aafeb74babf0b0ad8d3af8bd5151b03f65da275` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@5196364` m12=`c5d9b7b96d288c9f.dxbc` kind=`dxil`
  - `05a70d2b3a5d5ec07aa43670133f3207a24a32bbbeccd69943e62a348b9aa094` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@4222212` m12=`c252859a0e95c991.dxbc` kind=`dxil`
  - `06aeee51c1c9f93022575f43ed7bc30c8180ba105bcfcef1cd6e6e8e50e8e213` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@2209028` m12=`2a329e27217278b5.dxbc` kind=`dxil`

## subnautica-2

- D3DMetal bytecode occurrences: `10453`
- D3DMetal unique bytecode containers: `10431`
- D3DMetal kind counts: `{'dxil': 10431}`
- D3DMetal shader-kind counts: `{'compute': 6485, 'geometry': 5, 'pixel': 2924, 'unknown': 257, 'vertex': 760}`
- Top chunk names: `{'DXIL': 10431, 'HASH': 10431, 'ILDN': 10431, 'ISG1': 10431, 'OSG1': 10431, 'PRIV': 10431, 'PSV0': 10431, 'SFI0': 10431, 'PSG1': 257}`
- M12 `.dxbc` files: `489` unique=`489` kinds=`{'dxil': 489}`
- Raw SHA overlap: `485`
- D3DMetal-only unique containers: `9946`
- M12-only unique containers: `4`
- Overlap examples:
  - `00e81f44017001d3fded9f42a276f10da01cbdc1a1b58fa0bedc0564587f8cd1` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@11316988` m12=`26192b08e1f2753b.dxbc` kind=`dxil`
  - `018ffcbedab655c29411e11e5cce62a558509892ccc893bc3d55d67694d45267` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@11710476` m12=`f000374b3f156c78.dxbc` kind=`dxil`
  - `01dc75c1feacfb3257655563eff9b698842c0e80dbc7268978c8683fee3dc4f0` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@13378932` m12=`3d5e10a66b67ee8a.dxbc` kind=`dxil`
  - `0210bfcd9040d2137f241ed332a1b14045b2073e936637387645c97259cf1e6d` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@11835324` m12=`a6bd8514e4d39cc8.dxbc` kind=`dxil`
  - `03952bb2174cebeccdb3f03210fe2e42dd0917b005228706454d3fbfb50a4f07` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@709676` m12=`d243f363a80cb325.dxbc` kind=`dxil`
  - `03d22b71175058ccd72ebea9e7565b4e79eb872dc4ba78033d243ffaeef2cac9` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@326148` m12=`62a40afa97ed2ae6.dxbc` kind=`dxil`
  - `043b3f2feb49cb065b16df6cc4846f114d9d3ac24d87b7f34bb9d632413a3a21` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@12399764` m12=`38bb91eaa387da43.dxbc` kind=`dxil`
  - `04cbfefbdef7a1c5323cd600d4bc9c52887b9d3db4161bbf1c4a901d37c4c8b3` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@11335796` m12=`b1ae4de62fdfd543.dxbc` kind=`dxil`
  - `051364feb001736cb57b55b5df3599f0f34f607d34218f267ebed49e9ac9c715` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@12738020` m12=`cb3bd258ef9ceb88.dxbc` kind=`dxil`
  - `05af1c3ac14ae8ce169972b9b324accc26812234c1a053949c2f0a9f321acd47` d3dm=`shaders.cache/MTLGPUFamilyApple9_0/bytecode_cache.bin@11433380` m12=`20ba6457cddb2533.dxbc` kind=`dxil`

