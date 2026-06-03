pub mod apc;
pub mod code_integrity;
pub mod es_bridge;
#[cfg(target_os = "macos")]
pub mod handle_bridge;
pub mod handle_table;
mod nt_to_xnu;
pub mod probe;
pub mod types;

#[cfg(not(target_os = "macos"))]
pub mod handle_bridge {
    use serde_json::{json, Map, Value};
    macro_rules! stub {
        ($name:ident) => {
            pub fn $name(_body: &Map<String, Value>) -> Value {
                json!({"ok": false, "error": "handle enumeration bridge requires macOS"})
            }
        };
    }
    stub!(handle_enumerate_fds);
    stub!(handle_enumerate_ports);
    stub!(handle_unified_snapshot);
    stub!(handle_snapshot_all);
}

use serde_json::{json, Map, Value};

pub fn handle_kernel_translation_probe(_body: &Map<String, Value>) -> Value {
    let host_os = std::env::consts::OS;
    let host_arch = std::env::consts::ARCH;

    let syscall_coverage = nt_to_xnu::syscall_coverage_report();
    let struct_coverage = nt_to_xnu::struct_coverage_report();
    let object_type_coverage = nt_to_xnu::object_type_coverage_report();
    let drill_targets = nt_to_xnu::drill_target_summary();
    let executive_summary = nt_to_xnu::executive_function_summary();
    let endpoint_security = nt_to_xnu::endpoint_security_summary();
    let category_breakdown = nt_to_xnu::syscall_category_breakdown();

    json!({
        "ok": true,
        "host": { "os": host_os, "arch": host_arch },
        "translationReady": host_os == "macos" && host_arch == "aarch64",
        "syscallCoverage": syscall_coverage,
        "structCoverage": struct_coverage,
        "objectTypeCoverage": object_type_coverage,
        "categoryBreakdown": category_breakdown,
        "drillTargets": drill_targets,
        "executiveFunctions": executive_summary,
        "endpointSecurity": endpoint_security,
        "summary": format!(
            "Phase 1 complete: {} NT syscalls ({} direct), {} structs, {} object types, {} drill targets. Executive: {} categories, {} EndpointSecurity events.",
            syscall_coverage.total,
            syscall_coverage.direct,
            struct_coverage.total,
            object_type_coverage.total,
            drill_targets.len(),
            executive_summary.len(),
            endpoint_security.len(),
        ),
        "nextActions": vec![
            "Phase 5A: EndpointSecurity bridge for process/thread/image-load callbacks",
            "Phase 5B: Thread notification via task_threads polling",
            "Phase 6: ObRegisterCallbacks equivalent via MACF/Wine handle callback",
        ],
    })
}

pub fn handle_syscall_lookup(body: &Map<String, Value>) -> Value {
    match body.get("nt_name").and_then(|v| v.as_str()) {
        Some(name) => nt_to_xnu::handle_syscall_lookup(name),
        None => json!({"ok": false, "error": "nt_name field required"}),
    }
}
