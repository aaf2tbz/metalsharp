use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};

const SCHEMA: &str = "metalsharp.launch.validation.matrix.v1";
const ENTRY_SCHEMA: &str = "metalsharp.launch.validation.entry.v1";

#[derive(Clone, Copy)]
enum ProofStatus {
    Proven,
    FilesystemValidated,
    PendingLaunchProof,
    PolicyBlocked,
}

impl ProofStatus {
    fn as_str(self) -> &'static str {
        match self {
            ProofStatus::Proven => "proven",
            ProofStatus::FilesystemValidated => "filesystem_validated",
            ProofStatus::PendingLaunchProof => "pending_launch_proof",
            ProofStatus::PolicyBlocked => "policy_blocked",
        }
    }
}

struct ValidationEntry {
    id: &'static str,
    source: &'static str,
    route: &'static str,
    runtime_contract_id: &'static str,
    required_evidence: &'static [&'static str],
    status: ProofStatus,
    evidence: Value,
    limitations: Vec<String>,
    next_actions: Vec<String>,
}

pub fn report() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    report_for_home(&home)
}

fn report_for_home(home: &Path) -> Value {
    let ms_home = crate::platform::metalsharp_home_dir_for(home);
    let entries = validation_entries(&ms_home);
    let proven = entries.iter().filter(|entry| matches!(entry.status, ProofStatus::Proven)).count();
    let pending = entries
        .iter()
        .filter(|entry| matches!(entry.status, ProofStatus::PendingLaunchProof | ProofStatus::PolicyBlocked))
        .count();
    json!({
        "ok": pending == 0,
        "schema": SCHEMA,
        "readOnly": true,
        "summary": {
            "total": entries.len(),
            "proven": proven,
            "pending": pending,
            "filesystemValidated": entries.iter().filter(|entry| matches!(entry.status, ProofStatus::FilesystemValidated)).count(),
            "policyBlocked": entries.iter().filter(|entry| matches!(entry.status, ProofStatus::PolicyBlocked)).count(),
        },
        "entries": entries.into_iter().map(entry_json).collect::<Vec<_>>(),
        "invariants": [
            "This matrix is filesystem-only and does not launch games, Wine, Steam, GOGDL, or wineboot.",
            "Filesystem readiness is not launch proof; a route is proven only when a matching receipt or explicit evidence record exists.",
            "VKD3D-Proton remains a fallback below M12/dxmt_m12 until MoltenVK/Vulkan feature proof exists.",
            "Install replacement remains guarded even if this matrix is green."
        ],
    })
}

fn validation_entries(ms_home: &Path) -> Vec<ValidationEntry> {
    vec![
        steam_route_entry(ms_home, "m12_dxmt_m12", "m12", "runtime/wine/lib/dxmt_m12", &[]),
        steam_route_entry(
            ms_home,
            "dxvk_d9",
            "dxvk_d9",
            "runtime/wine/lib/dxvk",
            &["Experimental Vulkan fallback; run a controlled D3D9 launch only after explicit approval."],
        ),
        steam_route_entry(
            ms_home,
            "dxvk_d11",
            "dxvk_d11",
            "runtime/wine/lib/dxvk",
            &["Experimental Vulkan fallback; run a controlled D3D11 launch only after explicit approval."],
        ),
        steam_route_entry(
            ms_home,
            "vkd3d_d12",
            "vkd3d_d12",
            "runtime/wine/lib/vkd3d",
            &[
                "Experimental Vulkan fallback; does not replace M12/dxmt_m12.",
                "Requires MoltenVK feature-level proof before per-game D3D12 validation.",
            ],
        ),
        gog_entry(ms_home),
        native_mono_entry(ms_home, "native_mono_arm64", "native_mono_arm64", "runtime/mono-arm64/bin/mono"),
        native_mono_entry(ms_home, "native_mono_x86", "native_mono_x86", "runtime/mono-x86/bin/mono"),
        launcher_profiles_entry(),
    ]
}

