mod nt_to_xnu;
pub mod probe;
mod types;

use serde_json::{json, Map, Value};

pub fn handle_kernel_translation_probe(_body: &Map<String, Value>) -> Value {
    let host_os = std::env::consts::OS;
    let host_arch = std::env::consts::ARCH;

    let syscall_coverage = nt_to_xnu::syscall_coverage_report();
    let struct_coverage = nt_to_xnu::struct_coverage_report();
    let blocker_summary = nt_to_xnu::blocker_summary();

    json!({
        "ok": true,
        "host": { "os": host_os, "arch": host_arch },
        "translationReady": host_os == "macos" && host_arch == "aarch64",
        "syscallCoverage": syscall_coverage,
        "structCoverage": struct_coverage,
        "blockers": blocker_summary,
        "summary": format!(
            "Kernel translation layer: {} NT syscalls mapped, {} structs paired, {} blockers identified.",
            syscall_coverage.total,
            struct_coverage.total,
            blocker_summary.blocker_count,
        ),
        "nextActions": vec![
            "See docs/research/kernel-translation/ for the complete reference and implementation mapping.",
            "Phase P1-P10 (userspace Wine) already works via existing Wine ntdll.",
            "Phase P11 (anti-cheat bridge via EndpointSecurity) is the next implementation target.",
        ],
    })
}
