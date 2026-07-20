# Historical Rust → Genuine C Port Checklist

Oracle source: `18bff9c14afa9395ef0374114739695b01f33169^`. Generated from the historical Rust tree; status is conservative and marker-based, not a parity claim.

| Rust module | Lines | Route literals | C counterpart | Status |
|---|---:|---:|---|---|
| `binding_contract.rs` | 612 | 0 | `app/src-c/runtime/genuine/binding_contract.c` | partial/stub |
| `bottles.rs` | 7432 | 53 | `—` | missing or folded into another module |
| `command_contract.rs` | 706 | 0 | `app/src-c/runtime/genuine/command_contract.c` | partial/stub |
| `d3d12_runtime_doctor.rs` | 760 | 1 | `—` | missing or folded into another module |
| `d3dmetal_gptk.rs` | 1682 | 11 | `app/src-c/runtime/genuine/d3dmetal_gptk.c` | partial/stub |
| `diagnostics.rs` | 631 | 0 | `app/src-c/runtime/genuine/diagnostics.c` | partial/stub |
| `fna_profile.rs` | 580 | 1 | `app/src-c/runtime/genuine/fna_profile.c` | partial/stub |
| `gog.rs` | 1238 | 10 | `—` | missing or folded into another module |
| `installer.rs` | 2865 | 32 | `—` | missing or folded into another module |
| `kernel_translation/anti_debug.rs` | 968 | 0 | `—` | missing or folded into another module |
| `kernel_translation/apc.rs` | 765 | 0 | `—` | missing or folded into another module |
| `kernel_translation/code_integrity.rs` | 670 | 0 | `—` | missing or folded into another module |
| `kernel_translation/driver_model.rs` | 960 | 0 | `—` | missing or folded into another module |
| `kernel_translation/es_bridge.rs` | 1174 | 1 | `—` | missing or folded into another module |
| `kernel_translation/es_live.rs` | 480 | 3 | `—` | missing or folded into another module |
| `kernel_translation/handle_bridge.rs` | 802 | 0 | `—` | missing or folded into another module |
| `kernel_translation/handle_callbacks.rs` | 919 | 0 | `—` | missing or folded into another module |
| `kernel_translation/handle_table.rs` | 670 | 0 | `—` | missing or folded into another module |
| `kernel_translation/integration.rs` | 1026 | 0 | `—` | missing or folded into another module |
| `kernel_translation/ipc_bridge.rs` | 907 | 0 | `—` | missing or folded into another module |
| `kernel_translation/mod.rs` | 99 | 0 | `—` | missing or folded into another module |
| `kernel_translation/nt_to_xnu.rs` | 113 | 0 | `—` | missing or folded into another module |
| `kernel_translation/probe.rs` | 77 | 0 | `—` | missing or folded into another module |
| `kernel_translation/thread_notify.rs` | 966 | 0 | `—` | missing or folded into another module |
| `kernel_translation/types.rs` | 755 | 0 | `—` | missing or folded into another module |
| `launch.rs` | 461 | 12 | `app/src-c/runtime/genuine/launch.c` | partial/stub |
| `launcher_evidence.rs` | 481 | 0 | `app/src-c/runtime/genuine/launcher_evidence.c` | partial/stub |
| `main.rs` | 2831 | 277 | `app/src-c/runtime/genuine/main.c` | implemented; audit pending |
| `metalfx.rs` | 252 | 0 | `app/src-c/runtime/genuine/metalfx.c` | partial/stub |
| `migrate.rs` | 3405 | 9 | `—` | missing or folded into another module |
| `mono.rs` | 875 | 7 | `app/src-c/runtime/genuine/mono.c` | partial/stub |
| `mtsp/default_rules.rs` | 212 | 0 | `—` | missing or folded into another module |
| `mtsp/engine.rs` | 958 | 0 | `app/src-c/runtime/genuine/mtsp_engine.c` | partial/stub |
| `mtsp/launcher.rs` | 6535 | 41 | `—` | missing or folded into another module |
| `mtsp/mod.rs` | 7 | 0 | `—` | missing or folded into another module |
| `mtsp/pe.rs` | 346 | 0 | `—` | missing or folded into another module |
| `mtsp/recipe.rs` | 1746 | 1 | `—` | missing or folded into another module |
| `mtsp/rules.rs` | 787 | 2 | `—` | missing or folded into another module |
| `mtsp/shader_cache.rs` | 675 | 0 | `—` | missing or folded into another module |
| `platform.rs` | 1508 | 13 | `app/src-c/runtime/genuine/platform.c` | partial/stub |
| `scan.rs` | 949 | 5 | `app/src-c/runtime/genuine/scan.c` | partial/stub |
| `setup.rs` | 2185 | 17 | `app/src-c/runtime/genuine/setup.c` | partial/stub |
| `sharp_library.rs` | 2321 | 17 | `app/src-c/runtime/genuine/sharp_library.c` | partial/stub |
| `steam.rs` | 1738 | 6 | `app/src-c/runtime/genuine/steam.c` | partial/stub |
| `updater.rs` | 472 | 0 | `app/src-c/runtime/genuine/updater.c` | partial/stub |

**Total:** 45 Rust modules, 56,601 lines.

Completion requires route-specific valid/invalid/stateful/restart/cancellation/filesystem/process/network fixtures; an “implemented” marker here does not satisfy behavioral parity.
