# M12 Performance Harness and Optimization Plan

Date: 2026-06-15
Branch: `fix/m12-shader-probe-lab`

## Status shift

The M12 workstream has moved from correctness repair to performance optimization.

Current correctness defaults are considered the baseline:

```text
DXMT_D3D12_PSO_WORKERS=1
DXMT_ASYNC_PIPELINE_COMPILE=1
DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=1
DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1
DXMT_D3D12_VERTEX_RANGE_SAFE_DRAW=1
```

Recent evidence:

```text
workers=1 + async=1: drawn=24/24, render failures=0, unsafe skips=0
empty cache: drawn=24/24, render failures=0, unsafe skips=0
```

The next goal is not to fix render failures. The next goal is to produce a dedicated M12 performance system that can make M12 games faster and smoother in general without weakening runtime correctness.

## User-observed problem

M12 is now functionally correct, but loading pressure accumulates over time. Once the game gets far enough into loading and character creation, the load catches up heavily until the character-creation menu becomes effectively unclickable. The game can still be loaded and rendered, but interaction/frame pacing becomes unusable.

This points to runtime pressure and scheduling rather than discrete PSO failure buckets.

## Main working theory

The strongest current theory is a combination of:

1. **Texture/upload pressure**
   - too much texture/resource upload work lands on critical paths
   - staging/copy work may not be paced against frame boundaries
   - memory allocation or resource residency may be too bursty

2. **PSO compile pressure**
   - even when PSO creation succeeds, compile bursts can starve UI/game progress
   - async compile helps correctness/scheduling but still needs pacing/backpressure
   - forced source compile is correct but may make first-run and ongoing cache-refresh work too expensive

3. **Wide-spread runtime pipeline overhead**
   - M12 currently has performance-sensitive work spread across many systems/files
   - broad runtime paths make it harder to optimize hot behavior coherently
   - M11/DXMT appears much more mature and optimized, likely with tighter hot paths, better cache behavior, and fewer avoidable state/resource transitions

The performance plan should test this theory directly rather than randomly changing knobs.

## References to study before deep optimization

Use these as design references, not as excuses to weaken correctness:

- latest Apple Metal performance documentation
- Apple Game Porting Toolkit guidance and observed behavior
- DXMT M11/D3D11 implementation patterns
- existing DXMT shared backend abstractions in `vendor/dxmt/src/dxmt/`

Specific Apple/Metal topics to review:

- pipeline state creation and binary archives
- resource heaps and allocation reuse
- staging buffers and upload pacing
- command buffer scheduling and completion handlers
- avoiding synchronous CPU/GPU waits
- argument buffers/resource binding overhead
- texture streaming, mip residency, and storage modes
- frame pacing and drawable acquisition timing

Specific M11/DXMT topics to compare:

- shader/pipeline cache design
- resource renaming and texture allocation pools
- command context submission model
- residency tracking
- dynamic/staging buffer upload paths
- render pipeline state cache keys and reuse
- hot state-change deduplication

## Harness objective

Build a dedicated M12 performance harness that answers:

```text
What caused this frame/loading stall?
Was it PSO compile pressure, texture/upload pressure, command submission, resource allocation, or game CPU time?
Which exact runtime subsystem produced the pressure?
Did an optimization improve frame pacing without reducing correctness?
```

The harness must be reusable across M12 games, not Elden Ring-only. From this point forward, performance validation should include multiple real DX12 gameplay titles.

## Required multi-game coverage

The performance harness should treat Elden Ring as only one workload. Required coverage should include:

```text
Elden Ring
Schedule 1 or PEAK
Subnautica 2
```

All of these support DX12 gameplay and should expose different M12 pressure patterns:

- **Elden Ring**: heavy PSO churn, long loading, character creation/menu responsiveness, large asset streaming
- **Schedule 1 or PEAK**: additional Unity/modern DX12-style gameplay coverage; useful as a second non-FromSoftware workload
- **Subnautica 2**: broad streaming/texture/effects workload and existing M12 correctness corpus coverage

The harness should support per-game profiles instead of hardcoding Elden Ring:

```text
tools/d3d12-metal-sdk/profiles/m12-perf/elden-ring.json
tools/d3d12-metal-sdk/profiles/m12-perf/schedule-1.json
tools/d3d12-metal-sdk/profiles/m12-perf/peak.json
tools/d3d12-metal-sdk/profiles/m12-perf/subnautica-2.json
```

Each profile should define:

