# Unreal Sectional Clone/Build Policy

User approval received: Unreal Engine may be cloned and built for this SM6/Nanite research track, but only in bounded sections.

## Required workflow

Each round must follow this cycle:

1. **Plan the section**
   - Define branch, sparse paths, build target, expected outputs, and maximum disk budget.
   - Record the plan in `00-manifests/phase-status.md` or a phase note.

2. **Fetch/build only the section**
   - Prefer `--filter=blob:none`, shallow/sparse checkout, and targeted build commands.
   - Avoid full engine checkout/build unless a later explicit phase requires it.

3. **Extract durable artifacts**
   - Source maps, grep indexes, build logs, reduced shader corpus, patches, manifests, and checksums.
   - Avoid uploading licensed/private Epic source unless the private archive policy explicitly covers it.

4. **Archive/checkpoint**
   - Compress artifacts under `07-archives/`.
   - Push a manifest/checkpoint to a private git repo.
   - Use Git LFS or split artifacts for anything near GitHub size limits.

5. **Verify remote checkpoint**
   - Confirm the private git push succeeded.
   - Confirm hashes for uploaded archives/manifests.

6. **Wipe local intermediates**
   - Remove build intermediates and temporary checkout sections that are no longer needed.
   - Keep only scripts, manifests, notes, and archives needed for the next phase.

## Disk safety

- AverySSD available at setup: ~634 GiB.
- Phase hard cap by default: 250 GiB local growth unless explicitly revised.
- If free space drops below 250 GiB, stop and archive/wipe before continuing.
- Use `08-scripts/05-disk-guard.sh` before/after each phase.

## Private git rules

- Private git target must be confirmed before first large phase.
- Do not push to a public repo.
- Prefer publishing:
  - manifests
  - notes
  - generated indexes
  - patches/diffs
  - reduced corpora
  - logs
  - checksums
- Be careful with full Epic source snapshots because UnrealEngine is private/licensed.

## Stop conditions

Stop immediately if:

- A script would perform an unbounded/full clone or full build outside the planned section.
- Phase disk budget would be exceeded.
- GitHub/private remote push fails and local wipe would lose required evidence.
- A step would resume live Subnautica 2 launches before offline gates are explicitly green.
