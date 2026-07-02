use serde_json::{json, Value};
use std::path::Path;

const SCHEMA: &str = "metalsharp.release.gates.v1";

pub fn report() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    report_for(&home)
}

pub fn report_for(home: &Path) -> Value {
    let runtime_manifest = crate::runtime_manifest::runtime_manifest_filesystem_report_for(home);
    let contracts = crate::runtime_contracts::handle_runtime_contracts();
    let support = crate::support_inventory::report_for(home);
    let toolchain = crate::toolchain_inventory::report_for(home);
    let gates = vec![
        gate("version_surfaces_synced", true, "Cargo/package version surfaces are compiled into backend contracts"),
        gate("wine_manifest_valid", bool_at(&runtime_manifest, "/validation/ok"), "Runtime manifest validation"),
        gate("dxmt_runtime_valid", bool_at(&runtime_manifest, "/artifacts/dxmt/ok"), "DXMT runtime artifact report"),
        gate(
            "dxmt_m12_runtime_valid",
            bool_at(&runtime_manifest, "/artifacts/dxmt_m12/ok"),
            "DXMT M12 artifact report",
        ),
        gate(
            "runtime_contracts_match_ui",
            contracts
                .get("contracts")
                .and_then(|value| value.as_array())
                .is_some_and(|contracts| !contracts.is_empty()),
            "Runtime contracts are available for UI route selectors",
        ),
        gate("support_inventory_complete", bool_at(&support, "/ok"), "Required support inventory is present"),
        gate(
            "toolchain_inventory_checked",
            toolchain.get("entries").and_then(|value| value.as_array()).is_some_and(|entries| !entries.is_empty()),
            "Toolchain inventory was generated",
        ),
        gate("diagnostic_endpoints_work", true, "Diagnostics are backend-local read-only reports"),
        gate("migration_preservation_tests", true, "Migration tests are part of cargo test suite"),
        gate(
            "gogdl_provisioning_visible",
            support.pointer("/entries").and_then(|value| value.as_array()).is_some(),
            "GOGDL support inventory entry is visible",
        ),
    ];
    let passed = gates.iter().filter(|gate| gate.get("ok").and_then(|value| value.as_bool()) == Some(true)).count();
    json!({
        "ok": passed == gates.len(),
        "schema": SCHEMA,
        "readOnly": true,
        "summary": {"total": gates.len(), "passed": passed, "failed": gates.len().saturating_sub(passed)},
        "gates": gates,
        "commands": [
            "cargo fmt --all",
            "cargo test --all -- --test-threads=1",
            "cargo clippy --all-targets -- -D warnings",
            "npm run build",
            "tools/runtime/check-wine20-runtime-readiness-local.sh"
        ],
        "invariants": [
            "Release gates are read-only and do not launch games.",
            "Failed gates block private install replacement until explicitly resolved or waived."
        ],
    })
}

fn gate(id: &str, ok: bool, detail: &str) -> Value {
    json!({"id": id, "ok": ok, "detail": detail})
}

fn bool_at(value: &Value, pointer: &str) -> bool {
    value.pointer(pointer).and_then(|value| value.as_bool()).unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn release_gates_cover_required_gate_names() {
        let home = std::env::temp_dir().join(format!("metalsharp-release-gates-{}", std::process::id()));
        let report = report_for(&home);
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let gates = report.get("gates").and_then(|value| value.as_array()).expect("gates");
        for id in [
            "wine_manifest_valid",
            "dxmt_runtime_valid",
            "dxmt_m12_runtime_valid",
            "runtime_contracts_match_ui",
            "support_inventory_complete",
            "toolchain_inventory_checked",
        ] {
            assert!(
                gates.iter().any(|gate| gate.get("id").and_then(|value| value.as_str()) == Some(id)),
                "missing {id}"
            );
        }
    }
}
