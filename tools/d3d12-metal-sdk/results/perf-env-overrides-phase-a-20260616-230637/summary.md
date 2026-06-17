# Phase A backend performance env override plumbing

Implemented opt-in/profile-only forwarding for Apple-performance backend knobs.

## Forwarded GPTK/D3DMetal keys

- `METALSHARP_GPTK_D3DM_MTL4` -> `D3DM_MTL4`
- `METALSHARP_GPTK_MAX_FPS` -> `D3DM_MAX_FPS`
- `METALSHARP_GPTK_HUD` -> `MTL_HUD_ENABLED`, `D3DM_SHOW_HUD_STATS`
- `METALSHARP_GPTK_SHADER_VALIDATION` -> `MTL_SHADER_VALIDATION`
- `METALSHARP_GPTK_EXE_OVERRIDE` -> `D3DM_EXE_OVERRIDE`
- `METALSHARP_GPTK_METALFX` -> `D3DM_ENABLE_METALFX`

## Forwarded DXVK/MoltenVK keys

- `METALSHARP_MVK_MAXIMIZE_CONCURRENT_COMPILATION` -> `MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION`
- `METALSHARP_MVK_SYNCHRONOUS_QUEUE_SUBMITS` -> `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS`
- `METALSHARP_MVK_PREFILL_COMMAND_BUFFERS` -> `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS`
- `METALSHARP_MVK_PRESENT_WITH_COMMAND_BUFFER` -> `MVK_CONFIG_PRESENT_WITH_COMMAND_BUFFER`
- `METALSHARP_MVK_MAX_ACTIVE_COMMAND_BUFFERS_PER_QUEUE` -> `MVK_CONFIG_MAX_ACTIVE_METAL_COMMAND_BUFFERS_PER_QUEUE`
- `METALSHARP_MVK_FAST_MATH` -> `MVK_CONFIG_FAST_MATH_ENABLED`
- `METALSHARP_MVK_USE_MTLHEAP` -> `MVK_CONFIG_USE_MTLHEAP`
- `METALSHARP_MVK_USE_COMMAND_POOLING` -> `MVK_CONFIG_USE_COMMAND_POOLING`
- `METALSHARP_MVK_PERFORMANCE_TRACKING` -> `MVK_CONFIG_PERFORMANCE_TRACKING`
- `METALSHARP_MVK_PERFORMANCE_LOGGING_FRAME_COUNT` -> `MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT`

## Guardrails

- Empty host values are ignored.
- GPTK/D3DMetal keys are not forwarded to M12/DXMT.
- MoltenVK keys are scoped to `dxvk` backend/graphics backend only.
- Launch logs now record `sync.WINEMSYNC=1` and active backend env overrides.

## Validation

- `cargo fmt --all`
- `cargo test gptk_d3dmetal_overrides_are_forwarded_only_to_gptk_family -- --nocapture`
- `cargo test moltenvk_overrides_are_scoped_to_dxvk_backend -- --nocapture`
- `cargo test mtsp::launcher::tests:: -- --nocapture`
- `cargo clippy --all-targets -- -D warnings`
