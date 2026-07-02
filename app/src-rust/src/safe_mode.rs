use serde_json::{json, Map, Value};

const SCHEMA: &str = "metalsharp.safe-mode.profile.v1";

pub fn report() -> Value {
    profile_for(None)
}

pub fn preview(body: &Map<String, Value>) -> Value {
    let source = body.get("source").and_then(|value| value.as_str());
    let mut profile = profile_for(source);
    if let Some(obj) = profile.as_object_mut() {
        obj.insert("target".into(), Value::Object(body.clone()));
    }
    profile
}

fn profile_for(source: Option<&str>) -> Value {
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "source": source.unwrap_or("any"),
        "disables": ["overlays", "MetalFX", "async_pipeline_compile", "nonessential_route_dlls", "launcher_gpu_acceleration"],
        "enables": ["verbose_logs", "wined3d_or_plain_wine_fallback", "cef_software_path", "minimal_env", "crash_classification"],
        "env": {
            "WINEDEBUG": "+timestamp,+pid,+tid,+seh,+loaddll",
            "DXVK_ASYNC": "0",
            "METALSHARP_SAFE_MODE": "1",
            "METALSHARP_GRAPHICS_RUNTIME_LOGS": "1"
        },
        "routePreference": ["wine_bare", "m11", "dxvk_d11", "m12_dxmt_m12"],
        "launches": false,
        "next": "Apply only after explicit user safe-mode launch action.",
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn safe_mode_profile_disables_risky_features() {
        let report = report();
        assert_eq!(report.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        let disables = report.get("disables").and_then(|value| value.as_array()).expect("disables");
        assert!(disables.iter().any(|value| value.as_str() == Some("overlays")));
        assert!(disables.iter().any(|value| value.as_str() == Some("async_pipeline_compile")));
        assert_eq!(report.get("launches").and_then(|value| value.as_bool()), Some(false));
    }
}
