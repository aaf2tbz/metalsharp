use serde_json::{json, Value};

const SCHEMA: &str = "metalsharp.source.adapters.v1";

fn status_string(value: &Value) -> String {
    value.get("status").and_then(|value| value.as_str()).unwrap_or("unknown").to_string()
}

fn bool_field(value: &Value, key: &str) -> bool {
    value.get(key).and_then(|value| value.as_bool()).unwrap_or(false)
}

fn string_field(value: &Value, key: &str) -> Option<String> {
    value.get(key).and_then(|value| value.as_str()).map(str::to_string)
}

fn steam_adapter() -> Value {
    let status = crate::steam::status();
    let windows_ready = bool_field(&status, "installed");
    let mac_ready = bool_field(&status, "mac_installed");
    json!({
        "id": "steam",
        "schema": "metalsharp.source.adapter.v1",
        "displayName": "Steam",
        "kind": "storefront",
        "libraryEndpoint": "/games",
        "statusEndpoint": "/steam/status",
        "launchEndpoint": "/source-adapters/launch",
        "delegateLaunchEndpoint": "/steam/launch-game",
        "prepareEndpoint": "/source-adapters/prepare",
        "runtimeContractIds": ["dxmt_m12", "dxvk_d9", "dxvk_d11", "vkd3d_d12"],
        "prefixPolicy": {
            "id": "steam",
            "type": "shared_storefront_prefix",
            "path": crate::platform::metalsharp_home_dir().join("prefix-steam").to_string_lossy(),
            "mustNotAlias": ["gog"]
        },
        "ready": windows_ready || mac_ready,
        "status": if windows_ready { "ready" } else if mac_ready { "native_steam_ready" } else { "not_installed" },
        "installed": windows_ready,
        "running": bool_field(&status, "running"),
        "details": status,
        "capabilities": {
            "scan": true,
            "install": true,
            "prepare": true,
            "launch": true,
            "stop": true,
            "receipts": "preview_and_runtime"
        },
        "limitations": []
    })
}

fn gog_adapter() -> Value {
    let status = crate::gog::handle_status();
    let gog_status = status.get("status").cloned().unwrap_or_else(|| json!({}));
    json!({
        "id": "gog",
        "schema": "metalsharp.source.adapter.v1",
        "displayName": "GOG",
        "kind": "storefront",
        "libraryEndpoint": "/sharp-library/gog/games",
        "statusEndpoint": "/sharp-library/gog/status",
        "launchEndpoint": "/source-adapters/launch",
        "delegateLaunchEndpoint": "/sharp-library/gog/play",
        "prepareEndpoint": "/source-adapters/prepare",
        "runtimeContractIds": ["gogdl_wine"],
        "prefixPolicy": {
            "id": "gog",
            "type": "dedicated_storefront_prefix",
            "path": string_field(&gog_status, "winePrefix"),
            "mustNotAlias": ["steam"]
        },
        "ready": bool_field(&gog_status, "ready"),
        "status": status_string(&gog_status),
        "installed": bool_field(&gog_status, "gogdlAvailable"),
        "running": false,
        "details": gog_status,
        "capabilities": {
            "scan": true,
            "install": true,
            "prepare": true,
            "launch": true,
            "stop": true,
            "receipts": "runtime"
        },
        "limitations": ["Unified launch dispatch requires explicit confirmed=true and delegates to the dedicated gogdl endpoint"]
    })
}

fn sharp_app_count() -> usize {
    let path = crate::platform::metalsharp_home_dir().join("sharp-library").join("library.json");
    std::fs::read_to_string(path)
        .ok()
        .and_then(|data| serde_json::from_str::<Value>(&data).ok())
        .and_then(|value| value.as_array().map(|apps| apps.len()))
        .unwrap_or(0)
}

fn sharp_adapter() -> Value {
    let installed = sharp_app_count();
    json!({
        "id": "sharp",
        "schema": "metalsharp.source.adapter.v1",
        "displayName": "Sharp Library",
        "kind": "local_library",
        "libraryEndpoint": "/sharp-library",
        "statusEndpoint": "/sharp-library",
        "launchEndpoint": "/source-adapters/launch",
        "delegateLaunchEndpoint": "/sharp-library/launch",
        "prepareEndpoint": "/source-adapters/prepare",
        "runtimeContractIds": ["dxmt_m12", "dxvk_d9", "dxvk_d11", "vkd3d_d12", "native_mono_arm64", "native_mono_x86"],
        "prefixPolicy": {
            "id": "sharp",
            "type": "per_app_or_imported_bottle",
            "path": crate::platform::metalsharp_home_dir().join("bottles").to_string_lossy(),
            "mustNotAlias": []
        },
        "ready": true,
        "status": "ready",
        "installed": installed,
        "running": null,
        "details": {
            "appCount": installed
        },
        "capabilities": {
            "scan": true,
            "install": true,
            "prepare": true,
            "launch": true,
            "stop": true,
            "receipts": "runtime"
        },
        "limitations": ["Unified launch dispatch requires explicit confirmed=true and delegates to the Sharp Library launch endpoint"]
    })
}

pub fn report() -> Value {
    json!({
        "ok": true,
        "schema": SCHEMA,
        "adapters": [steam_adapter(), gog_adapter(), sharp_adapter()],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn report_lists_canonical_source_adapters() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let adapters = report.get("adapters").and_then(|value| value.as_array()).unwrap();
        let ids: Vec<&str> = adapters.iter().filter_map(|adapter| adapter.get("id")?.as_str()).collect();
        assert!(ids.contains(&"steam"));
        assert!(ids.contains(&"gog"));
        assert!(ids.contains(&"sharp"));
        for adapter in adapters {
            assert_eq!(adapter.get("schema").and_then(|value| value.as_str()), Some("metalsharp.source.adapter.v1"));
            assert!(adapter.get("runtimeContractIds").and_then(|value| value.as_array()).is_some());
            assert!(adapter.get("prefixPolicy").and_then(|value| value.as_object()).is_some());
        }
    }
}
