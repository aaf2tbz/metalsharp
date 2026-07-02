use serde_json::{json, Map, Value};
use std::fs;
use std::path::{Path, PathBuf};

const SCHEMA: &str = "metalsharp.source.prepare.preview.v1";

pub fn handle_prepare(body: &Map<String, Value>) -> Value {
    let Some(source) = body.get("source").and_then(|value| value.as_str()).map(|value| value.to_ascii_lowercase())
    else {
        return json!({"ok": false, "error": "source is required"});
    };
    let route =
        body.get("route").or_else(|| body.get("launchMethod")).and_then(|value| value.as_str()).unwrap_or("m12");
    match source.as_str() {
        "steam" => prepare_steam(body, route),
        "gog" => prepare_gog(body),
        "sharp" | "sharp_library" => prepare_sharp(body, route),
        _ => json!({"ok": false, "error": format!("unsupported source {}", source)}),
    }
}

fn prepare_steam(body: &Map<String, Value>, route: &str) -> Value {
    let Some(appid) = body.get("appId").or_else(|| body.get("appid")).and_then(|value| value.as_u64()) else {
        return json!({"ok": false, "error": "appId is required for steam source"});
    };
    let pipeline =
        crate::mtsp::engine::PipelineId::from_str_flexible(route).unwrap_or(crate::mtsp::engine::PipelineId::M12);
    let node = crate::mtsp::engine::get_pipeline(pipeline);
    let ms_home = crate::platform::metalsharp_home_dir();
    let prefix = ms_home.join("prefix-steam");
    let wine_root = ms_home.join("runtime").join("wine");
    let receipt = json!({
        "schema": "metalsharp.launch.receipt.v1",
        "preview": true,
        "dryRun": true,
        "source": "steam",
        "appId": appid,
        "route": pipeline.user_selectable_id().unwrap_or_else(|| pipeline.to_legacy_method()),
        "runtimeContractId": crate::runtime_contracts::runtime_contract_id_for_pipeline(pipeline),
        "pipeline": pipeline,
        "pipelineName": node.name,
        "backend": node.backend,
        "prefix": prefix.to_string_lossy(),
        "wine": crate::platform::runtime_wine_binary(&wine_root).to_string_lossy(),
        "gameDir": Value::Null,
        "exePath": Value::Null,
        "dllsStaged": [],
        "dylibsUsed": [],
        "envKeys": ["WINEPREFIX", "WINEDEBUG", "WINEMSYNC", "SteamAppId", "SteamGameId"],
        "logPath": Value::Null,
        "pid": Value::Null,
        "warnings": ["Filesystem-only source prepare preview; use /mtsp/prepare for staged Steam handoff."],
    });
    source_prepare_response("steam", appid.to_string(), pipeline.user_selectable_id().unwrap_or("m12"), receipt)
}

fn prepare_gog(body: &Map<String, Value>) -> Value {
    let Some(product_id) =
        body.get("productId").or_else(|| body.get("appId")).or_else(|| body.get("id")).and_then(value_to_string)
    else {
        return json!({"ok": false, "error": "productId is required for gog source"});
    };
    let ms_home = crate::platform::metalsharp_home_dir();
    let game = gog_game_from_cache(&ms_home, &product_id);
    let prefix = ms_home.join("bottles").join("gog-prefix").join("prefix");
    let wine_root = ms_home.join("runtime").join("wine");
    let receipt = json!({
        "schema": "metalsharp.launch.receipt.v1",
        "preview": true,
        "dryRun": true,
        "source": "gog",
        "appId": product_id,
        "title": game.as_ref().and_then(|game| game.get("title")).cloned().unwrap_or(Value::Null),
        "route": "gogdl_wine",
        "runtimeContractId": "gogdl_wine",
        "pipeline": "gogdl_wine",
        "pipelineName": "GOGDL Wine",
        "backend": "gogdl",
        "prefix": prefix.to_string_lossy(),
        "wine": crate::platform::runtime_wine_binary(&wine_root).to_string_lossy(),
        "gameDir": game.as_ref().and_then(|game| game.get("gameFolder").or_else(|| game.get("game_folder"))).cloned().unwrap_or(Value::Null),
        "exePath": game.as_ref().and_then(|game| game.get("primaryExe").or_else(|| game.get("primary_exe"))).cloned().unwrap_or(Value::Null),
        "dllsStaged": [],
        "dylibsUsed": [],
        "envKeys": ["GOGDL_CONFIG_PATH", "GOGDL_SUPPORT_PATH", "WINEPREFIX"],
        "logPath": Value::Null,
        "pid": Value::Null,
        "warnings": ["Filesystem-only source prepare preview; /sharp-library/gog/play owns actual gogdl launch."],
    });
    source_prepare_response("gog", product_id, "gogdl_wine", receipt)
}