```text
appid or launcher target
default run duration
cache path
known DX12 launch flags, if any
scenario names
manual marker labels
expected log locations
comparison baseline path
```

Minimum performance matrix for future optimization work:

```text
Elden Ring smoke/loading/character-creation
Schedule 1 or PEAK smoke/gameplay
Subnautica 2 smoke/loading/gameplay
empty-cache first run for at least one title
warm-cache second run for all tested titles
```

## Phase 1 — performance telemetry foundation

Create an M12 perf telemetry mode, off by default:

```text
DXMT_D3D12_M12_PERF_TRACE=1
METALSHARP_M12_PERF_TRACE=1
```

The trace must be lightweight enough to leave enabled during bounded gameplay and should write structured JSONL or compact CSV, not only free-text logs.

Suggested output path:

```text
~/.metalsharp/logs/m12-perf/<appid>/<launch-id>/
```

### Required per-frame metrics

Record one row per present/frame:

```text
frame_index
wall_time_ms_since_launch
frame_wall_ms
present_interval_ms
drawn
pso_compile_count_this_frame
pso_compile_ms_this_frame
pso_wait_ms_this_frame
texture_create_count_this_frame
texture_create_bytes_this_frame
texture_upload_count_this_frame
texture_upload_bytes_this_frame
buffer_upload_count_this_frame
buffer_upload_bytes_this_frame
command_buffers_submitted_this_frame
command_encoder_switches_this_frame
gpu_wait_or_fence_wait_ms_this_frame
draw_count
Dispatch_count
resource_barrier_count
heap_or_allocation_count_this_frame
cache_hits_this_frame
cache_misses_this_frame
```

If exact frame attribution is hard initially, start with rolling 250ms/500ms buckets and correlate by timestamp.

### Required event metrics

Emit events for expensive operations:

```text
pso_compile_start/end
newLibraryWithSource_start/end
newRenderPipelineState_start/end
newComputePipelineState_start/end
texture_create
texture_upload/replaceregion/copy
buffer_upload
heap_allocate/resource_allocate
command_buffer_commit
command_buffer_complete
fence_wait/gpu_wait
present_start/end
drawable_acquire
```

Each event should include:

```text
timestamp_us
thread_id
event_name
duration_us, if complete event
appid
frame_index_or_bucket
resource size / dimensions / format, if relevant
shader/pso hash, if relevant
cache hit/miss/stale state, if relevant
```

## Phase 2 — external sampler harness

Extend or add a script:

```text
tools/d3d12-metal-sdk/scripts/m12-performance-run.sh
```

It should wrap bounded launch but add performance collection and work for any configured M12 perf profile:

```text
--profile elden-ring
--seconds 180
--scenario character-creation
--perf-trace
--sample-process
--sample-interval-ms 250
--output results/perf-runs/<game>-<timestamp>/
```

Collect:

```text
bounded launch summary
M12 perf trace JSONL/CSV
process CPU/RSS/thread count samples
cache artifact delta
log failure bucket summary
optional Instruments/Metal System Trace instructions or hooks
```

Produce:

```text
summary.md
summary.json
frame-times.csv
pressure-buckets.csv
hot-events.md
pso-pressure.md
upload-pressure.md
cache-pressure.md
```

## Phase 3 — game progression timing

The harness must support both game-specific progression scenarios and generic smoke/gameplay scenarios. Elden Ring character creation is the first known stress point, but it should not be the only scenario.

Required scenario examples:

```text
elden-ring-character-creation
elden-ring-loading-to-gameplay
schedule-1-gameplay-smoke
peak-gameplay-smoke
subnautica-2-loading-to-gameplay
subnautica-2-gameplay-streaming
```

## Phase 3A — Elden Ring character creation progression timing

The current symptom appears only after enough loading progress, so a simple 45–60s launch is no longer sufficient.

Add a scenario protocol for Elden Ring character creation:

```text
scenario: elden-ring-character-creation
warmup: launch to menu/loading
measurement: continue until character creation is visible and menu interaction becomes available
manual marker support: user can press a key or create a marker file when reaching milestones
```

Marker mechanism:

```text
~/.metalsharp/logs/m12-perf/<appid>/<launch-id>/markers.jsonl
```

Markers:

```text
launch_started
first_present
main_menu_visible
load_started
character_creation_visible
menu_first_click_attempt
menu_unresponsive
menu_responsive_again
run_stopped
```

This lets us correlate user-visible freezes with exact PSO/upload/resource pressure windows.