fn steam_route_entry(
    ms_home: &Path,
    id: &'static str,
    route: &'static str,
    runtime_rel: &'static str,
    extra_limitations: &[&str],
) -> ValidationEntry {
    let receipt = latest_steam_receipt_for_contract(ms_home, id);
    let runtime_path = ms_home.join(runtime_rel);
    let runtime_present = runtime_path.exists();
    let status = if receipt.is_some() {
        ProofStatus::Proven
    } else if runtime_present {
        ProofStatus::FilesystemValidated
    } else {
        ProofStatus::PendingLaunchProof
    };
    let mut limitations = vec!["Requires an actual launch receipt to be considered proven.".to_string()];
    limitations.extend(extra_limitations.iter().map(|item| (*item).to_string()));
    ValidationEntry {
        id,
        source: "steam",
        route,
        runtime_contract_id: id,
        required_evidence: &["runtime_contract", "runtime_payload", "mtsp_prepare_preview", "actual_launch_receipt"],
        status,
        evidence: json!({
            "runtimePath": runtime_path.to_string_lossy(),
            "runtimePresent": runtime_present,
            "receiptPath": receipt.as_ref().map(|(path, _)| path.to_string_lossy().to_string()),
            "receipt": receipt.map(|(_, value)| value),
        }),
        limitations,
        next_actions: if matches!(status, ProofStatus::Proven) {
            vec!["Keep receipt and launch logs with proof notes.".into()]
        } else {
            vec![format!("Run controlled {route} proof only after explicit user approval and keep launch receipt.")]
        },
    }
}

fn gog_entry(ms_home: &Path) -> ValidationEntry {
    let receipt = latest_json_in(&ms_home.join("gog").join("receipts"));
    let prefix = ms_home.join("bottles").join("gog-prefix").join("prefix");
    let status = if receipt.is_some() {
        ProofStatus::Proven
    } else if prefix.join("drive_c").is_dir() {
        ProofStatus::FilesystemValidated
    } else {
        ProofStatus::PendingLaunchProof
    };
    ValidationEntry {
        id: "gogdl_wine",
        source: "gog",
        route: "gogdl_wine",
        runtime_contract_id: "gogdl_wine",
        required_evidence: &["gog_doctor", "dedicated_gog_prefix", "gog_launch_receipt"],
        status,
        evidence: json!({
            "prefixPath": prefix.to_string_lossy(),
            "prefixInitialized": prefix.join("drive_c").is_dir(),
            "receiptPath": receipt.as_ref().map(|(path, _)| path.to_string_lossy().to_string()),
            "receipt": receipt.map(|(_, value)| value),
        }),
        limitations: vec!["GOG must remain on dedicated gog-prefix and must never use prefix-steam.".into()],
        next_actions: if matches!(status, ProofStatus::Proven) {
            vec!["Keep GOG receipt with source-specific proof notes.".into()]
        } else {
            vec!["Run GOG launch proof only after explicit user approval; do not use prefix-steam.".into()]
        },
    }
}

fn native_mono_entry(ms_home: &Path, id: &'static str, route: &'static str, mono_rel: &'static str) -> ValidationEntry {
    let mono = ms_home.join(mono_rel);
    let receipts = fna_staging_receipts(ms_home);
    let status = if !receipts.is_empty() && mono.is_file() {
        ProofStatus::Proven
    } else if mono.is_file() {
        ProofStatus::FilesystemValidated
    } else {
        ProofStatus::PendingLaunchProof
    };
    ValidationEntry {
        id,
        source: "steam_or_sharp",
        route,
        runtime_contract_id: id,
        required_evidence: &["mono_binary_arch", "fna_platform_doctor", "asset_staging_receipt", "launch_receipt"],
        status,
        evidence: json!({
            "monoPath": mono.to_string_lossy(),
            "monoPresent": mono.is_file(),
            "stagingReceipts": receipts,
        }),
        limitations: vec!["Native Mono/FNA applies only to known FNA/XNA targets or approved proof targets.".into()],
        next_actions: if matches!(status, ProofStatus::Proven) {
            vec!["Keep FNA staging receipt with per-game launch notes.".into()]
        } else {
            vec!["Stage native FNA assets and capture launch proof only for an approved target.".into()]
        },
    }
}

fn launcher_profiles_entry() -> ValidationEntry {
    ValidationEntry {
        id: "launcher_profiles",
        source: "sharp_library",
        route: "launcher_profiles",
        runtime_contract_id: "launcher_profiles",
        required_evidence: &["launcher_profile_contract", "launcher_evidence_report", "bottle_manifest"],
        status: ProofStatus::PendingLaunchProof,
        evidence: json!({
            "contractEndpoint": "/launcher/profiles",
            "evidenceEndpoint": "/launcher/evidence",
        }),
        limitations: vec!["Launcher profile contract is present, but individual launchers still need evidence reports and controlled proof.".into()],
        next_actions: vec!["Use /launcher/evidence for EA/Ubisoft/Minecraft proof bottles before any launch-status promotion.".into()],
    }
}

