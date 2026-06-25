# Phase 19 Subnautica 2 read-only dry-run

- no live launch performed: `True`
- m12 dry-run ok: `True`
- pipeline dry-run ok: `True`
- runtime doctor ok: `True`
- d3d12 runtime doctor ok: `False` (timeout_or_error)

## Launch shape
- `WINEDLLOVERRIDES` = `None`
- `WINEDLLPATH` = `None`
- `DXMT_D3D12_UE_SM6_COMPAT` = `None`
- `DXMT_D3D12_LIVE_PRESENT` = `None`
- `DXMT_WINEMETAL_UNIXLIB` = `None`
- `DXMT_SHADER_CACHE_PATH` = `None`
- `DXMT_PIPELINE_CACHE_PATH` = `None`

## Deploy DLLs
- `d3d12.dll` present=`True` sha256=`0512b37fe4af766fd2ea80beffc4f1424c2f305eed587c31d9480cc4a32b35e0`
- `d3d11.dll` present=`True` sha256=`c14dcca36e3aa0f48be9b794c04307736672999bc4003c683a8ec75ccb83f0dd`
- `dxgi.dll` present=`True` sha256=`e815291564c94703b08060eb48de2dece3a5454d5cf668a256b0ddcbde172351`
- `dxgi_dxmt.dll` present=`True` sha256=`df801477f074a7dca8dbe86d1b77a24897221123d3e5ee1f0da124f11d5134c0`
- `d3d10core.dll` present=`True` sha256=`7903ea8c3af7e6134fdb1ce943412311d06c08f54d630b06dd734c53c6d900e9`
- `winemetal.dll` present=`True` sha256=`2d68a9cde162350f5fbaa9c421a80031daecba8207968738af9d2ba1a3c81acd`
- `nvapi64.dll` present=`True` sha256=`0ad95777863b97a5757adfecc45e4ee55195a9befd05878cbc975a4f6c8ac841`
- `nvngx.dll` present=`True` sha256=`3c47b6f378fa0f75cd3002791a59e63c899a0421d013f1cadc89012a6fe6fd82`
