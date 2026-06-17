# AC6 clip-W fix visual success

After `319c18b fix: preserve dxil float literal outputs in m12 lowering`, AC6 was launched through the freshly rebuilt separate backend on `127.0.0.1:9277` with force-source compile and strict staged runtime hashes.

Run root:

- `tools/d3d12-metal-sdk/results/perf-runs/ac6-clipw-fix-force-source-20260616-221510/`

Evidence:

- bounded launch completed under 120s with `launch_ok=True`
- force-source compile override: `1`
- drawn presents: `28/28`
- graphics PSOs compiled: `683`
- render/compile failures: `0`
- user visually confirmed AC6 finally loaded/rendered instead of black output
- observed performance was low (~16 FPS), similar to the previously bogged Elden Ring runtime, so performance remains a follow-up issue rather than a correctness blocker

Staged runtime hashes used:

- `d3d12.dll`: `57e1d5e9d239e58e8d438be9ff61532282a84228738a63c1b92fdd397b58f9bf`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `0a0d553b649beb6e0f9c62fd327257f70757df07f2ae3cf4713882de21e3685e`
- `winemetal.dll`: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- `winemetal.so`: `cad141357fecf172c9c3966daeb8c5d0972d4e06d383e30cf495593dbc9dab7d`

Conclusion: AC6 black-output blocker is visually resolved by the DXIL float-literal/clip-W fix. Remaining work: performance recovery and broader D3DMetal oracle completion/validation.
