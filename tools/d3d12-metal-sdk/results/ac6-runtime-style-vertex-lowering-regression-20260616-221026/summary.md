# AC6 runtime-style vertex lowering regression

- selected: `12`
- ok: `True`

| VS | airconv | metal | position.w |
|---|---:|---:|---|
| `159922bca5a911bd` | 0 | 0 | `out.position.w = 0;` |
| `967d11c10e2250d3` | 0 | 0 | `out.position.w = 0;` |
| `afb44dd81d651e16` | 0 | 0 | `out.position.w = 0;` |
| `77bd500cbd58c09a` | 0 | 0 | `out.position.w = 0;` |
| `9ee8c6cc473ff9ed` | 0 | 0 | `out.position.w = 0;` |
| `bc78391873944339` | 0 | 0 | `out.position.w = 0;` |
| `63d0e672f8ab1679` | 0 | 0 | `out.position.w = 0;` |
| `ca33abe9a2d27ce9` | 0 | 0 | `out.position.w = 1.0f;` |
| `05c33d3d19fb65e2` | 0 | 0 | `out.position.w = 0;` |
| `0a42e0976e07ab10` | 0 | 0 | `out.position.w = 0;` |
| `4f5427c4d23e9c73` | 0 | 0 | `out.position.w = 0;` |
| `edac4643953d29d5` | 0 | 0 | `out.position.w = 0;` |
