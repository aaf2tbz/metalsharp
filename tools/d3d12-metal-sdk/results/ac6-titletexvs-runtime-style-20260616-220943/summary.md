# AC6 TitleTexVs runtime-style source validation

- airconv_rc: 0
- metal_rc: 0
- shader: ca33abe9a2d27ce9 / TitleTexVs
- input layout: POSITION reg0 dxgi6 offset0; COLOR reg1 dxgi28 offset12; TEXCOORD reg2 dxgi2 offset16

## Previous cached runtime line

```
159:  out.position.w = 0.0;
```

## Fixed runtime-style out.position lines
```
128:  out.position = float4(0.0, 0.0, 0.0, 1.0);
156:  out.position.x = m12_load_vertex_attr(0, 0, 6, 0, 1, vid, iid, buf16, buf0, buf29, buf30).x;
157:  out.position.y = m12_load_vertex_attr(0, 0, 6, 0, 1, vid, iid, buf16, buf0, buf29, buf30).y;
158:  out.position.z = m12_load_vertex_attr(0, 0, 6, 0, 1, vid, iid, buf16, buf0, buf29, buf30).z;
159:  out.position.w = 1.0f;
```

## airconv stderr
```
```

## metal stderr
```
metal: warning: tools/d3d12-metal-sdk/results/ac6-titletexvs-runtime-style-20260616-220943/ca33abe9a2d27ce9.runtime-style.msl: 'linker' input unused [-Wunused-command-line-argument]
```