## Phase 4 — analysis scripts

Add scripts:

```text
tools/d3d12-metal-sdk/scripts/analyze-m12-perf-trace.py
tools/d3d12-metal-sdk/scripts/compare-m12-perf-runs.py
tools/d3d12-metal-sdk/scripts/analyze-m12-upload-pressure.py
tools/d3d12-metal-sdk/scripts/analyze-m12-pso-pressure.py
```

Core outputs:

```text
worst 1% frame intervals
longest contiguous stall windows
top PSO compile bursts by time window
top texture upload bursts by time window
top resource allocation bursts by time window
CPU thread saturation windows
cache miss/stale windows
correlation between upload bytes and frame stalls
correlation between PSO compile ms and frame stalls
```

Success criterion: the analyzer should produce a short answer like:

```text
Worst stall: 14.2s at T+96.4s.
During this window: 812MB texture uploads, 74 texture creations, 139 PSO source compiles, 2.1s fence wait.
Primary pressure: texture upload + PSO compile overlap.
```

## Phase 5 — optimization tracks

Only start these once the harness can measure before/after.

### Track A — persistent refreshed shader cache

Problem:

```text
DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1
```

is correct but expensive. Empty cache works, but source compile pressure remains high.

Optimization direction:

- add an M12 shader cache epoch/version salt for the new correct ABI
- treat stale metallibs as invalid only when epoch mismatches
- persist refreshed DXIL metallibs and reflection data once regenerated
- avoid recompiling source every run after the cache is refreshed

Expected win:

- large reduction in first minutes of PSO compile pressure after warm cache
- smoother character creation/loading after one successful run

Correctness guard:

- epoch must include ABI-affecting toggles:
  - typed stage-in descriptors
  - DXIL source compile path version
  - vertex descriptor reflection behavior
  - shader lowering version

### Track B — PSO compile pacing/backpressure

Problem:

Even successful PSO compiles can flood CPU and block interaction.

Optimization direction:

- limit compile bursts per time window
- prioritize visible/needed PSOs over speculative compile
- avoid scheduling too many source compiles during menu/input-heavy phases
- make async queue cooperative with frame pacing
- keep default workers=1 but add smarter queue policy

Candidate metrics:

```text
PSO compile ms per second
number of concurrent active compiles
compile queue depth
frames with pso_compile_ms > threshold
```

Expected win:

- fewer long CPU stalls
- more responsive menus during ongoing loading

Correctness guard:

- never use invalid PSO
- missing PSO fallback behavior must remain explicit and tracked

### Track C — texture/upload pacing

Problem:

Texture/resource uploads may saturate CPU, memory bandwidth, or Metal command scheduling.

Optimization direction:

- instrument all texture create/upload/copy paths
- detect huge upload bursts and spread them across frames when possible
- batch small uploads efficiently
- reuse staging buffers/ring buffers
- reduce synchronous texture replacement on hot path
- investigate MTLHeap usage/resource pooling where safe
- compare M11 texture renaming/pooling patterns

Candidate metrics:

```text
texture upload bytes/sec
texture create count/sec
largest upload event
upload events during worst frame windows
allocation count/sec
RSS growth and memory pressure
```

Expected win:

- less frame pacing collapse during asset streaming
- reduced menu unresponsiveness while world/character assets continue loading

Correctness guard:

- resource visibility/order must preserve D3D12 semantics
- no delayed upload if draw/dispatch requires current data

### Track D — command submission and GPU wait reduction

Problem:

The game may render but input/UI becomes unresponsive if CPU blocks on GPU fences or command submission synchronization.

Optimization direction:

- instrument fence waits and command buffer completion waits
- identify synchronous waits during loading
- avoid unnecessary command buffer flushes
- batch command buffers without increasing latency too far
- compare M11 command queue/context behavior

Expected win:

- lower CPU blocking time
- smoother input responsiveness

### Track E — focused hot runtime pipeline

Problem:

M12 performance-sensitive work is spread across many files and systems, making hot-path optimization difficult.

Optimization direction:

- map the actual hot files/functions from telemetry
- create a small set of focused M12 hot-path modules for:
  - PSO cache/compile scheduling
  - upload/staging scheduler
  - resource residency/allocation pacing
  - frame pacing/command submission tracking
- avoid broad cross-file incidental work in steady-state frame execution

Candidate output:

```text
tools/d3d12-metal-sdk/plans/m12-hot-path-map.md
```

Expected win:

- fewer redundant transitions/checks/logging on frame path
- easier optimization and regression control

