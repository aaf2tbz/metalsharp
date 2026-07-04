use serde_json::{json, Value};

const SCHEMA: &str = "metalsharp.doctor.registry.v1";

pub fn report() -> Value {
    let families = vec![
        family("wine_runtime", "/runtime/diagnostics", &["Missing Runtime", "Wrong Architecture", "Needs Repair"]),
        family("prefix", "/runtime/diagnostics", &["Stale Prefix", "Needs Repair", "Unsupported"]),
        family("dxmt_m12", "/runtime/diagnostics", &["Missing Runtime", "Needs Repair", "Wrong Architecture"]),
        family("dxvk", "/runtime/diagnostics", &["Missing Runtime", "Needs Repair", "Unsupported"]),
        family("vkd3d", "/runtime/diagnostics", &["Missing Runtime", "Needs Repair", "Unsupported"]),
        family("d3dmetal", "/runtime/diagnostics", &["Missing Runtime", "Unsupported", "Needs Repair"]),
        family("mono_fna", "/diagnostics/fna/platform", &["Missing Runtime", "Wrong Architecture", "Needs Repair"]),
        family("steam", "/source-adapters", &["Auth Expired", "Missing Runtime", "Blocked Anti-Cheat"]),
        family("gog", "/diagnostics/gog", &["Auth Expired", "Missing Runtime", "Needs Repair"]),
        family("launcher", "/launcher/evidence", &["Missing Redist", "Needs Repair", "Unsupported"]),
        family("migration", "/update/migrate/policy", &["Stale Prefix", "Needs Repair", "Unsupported"]),
        family("save_manager", "/save-manager/inventory", &["Needs Repair", "Unsupported"]),
    ];
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "states": ["Ready", "Needs Repair", "Missing Runtime", "Stale Prefix", "Wrong Architecture", "Auth Expired", "Missing Redist", "Blocked Anti-Cheat", "Unsupported"],
        "families": families,
        "invariants": [
            "Doctor registry is the cross-route schema catalog; individual doctor endpoints remain read-only unless explicitly documented otherwise.",
            "Failed play/launch surfaces should map to a family, state, explanation, and repair action before launching."
        ],
    })
}

fn family(id: &str, endpoint: &str, states: &[&str]) -> Value {
    json!({
        "id": id,
        "schema": "metalsharp.doctor.family.v1",
        "endpoint": endpoint,
        "states": states,
        "repairPolicy": "explain_first_explicit_action_required",
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn doctor_registry_covers_required_families() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let families = report.get("families").and_then(|value| value.as_array()).expect("families");
        for id in [
            "wine_runtime",
            "prefix",
            "dxmt_m12",
            "dxvk",
            "vkd3d",
            "d3dmetal",
            "mono_fna",
            "steam",
            "gog",
            "launcher",
            "migration",
        ] {
            assert!(
                families.iter().any(|family| family.get("id").and_then(|value| value.as_str()) == Some(id)),
                "missing {id}"
            );
        }
    }
}
