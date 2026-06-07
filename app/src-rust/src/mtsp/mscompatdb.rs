use super::engine::{get_pipeline, PipelineId};
use super::rules::get_game_recipe;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone, serde::Serialize)]
pub struct CompatDbDllRule {
    pub dll: String,
    pub backend: String,
    pub source_dir: String,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct CompatDbGameRule {
    pub appid: u32,
    pub name: String,
    pub process_patterns: Vec<String>,
    pub graphics_backend: String,
    pub dll_redirects: Vec<CompatDbDllRule>,
    pub offline_capable: bool,
    pub anticheat: Option<String>,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct CompatDbRules {
    pub version: u32,
    pub generated_at: String,
    pub backends: HashMap<String, String>,
    pub game_rules: Vec<CompatDbGameRule>,
    pub default_backend: String,
}

fn dll_redirects_for_pipeline(pipeline: PipelineId) -> Vec<CompatDbDllRule> {
    let node = get_pipeline(pipeline);
    let backend = node.graphics_backend.to_string();
    node.deploy_dlls
        .iter()
        .map(|dll| CompatDbDllRule {
            dll: dll.filename.to_string(),
            backend: backend.clone(),
            source_dir: dll.source_subpath.to_string(),
        })
        .collect()
}

fn known_process_patterns(appid: u32) -> Vec<String> {
    match appid {
        1245620 => vec!["eldenring.exe".into(), "start_protected_game.exe".into()],
        1888160 => vec!["armoredcore6.exe".into(), "start_protected_game.exe".into()],
        1091500 => vec!["cyberpunk2077.exe".into()],
        2050650 => vec!["re4.exe".into()],
        1551360 => vec!["ForzaHorizon5.exe".into()],
        1716740 => vec!["starfield.exe".into()],
        990080 => vec!["HogwartsLegacy.exe".into()],
        2767030 => vec!["MarvelRivals.exe".into(), "start_protected_game.exe".into()],
        553850 => vec!["helldivers2.exe".into()],
        1643320 => vec!["S.T.A.L.K.E.R.2.exe".into(), "Stalker2.exe".into()],
        _ => vec![],
    }
}

pub fn generate_compatdb_rules() -> CompatDbRules {
    let mut backends = HashMap::new();
    backends.insert("dxmt".into(), "lib/dxmt/x86_64-windows".into());
    backends.insert("gptk".into(), "lib/gptk/x86_64-windows".into());
    backends.insert("wine".into(), "lib/wine/x86_64-windows".into());

    let mut game_rules = Vec::new();

    for pipeline_id in [PipelineId::M9, PipelineId::M10, PipelineId::M11, PipelineId::M12, PipelineId::M13, PipelineId::D3DMetal] {
        let node = get_pipeline(pipeline_id);
        let appids_for_pipeline = appids_with_pipeline(pipeline_id);

        for appid in appids_for_pipeline {
            let recipe = get_game_recipe(appid);
            let patterns = if let Some(ref r) = recipe {
                if !r.name.is_empty() {
                    let mut p = known_process_patterns(appid);
                    if p.is_empty() {
                        let base: String = r.name.to_lowercase().replace([' ', ':', '-', '\''], "");
                        let exe_guess = base.split_whitespace().next().unwrap_or("");
                        if !exe_guess.is_empty() {
                            p.push(format!("{}.exe", exe_guess));
                        }
                    }
                    p
                } else {
                    known_process_patterns(appid)
                }
            } else {
                known_process_patterns(appid)
            };

            game_rules.push(CompatDbGameRule {
                appid,
                name: recipe.as_ref().map(|r| r.name.clone()).unwrap_or_default(),
                process_patterns: patterns,
                graphics_backend: node.graphics_backend.to_string(),
                dll_redirects: dll_redirects_for_pipeline(pipeline_id),
                offline_capable: recipe.as_ref().map(|r| r.offline_capable).unwrap_or(false),
                anticheat: recipe.as_ref().and_then(|r| r.anticheat.clone()),
            });
        }
    }

    game_rules.sort_by_key(|r| r.appid);

    CompatDbRules {
        version: 2,
        generated_at: format!("{}", SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs()),
        backends,
        game_rules,
        default_backend: "dxmt".into(),
    }
}

fn appids_with_pipeline(target: PipelineId) -> Vec<u32> {
    let rules = super::rules::all_rules_with_recipes();
    rules.iter().filter(|(_, recipe)| recipe.pipeline == target).map(|(appid, _)| *appid).collect()
}

pub fn write_compatdb_rules(ms_root: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let rules = generate_compatdb_rules();
    let share_dir = ms_root.join("share").join("metalsharp");
    fs::create_dir_all(&share_dir)?;
    let rules_path = share_dir.join("mscompatdb-rules.json");
    let json = serde_json::to_string_pretty(&rules)?;
    fs::write(&rules_path, json)?;
    Ok(())
}

pub fn handle_generate_compatdb_rules() -> Value {
    let rules = generate_compatdb_rules();
    json!({
        "ok": true,
        "version": rules.version,
        "backend_count": rules.backends.len(),
        "game_rule_count": rules.game_rules.len(),
        "default_backend": rules.default_backend,
        "rules": rules,
    })
}