fn prepare_sharp(body: &Map<String, Value>, route: &str) -> Value {
    let Some(id) = body.get("id").or_else(|| body.get("appId")).and_then(|value| value.as_str()) else {
        return json!({"ok": false, "error": "id is required for sharp source"});
    };
    let ms_home = crate::platform::metalsharp_home_dir();
    let app = sharp_app_from_library(&ms_home, id);
    let pipeline =
        crate::mtsp::engine::PipelineId::from_str_flexible(route).unwrap_or(crate::mtsp::engine::PipelineId::WineBare);
    let node = crate::mtsp::engine::get_pipeline(pipeline);
    let prefix = app
        .as_ref()
        .and_then(|app| app.get("bottle_id").or_else(|| app.get("bottleId")))
        .and_then(|value| value.as_str())
        .map(|bottle_id| ms_home.join("bottles").join(bottle_id).join("prefix"))
        .unwrap_or_else(|| ms_home.join("prefix-steam"));
    let install_dir = app
        .as_ref()
        .and_then(|app| app.get("install_dir").or_else(|| app.get("installDir")))
        .and_then(|value| value.as_str())
        .map(PathBuf::from);
    let exe_rel = app
        .as_ref()
        .and_then(|app| app.get("exe_path").or_else(|| app.get("exePath")))
        .and_then(|value| value.as_str())
        .map(PathBuf::from);
    let exe_path = match (&install_dir, &exe_rel) {
        (Some(dir), Some(exe)) => Some(dir.join(exe)),
        _ => None,
    };
    let wine_root = ms_home.join("runtime").join("wine");
    let receipt = json!({
        "schema": "metalsharp.launch.receipt.v1",
        "preview": true,
        "dryRun": true,
        "source": "sharp",
        "appId": id,
        "title": app.as_ref().and_then(|app| app.get("name")).cloned().unwrap_or(Value::Null),
        "route": pipeline.user_selectable_id().unwrap_or_else(|| pipeline.to_legacy_method()),
        "runtimeContractId": crate::runtime_contracts::runtime_contract_id_for_pipeline(pipeline),
        "pipeline": pipeline,
        "pipelineName": node.name,
        "backend": node.backend,
        "prefix": prefix.to_string_lossy(),
        "wine": crate::platform::runtime_wine_binary(&wine_root).to_string_lossy(),
        "gameDir": install_dir.as_ref().map(|path| path.to_string_lossy().to_string()),
        "exePath": exe_path.as_ref().map(|path| path.to_string_lossy().to_string()),
        "dllsStaged": [],
        "dylibsUsed": [],
        "envKeys": ["WINEPREFIX", "WINEDEBUG", "WINEMSYNC"],
        "logPath": Value::Null,
        "pid": Value::Null,
        "warnings": ["Filesystem-only source prepare preview; /sharp-library/launch owns actual custom launch."],
    });
    source_prepare_response("sharp", id.to_string(), pipeline.user_selectable_id().unwrap_or("wine_bare"), receipt)
}

fn source_prepare_response(source: &str, app_id: String, route: &str, receipt: Value) -> Value {
    json!({
        "ok": true,
        "schema": SCHEMA,
        "readOnly": true,
        "source": source,
        "appId": app_id,
        "route": route,
        "launchReceiptPreview": receipt,
        "mutates": false,
        "launches": false,
        "next": "Use source-specific launch only after explicit user action; this endpoint is preview-only.",
    })
}

fn gog_game_from_cache(ms_home: &Path, product_id: &str) -> Option<Value> {
    let cache = read_json(&ms_home.join("gog").join("library.json"))?;
    cache
        .get("games")
        .and_then(|value| value.as_array())
        .and_then(|games| {
            games.iter().find(|game| {
                game.get("productId").or_else(|| game.get("product_id")).and_then(|value| value.as_str())
                    == Some(product_id)
            })
        })
        .cloned()
}

fn sharp_app_from_library(ms_home: &Path, id: &str) -> Option<Value> {
    let library = read_json(&ms_home.join("sharp-library").join("library.json"))?;
    library
        .as_array()
        .and_then(|apps| apps.iter().find(|app| app.get("id").and_then(|value| value.as_str()) == Some(id)).cloned())
}

fn read_json(path: &Path) -> Option<Value> {
    fs::read_to_string(path).ok().and_then(|data| serde_json::from_str(&data).ok())
}

fn value_to_string(value: &Value) -> Option<String> {
    value.as_str().map(str::to_string).or_else(|| value.as_u64().map(|number| number.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn steam_prepare_preview_is_read_only_and_uses_launch_receipt_schema() {
        let body =
            serde_json::from_value::<Map<String, Value>>(json!({"source":"steam","appId":620,"route":"m12"})).unwrap();
        let response = handle_prepare(&body);
        assert_eq!(response.get("schema").and_then(|value| value.as_str()), Some(SCHEMA));
        assert_eq!(response.get("readOnly").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(
            response.pointer("/launchReceiptPreview/schema").and_then(|value| value.as_str()),
            Some("metalsharp.launch.receipt.v1")
        );
        assert_eq!(response.pointer("/launchReceiptPreview/preview").and_then(|value| value.as_bool()), Some(true));
        assert_eq!(
            response.pointer("/launchReceiptPreview/runtimeContractId").and_then(|value| value.as_str()),
            Some("m12_dxmt_m12")
        );
    }

    #[test]
    fn gog_prepare_preview_uses_dedicated_gog_route() {
        let body =
            serde_json::from_value::<Map<String, Value>>(json!({"source":"gog","productId":"1876546888"})).unwrap();
        let response = handle_prepare(&body);
        assert_eq!(response.get("source").and_then(|value| value.as_str()), Some("gog"));
        assert_eq!(
            response.pointer("/launchReceiptPreview/runtimeContractId").and_then(|value| value.as_str()),
            Some("gogdl_wine")
        );
        assert!(response
            .pointer("/launchReceiptPreview/prefix")
            .and_then(|value| value.as_str())
            .unwrap_or_default()
            .ends_with("bottles/gog-prefix/prefix"));
    }
}
