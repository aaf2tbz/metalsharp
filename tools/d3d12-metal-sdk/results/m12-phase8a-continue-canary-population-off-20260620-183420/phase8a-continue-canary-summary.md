# M12 Phase 8A Continue canary — population off

## Supersession notice — do not use as current failure evidence

This result is **superseded / invalidated** by the later full-DLL staging fix and full-DLL staged AC6 run:

```text
tools/d3d12-metal-sdk/results/m12-full-dll-staging-fix-20260620-215250/
tools/d3d12-metal-sdk/results/m12-full-dll-staging-fix-launch-20260620-215442/
```

After this capture, we discovered the staged M12 runtime had incomplete DLL coverage: `d3d11.dll` and `d3d10core.dll` were not explicitly rebuilt/staged/preflight-parity-checked with the rest of the M12 surface. The corrected full-DLL staged run rendered and was user-confirmed as working. Therefore this older run should not be cited as proof that Phase 8 population-off binary archive currently fails.

Keep this file only as historical evidence of the mixed/partial-runtime failure shape.

## Historical result

FAIL under later-invalidated partial/mixed runtime staging — user observed typical post-Continue black screen / severe hang / GPU-MMU-firmware-lockup class.

This run intentionally kept binary archive population disabled, but runtime staging was later found invalid.

## Launch

- Route: `POST /steam/launch-game`
- Port: `METALSHARP_PORT=9277`
- AppID: `1888160`
- Pipeline: `m12`
- Request: `ac6-m12-phase8a-continue-canary-request.json`
- Launch response: `ok=true`
- Game PID: `28184`

## Applied overrides

Backend reported these overrides were accepted:

- `METALSHARP_M12_LOG_LEVEL=none` -> `DXMT_LOG_LEVEL=none`
- `METALSHARP_M12_LOG_PATH=none` -> `DXMT_LOG_PATH=none`
- `METALSHARP_M12_TRACE_CAPTURE=0` -> DXMT trace/debug env disabled
- `METALSHARP_M12_BINARY_ARCHIVE=1` -> `DXMT_D3D12_BINARY_ARCHIVE=1`
- `METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP=1` -> `DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP=1`
- `METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE=0`
- `METALSHARP_M12CORE_ENABLE=1`
- `METALSHARP_M12CORE_REQUIRED=0`

Population was intentionally omitted:

- no `METALSHARP_M12_BINARY_ARCHIVE_POPULATE`
- no `DXMT_D3D12_BINARY_ARCHIVE_POPULATE` in `env_handoff`

## Archive side-effect baseline

Before launch, the existing AC6 binary archive was:

```text
/Users/alexmondello/.metalsharp/pipeline-cache/m12/1888160/m12-metal-binary-archive-00000001000003c3.binarchive
mtime=2026-06-20 17:43:10
size=9,269,840 bytes
```

This confirms the canary did not depend on live archive population.

## Debugger capture

User requested Xcode probe only after observing the black screen/hang.

- Directory: `xcodedbg-readonly-postcontinue-20260620-183633/`
- Summary: `xcodedbg-readonly-postcontinue-20260620-183633/summary.md`
- Sample: `xcodedbg-readonly-postcontinue-20260620-183633/sample.txt`
- Exact interesting lines: `xcodedbg-readonly-postcontinue-20260620-183633/exact-interesting-lines.txt`

LLDB produced only a minimal stop at `mach_msg2_trap`; sample captured the useful activity.

## Key sample evidence

The sample contains substantial M12Core DXIL-to-MSL lowering work:

```text
_WMTM12CoreLowerDXILToMSL (in winemetal.so) winemetal_unix.c:497
m12core_lower_dxil_to_msl (in libm12core.dylib) m12core.cpp:572
dxmt::dxil::MSLLowering::lower(...) msl_lowering.cpp:4623
dxmt::dxil::emitTypedInstruction(...) msl_lowering.cpp:3307 / 3349 / 3523
```

It also shows Metal command queue submission activity:

```text
-[_MTLCommandQueue _submitAvailableCommandBuffers]
-[IOGPUMetalCommandQueue submitCommandBuffers:count:]
-[IOGPUMetalCommandQueue _submitCommandBuffers:count:]
IOGPUCommandQueueSubmitCommandBuffers
```

No `BinaryArchive` / `MTLBinaryArchive` symbols appeared in the extracted sample lines.

## Interpretation

The Phase 7 menu regression was fixed by disabling live binary-archive population. However, the original post-Continue severe hang remains reproducible with population off. The current evidence points back to the broader post-Continue compile/submit pressure class: M12Core generated-MSL lowering and Metal command queue submission are active during the black-screen/hang window, without binary-archive mutation showing in the stack sample.

Next work should continue root-cause analysis of post-Continue GPU/command-buffer/MMU pressure separately from binary archive population.