fn entry_json(entry: ValidationEntry) -> Value {
    json!({
        "schema": ENTRY_SCHEMA,
        "id": entry.id,
        "source": entry.source,
        "route": entry.route,
        "runtimeContractId": entry.runtime_contract_id,
        "status": entry.status.as_str(),
        "requiredEvidence": entry.required_evidence,
        "evidence": entry.evidence,
        "limitations": entry.limitations,
        "nextActions": entry.next_actions,
    })
}

fn latest_steam_receipt_for_contract(ms_home: &Path, contract_id: &str) -> Option<(PathBuf, Value)> {
    let dir = ms_home.join("launch-receipts").join("steam");
    let mut matches = json_files_in(&dir)
        .into_iter()
        .filter_map(|path| {
            let value = read_json(&path)?;
            (value.get("runtimeContractId").and_then(|value| value.as_str()) == Some(contract_id))
                .then_some((path, value))
        })
        .collect::<Vec<_>>();
    matches.sort_by_key(|(path, _)| std::cmp::Reverse(path.metadata().and_then(|meta| meta.modified()).ok()));
    matches.into_iter().next()
}

fn latest_json_in(dir: &Path) -> Option<(PathBuf, Value)> {
    let mut files = json_files_in(dir);
    files.sort_by_key(|path| std::cmp::Reverse(path.metadata().and_then(|meta| meta.modified()).ok()));
    files.into_iter().find_map(|path| read_json(&path).map(|value| (path, value)))
}

fn json_files_in(dir: &Path) -> Vec<PathBuf> {
    fs::read_dir(dir)
        .ok()
        .into_iter()
        .flat_map(|entries| entries.flatten())
        .map(|entry| entry.path())
        .filter(|path| path.extension().and_then(|ext| ext.to_str()) == Some("json"))
        .collect()
}

fn fna_staging_receipts(ms_home: &Path) -> Vec<String> {
    let mut receipts = Vec::new();
    for root in [ms_home.join("games"), ms_home.join("sharp-library")] {
        collect_fna_receipts(&root, 0, &mut receipts);
    }
    receipts.sort();
    receipts
}

fn collect_fna_receipts(root: &Path, depth: usize, receipts: &mut Vec<String>) {
    if depth > 4 || !root.is_dir() {
        return;
    }
    let receipt = root.join(".metalsharp").join("fna-staging.json");
    if receipt.is_file() {
        receipts.push(receipt.to_string_lossy().to_string());
    }
    if let Ok(entries) = fs::read_dir(root) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                collect_fna_receipts(&path, depth + 1, receipts);
            }
        }
    }
}

fn read_json(path: &Path) -> Option<Value> {
    fs::read_to_string(path).ok().and_then(|data| serde_json::from_str(&data).ok())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_home(name: &str) -> PathBuf {
        let home = std::env::temp_dir().join(format!("metalsharp-launch-validation-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&home);
        fs::create_dir_all(&home).expect("create home");
        home
    }

    #[test]
    fn matrix_promotes_matching_receipt_to_proven_without_launching() {
        let home = temp_home("receipt");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let receipts = ms_home.join("launch-receipts").join("steam");
        fs::create_dir_all(&receipts).expect("create receipts");
        fs::create_dir_all(ms_home.join("runtime/wine/lib/dxmt_m12")).expect("create runtime");
        fs::write(
            receipts.join("620-launch.json"),
            br#"{"schema":"metalsharp.launch.receipt.v1","runtimeContractId":"m12_dxmt_m12","preview":false,"pid":42}"#,
        )
        .expect("write receipt");

        let report = report_for_home(&home);
        let entries = report.get("entries").and_then(|value| value.as_array()).expect("entries");
        let m12 = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("m12_dxmt_m12"))
            .expect("m12 entry");
        assert_eq!(m12.get("status").and_then(|value| value.as_str()), Some("proven"));
        assert_eq!(m12.pointer("/evidence/receipt/preview").and_then(|value| value.as_bool()), Some(false));
        let _ = fs::remove_dir_all(&home);
    }

    #[test]
    fn matrix_marks_vulkan_runtime_without_receipt_as_filesystem_validated() {
        let home = temp_home("vulkan-fs");
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(ms_home.join("runtime/wine/lib/dxvk")).expect("create dxvk runtime");
        let report = report_for_home(&home);
        let entries = report.get("entries").and_then(|value| value.as_array()).expect("entries");
        let dxvk = entries
            .iter()
            .find(|entry| entry.get("id").and_then(|value| value.as_str()) == Some("dxvk_d11"))
            .expect("dxvk entry");
        assert_eq!(dxvk.get("status").and_then(|value| value.as_str()), Some("filesystem_validated"));
        let _ = fs::remove_dir_all(&home);
    }
}
