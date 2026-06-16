# M12 Elden Ring Slow Repair Plan

This plan supersedes the earlier fast repair attempt. The priority is to avoid regressions and keep the currently stable Elden Ring baseline intact.

## Current stable setup

Stable runtime + cache baseline preserved at:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-stable-baseline-20260615-192313
```

Stable baseline required both:

1. Known-good `dxmt_m12` runtime.
2. Shader cache restored from the successful loaded snapshot.

Runtime hashes:

```text
d3d12.dll      92fba1da24895a9bb3c66c7f5a595001caf6f4375e6195966ccbdbabf3525a16
dxgi_dxmt.dll  793884a39b195c74121fe322be56617b100a952c1f1bb54d1aaa43d4c6fd31a2
winemetal.so   e82da21aca2a5b748cefdacc9794be9d96c1d028b90d4f94414c14214993daaa
```

Important discovery: running offline replay against the live game cache polluted the cache and caused the same runtime to stop rendering. Offline tools must never write into `~/.metalsharp/shader-cache/m12/1245620` again.

## Hard rules

1. Do not run `replay-shader-corpus.py --force` against the live game cache.
2. Do not run `offline-pso-factory.py` against a writable live cache if it depends on outputs produced by replay.
3. Before offline work, copy the stable corpus to scratch storage under `/Volumes/AverySSD` or `/tmp`.
4. Before runtime experiments, preserve both runtime and shader cache.
5. One runtime behavior change per commit.
6. Runtime changes must be env-gated until proven visually safe.
7. First runtime test after any rebuild is gate/default OFF; it must match stable baseline before gate ON is tried.
8. If default-OFF does not render, stop and bisect/rebuild baseline. Do not continue feature work.
9. The final commit in any repair sequence must be the successful bounded game test evidence.

## Working directories

Live cache, do not mutate for offline work:

```text
~/.metalsharp/shader-cache/m12/1245620
```

Scratch corpus root for offline work:

```text
/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch
```

Stable rollback helper:

```bash
tools/d3d12-metal-sdk/scripts/restore-known-good-m12-runtime.sh
```

If cache rollback is needed, use the stable baseline snapshot:

```bash
rsync -a --delete \
  /Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-stable-baseline-20260615-192313/shader-cache-m12-1245620/ \
  "$HOME/.metalsharp/shader-cache/m12/1245620/"
```

## Current observed runtime failures

Stable loaded run had:

```text
M12 present entry: 24
classification=drawn: 24
Graphics PSO compiled: 1610
Failed to create render PSO: 204
unsafe draw skips: 0
SM50Compile failed: 0
DXIL MSL compilation failed: 0
unix_call_failed: 0
```

Polluted-cache/no-render run had:

```text
M12 present entry: 22
classification=drawn: 0
Graphics PSO compiled: 180
Failed to create render PSO: 3804
Vertex function has input attributes but no vertex descriptor was set: 8698
mismatching vertex shader output: 102
unsafe draw skips: 256
DXIL MSL compilation failed: 4
```

Primary failure surfaces to investigate offline:

1. Vertex descriptor missing/type mismatch.
2. VS/PS varying mismatch.
3. Zero-output vertex captures.
4. Incomplete capture/missing metallib.
5. Cache poisoning/replay output incompatibility.

## SDK tool sequence

All commands below must use a scratch corpus, not the live cache.

### Phase 0 — Create scratch corpus

```bash
SCRATCH=/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-$(date +%Y%m%d-%H%M%S)
mkdir -p "$SCRATCH"
rsync -a ~/.metalsharp/shader-cache/m12/1245620/ "$SCRATCH/"
```

### Phase 1 — Structural manifest audits

```bash
mkdir -p /tmp/metalsharp-m12-slow-repair

python3 tools/d3d12-metal-sdk/scripts/d3d12-graphics-pso-audit.py \
  --corpus "$SCRATCH" \
  --markdown /tmp/metalsharp-m12-slow-repair/d3d12-graphics-pso-audit.md \
  --json /tmp/metalsharp-m12-slow-repair/d3d12-graphics-pso-audit.json

python3 tools/d3d12-metal-sdk/scripts/d3d12-compute-pso-audit.py \
  --corpus "$SCRATCH" \
  --markdown /tmp/metalsharp-m12-slow-repair/d3d12-compute-pso-audit.md \
  --json /tmp/metalsharp-m12-slow-repair/d3d12-compute-pso-audit.json
```

Purpose: identify invalid manifests, stage-in/input-layout mismatches, geometry/tessellation PSOs, depth/stencil issues, and structural warnings before changing runtime.

### Phase 2 — Shader corpus replay in scratch only

```bash
python3 tools/d3d12-metal-sdk/scripts/replay-shader-corpus.py \
  --corpus "$SCRATCH" \
  --profile elden-ring-stable-scratch \
  --results-dir /tmp/metalsharp-m12-slow-repair \
  --force --allow-empty
```

Purpose: prove all captured DXBC blobs still convert without touching live cache.

### Phase 3 — Offline PSO factory + classification

```bash
python3 tools/d3d12-metal-sdk/scripts/offline-pso-factory.py \
  --corpus "$SCRATCH" \
  --profile elden-ring-stable-scratch \
  --results-dir /tmp/metalsharp-m12-slow-repair \
  --allow-empty

python3 tools/d3d12-metal-sdk/scripts/classify-offline-pso-failures.py \
  /tmp/metalsharp-m12-slow-repair/offline-pso-factory-elden-ring-stable-scratch.json \
  --corpus "$SCRATCH"
```

Purpose: classify failures into stable buckets without polluting runtime state.

### Phase 4 — Cache poisoning investigation

Compare live-stable cache vs scratch-replayed cache:

- Which files changed?
- Which `.metallib`/`.json` outputs differ from the stable cache?
- Does replay write converter outputs that runtime loads differently than captured runtime outputs?

No runtime fix should begin until this is understood.

### Phase 5 — Rebuild baseline before feature fixes

Before any feature change, prove that current source with no new behavior can rebuild to a rendering baseline. If a clean rebuild/default-off does not render, the task is source/binary baseline recovery, not descriptor fixing.

Required checks:

1. Restore stable runtime + stable cache.
2. Rebuild current source with no feature change.
3. Stage rebuilt runtime.
4. Launch default/off only.
5. If not rendering, bisect current source against the known-good binary/source state.

### Phase 6 — Only then fix one bucket

After rebuilt baseline renders, pick exactly one bucket:

1. Vertex descriptor missing/type mismatch.
2. VS/PS varying mismatch.
3. Zero-output/tessellation fallback routing.
4. Capture completeness.

Each bucket gets:

- offline scratch proof
- one env-gated runtime commit
- default-off launch
- gate-on launch
- preserve evidence

## Success criteria

No change is considered successful unless:

- live cache remains restorable and unpolluted
- stable baseline can still render
- default-off rebuilt runtime renders before gate-on testing
- bounded launch metrics do not regress drawn presents or unsafe draw skips
- the final commit contains the game-test evidence
