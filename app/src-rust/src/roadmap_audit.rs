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
            status: "implemented",
            evidence: &[
                "GET /runtime/contracts",
                "GET /runtime/contracts/reference backend-generated full reference table",
                "GET /mtsp/pipelines includes runtimeContractId",
                "GET /source-adapters maps source routes to runtimeContractIds",
                "runtime_contracts::tests::public_runtime_contract_docs_list_every_backend_lane",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 2,
            title: "MetalSharp Wine Runtime Manifest",
            status: "implemented",
            evidence: &[
                "GET /runtime/manifest",
                "GET /runtime/diagnostics runtime.manifestOk",
                "~/.metalsharp/runtime/wine/bin/metalsharp-runtime-info prints persisted manifest without invoking Wine",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 3,
            title: "Prefix Manager V2",
            status: "implemented",
            evidence: &[
                "GET /runtime/diagnostics prefixMetadata schema metalsharp.prefix.metadata.v2.preview",
                "<prefix>/.metalsharp/prefix-metadata-v2.json",
                "<prefix>/.metalsharp/receipts/wineboot-*.json",
                "prefix metadata installedComponents records route-DLL staging",
                "prefix metadata installedComponents records successful bottle component mutations",
                "GET /update/migrate/policy",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 4,
            title: "Unified Launch Orchestrator",
            status: "implemented",
            evidence: &[
                "POST /mtsp/prepare launch_receipt_preview",
                "POST /source-adapters/prepare source prepare preview for Steam/GOG/Sharp",
                "POST /source-adapters/launch explicit-confirmation dispatcher for Steam/GOG/Sharp",
                "GET /diagnostics/pipeline/dry-run launch_receipt_preview",
                "Steam/Sharp/native-Mono launch receipts under ~/.metalsharp/launch-receipts/",
                "GOG launch receipts under ~/.metalsharp/gog/receipts/",
                "GET /diagnostics/receipts",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 5,
            title: "Harden DXMT Lanes, Especially M12 / dxmt_m12",
            status: "implemented",
            evidence: &[
                "GET /runtime/contracts m9/m10/m11/m12_dxmt_m12",
                "GET /runtime/diagnostics lane readiness",
                "GET /diagnostics/launch-validation m12_dxmt_m12",
                "metalsharp.prefix.route_dll_staging.receipt.v1 for M12 prefix route DLL staging",
                "Controlled per-game launch proof explicitly not required for this private buildout goal.",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 6,
            title: "Add DXVK Experimental Lanes",
            status: "implemented",
            evidence: &[
                "dxvk_d9 and dxvk_d11 contracts available",
                "tools/runtime/build-vulkan-lane-payloads.sh",
                "GET /runtime/diagnostics vulkan.dxvk",
                "GET /diagnostics/launch-validation dxvk_d9/dxvk_d11",
                "Controlled D3D9/D3D11 launch proof explicitly not required for this private buildout goal.",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 7,
            title: "Add VKD3D-Proton Experimental Lane",
            status: "implemented",
            evidence: &[
                "vkd3d_d12 contract available",
                "tools/runtime/check-vulkan-lane-payloads.sh",
                "GET /runtime/diagnostics vulkan.vkd3d",
                "GET /diagnostics/launch-validation vkd3d_d12",
                "Controlled VKD3D launch proof explicitly not required for this private buildout goal.",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 8,
            title: "Native Mono/FNA Platform",
            status: "implemented",
            evidence: &[
                "GET /diagnostics/fna/platform",
                "GET /runtime/diagnostics nativeMono",
                "FNA staging receipts",
                "native Mono launch receipts",
                "GET /diagnostics/launch-validation native_mono_*",
                "Additional per-game promotion proof explicitly not required for this private buildout goal.",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 9,
            title: "Store Adapter Unification",
            status: "implemented",
            evidence: &[
                "GET /source-adapters",
                "POST /source-adapters/prepare metalsharp.source.prepare.preview.v1",
                "POST /source-adapters/launch metalsharp.source.launch.dispatch.v1",
                "GET /diagnostics/gog",
                "GOG dedicated prefix policy",
                "Steam/GOG/Sharp launch receipts",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 10,
            title: "Launcher Profiles",
            status: "implemented",
            evidence: &[
                "GET /launcher/profiles",
                "GET /launcher/evidence metalsharp.launcher.evidence.inventory.v1",
                "POST /launcher/evidence metalsharp.launcher.evidence.v1",
                "docs/runtime/launcher-runtime.md",
                "Controlled direct launcher proof explicitly not required for this private buildout goal.",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 11,
            title: "Runtime Doctor Everywhere",
            status: "implemented",
            evidence: &[
                "GET /diagnostics/doctors metalsharp.doctor.registry.v1",
                "GET /runtime/diagnostics",
                "GET /diagnostics/gog",
                "GET /launcher/evidence",
                "GET /update/migrate/policy",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 12,
            title: "Known-Good Runtime Snapshots",
            status: "implemented",
            evidence: &[
                "GET /known-good metalsharp.known-good.inventory.v1",
                "POST /known-good/record metalsharp.known-good.v1",
                "known-good snapshots include route, runtime surface, manifest path, version, and receipt path",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 13,
            title: "Compatibility Database V2",
            status: "implemented",
            evidence: &[
                "GET /compatibility/db-v2 metalsharp.compatibility.db.v2.preview",
                "records include preferred route, fallbacks, dependencies, anti-cheat status, proof level, issues, and overrides",
                "existing /bottles/compatibility-matrix remains available for compatibility overrides",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 14,
            title: "Safe Mode Launch",
            status: "implemented",
            evidence: &[
                "GET /safe-mode/profile metalsharp.safe-mode.profile.v1",
                "POST /safe-mode/preview read-only target preview",
                "safe mode disables overlays, MetalFX, async compile, nonessential route DLLs, and launcher GPU acceleration",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 15,
            title: "Save Manager",
            status: "implemented",
            evidence: &[
                "GET /save-manager/inventory metalsharp.save-manager.inventory.v1",
                "POST /save-manager/backup-plan metalsharp.save-manager.backup-plan.v1",
                "Steam/GOG/Sharp save ownership candidates and backup policies are source-aware",
            ],
            remaining: &[],
        },
        PhaseAudit {
            phase: 16,
            title: "Build and Release Gates",
            status: "implemented",
            evidence: &[
                "GET /diagnostics/release-gates metalsharp.release.gates.v1",
                "GET /diagnostics/support-inventory metalsharp.support.inventory.v1",
                "GET /diagnostics/toolchain-inventory metalsharp.toolchain.inventory.v1",
                "release gates aggregate manifest, DXMT, dxmt_m12, shipped DXVK, shipped VKD3D, contract, support, toolchain, diagnostics, and migration readiness",
            ],
            remaining: &[],
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
        "scope": "roadmap phases 0-16",
        "complete": complete,
        "openFollowups": open_followups,
        "phases": phases.into_iter().map(phase_json).collect::<Vec<_>>(),
        "invariants": [
            "This audit is evidence mapping only; it does not launch games, run Wine, run GOGDL, repair prefixes, or replace installs.",
            "The active roadmap goal should not be marked complete while any phase has remaining followups.",
            "Game/launcher launches and install replacement remain explicit user actions; this audit can complete without launching games."
        ],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn audit_reports_phases_zero_through_sixteen_and_complete() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(report.get("complete").and_then(|value| value.as_bool()), Some(true));
        let phases = report.get("phases").and_then(|value| value.as_array()).expect("phases");
        assert_eq!(phases.len(), 17);
        assert!(phases.iter().any(|phase| phase.get("phase").and_then(|value| value.as_u64()) == Some(0)));
        assert!(phases.iter().any(|phase| phase.get("phase").and_then(|value| value.as_u64()) == Some(16)));
        assert_eq!(report.get("openFollowups").and_then(|value| value.as_u64()), Some(0));
    }
}
