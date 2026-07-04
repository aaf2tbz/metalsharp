use serde_json::{json, Value};
use std::path::{Path, PathBuf};

const SCHEMA: &str = "metalsharp.support.inventory.v1";

pub fn report() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    report_for(&home)
}

pub fn report_for(home: &Path) -> Value {
    let ms_home = crate::platform::metalsharp_home_dir_for(&home.to_path_buf());
    let runtime = ms_home.join("runtime");
    let wine = runtime.join("wine");
    let entries = vec![
        entry("wine_binary", "Wine runtime", wine.join("bin/metalsharp-wine"), true),
        entry("runtime_manifest", "Runtime manifest", crate::runtime_manifest::manifest_path_for(home), true),
        entry(
            "runtime_info_helper",
            "Runtime info helper",
            crate::runtime_manifest::runtime_info_helper_path_for(home),
            true,
        ),
        entry("dxmt_m12_surface", "DXMT M12 surface", wine.join("lib/dxmt_m12"), true),
        entry("dxvk_surface", "DXVK surface", wine.join("lib/dxvk"), false),
        entry("vkd3d_surface", "VKD3D-Proton surface", wine.join("lib/vkd3d"), false),
        entry("moltenvk_icd", "MoltenVK ICD", runtime.join("vulkan/icd.d"), false),
        entry("mono_arm64", "Native Mono ARM64", runtime.join("mono-arm64/bin/mono"), false),
        entry("mono_x86", "Native Mono x86_64", runtime.join("mono-x86/bin/mono"), false),
        entry("gogdl", "GOGDL tool", ms_home.join("tools/gogdl"), false),
        entry("redist_root", "Redistributable root", runtime.join("redist"), false),
        entry("fna_support", "FNA support assets", runtime.join("fna"), false),
        entry("goldberg_x64", "Goldberg x64 Steam API", runtime.join("goldberg/x64/steam_api64.dll"), false),
        entry(
            "launcher_cef_wrapper",
            "CEF launcher wrapper",
            wine.join("lib/metalsharp/x86_64-windows/metalsharp-cefchildhook.dll"),
            false,
        ),
        entry("steam_launch_receipts", "Steam launch receipts", ms_home.join("launch-receipts/steam"), false),
        entry("gog_launch_receipts", "GOG launch receipts", ms_home.join("gog/receipts"), false),
    ];
    let required_total =
        entries.iter().filter(|entry| entry.get("required").and_then(|v| v.as_bool()) == Some(true)).count();
    let required_present = entries
        .iter()
        .filter(|entry| {
            entry.get("required").and_then(|v| v.as_bool()) == Some(true)
                && entry.get("present").and_then(|v| v.as_bool()) == Some(true)
        })
        .count();
    json!({
        "ok": required_present == required_total,
        "schema": SCHEMA,
        "readOnly": true,
        "metalsharpHome": ms_home.to_string_lossy(),
        "summary": {
            "total": entries.len(),
            "requiredTotal": required_total,
            "requiredPresent": required_present,
            "optionalPresent": entries.iter().filter(|entry| entry.get("required").and_then(|v| v.as_bool()) != Some(true) && entry.get("present").and_then(|v| v.as_bool()) == Some(true)).count(),
        },
        "entries": entries,
        "invariants": [
            "Support inventory is filesystem-only and does not launch Wine, games, launchers, or repair tools.",
            "Optional fallback surfaces can be absent unless their runtime contract is selected or shipped."
        ],
    })
}

fn entry(id: &str, label: &str, path: PathBuf, required: bool) -> Value {
    let exists = path.exists();
    json!({
        "id": id,
        "label": label,
        "path": path.to_string_lossy(),
        "present": exists,
        "required": required,
        "kind": if path.extension().is_some() { "file" } else { "path" },
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn support_inventory_reports_required_runtime_helpers() {
        let home = std::env::temp_dir().join(format!("metalsharp-support-inventory-{}", std::process::id()));
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(ms_home.join("runtime/wine/bin")).expect("create bin");
        fs::create_dir_all(ms_home.join("runtime/wine/lib/dxmt_m12")).expect("create m12");
        fs::write(ms_home.join("runtime/wine/bin/metalsharp-wine"), b"wine").expect("write wine");
        fs::write(ms_home.join("runtime/wine/bin/metalsharp-runtime-info"), b"info").expect("write helper");
        fs::write(ms_home.join("runtime/metalsharp-runtime-manifest.json"), b"{}").expect("write manifest");
        let report = report_for(&home);
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(report.get("ok").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(report.pointer("/summary/requiredPresent").and_then(|value| value.as_u64()), Some(4));
        let _ = fs::remove_dir_all(home);
    }
}
