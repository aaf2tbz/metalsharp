use serde_json::{json, Value};

const SCHEMA: &str = "metalsharp.wine20.roadmap.audit.v1";

struct PhaseAudit {
    phase: u8,
    title: &'static str,
    status: &'static str,
    evidence: &'static [&'static str],
    remaining: &'static [&'static str],
}

fn phases() -> Vec<PhaseAudit> {
    vec![
        PhaseAudit {
            phase: 0,
            title: "Ground Truth and Naming Cleanup",
            status: "implemented",
            evidence: &[
                "GET /runtime/contracts canonicalM12Surface=dxmt_m12",
                "GET /runtime/manifest canonical installed path runtime/wine/lib/dxmt_m12",
                "GET /runtime/diagnostics contracts.canonicalM12Ok",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 1,
            title: "Runtime Contract Unification",
            status: "implemented_with_open_followups",
            evidence: &[
                "GET /runtime/contracts",
                "GET /mtsp/pipelines includes runtimeContractId",
                "GET /source-adapters maps source routes to runtimeContractIds",
            ],
            remaining: &["Generate/check more docs directly from backend contract table."],
        },
        PhaseAudit {
            phase: 2,
            title: "MetalSharp Wine Runtime Manifest",
            status: "implemented_with_open_followups",
            evidence: &["GET /runtime/manifest", "GET /runtime/diagnostics runtime.manifestOk"],
            remaining: &["Expose a command-line metalsharp-wine --metalsharp-runtime-info equivalent if still desired."],
        },
        PhaseAudit {
            phase: 3,
            title: "Prefix Manager V2",
            status: "implemented_with_open_followups",
            evidence: &[
                "GET /runtime/diagnostics prefixMetadata schema metalsharp.prefix.metadata.v2.preview",
                "<prefix>/.metalsharp/prefix-metadata-v2.json",
                "<prefix>/.metalsharp/receipts/wineboot-*.json",
                "prefix metadata installedComponents records route-DLL staging",
                "GET /update/migrate/policy",
            ],
            remaining: &["Broaden installedComponents persistence to additional source-owned prefix mutations as they become mutating paths."],
        },
        PhaseAudit {
            phase: 4,
            title: "Unified Launch Orchestrator",
            status: "implemented_with_open_followups",
            evidence: &[
                "POST /mtsp/prepare launch_receipt_preview",
                "GET /diagnostics/pipeline/dry-run launch_receipt_preview",
                "Steam/Sharp/native-Mono launch receipts under ~/.metalsharp/launch-receipts/",
                "GOG launch receipts under ~/.metalsharp/gog/receipts/",
                "GET /diagnostics/receipts",
            ],
            remaining: &["Complete one shared prepare endpoint for all sources; GOG and Sharp still have source-local prepare/launch surfaces."],
        },
        PhaseAudit {
            phase: 5,
            title: "Harden DXMT Lanes, Especially M12 / dxmt_m12",
            status: "implemented_with_open_followups",
            evidence: &[
                "GET /runtime/contracts m9/m10/m11/m12_dxmt_m12",
                "GET /runtime/diagnostics lane readiness",
                "GET /diagnostics/launch-validation m12_dxmt_m12",
                "metalsharp.prefix.route_dll_staging.receipt.v1 for M12 prefix route DLL staging",
            ],
            remaining: &["Controlled per-game launch proof remains pending until user approval."],
        },
        PhaseAudit {
            phase: 6,
            title: "Add DXVK Experimental Lanes",
            status: "implemented_with_open_followups",
            evidence: &["dxvk_d9 and dxvk_d11 contracts available", "tools/runtime/build-vulkan-lane-payloads.sh", "GET /runtime/diagnostics vulkan.dxvk", "GET /diagnostics/launch-validation dxvk_d9/dxvk_d11"],
            remaining: &["Controlled D3D9/D3D11 launch proof remains pending until user approval."],
        },
        PhaseAudit {
            phase: 7,
            title: "Add VKD3D-Proton Experimental Lane",
            status: "implemented_with_open_followups",
            evidence: &["vkd3d_d12 contract available", "tools/runtime/check-vulkan-lane-payloads.sh", "GET /runtime/diagnostics vulkan.vkd3d", "GET /diagnostics/launch-validation vkd3d_d12"],
            remaining: &["MoltenVK feature proof and controlled VKD3D launch proof remain pending."],
        },
        PhaseAudit {
            phase: 8,
            title: "Native Mono/FNA Platform",
            status: "implemented_with_open_followups",
            evidence: &["GET /diagnostics/fna/platform", "GET /runtime/diagnostics nativeMono", "FNA staging receipts", "native Mono launch receipts", "GET /diagnostics/launch-validation native_mono_*"],
            remaining: &["Promote additional FNA/XNA titles only after per-game receipts and proof notes."],
        },
        PhaseAudit {
            phase: 9,
            title: "Store Adapter Unification",
            status: "implemented_with_open_followups",
            evidence: &[
                "GET /source-adapters",
                "POST /source-adapters/prepare metalsharp.source.prepare.preview.v1",
                "GET /diagnostics/gog",
                "GOG dedicated prefix policy",
                "Steam/GOG/Sharp launch receipts",
            ],
            remaining: &["Actual GOG and Sharp launches still use source-specific endpoints; unified prepare is preview-only."],
        },
        PhaseAudit {
            phase: 10,
            title: "Launcher Profiles",
            status: "implemented_with_open_followups",
            evidence: &["GET /launcher/profiles", "POST /launcher/evidence", "docs/runtime/launcher-runtime.md"],
            remaining: &["Controlled direct launcher proof for Minecraft/EA/Ubisoft remains pending until user approval."],
        },
    ]
}

fn phase_json(phase: PhaseAudit) -> Value {
    json!({
        "phase": phase.phase,
        "title": phase.title,
        "status": phase.status,
        "evidence": phase.evidence,
        "remaining": phase.remaining,
    })
}

pub fn report() -> Value {
    let phases = phases();
    let complete = phases.iter().all(|phase| phase.remaining.is_empty() && phase.status == "implemented");
    let open_followups = phases.iter().filter(|phase| !phase.remaining.is_empty()).count();
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "scope": "roadmap phases 0-10",
        "complete": complete,
        "openFollowups": open_followups,
        "phases": phases.into_iter().map(phase_json).collect::<Vec<_>>(),
        "invariants": [
            "This audit is evidence mapping only; it does not launch games, run Wine, run GOGDL, repair prefixes, or replace installs.",
            "The active roadmap goal should not be marked complete while any phase has remaining followups.",
            "Controlled launch proof and install replacement require explicit user approval."
        ],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn audit_reports_phases_zero_through_ten_and_not_complete() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(report.get("complete").and_then(|value| value.as_bool()), Some(false));
        let phases = report.get("phases").and_then(|value| value.as_array()).expect("phases");
        assert_eq!(phases.len(), 11);
        assert!(phases.iter().any(|phase| phase.get("phase").and_then(|value| value.as_u64()) == Some(0)));
        assert!(phases.iter().any(|phase| phase.get("phase").and_then(|value| value.as_u64()) == Some(10)));
        assert!(report.get("openFollowups").and_then(|value| value.as_u64()).unwrap_or(0) > 0);
    }
}
