use serde_json::{json, Map, Value};

const SCHEMA: &str = "metalsharp.source.launch.dispatch.v1";

pub fn handle_launch(body: &Map<String, Value>) -> Value {
    let Some(source) = body.get("source").and_then(|value| value.as_str()).map(|value| value.to_ascii_lowercase())
    else {
        return json!({"ok": false, "schema": SCHEMA, "error": "source is required"});
    };
    let confirmed = body.get("confirmed").and_then(|value| value.as_bool()).unwrap_or(false);
    if !confirmed {
        return confirmation_required(&source, body);
    }

    let result = match source.as_str() {
        "steam" => launch_steam(body),
        "gog" => crate::gog::handle_play(&Value::Object(body.clone())),
        "sharp" | "sharp_library" => crate::sharp_library::handle_launch(body),
        _ => json!({"ok": false, "error": format!("unsupported source {}", source)}),
    };
    wrap_result(&source, result)
}

fn confirmation_required(source: &str, body: &Map<String, Value>) -> Value {
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "status": "confirmation_required",
        "source": source,
        "launches": false,
        "mutates": false,
        "wouldLaunch": launch_target_summary(source, body),
        "required": {"confirmed": true},
        "next": "Resubmit this request with confirmed=true only after explicit user launch action.",
    })
}

fn wrap_result(source: &str, result: Value) -> Value {
    json!({
        "ok": result.get("ok").and_then(|value| value.as_bool()).unwrap_or(false),
        "schema": SCHEMA,
        "readOnly": false,
        "status": "dispatched",
        "source": source,
        "launches": true,
        "mutates": true,
        "result": result,
    })
}

fn launch_target_summary(source: &str, body: &Map<String, Value>) -> Value {
    match source {
        "steam" => json!({
            "endpoint": "/source-adapters/launch",
            "delegatesTo": "mtsp::launcher::launch_with_pipeline",
            "appId": body.get("appId").or_else(|| body.get("appid")).cloned().unwrap_or(Value::Null),
            "route": body.get("route").or_else(|| body.get("pipeline")).or_else(|| body.get("launchMethod")).cloned().unwrap_or(Value::Null),
        }),
        "gog" => json!({
            "endpoint": "/source-adapters/launch",
            "delegatesTo": "/sharp-library/gog/play",
            "productId": body.get("productId").or_else(|| body.get("appId")).or_else(|| body.get("id")).cloned().unwrap_or(Value::Null),
        }),
        "sharp" | "sharp_library" => json!({
            "endpoint": "/source-adapters/launch",
            "delegatesTo": "/sharp-library/launch",
            "id": body.get("id").or_else(|| body.get("appId")).cloned().unwrap_or(Value::Null),
            "engine": body.get("engine").or_else(|| body.get("route")).cloned().unwrap_or(Value::Null),
        }),
        _ => json!({"endpoint": "/source-adapters/launch"}),
    }
}

fn launch_steam(body: &Map<String, Value>) -> Value {
    let Some(appid) = body.get("appId").or_else(|| body.get("appid")).and_then(|value| value.as_u64()) else {
        return json!({"ok": false, "error": "appId is required for steam source"});
    };
    let route = body
        .get("route")
        .or_else(|| body.get("pipeline"))
        .or_else(|| body.get("launchMethod"))
        .and_then(|value| value.as_str());
    let pipeline = crate::bottles::resolve_steam_pipeline_for_request(
        appid as u32,
        route.and_then(crate::mtsp::engine::PipelineId::from_str_flexible),
    );
    match crate::mtsp::launcher::launch_with_pipeline(appid as u32, pipeline) {
        Ok((pid, game_type)) => json!({
            "ok": true,
            "pid": pid,
            "gameType": game_type,
            "appid": appid,
            "pipeline": pipeline,
        }),
        Err(error) => json!({"ok": false, "error": error.to_string()}),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn source_launch_requires_confirmation_without_launching() {
        let body = serde_json::from_value::<Map<String, Value>>(json!({
            "source": "gog",
            "productId": "1876546888"
        }))
        .unwrap();
        let response = handle_launch(&body);
        assert_eq!(response.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(response.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(response.get("launches").and_then(|value| value.as_bool()), Some(false));
        assert_eq!(response.get("status").and_then(|value| value.as_str()), Some("confirmation_required"));
        assert_eq!(response.pointer("/required/confirmed").and_then(|value| value.as_bool()), Some(true));
    }

    #[test]
    fn source_launch_preview_covers_all_sources() {
        for source in ["steam", "gog", "sharp"] {
            let body = serde_json::from_value::<Map<String, Value>>(json!({
                "source": source,
                "appId": 620,
                "id": "demo",
                "route": "m12"
            }))
            .unwrap();
            let response = handle_launch(&body);
            assert_eq!(response.get("status").and_then(|value| value.as_str()), Some("confirmation_required"));
            assert!(response.get("wouldLaunch").and_then(|value| value.as_object()).is_some());
        }
    }
}
