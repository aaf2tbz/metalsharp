# M12 Phase 7 menu canary summary

## Result

FAIL — AC6 reached a running process, but the user observed no-render / black / broken menu.

No Continue was tested.

## Launch

- Route: `POST /steam/launch-game`
- Port: `METALSHARP_PORT=9277`
- AppID: `1888160`
- Pipeline: `m12`
- Request: `ac6-m12-phase7-menu-canary-request.json`
- Launch response: `ok=true`
- Game PID: `77440`

## Applied overrides

Backend reported all intended overrides were accepted:

- `METALSHARP_M12_LOG_LEVEL=none` -> `DXMT_LOG_LEVEL=none`
- `METALSHARP_M12_LOG_PATH=none` -> `DXMT_LOG_PATH=none`
- `METALSHARP_M12_TRACE_CAPTURE=0` -> DXMT trace/debug env disabled
- `METALSHARP_M12_BINARY_ARCHIVE=1` -> `DXMT_D3D12_BINARY_ARCHIVE=1`
- `METALSHARP_M12_BINARY_ARCHIVE_BYPASS_LOOKUP=1` -> `DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP=1`
- `METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE=0`
- `METALSHARP_M12CORE_ENABLE=1`
- `METALSHARP_M12CORE_REQUIRED=0`

## Debugger capture

User requested xcodedbg-style probe before termination. `xcodedbg` equivalent was captured with the existing low-overhead debugger script:

- Directory: `xcodedbg-readonly-20260620-174524/`
- Summary: `xcodedbg-readonly-20260620-174524/summary.md`
- Sample: `xcodedbg-readonly-20260620-174524/sample.txt`
- LLDB output: `xcodedbg-readonly-20260620-174524/lldb.txt`
- LLDB error: `xcodedbg-readonly-20260620-174524/lldb.err`

LLDB attached and stopped the Wine host, but the scripted interrupt/backtrace did not complete:

```text
error: Failed to halt process: Process is not running.
```

The `sample` capture did record relevant stacks.

## Key sample evidence

M12Core render pipeline creation was active and blocked on Objective-C synchronization:

```text
_WMTM12CoreCreatePipelineState (in winemetal.so) winemetal_unix.c:660
m12core_create_pipeline_state (in libm12core.dylib) m12core_metal.c:417
m12core_create_render_pipeline (in libm12core.dylib) m12core_metal.c:346
objc_sync_enter
_os_unfair_lock_lock_slow
```

WineMetal/AGX command-buffer/render-encoder activity was also present:

```text
_MTLCommandBuffer_waitUntilCompleted (in winemetal.so) winemetal_unix.c:1366
_MTLCommandBuffer_renderCommandEncoder (in winemetal.so) winemetal_unix.c:1869
-[AGXG16GFamilyCommandBuffer renderCommandEncoderWithDescriptor:]
-[AGXG16GFamilyRenderContext initWithCommandBuffer:descriptor:subEncoderIndex:framebuffer:]
AGX::FramebufferDriverConfig...
-[AGXG16GFamilyTexture heap]
_MTLCommandBuffer_commit (in winemetal.so) winemetal_unix.c:1359
```

No `BinaryArchive` / `MTLBinaryArchive` symbols appeared in the sample text.

## Termination

After debugger capture, AC6/Wine processes were terminated per user approval. Post-termination process snapshot was empty for the launch patterns.


## Archive side effect evidence

The failed canary created/serialized a real binary archive during startup:

```text
/Users/alexmondello/.metalsharp/pipeline-cache/m12/1888160/m12-metal-binary-archive-00000001000003c3.binarchive
size=9,269,840 bytes
mtime=2026-06-20 17:43:10
```

Launch began at approximately `17:42:24`, so archive population/serialization was active during the no-render startup window. This supports the root-cause class: synchronous per-PSO archive mutation and early batch serialization perturb startup/menu rendering even when lookup is bypassed.

## Interpretation

Phase 7 binary archive population with lookup bypass is not accepted yet because it failed the menu-parity gate. The immediate sample points at M12Core render pipeline creation synchronizing while WineMetal/AGX command-buffer/render-encoder work is active. This is enough to reject live progression and return to offline/root-cause analysis before any more AC6 launches.