## Phase 6 — M11 comparison plan

Create a focused comparison document:

```text
tools/d3d12-metal-sdk/plans/m12-vs-m11-performance-comparison.md
```

Questions:

```text
How does M11 cache pipeline state?
How does M11 avoid repeated shader compilation?
How does M11 allocate/reuse textures and staging buffers?
How does M11 batch command submission?
Where does M11 use shared dxmt abstractions that M12 could adopt?
Which M12 paths bypass optimized shared infrastructure?
```

Deliverable:

```text
ranked list of M11 patterns to port/adapt to M12
```

## Phase 7 — performance gates for future updates

Every future M12 optimization should run through this multi-game matrix:

```text
Elden Ring 60s smoke
Elden Ring 180s loading/character-creation perf scenario
Schedule 1 or PEAK 60s smoke/gameplay
Subnautica 2 60s smoke
Subnautica 2 loading/gameplay perf scenario
empty cache first-run for at least one representative title
warm cache second-run for all titles under test
```

Required pass conditions:

```text
render_pso_failed=0
vertex_descriptor_missing=0
vs_ps_varying_mismatch=0
unsafe_draw_skips=0
no new dxil_msl_compile_failed
no new fail markers
```

Required improvement evidence for perf PRs:

```text
lower worst 1% frame time, or
lower longest stall window, or
lower PSO compile pressure during loading, or
lower upload pressure during loading, or
better character-creation interaction timing
```

## Immediate implementation tasks

1. Add `m12-performance-run.sh` wrapper around bounded launch.
2. Add lightweight process sampling to capture CPU/RSS/thread count at 250ms intervals.
3. Add perf-run result directory schema and summary generation.
4. Add runtime trace events for PSO compile start/end first.
5. Add runtime trace events for texture create/upload next.
6. Add `analyze-m12-perf-trace.py` to correlate stalls with PSO/upload pressure.
7. Run baseline scenarios across multiple games:
   - Elden Ring empty cache, default workers=1 async=1
   - Elden Ring warm cache, default workers=1 async=1
   - Elden Ring character-creation manual progression run
   - Schedule 1 or PEAK warm-cache gameplay smoke
   - Subnautica 2 warm-cache gameplay/loading smoke
8. Use the multi-game baseline to choose the first real optimization track:
   - persistent refreshed shader cache
   - PSO queue pacing
   - texture upload pacing

## Non-goals

Do not weaken the correctness path to gain apparent FPS.

Avoid:

```text
disabling validation silently
skipping failed draws as a performance feature
using stale metallibs without an epoch check
turning off source compile unless refreshed cache safety exists
blindly increasing worker counts
optimizing only a 45s launch smoke test
```

## Definition of success

The harness is successful when it can explain the character-creation slowdown in measurable terms and compare optimizations with evidence.

The optimization program is successful when M12 can keep runtime correctness while substantially reducing:

```text
loading stalls
menu unresponsiveness
worst-frame spikes
PSO compile bursts
texture/upload bursts
cache regeneration cost
```

across Elden Ring, Schedule 1/PEAK, Subnautica 2, and other M12 games.

## Harness foundation started — 2026-06-15

Initial implementation added:

```text
tools/d3d12-metal-sdk/profiles/m12-perf/elden-ring.json
tools/d3d12-metal-sdk/profiles/m12-perf/subnautica-2.json
tools/d3d12-metal-sdk/profiles/m12-perf/schedule-1.json
tools/d3d12-metal-sdk/profiles/m12-perf/peak.json
tools/d3d12-metal-sdk/scripts/m12-performance-run.sh
tools/d3d12-metal-sdk/scripts/sample-m12-process.py
tools/d3d12-metal-sdk/scripts/analyze-m12-perf-run.py
```

`m12-bounded-launch.sh` now supports concurrent process CSV sampling through:

```text
M12_PROCESS_SAMPLE_CSV=<path>
M12_PROCESS_SAMPLE_INTERVAL_MS=250
```

First smoke proof:

```text
tools/d3d12-metal-sdk/results/perf-runs/elden-ring-smoke-20260615-213811/perf-analysis.md
```

Captured:

```text
drawn/present=22/22
render_pso_failed=0
dxil_msl_compile_failed=0
unsafe_draw_skips=0
PSO compiles total=445
process samples=77
max CPU %=345.4
avg CPU %=168.69
max RSS bytes=3127136256
max threads=93
```

