# Phase 16 binding/root-signature reconciliation

Scope: generated Unreal material fallback corpus (51 METAL_SM6 DXIL artifacts) because Phase 14 blocks Mac-host PCD3D_SM6 commandlet output.

- Unreal debug reflection files: `51`
- MSC4 reflection files: `51`
- Unreal reflection max CBV slot: `4`
- MSC4 reflection max CBV slot: `4`
- M12 root signature: `5 root CBV descriptors b0-b4, space0, all shader visibility; no descriptor tables/ranges/static samplers; graphics harness also supplies IA layout and 6 RTVs.`
- M12 binding plans: `root_desc=5`, `lookup_checks=5`, `lookup_mismatches=0` (compute and graphics logs).
- M12 PSO results: compute `4/4`; graphics `35/35` after the lowering fix.

## CBV slots
### Unreal debug reflection
- `space0_b0`: `51`
- `space0_b1`: `51`
- `space0_b2`: `51`
- `space0_b3`: `49`
- `space0_b4`: `23`

### MSC4 reflection
- `space0_b0`: `51`
- `space0_b1`: `51`
- `space0_b2`: `51`
- `space0_b3`: `49`
- `space0_b4`: `23`

## Interpretation

- The corpus has no reflected SRV/UAV/sampler bindings; table placeholder entries are non-resource argument-buffer placeholders.
- The 5-root-CBV harness covers every reflected CBV slot (`b0`-`b4`, `space0`).
- M12 binding logs report zero lookup mismatches; PSO creation succeeds for all replayed compute/graphics cases.
