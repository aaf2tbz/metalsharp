# M12 Phase 8 — full-DLL staged AC6 Continue/turtle summary

## Result

PASS / FIXED — the apparent Phase 8 post-Continue failure was caused by invalid partial M12 runtime staging, not by the population-off binary archive path.

The corrected full-DLL staged run rendered successfully and was confirmed by the user. Do **not** treat the earlier `m12-phase8a-continue-canary-population-off-20260620-183420` black-screen/hang as current Phase 8 failure evidence; that run was later invalidated by incomplete `d3d11.dll` / `d3d10core.dll` build+stage+preflight coverage.

## Evidence

- Full-DLL staging/preflight result:
  - `tools/d3d12-metal-sdk/results/m12-full-dll-staging-fix-20260620-215250/`
- Launch result:
  - `tools/d3d12-metal-sdk/results/m12-full-dll-staging-fix-launch-20260620-215442/`
- Launch request:
  - `ac6-m12-full-dll-staged-request.json`
- Launch response:
  - `launch-response.json`
- Turtle capture:
  - `xcodedbg-readonly-postcontinue-turtle-20260620-215945/summary.md`
- Extracted sample lines:
  - `xcodedbg-readonly-postcontinue-turtle-20260620-215945/exact-interesting-lines.txt`

## Required atomic M12 runtime set

Future AC6 Continue evidence is invalid unless this full set is rebuilt, staged, and preflight parity-checked together:

```text
d3d12.dll
d3d11.dll
d3d10core.dll
dxgi.dll
dxgi_dxmt.dll
winemetal.dll
winemetal.so
libm12core.dylib
```

Important: `d3d11.dll` and `d3d10core.dll` must be explicitly covered. Do not assume they are valid because files exist in the runtime directory.

## Launch mode used

- Route: `POST /steam/launch-game`
- Port: `METALSHARP_PORT=9277`
- AppID: `1888160`
- Pipeline: `m12`
- Runtime: `~/.metalsharp/runtime/wine/lib/dxmt_m12`
- `mscompatdb`: absent / must remain absent
- Binary archive: enabled
- Binary archive lookup: bypassed
- Binary archive population: omitted / off
- Logging/tracing: none

Request overrides:

```json
{
  "METALSHARP_M12_LOG_LEVEL": "none",
  "METALSHARP_M12_LOG_PATH": "none",
  "METALSHARP_M12_TRACE_CAPTURE": "0",
  "METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE": "0",
  "METALSHARP_M12CORE_ENABLE": "1",
  "METALSHARP_M12CORE_REQUIRED": "0",
  "METALSHARP_M12_BINARY_ARCHIVE": "1",
  "METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP": "1"
}
```

Population was intentionally omitted:

```text
no METALSHARP_M12_BINARY_ARCHIVE_POPULATE
no DXMT_D3D12_BINARY_ARCHIVE_POPULATE in env_handoff
```

## Turtle capture interpretation

The post-Continue turtle sample shape was cleaner than the earlier mixed-runtime capture:

- only minimal `m12core` presence;
- no earlier DXIL→MSL lowering storm;
- dominant useful stack was Metal/IOGPU command queue submission:

```text
com.Metal.CommandQueueDispatch
-[_MTLCommandQueue _submitAvailableCommandBuffers]
-[IOGPUMetalCommandQueue submitCommandBuffers:count:]
-[IOGPUMetalCommandQueue _submitCommandBuffers:count:]
```

This supports the conclusion that the earlier Phase 8A failure should not be used as current failure evidence.

## Guardrail for future work

Before any future Continue test, first prove full build↔staged hash parity for the atomic runtime set. If parity is missing, stop and restage; do not launch and do not interpret AC6 behavior.

Live binary archive population remains deferred/offline-first. Keep enable/lookup separate from population/mutation.