This is only the harness foundation. Next implementation layer should add native runtime perf trace events for PSO compile durations and texture/upload events, since process-level sampling alone can show pressure but cannot yet attribute it precisely.

## Multi-game harness targets concretized — 2026-06-15

Installed DX12 gameplay targets discovered locally:

```text
ELDEN RING    appid=1245620
Subnautica 2  appid=1962700
Schedule I    appid=3164500
PEAK          appid=3527290
```

Harness/profile updates:

```text
tools/d3d12-metal-sdk/profiles/m12-perf/schedule-1.json
tools/d3d12-metal-sdk/profiles/m12-perf/peak.json
tools/d3d12-metal-sdk/scripts/m12-bounded-launch.sh
```

`m12-bounded-launch.sh` now supports:

```text
--profile elden-ring
--profile subnautica-2
--profile schedule-1
--profile peak
```

Preflight now checks the isolated M12 runtime explicitly:

```text
~/.metalsharp/runtime/wine/lib/dxmt_m12
```

The static M12 pipeline metadata was aligned with the actual launcher default:

```text
DXMT_D3D12_PSO_WORKERS=1
DXMT_ASYNC_PIPELINE_COMPILE=1
```

Dry-run verification was performed without gameplay launches for all four appids. Each reported:

```text
ok=true
missing=[]
d3d12_present=true
workers=1
async=1
DXMT_SHADER_CACHE_PATH=~/.metalsharp/shader-cache/m12/<appid>/
```

Next safe step is controlled smoke runs, one game at a time, with the external perf harness only. Do not reintroduce native perf trace injection until launch/runtime staging is fully stable.

## First multi-game smoke findings — 2026-06-15

The initial external-harness multi-game pass was intentionally short and safe. Native runtime perf tracing remains disabled.

### PEAK

```text
tools/d3d12-metal-sdk/results/perf-runs/peak-smoke-20260615-215309/perf-analysis.md
```

Result:

```text
launch_ok=false
pid=None
error="Agility SDK x64 payload not found for version 611"
```

Interpretation:

- PEAK is currently prelaunch-blocked by Agility SDK 611 staging.
- This is not an M12 render/runtime failure.
- Harness now captures this as a structured failed launch instead of aborting the script.

### Subnautica 2

```text
tools/d3d12-metal-sdk/results/perf-runs/subnautica-2-smoke-20260615-215329/perf-analysis.md
tools/d3d12-metal-sdk/results/perf-runs/subnautica-2-smoke-20260615-215329/bounded-summary.md
```

Result:

```text
launch_ok=true
drawn/present=20/21
render_pso_failed=0
unsafe_draw_skips=0
graphics_pso_compiled=1
compute_pso_compiled=156
dxil_msl_compile_failed=21
new_msl_errors=21
```

Interpretation:

- Subnautica 2 reaches M12 presents and is a valid multi-game perf target.
- It exposes compute shader lowering/MSL failures and heavy compute PSO pressure.
- Treat it as a separate M12 coverage item before using it as pure performance evidence.

### Schedule I

```text
tools/d3d12-metal-sdk/results/perf-runs/schedule-1-smoke-20260615-215451/perf-analysis.md
tools/d3d12-metal-sdk/results/perf-runs/schedule-1-smoke-20260615-215451/bounded-summary.md
```

Result:

```text
launch_ok=true
present_count=0
process samples=111
max CPU %=81.9
max RSS bytes=700764160
max threads=59
```

The launch log reaches M12/DXMT initialization:

```text
Maximum supported feature level: D3D_FEATURE_LEVEL_12_1
Using feature level D3D_FEATURE_LEVEL_11_1
```

then terminates before presents with:

```text
NSInvalidArgumentException: attempt to insert nil object from objects[1]
libc++abi: terminating due to uncaught exception of type NSException
```

Interpretation:

- Schedule I launches far enough to initialize M12, but is not yet a valid frame-pacing target.
- This should be classified separately from Elden Ring performance work.

### Harness behavior improvement

`m12-bounded-launch.sh` now records backend launch error JSON instead of exiting on HTTP 500. This lets the multi-game matrix classify prelaunch blockers such as missing Agility payloads without losing the run directory.

Next logical step:

1. Keep Elden Ring as the primary long-run performance target.
2. Fix/prepare PEAK Agility staging so it can enter the matrix.
3. Classify Subnautica 2 compute MSL failures separately from performance.
4. Classify Schedule I native exception separately from performance.
5. Continue adding external measurements before any native runtime instrumentation.
