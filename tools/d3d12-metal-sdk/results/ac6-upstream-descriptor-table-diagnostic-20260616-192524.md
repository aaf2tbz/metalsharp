# AC6 upstream descriptor-table diagnostic update

## Change
- Extended the opt-in `DXMT_D3D12_AC6_PRODUCER_DIAGNOSTIC` upstream diagnostic for the PSO:
  - VS `42dbf5610021bd23`
  - PS `6aaa91c23c794ed8`
- The diagnostic now walks pixel-visible root descriptor-table ranges instead of relying only on hard-coded SRV ordinals 0–3.
- Logs per-table/per-range metadata: root index, descriptor-table GPU handle, range type, base register, register space, descriptor count, and table offset.
- Logs each probed SRV descriptor with root/range/descriptor indices, shader register, descriptor summary, and resource summary.
- Captures readbacks for up to eight discovered pixel SRVs; remaining descriptor entries are logged but not captured.
- Unbounded descriptor ranges (`UINT32_MAX`) are capped to one probe for diagnostic safety.

## Safety
- Still opt-in via AC6 producer diagnostic gate.
- Does not mutate live shader caches.
- Preserves existing render-encoder close/blit/reopen pattern for readbacks.
- Null descriptors/resources are tolerated.

## Validation
- `ninja -C vendor/dxmt/build-metalsharp-x64 src/d3d12/d3d12.dll` passed.
- Focused autoreview found no actionable issues after capping unbounded ranges.

## Next runtime step
- Rebuild/stage/hash-verify the diagnostic runtime before any approved live AC6 capture.
- Do not treat this as visual correctness; it only improves upstream resource provenance for the known zero-output pass.
