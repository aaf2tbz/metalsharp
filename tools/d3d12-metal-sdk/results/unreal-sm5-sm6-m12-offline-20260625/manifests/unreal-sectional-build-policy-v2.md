# Unreal Sectional Clone/Build Policy v2

Correction received: keep the main Unreal workspace persistent. The phase/wipe loop applies to **huge generated outputs**, especially compiled shader corpora, not to the source checkout itself.

## Current authoritative policy

1. All Unreal work happens under:
   - `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab`
2. Main Unreal source checkout should be:
   - full working tree, not sparse
   - on AverySSD
   - persistent across phases
   - shallow history is acceptable if it preserves a complete working tree for the selected branch
3. Do not wipe the main Unreal source checkout between phases.
4. Do not wipe durable source indexes, notes, scripts, manifests, or build plans.
5. Phase/wipe only bulky generated outputs after archive + private checkpoint, including:
   - compiled shader object corpora
   - DXIL/DXBC dumps
   - MSL/metallib replay outputs
   - DerivedDataCache slices
   - huge build intermediates when not needed for the next immediate step
   - temporary logs beyond summarized evidence
6. Tarball/checkpoint before clearing any generated output.
7. The goal is full translation/analysis from Unreal into MetalSharp M12 to the best of our ability, not a permanently sparse source sampling.

## Persistent directories

Keep these unless explicitly superseded:

- `02-unreal-full/` — full Unreal checkout(s)
- `00-manifests/` — phase records and decisions
- `01-docs/` — docs/research
- `05-probes/` — probe specs
- `08-scripts/` — automation
- `09-notes/` — transfer analysis
- `private-checkpoint-repo/` — private Git mirror

## Generated/bulky directories

Archive/checkpoint, then clean as needed:

- `04-corpus/generated/`
- `04-corpus/compiled-sm5/`
- `04-corpus/compiled-sm6/`
- `04-corpus/compiled-sm7/`
- `06-results/in-progress/`
- Unreal `DerivedDataCache` slices created for shader compilation phases
- Unreal `Intermediate` and target build products when they exceed budget and are not needed immediately

## Implication for previous sparse Phase 1

The sparse checkout/inventory was a bootstrap sanity check only. It is not the authoritative Unreal workspace. The next real acquisition phase should create a full non-sparse Unreal working tree and keep it.
