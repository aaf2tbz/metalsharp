use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

const SCHEMA: &str = "metalsharp.known-good.inventory.v1";
const SNAPSHOT_SCHEMA: &str = "metalsharp.known-good.v1";

pub fn inventory() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    inventory_for(&home)
}

pub fn inventory_for(home: &Path) -> Value {
    let root = snapshots_dir_for(home);
    let snapshots = read_snapshots(&root);
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "root": root.to_string_lossy(),
        "count": snapshots.len(),
        "snapshots": snapshots,
        "invariants": [
            "Known-good inventory reads persisted snapshots only and does not launch games.",
            "Snapshots are written from receipts or explicit user confirmation after a known working state."
        ],
    })
}

pub fn handle_record(body: &serde_json::Map<String, Value>) -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    match record_for(&home, body) {
        Ok(path) => json!({"ok": true, "schema": SNAPSHOT_SCHEMA, "path": path.to_string_lossy()}),
        Err(error) => json!({"ok": false, "error": error}),
    }
}

pub fn record_for(home: &Path, body: &serde_json::Map<String, Value>) -> Result<PathBuf, String> {
    let app_id = body
        .get("appId")
        .or_else(|| body.get("appid"))
        .and_then(value_to_string)
        .ok_or_else(|| "appId is required".to_string())?;
    let source = body.get("source").and_then(|value| value.as_str()).unwrap_or("unknown");
    let route = body
        .get("route")
        .or_else(|| body.get("runtimeContractId"))
        .and_then(|value| value.as_str())
        .unwrap_or("unknown");
    let game = body.get("game").or_else(|| body.get("title")).and_then(|value| value.as_str()).unwrap_or(&app_id);
    let receipt_path = body.get("receiptPath").and_then(|value| value.as_str());
    let runtime_manifest = crate::runtime_manifest::runtime_manifest_filesystem_report_for(home);
    let snapshot = json!({
        "schema": SNAPSHOT_SCHEMA,
        "game": game,
        "source": source,
        "appId": app_id,
        "route": route,
        "runtimeContractId": body.get("runtimeContractId").and_then(|value| value.as_str()).unwrap_or(route),
        "wineVersion": runtime_manifest.pointer("/expected/wine_version").cloned().unwrap_or(Value::Null),
        "runtimeSurface": runtime_surface_for_route(route),
        "runtimeManifestPath": runtime_manifest.get("manifestPath").cloned().unwrap_or(Value::Null),
        "receiptPath": receipt_path,
        "metalsharpVersion": env!("CARGO_PKG_VERSION"),
        "lastKnownGood": true,
        "createdAt": now_secs(),
    });
    let dir = snapshots_dir_for(home).join(source);
    fs::create_dir_all(&dir).map_err(|error| format!("create {}: {}", dir.display(), error))?;
    let path = dir.join(format!("{}-{}-known-good.json", sanitize(&app_id), sanitize(route)));
    fs::write(&path, serde_json::to_vec_pretty(&snapshot).map_err(|error| error.to_string())?)
        .map_err(|error| format!("write {}: {}", path.display(), error))?;
    Ok(path)
}

fn read_snapshots(root: &Path) -> Vec<Value> {
    let mut out = Vec::new();
    let Ok(entries) = fs::read_dir(root) else { return out };
    for entry in entries.flatten() {
        if entry.file_type().map(|kind| kind.is_dir()).unwrap_or(false) {
            let Ok(files) = fs::read_dir(entry.path()) else { continue };
            for file in files.flatten() {
                if file.path().extension().and_then(|ext| ext.to_str()) == Some("json") {
                    if let Ok(data) = fs::read_to_string(file.path()) {
                        if let Ok(value) = serde_json::from_str::<Value>(&data) {
                            out.push(value);
                        }
                    }
                }
            }
        }
    }
    out.sort_by_key(|value| std::cmp::Reverse(value.get("createdAt").and_then(|v| v.as_u64()).unwrap_or(0)));
    out
}

fn snapshots_dir_for(home: &Path) -> PathBuf {
    crate::platform::metalsharp_home_dir_for(&home.to_path_buf()).join("known-good")
}

fn runtime_surface_for_route(route: &str) -> &'static str {
    match route {
        "m12" | "m12_dxmt_m12" => "dxmt_m12",
        "dxvk_d9" | "dxvk_d11" => "dxvk",
        "vkd3d_d12" => "vkd3d",
        "native_mono_arm64" => "mono_arm64",
        "native_mono_x86" => "mono_x86",
        "d3dmetal_gptk" => "d3dmetal",
        _ => "wine",
    }
}

fn value_to_string(value: &Value) -> Option<String> {
    value.as_str().map(str::to_string).or_else(|| value.as_u64().map(|value| value.to_string()))
}

fn sanitize(value: &str) -> String {
    value.chars().map(|ch| if ch.is_ascii_alphanumeric() { ch } else { '-' }).collect()
}

fn now_secs() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|duration| duration.as_secs()).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn records_known_good_snapshot_without_launching() {
        let home = std::env::temp_dir().join(format!("metalsharp-known-good-{}", std::process::id()));
        let body = serde_json::from_value::<serde_json::Map<String, Value>>(json!({
            "source": "steam",
            "appId": 620,
            "route": "m12_dxmt_m12",
            "game": "Portal 2"
        }))
        .unwrap();
        let path = record_for(&home, &body).expect("record snapshot");
        assert!(path.is_file());
        let inventory = inventory_for(&home);
        assert_eq!(inventory.get("count").and_then(|value| value.as_u64()), Some(1));
        assert_eq!(inventory.pointer("/snapshots/0/runtimeSurface").and_then(|value| value.as_str()), Some("dxmt_m12"));
        let _ = fs::remove_dir_all(home);
    }
}
