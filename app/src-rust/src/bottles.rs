use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::{hash_map::DefaultHasher, HashMap, HashSet};
use std::fs::{self, OpenOptions};
use std::hash::{Hash, Hasher};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use std::thread;
use std::time::Duration;
use walkdir::WalkDir;

const BOTTLES_DIR: &str = "bottles";
const COMPATDATA_DIR: &str = "compatdata";
const MANIFEST_FILE: &str = "bottle.json";
const COMPATDATA_MANIFEST_FILE: &str = "metalsharp-compatdata.json";
const COMPATIBILITY_MATRIX_FILE: &str = "compatibility-matrix.json";
const LAUNCH_WATCH_INTERVAL_SECS: u64 = 5;
const LAUNCH_WATCH_MAX_POLLS: usize = 4320;
const LAUNCH_WATCH_LOG_STABLE_POLLS: usize = 3;
const WINDOWS_VERSION_COMPONENT_PREFIX: &str = "windows_version_";
static BOTTLE_SAVE_LOCK: Mutex<()> = Mutex::new(());

#[derive(Debug, Clone, Copy, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum BottleType {
    Steam,
    SharpApp,
    Installer,
    Utility,
}

#[derive(Debug, Clone, Copy, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum BottleArch {
    Win32,
    Win64,
    Wow64,
}

#[derive(Debug, Clone, Copy, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RuntimeProfile {
    Plain,
    Launcher,
    GameInstall,
    M9,
    M10,
    M11,
    M12,
    M13,
    Dotnet,
    Win32Dotnet,
    Webview,
    JavaLauncher,
    FnaArm64,
    FnaX86,
}

#[derive(Debug, Clone, Copy, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ComponentState {
    Missing,
    Installed,
    NeedsRepair,
    Unknown,
}

#[derive(Debug, Clone, Deserialize, Eq, PartialEq, Serialize)]
pub struct RuntimeComponent {
    pub id: String,
    pub state: ComponentState,
}

#[derive(Debug, Clone, Deserialize, Eq, PartialEq, Serialize)]
pub struct AppDetection {
    pub name: String,
    pub exe_path: String,
    pub source: String,
}

#[derive(Debug, Clone, Deserialize, Eq, PartialEq, Serialize)]
pub struct BottleRuntimeAsset {
    pub id: String,
    pub kind: String,
    pub source_path: String,
    pub present: bool,
}

#[derive(Debug, Clone, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum BottleHealth {
    New,
    Ready,
    NeedsRepair,
    Failed,
}

#[derive(Debug, Clone, Copy, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum InstallerKind {
    Exe,
    Msi,
    Nsis,
    Inno,
    Wix,
    Squirrel,
    Electron,
    Unity,
    Webview,
    Java,
    Unknown,
}

#[derive(Debug, Clone, Deserialize, Eq, PartialEq, Serialize)]
pub struct BottleManifest {
    pub id: String,
    pub name: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub custom_name: Option<String>,
    pub bottle_type: BottleType,
    pub steam_app_id: Option<u32>,
    pub prefix_path: String,
    pub arch: BottleArch,
    pub runtime_profile: RuntimeProfile,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub preferred_pipeline: Option<String>,
    #[serde(default)]
    pub installed_components: Vec<RuntimeComponent>,
    pub source_installer_path: Option<String>,
    pub installer_kind: Option<InstallerKind>,
    pub game_install_path: Option<String>,
    #[serde(default)]
    pub runtime_assets: Vec<BottleRuntimeAsset>,
    #[serde(default)]
    pub installed_app_detections: Vec<AppDetection>,
    pub health: BottleHealth,
    pub last_launch_log: Option<String>,
    #[serde(default)]
    pub last_launch_pid: Option<u32>,
    #[serde(default)]
    pub last_launch_status: Option<String>,
    #[serde(default)]
    pub last_launch_finished_at: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Eq, PartialEq, Serialize)]
pub struct InstallerClassification {
    pub arch: BottleArch,
    pub installer_kind: InstallerKind,
    pub runtime_profile: RuntimeProfile,
    pub pipeline: crate::mtsp::engine::PipelineId,
    #[serde(default)]
    pub hints: Vec<String>,
}

struct KnownLauncherRecipe {
    id: &'static str,
    label: &'static str,
    tokens: &'static [&'static str],
    installer_kind: InstallerKind,
    runtime_profile: RuntimeProfile,
    forced_pipeline: Option<crate::mtsp::engine::PipelineId>,
}

#[derive(Debug, Clone, Serialize)]
pub struct BottleDiagnostic {
    pub id: String,
    pub ready: bool,
    pub summary: String,
    pub checks: Vec<BottleCheck>,
    pub actions: Vec<BottleAction>,
    pub component_sources: Vec<ComponentSourcePolicy>,
}

#[derive(Debug, Clone, Serialize)]
pub struct BottleCheck {
    pub id: String,
    pub ok: bool,
    pub detail: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct BottleAction {
    pub id: String,
    pub status: String,
    pub detail: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct ComponentSourcePolicy {
    pub id: String,
    pub source: String,
    pub available: bool,
    pub detail: String,
    pub path: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct RuntimeProfileDefinition {
    pub id: RuntimeProfile,
    pub name: &'static str,
    pub arch: BottleArch,
    pub wineboot: bool,
    pub components: Vec<String>,
    pub launch_pipeline: crate::mtsp::engine::PipelineId,
    pub mono_runtime: Option<MonoRuntimeDefinition>,
}

#[derive(Debug, Clone, Serialize)]
pub struct MonoRuntimeDefinition {
    pub id: &'static str,
    pub binary_path: String,
    pub expected_arch: &'static str,
    pub known_version: &'static str,
    pub config_path: Option<&'static str>,
    pub launch_wrapper: &'static str,
    pub notes: &'static str,
}

#[derive(Debug, Clone, Serialize)]
pub struct ComponentRepairReport {
    pub id: String,
    pub status: String,
    pub detail: String,
    pub asset_path: Option<String>,
    pub log_path: Option<String>,
    pub pid: Option<u32>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct CompatibilityCase {
    pub id: String,
    pub name: String,
    pub case_type: String,
    pub required_profile: RuntimeProfile,
    pub installer_opens: String,
    pub final_app_detected: String,
    pub final_app_launches: String,
    pub known_missing_runtime: String,
    pub bottle_id: Option<String>,
    #[serde(default)]
    pub notes: String,
    #[serde(default)]
    pub evidence_updated_at: Option<String>,
    #[serde(default)]
    pub per_game_prefix_recommendation: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct RedistSourceGuide {
    pub id: String,
    pub name: String,
    pub source_url: String,
    pub local_targets: Vec<String>,
    pub policy: String,
    pub notes: String,
}

struct ComponentInstaller {
    path: PathBuf,
    args: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SteamRuntimeDiagnostic {
    pub appid: Option<u32>,
    pub bottle_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bottle_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub preferred_pipeline: Option<String>,
    pub pipeline: crate::mtsp::engine::PipelineId,
    pub runtime_profile: RuntimeProfile,
    pub prefix_path: String,
    pub game_install_path: Option<String>,
    pub runtime_assets: Vec<BottleRuntimeAsset>,
    pub components: Vec<RuntimeComponent>,
    pub actions: Vec<BottleAction>,
    pub compatdata: Option<SteamCompatdataRecord>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub recipe_missing_components: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub recipe_name: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub recipe_missing_dlls: Vec<String>,
    #[serde(skip_serializing_if = "std::collections::HashMap::is_empty")]
    pub recipe_env: std::collections::HashMap<String, String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub d3d12_sdk: Option<Value>,
}

#[derive(Debug, Clone, Deserialize, Eq, PartialEq, Serialize)]
pub struct SteamCompatdataRecord {
    pub appid: u32,
    pub name: String,
    pub bottle_id: String,
    pub compatdata_path: String,
    pub prefix_path: String,
    pub steam_prefix_path: String,
    pub game_install_path: Option<String>,
    pub runtime_profile: RuntimeProfile,
    pub launch_pipeline: String,
    pub steam_identity_mode: String,
    #[serde(default)]
    pub compat_tool_name: String,
    #[serde(default)]
    pub launch_command_template: String,
    pub log_dir: String,
    #[serde(default)]
    pub runtime_assets: Vec<BottleRuntimeAsset>,
    #[serde(default)]
    pub required_components: Vec<RuntimeComponent>,
    pub last_launch_log: Option<String>,
    pub last_launch_pid: Option<u32>,
    pub last_launch_status: Option<String>,
    pub last_launch_finished_at: Option<String>,
    pub updated_at: String,
}

pub fn bottles_root() -> PathBuf {
    crate::platform::metalsharp_home_dir().join(BOTTLES_DIR)
}

pub fn compatdata_root() -> PathBuf {
    crate::platform::metalsharp_home_dir().join(COMPATDATA_DIR)
}

fn steam_launch_prefix() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("prefix-steam")
}

pub fn bottle_dir(id: &str) -> PathBuf {
    bottles_root().join(id)
}

pub fn bottle_manifest_path(id: &str) -> PathBuf {
    bottle_dir(id).join(MANIFEST_FILE)
}

pub fn steam_compatdata_dir(appid: u32) -> PathBuf {
    compatdata_root().join(appid.to_string())
}

pub fn steam_compatdata_manifest_path(appid: u32) -> PathBuf {
    steam_compatdata_dir(appid).join(COMPATDATA_MANIFEST_FILE)
}

pub fn steam_compatdata_launch_log_path(appid: u32) -> PathBuf {
    steam_compatdata_dir(appid).join("logs").join(format!("launch-{}.log", timestamp_secs()))
}

fn validate_bottle_id(id: &str) -> Result<(), Box<dyn std::error::Error>> {
    let valid = !id.is_empty()
        && id.len() <= 128
        && id.bytes().all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-'));
    if valid {
        Ok(())
    } else {
        Err("invalid bottle id".into())
    }
}

fn compatibility_matrix_path() -> PathBuf {
    bottles_root().join(COMPATIBILITY_MATRIX_FILE)
}

pub fn installer_payload_dir(id: &str) -> PathBuf {
    bottle_dir(id).join("installers")
}

pub fn bottle_logs_dir(id: &str) -> PathBuf {
    bottle_dir(id).join("logs")
}

pub fn next_launch_log_path(id: &str) -> PathBuf {
    bottle_logs_dir(id).join(format!("launch-{}.log", timestamp_secs()))
}

pub fn load_bottle(id: &str) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    validate_bottle_id(id)?;
    let data = fs::read_to_string(bottle_manifest_path(id))?;
    Ok(serde_json::from_str(&data)?)
}

pub fn save_bottle(manifest: &BottleManifest) -> Result<(), Box<dyn std::error::Error>> {
    validate_bottle_id(&manifest.id)?;
    let _guard = BOTTLE_SAVE_LOCK.lock().map_err(|_| "bottle save lock poisoned")?;
    let dir = bottle_dir(&manifest.id);
    fs::create_dir_all(dir.join("prefix"))?;
    fs::create_dir_all(dir.join("installers"))?;
    fs::create_dir_all(dir.join("logs"))?;
    fs::create_dir_all(dir.join("assets"))?;
    let data = serde_json::to_string_pretty(manifest)?;
    let manifest_path = bottle_manifest_path(&manifest.id);
    write_bottle_manifest_atomic(&manifest_path, data.as_bytes())?;
    Ok(())
}

fn manifest_preferred_pipeline(manifest: &BottleManifest) -> Option<crate::mtsp::engine::PipelineId> {
    match manifest.preferred_pipeline.as_deref().and_then(crate::mtsp::engine::PipelineId::from_str_flexible) {
        Some(crate::mtsp::engine::PipelineId::Dxmt) | None => None,
        Some(pipeline) => Some(pipeline),
    }
}

fn pipeline_preference_id(pipeline: crate::mtsp::engine::PipelineId) -> &'static str {
    match pipeline {
        crate::mtsp::engine::PipelineId::Dxmt => "dxmt",
        crate::mtsp::engine::PipelineId::M9 => "m9",
        crate::mtsp::engine::PipelineId::M10 => "m10",
        crate::mtsp::engine::PipelineId::M11 => "m11",
        crate::mtsp::engine::PipelineId::M12 => "m12",
        crate::mtsp::engine::PipelineId::M13 => "m13",
        crate::mtsp::engine::PipelineId::M32 => "m32",
        crate::mtsp::engine::PipelineId::FnaArm64 => "fna_arm64",
        crate::mtsp::engine::PipelineId::Steam => "steam",
        crate::mtsp::engine::PipelineId::MacSteam => "mac_steam",
        crate::mtsp::engine::PipelineId::WineBare => "wine_bare",
    }
}

fn effective_pipeline_for_bottle_refresh(
    existing: Option<&BottleManifest>,
    requested: crate::mtsp::engine::PipelineId,
    respect_preferred_pipeline: bool,
) -> crate::mtsp::engine::PipelineId {
    if respect_preferred_pipeline {
        existing.and_then(manifest_preferred_pipeline).unwrap_or(requested)
    } else {
        requested
    }
}

pub fn preferred_pipeline_for_steam_app(appid: u32) -> Option<crate::mtsp::engine::PipelineId> {
    load_bottle(&steam_game_bottle_id(appid)).ok().as_ref().and_then(manifest_preferred_pipeline)
}

pub fn resolve_steam_pipeline_for_request(
    appid: u32,
    requested: Option<crate::mtsp::engine::PipelineId>,
) -> crate::mtsp::engine::PipelineId {
    match requested {
        Some(crate::mtsp::engine::PipelineId::Dxmt) | None => {
            preferred_pipeline_for_steam_app(appid).unwrap_or_else(|| crate::mtsp::rules::resolve_pipeline(appid))
        },
        Some(pipeline) => pipeline,
    }
}

pub fn load_steam_compatdata(appid: u32) -> Result<SteamCompatdataRecord, Box<dyn std::error::Error>> {
    let data = fs::read_to_string(steam_compatdata_manifest_path(appid))?;
    Ok(serde_json::from_str(&data)?)
}

pub fn save_steam_compatdata(
    manifest: &BottleManifest,
    pipeline: crate::mtsp::engine::PipelineId,
) -> Result<SteamCompatdataRecord, Box<dyn std::error::Error>> {
    let appid = manifest.steam_app_id.ok_or("steam compatdata requires steam appid")?;
    let record = steam_compatdata_record(manifest, pipeline);
    let dir = steam_compatdata_dir(appid);
    fs::create_dir_all(dir.join("logs"))?;
    fs::create_dir_all(dir.join("assets"))?;
    let data = serde_json::to_string_pretty(&record)?;
    let manifest_path = steam_compatdata_manifest_path(appid);
    write_bottle_manifest_atomic(&manifest_path, data.as_bytes())?;
    Ok(record)
}

fn steam_compatdata_record(
    manifest: &BottleManifest,
    pipeline: crate::mtsp::engine::PipelineId,
) -> SteamCompatdataRecord {
    let appid = manifest.steam_app_id.unwrap_or_default();
    SteamCompatdataRecord {
        appid,
        name: manifest.name.clone(),
        bottle_id: manifest.id.clone(),
        compatdata_path: steam_compatdata_dir(appid).to_string_lossy().to_string(),
        prefix_path: manifest.prefix_path.clone(),
        steam_prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
        game_install_path: manifest.game_install_path.clone(),
        runtime_profile: manifest.runtime_profile,
        launch_pipeline: pipeline_preference_id(pipeline).to_string(),
        steam_identity_mode: "wine_steam_background".to_string(),
        compat_tool_name: "MetalSharp".to_string(),
        launch_command_template: format!(
            "POST /steam/launch-game {{\"appid\":{},\"launchMethod\":\"{}\"}}",
            appid,
            pipeline_preference_id(pipeline)
        ),
        log_dir: steam_compatdata_dir(appid).join("logs").to_string_lossy().to_string(),
        runtime_assets: manifest.runtime_assets.clone(),
        required_components: manifest.installed_components.clone(),
        last_launch_log: manifest.last_launch_log.clone(),
        last_launch_pid: manifest.last_launch_pid,
        last_launch_status: manifest.last_launch_status.clone(),
        last_launch_finished_at: manifest.last_launch_finished_at.clone(),
        updated_at: timestamp_secs(),
    }
}

fn write_bottle_manifest_atomic(manifest_path: &Path, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
    let tmp_path = manifest_path.with_extension(format!("json.tmp-{}-{}", std::process::id(), timestamp_secs()));
    fs::write(&tmp_path, data)?;
    fs::rename(tmp_path, manifest_path)?;
    Ok(())
}

pub fn list_bottles() -> Result<Vec<BottleManifest>, Box<dyn std::error::Error>> {
    let root = bottles_root();
    if !root.exists() {
        return Ok(Vec::new());
    }

    let mut bottles = Vec::new();
    for entry in fs::read_dir(root)? {
        let Ok(entry) = entry else {
            continue;
        };
        let path = entry.path().join(MANIFEST_FILE);
        if !path.exists() {
            continue;
        }
        if let Ok(data) = fs::read_to_string(path) {
            if let Ok(mut manifest) = serde_json::from_str::<BottleManifest>(&data) {
                if refresh_manifest_launch_state(&mut manifest) {
                    manifest.updated_at = timestamp_secs();
                    let _ = save_bottle(&manifest);
                }
                bottles.push(manifest);
            }
        }
    }
    bottles.sort_by(|a, b| a.name.cmp(&b.name).then_with(|| a.id.cmp(&b.id)));
    Ok(bottles)
}

pub fn ensure_installer_bottle(
    source_installer: &Path,
    classification: &InstallerClassification,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    ensure_installer_bottle_with_id(source_installer, classification, installer_bottle_id(source_installer))
}

pub fn create_fresh_installer_bottle(
    source_installer: &Path,
    classification: &InstallerClassification,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    ensure_installer_bottle_with_id(source_installer, classification, fresh_installer_bottle_id(source_installer))
}

fn ensure_installer_bottle_with_id(
    source_installer: &Path,
    classification: &InstallerClassification,
    id: String,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let now = timestamp_secs();
    let name = source_installer
        .file_stem()
        .map(|n| n.to_string_lossy().to_string())
        .filter(|n| !n.trim().is_empty())
        .unwrap_or_else(|| "Windows Installer".to_string());

    let mut manifest = load_bottle(&id).unwrap_or_else(|_| BottleManifest {
        id: id.clone(),
        name,
        custom_name: None,
        bottle_type: BottleType::Installer,
        steam_app_id: None,
        prefix_path: bottle_dir(&id).join("prefix").to_string_lossy().to_string(),
        arch: classification.arch,
        runtime_profile: classification.runtime_profile,
        preferred_pipeline: None,
        installed_components: default_components_for(classification.runtime_profile),
        source_installer_path: Some(source_installer.to_string_lossy().to_string()),
        installer_kind: Some(classification.installer_kind),
        game_install_path: None,
        runtime_assets: Vec::new(),
        installed_app_detections: Vec::new(),
        health: BottleHealth::New,
        last_launch_log: None,
        last_launch_pid: None,
        last_launch_status: None,
        last_launch_finished_at: None,
        created_at: now.clone(),
        updated_at: now.clone(),
    });

    manifest.arch = classification.arch;
    manifest.runtime_profile = classification.runtime_profile;
    manifest.source_installer_path = Some(source_installer.to_string_lossy().to_string());
    manifest.installer_kind = Some(classification.installer_kind);
    manifest.installed_components =
        merge_components(manifest.installed_components, default_components_for(classification.runtime_profile));
    manifest.updated_at = now;
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn steam_game_bottle_id(appid: u32) -> String {
    format!("steam_{}", appid)
}

pub fn ensure_steam_game_bottle(
    appid: u32,
    name: &str,
    game_dir: Option<&Path>,
    pipeline: crate::mtsp::engine::PipelineId,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    ensure_steam_game_bottle_inner(appid, name, game_dir, pipeline, false)
}

pub fn refresh_steam_game_bottle(
    appid: u32,
    name: &str,
    game_dir: Option<&Path>,
    pipeline: crate::mtsp::engine::PipelineId,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    ensure_steam_game_bottle_inner(appid, name, game_dir, pipeline, true)
}

fn ensure_steam_game_bottle_inner(
    appid: u32,
    name: &str,
    game_dir: Option<&Path>,
    pipeline: crate::mtsp::engine::PipelineId,
    respect_preferred_pipeline: bool,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let id = steam_game_bottle_id(appid);
    let now = timestamp_secs();
    let existing = load_bottle(&id).ok();
    let effective_pipeline =
        effective_pipeline_for_bottle_refresh(existing.as_ref(), pipeline, respect_preferred_pipeline);
    let runtime_profile = runtime_profile_for_pipeline(effective_pipeline);
    let mut manifest = existing.unwrap_or_else(|| BottleManifest {
        id: id.clone(),
        name: name.to_string(),
        custom_name: None,
        bottle_type: BottleType::Steam,
        steam_app_id: Some(appid),
        prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
        arch: BottleArch::Wow64,
        runtime_profile,
        preferred_pipeline: None,
        installed_components: default_components_for(runtime_profile),
        source_installer_path: None,
        installer_kind: None,
        game_install_path: None,
        runtime_assets: Vec::new(),
        installed_app_detections: Vec::new(),
        health: BottleHealth::New,
        last_launch_log: None,
        last_launch_pid: None,
        last_launch_status: None,
        last_launch_finished_at: None,
        created_at: now.clone(),
        updated_at: now.clone(),
    });

    manifest.name = manifest.custom_name.clone().unwrap_or_else(|| name.to_string());
    manifest.bottle_type = BottleType::Steam;
    manifest.steam_app_id = Some(appid);
    manifest.prefix_path = steam_launch_prefix().to_string_lossy().to_string();
    manifest.runtime_profile = runtime_profile;
    manifest.installed_components =
        merge_components(manifest.installed_components, default_components_for(runtime_profile));
    manifest.game_install_path = game_dir.map(normalized_existing_path_string);
    manifest.runtime_assets = game_dir.map(detect_game_runtime_assets).unwrap_or_default();
    manifest.installed_components =
        merge_components(manifest.installed_components, infer_components_from_runtime_assets(&manifest.runtime_assets));
    manifest.installed_app_detections = game_dir.map(detect_apps_in_game_dir).unwrap_or_default();
    manifest.health =
        if game_dir.map(|dir| dir.exists()).unwrap_or(false) { BottleHealth::Ready } else { BottleHealth::New };
    manifest.updated_at = now;
    save_bottle(&manifest)?;
    let _ = save_steam_compatdata(&manifest, effective_pipeline);
    Ok(manifest)
}

pub fn prepare_steam_game_launch(
    appid: u32,
    pipeline: crate::mtsp::engine::PipelineId,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    if pipeline == crate::mtsp::engine::PipelineId::M12 {
        let _ = crate::setup::prepare_game(appid)?;
    }
    let dual = crate::scan::resolve_dual_game_dir(appid);
    let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
    let mut manifest = ensure_steam_game_bottle_inner(appid, &name, dual.wine_dir.as_deref(), pipeline, false)?;
    let prefix = PathBuf::from(&manifest.prefix_path);
    fs::create_dir_all(&prefix)?;
    manifest.installed_components = inspect_components_for_manifest(&manifest, &prefix, &manifest.installed_components);
    refresh_manifest_runtime_views(&mut manifest);
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    let _ = save_steam_compatdata(&manifest, pipeline);
    Ok(manifest)
}

pub fn sync_steam_game_bottles() -> Result<Vec<BottleManifest>, Box<dyn std::error::Error>> {
    let mut bottles = Vec::new();
    for appid in crate::steam::get_wine_steam_installed_games() {
        let dual = crate::scan::resolve_dual_game_dir(appid);
        let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
        let pipeline = crate::mtsp::rules::resolve_pipeline(appid);
        bottles.push(ensure_steam_game_bottle_inner(appid, &name, dual.wine_dir.as_deref(), pipeline, true)?);
    }
    Ok(bottles)
}

pub fn classify_installer(source_installer: &Path) -> InstallerClassification {
    let pe = fs::read(source_installer).ok().and_then(|data| crate::mtsp::pe::parse_pe_imports(&data));
    let strings = read_ascii_strings(source_installer, 512 * 1024);
    let lower_strings = strings.iter().map(|s| s.to_ascii_lowercase()).collect::<Vec<_>>();
    let imports = pe.as_ref().map(|p| p.imports.as_slice()).unwrap_or(&[]);
    let is_64_bit = pe.as_ref().map(|p| p.is_64_bit).unwrap_or(false);
    let is_msi =
        source_installer.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("msi")).unwrap_or(false);
    let arch = match pe {
        Some(ref info) if info.is_64_bit => BottleArch::Win64,
        Some(_) => BottleArch::Win32,
        None => BottleArch::Wow64,
    };

    let mut hints = Vec::new();
    let imports_mscoree = imports.iter().any(|import| import.eq_ignore_ascii_case("mscoree.dll"));
    let strings_dotnet = lower_strings.iter().any(|s| {
        s.contains("system.runtime")
            || s.contains(".netframework")
            || s.contains("mscoree")
            || s.contains("windowsruntime")
    });
    let strings_webview = lower_strings.iter().any(|s| s.contains("webview2") || s.contains("edgeupdate"));
    let strings_java = lower_strings.iter().any(|s| s.contains("java") || s.contains("jre") || s.contains("jdk"));
    let known_launcher = known_launcher_recipe(source_installer, &lower_strings);
    let installer_kind = known_launcher
        .map(|recipe| recipe.installer_kind)
        .unwrap_or_else(|| classify_installer_kind(source_installer, &lower_strings, is_msi));

    if is_msi {
        hints.push("msi_package".to_string());
    }
    if let Some(recipe) = known_launcher {
        hints.push(format!("known_launcher:{}", recipe.id));
        hints.push(format!("launcher_name:{}", recipe.label));
    }
    if imports_mscoree || strings_dotnet {
        hints.push("dotnet_or_clr".to_string());
    }
    if strings_webview {
        hints.push("webview".to_string());
    }
    if strings_java {
        hints.push("java_launcher".to_string());
    }
    if installer_kind != InstallerKind::Exe && installer_kind != InstallerKind::Unknown {
        hints.push(format!("installer_kind:{:?}", installer_kind).to_ascii_lowercase());
    }

    let pipeline = if let Some(recipe) = known_launcher {
        recipe.forced_pipeline.unwrap_or(crate::mtsp::engine::PipelineId::WineBare)
    } else if is_msi {
        crate::mtsp::engine::PipelineId::WineBare
    } else {
        installer_pipeline_from_pe(pe.as_ref())
    };
    let runtime_profile = if let Some(recipe) = known_launcher {
        recipe.runtime_profile
    } else if imports_mscoree || strings_dotnet {
        if is_64_bit {
            RuntimeProfile::Dotnet
        } else {
            RuntimeProfile::Win32Dotnet
        }
    } else if strings_webview {
        RuntimeProfile::Webview
    } else if strings_java {
        RuntimeProfile::JavaLauncher
    } else if matches!(installer_kind, InstallerKind::Squirrel | InstallerKind::Electron | InstallerKind::Webview) {
        RuntimeProfile::Launcher
    } else if matches!(
        installer_kind,
        InstallerKind::Msi | InstallerKind::Nsis | InstallerKind::Inno | InstallerKind::Wix
    ) {
        RuntimeProfile::GameInstall
    } else {
        match pipeline {
            crate::mtsp::engine::PipelineId::Dxmt => RuntimeProfile::GameInstall,
            crate::mtsp::engine::PipelineId::M9 => RuntimeProfile::M9,
            crate::mtsp::engine::PipelineId::M10 => RuntimeProfile::M10,
            crate::mtsp::engine::PipelineId::M11 => RuntimeProfile::M11,
            crate::mtsp::engine::PipelineId::M12 => RuntimeProfile::M12,
            crate::mtsp::engine::PipelineId::M13 => RuntimeProfile::M13,
            _ => RuntimeProfile::Plain,
        }
    };

    InstallerClassification { arch, installer_kind, runtime_profile, pipeline, hints }
}

pub fn set_last_launch_log(id: &str, log_path: &Path) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    manifest.last_launch_log = Some(log_path.to_string_lossy().to_string());
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn set_launch_started(id: &str, pid: u32, log_path: &Path) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    mark_manifest_launch_started(&mut manifest, pid, log_path);
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn watch_bottle_launch(id: String, pid: u32) {
    thread::spawn(move || {
        let watch = BottleLaunchWatch::for_bottle(&id);
        let mut prefix_wait = None;
        let mut prefix_wait_started = false;
        let mut stable_log_polls = 0usize;
        let mut last_log_size = watch.as_ref().and_then(|watch| watch.log_size());

        for _ in 0..LAUNCH_WATCH_MAX_POLLS {
            thread::sleep(Duration::from_secs(LAUNCH_WATCH_INTERVAL_SECS));
            if !prefix_wait_started {
                prefix_wait = watch.as_ref().and_then(|watch| watch.spawn_prefix_wait());
                prefix_wait_started = true;
            }

            let tree_active = is_process_tree_active(pid);
            if let Some(waiter) = prefix_wait.as_mut() {
                match waiter.try_wait() {
                    Ok(Some(_)) if !tree_active => {
                        let _ = complete_bottle_launch(&id, pid);
                        return;
                    },
                    Ok(Some(_)) => {
                        prefix_wait = None;
                    },
                    Ok(None) => {
                        continue;
                    },
                    Err(_) => {
                        prefix_wait = None;
                    },
                }
            }

            if tree_active {
                stable_log_polls = 0;
                last_log_size = watch.as_ref().and_then(|watch| watch.log_size());
                continue;
            }

            let current_log_size = watch.as_ref().and_then(|watch| watch.log_size());
            if current_log_size.is_some() && current_log_size == last_log_size {
                stable_log_polls += 1;
            } else {
                stable_log_polls = 0;
                last_log_size = current_log_size;
            }

            if stable_log_polls >= LAUNCH_WATCH_LOG_STABLE_POLLS || watch.is_none() {
                let _ = complete_bottle_launch(&id, pid);
                return;
            }
        }
    });
}

pub fn refresh_app_detections(id: &str) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    refresh_manifest_launch_state(&mut manifest);
    refresh_manifest_runtime_views(&mut manifest);
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn set_runtime_profile(id: &str, profile: RuntimeProfile) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    manifest.runtime_profile = profile;
    manifest.arch = runtime_profile_definition(profile).arch;
    manifest.installed_components = rebuild_components_for_profile(&manifest.installed_components, profile);
    if manifest.bottle_type == BottleType::Steam {
        let pipeline = runtime_profile_definition(profile).launch_pipeline;
        manifest.preferred_pipeline = pipeline.user_selectable_id().map(|id| id.to_string());
    }
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    if let Some(appid) = manifest.steam_app_id {
        let pipeline =
            manifest_preferred_pipeline(&manifest).unwrap_or_else(|| crate::mtsp::rules::resolve_pipeline(appid));
        let _ = save_steam_compatdata(&manifest, pipeline);
    }
    Ok(manifest)
}

pub fn edit_bottle(
    id: &str,
    name: Option<&str>,
    preferred_pipeline: Option<&str>,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    if let Some(name) = name {
        let trimmed = name.trim();
        manifest.custom_name = if trimmed.is_empty() { None } else { Some(trimmed.to_string()) };
        if let Some(custom_name) = &manifest.custom_name {
            manifest.name = custom_name.clone();
        } else if let Some(appid) = manifest.steam_app_id {
            manifest.name =
                crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
        }
    }
    if let Some(raw_pipeline) = preferred_pipeline {
        let trimmed = raw_pipeline.trim();
        if trimmed.is_empty() || trimmed.eq_ignore_ascii_case("auto") {
            manifest.preferred_pipeline = None;
            if let Some(appid) = manifest.steam_app_id {
                manifest.runtime_profile = runtime_profile_for_pipeline(crate::mtsp::rules::resolve_pipeline(appid));
            }
        } else {
            let pipeline =
                crate::mtsp::engine::PipelineId::from_str_flexible(trimmed).ok_or("unknown preferred pipeline")?;
            let pipeline = if let Some(appid) = manifest.steam_app_id {
                crate::mtsp::rules::resolve_requested_pipeline(appid, Some(pipeline))
            } else {
                pipeline
            };
            manifest.preferred_pipeline = Some(pipeline_preference_id(pipeline).to_string());
            manifest.runtime_profile = runtime_profile_for_pipeline(pipeline);
        }
        manifest.arch = runtime_profile_definition(manifest.runtime_profile).arch;
        manifest.installed_components =
            rebuild_components_for_profile(&manifest.installed_components, manifest.runtime_profile);
    }
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    if let Some(appid) = manifest.steam_app_id {
        let pipeline =
            manifest_preferred_pipeline(&manifest).unwrap_or_else(|| crate::mtsp::rules::resolve_pipeline(appid));
        let _ = save_steam_compatdata(&manifest, pipeline);
    }
    Ok(manifest)
}

pub fn diagnose_bottle(id: &str) -> Result<BottleDiagnostic, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    refresh_manifest_launch_state(&mut manifest);
    let prefix = PathBuf::from(&manifest.prefix_path);
    manifest.installed_components = inspect_components_for_manifest(&manifest, &prefix, &manifest.installed_components);
    let log_ok = manifest.last_launch_log.as_ref().map(|p| Path::new(p).exists()).unwrap_or(false);
    refresh_manifest_runtime_views(&mut manifest);
    let detections = manifest.installed_app_detections.clone();
    let runtime_assets = manifest.runtime_assets.clone();
    if manifest.bottle_type == BottleType::Steam {
        let pipeline = runtime_profile_definition(manifest.runtime_profile).launch_pipeline;
        let _ = save_steam_compatdata(&manifest, pipeline);
    }

    let mut checks = Vec::new();
    checks.push(BottleCheck {
        id: "prefix".to_string(),
        ok: prefix.exists(),
        detail: prefix.to_string_lossy().to_string(),
    });
    checks.push(BottleCheck {
        id: "components".to_string(),
        ok: components_ready(&manifest.installed_components),
        detail: format!("{} tracked components", manifest.installed_components.len()),
    });
    checks.push(BottleCheck {
        id: "launch_log".to_string(),
        ok: log_ok,
        detail: manifest.last_launch_log.clone().unwrap_or_else(|| "no launch log recorded".to_string()),
    });
    if let Some(status) = manifest.last_launch_status.as_deref() {
        let detail = match (manifest.last_launch_pid, manifest.last_launch_finished_at.as_deref()) {
            (Some(pid), Some(finished_at)) => format!("pid {} {} at {}", pid, status, finished_at),
            (Some(pid), None) => format!("pid {} {}", pid, status),
            (None, _) => status.to_string(),
        };
        checks.push(BottleCheck { id: "launch_state".to_string(), ok: status != "failed", detail });
    }
    checks.push(BottleCheck {
        id: "app_detection".to_string(),
        ok: !detections.is_empty(),
        detail: format!("{} candidate apps detected", detections.len()),
    });
    if manifest.bottle_type == BottleType::Steam {
        checks.push(BottleCheck {
            id: "game_runtime_assets".to_string(),
            ok: !runtime_assets.is_empty(),
            detail: format!("{} game runtime assets tracked", runtime_assets.len()),
        });
        if let Some(appid) = manifest.steam_app_id {
            let compatdata_manifest = steam_compatdata_manifest_path(appid);
            let compatdata_logs = steam_compatdata_dir(appid).join("logs");
            checks.push(BottleCheck {
                id: "compatdata".to_string(),
                ok: compatdata_manifest.exists(),
                detail: compatdata_manifest.to_string_lossy().to_string(),
            });
            checks.push(BottleCheck {
                id: "compatdata_logs".to_string(),
                ok: compatdata_logs.exists(),
                detail: compatdata_logs.to_string_lossy().to_string(),
            });
        }
    }

    let actions = component_actions(&manifest.installed_components);
    let component_sources = component_source_policies_for_manifest(&manifest);
    let ready = checks
        .iter()
        .filter(|check| check.id != "app_detection" && check.id != "game_runtime_assets" && check.id != "launch_log")
        .all(|check| check.ok);
    let summary = if ready {
        "Bottle runtime checks passed".to_string()
    } else {
        "Bottle needs runtime preparation or repair".to_string()
    };
    manifest.health = if ready { BottleHealth::Ready } else { BottleHealth::NeedsRepair };
    manifest.installed_app_detections = detections;
    manifest.runtime_assets = runtime_assets;
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    if manifest.bottle_type == BottleType::Steam {
        let pipeline = runtime_profile_definition(manifest.runtime_profile).launch_pipeline;
        let _ = save_steam_compatdata(&manifest, pipeline);
    }

    Ok(BottleDiagnostic { id: id.to_string(), ready, summary, checks, actions, component_sources })
}

fn mark_manifest_launch_started(manifest: &mut BottleManifest, pid: u32, log_path: &Path) {
    manifest.last_launch_log = Some(log_path.to_string_lossy().to_string());
    manifest.last_launch_pid = Some(pid);
    manifest.last_launch_status = Some("running".to_string());
    manifest.last_launch_finished_at = None;
    manifest.updated_at = timestamp_secs();
}

fn refresh_manifest_launch_state(manifest: &mut BottleManifest) -> bool {
    if manifest.last_launch_status.as_deref() != Some("running") {
        return false;
    }
    let Some(pid) = manifest.last_launch_pid else {
        manifest.last_launch_status = Some("unknown".to_string());
        manifest.last_launch_finished_at = Some(timestamp_secs());
        return true;
    };
    let prefix = PathBuf::from(&manifest.prefix_path);
    if is_process_tree_active(pid) || should_wait_for_prefix_idle(manifest) && is_wine_prefix_busy(&prefix) {
        return false;
    }
    manifest.last_launch_status = Some("exited".to_string());
    manifest.last_launch_finished_at = Some(timestamp_secs());
    true
}

fn refresh_manifest_runtime_views(manifest: &mut BottleManifest) {
    let prefix = PathBuf::from(&manifest.prefix_path);
    manifest.installed_app_detections = match manifest.game_install_path.as_deref() {
        Some(path) if manifest.bottle_type == BottleType::Steam => detect_apps_in_game_dir(Path::new(path)),
        _ => detect_apps_in_prefix(&prefix),
    };
    if let Some(path) = manifest.game_install_path.as_deref() {
        manifest.runtime_assets = detect_game_runtime_assets(Path::new(path));
    }
    manifest.health =
        if manifest.installed_app_detections.is_empty() { BottleHealth::NeedsRepair } else { BottleHealth::Ready };
}

fn complete_bottle_launch(id: &str, pid: u32) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    if manifest.last_launch_pid != Some(pid) || manifest.last_launch_status.as_deref() != Some("running") {
        return Ok(manifest);
    }
    manifest.last_launch_status = Some("exited".to_string());
    manifest.last_launch_finished_at = Some(timestamp_secs());
    manifest.installed_components = inspect_components_for_manifest(
        &manifest,
        &PathBuf::from(&manifest.prefix_path),
        &manifest.installed_components,
    );
    refresh_manifest_runtime_views(&mut manifest);
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    Ok(manifest)
}

fn is_process_tree_active(pid: u32) -> bool {
    if crate::launch::is_process_active(pid as i32) {
        return true;
    }
    process_descendants(pid).into_iter().any(|child| crate::launch::is_process_active(child as i32))
}

struct BottleLaunchWatch {
    prefix: PathBuf,
    log_path: Option<PathBuf>,
    wait_for_prefix_idle: bool,
}

impl BottleLaunchWatch {
    fn for_bottle(id: &str) -> Option<Self> {
        let manifest = load_bottle(id).ok()?;
        Some(Self {
            prefix: PathBuf::from(&manifest.prefix_path),
            log_path: manifest.last_launch_log.as_deref().map(PathBuf::from),
            wait_for_prefix_idle: should_wait_for_prefix_idle(&manifest),
        })
    }

    fn spawn_prefix_wait(&self) -> Option<Child> {
        if !self.wait_for_prefix_idle {
            return None;
        }
        spawn_wineserver_wait(&self.prefix).ok()
    }

    fn log_size(&self) -> Option<u64> {
        self.log_path.as_ref().and_then(|path| fs::metadata(path).ok()).map(|meta| meta.len())
    }
}

fn should_wait_for_prefix_idle(manifest: &BottleManifest) -> bool {
    let steam_prefix = steam_launch_prefix();
    manifest.bottle_type != BottleType::Steam && Path::new(&manifest.prefix_path) != steam_prefix.as_path()
}

fn spawn_wineserver_wait(prefix: &Path) -> Result<Child, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wineserver = ms_root.join("bin").join("wineserver");
    if !wineserver.exists() {
        return Err("wineserver not found".into());
    }
    let mut cmd = Command::new(wineserver);
    cmd.arg("-w").env("WINEPREFIX", prefix.to_string_lossy().to_string()).stdout(Stdio::null()).stderr(Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    Ok(cmd.spawn()?)
}

fn is_wine_prefix_busy(prefix: &Path) -> bool {
    let Ok(mut child) = spawn_wineserver_wait(prefix) else {
        return false;
    };
    thread::sleep(Duration::from_millis(200));
    match child.try_wait() {
        Ok(Some(_)) => false,
        Ok(None) => {
            let _ = child.kill();
            let _ = child.wait();
            true
        },
        Err(_) => false,
    }
}

fn process_descendants(pid: u32) -> Vec<u32> {
    let mut descendants = Vec::new();
    let mut stack = vec![pid];
    while let Some(parent) = stack.pop() {
        let Ok(output) = Command::new("pgrep").arg("-P").arg(parent.to_string()).output() else {
            continue;
        };
        if !output.status.success() {
            continue;
        }
        for line in String::from_utf8_lossy(&output.stdout).lines() {
            if let Ok(child) = line.trim().parse::<u32>() {
                if !descendants.contains(&child) {
                    descendants.push(child);
                    stack.push(child);
                }
            }
        }
    }
    descendants
}

pub fn prepare_bottle(id: &str) -> Result<BottleDiagnostic, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    let prefix = PathBuf::from(&manifest.prefix_path);
    fs::create_dir_all(prefix.join("drive_c"))?;
    fs::create_dir_all(bottle_logs_dir(id))?;
    fs::create_dir_all(installer_payload_dir(id))?;
    manifest.installed_components = inspect_components_for_manifest(&manifest, &prefix, &manifest.installed_components);
    manifest.health = BottleHealth::NeedsRepair;
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;

    let system32 = prefix.join("drive_c/windows/system32");
    let seeded_marker = prefix.join("drive_c/metalsharp-post-wineboot-seeded");
    if system32.exists() && !seeded_marker.exists() {
        let seed_log = bottle_logs_dir(id).join("post-wineboot-seed.log");
        let _ = seed_post_wineboot_config(&prefix, &seed_log);
    }

    diagnose_bottle(id)
}

pub fn repair_component(
    id: &str,
    component_id: &str,
    dry_run: bool,
) -> Result<ComponentRepairReport, Box<dyn std::error::Error>> {
    if component_id.trim().is_empty() {
        return Err("component id required".into());
    }

    let mut manifest = load_bottle(id)?;
    let prefix = PathBuf::from(&manifest.prefix_path);
    fs::create_dir_all(&prefix)?;
    fs::create_dir_all(bottle_logs_dir(id))?;
    refresh_manifest_runtime_views(&mut manifest);
    manifest.installed_components = inspect_components_for_manifest(&manifest, &prefix, &manifest.installed_components);

    if component_id != "d3d12_agility"
        && manifest
            .installed_components
            .iter()
            .any(|component| component.id == component_id && component.state == ComponentState::Installed)
    {
        manifest.health = BottleHealth::Ready;
        manifest.updated_at = timestamp_secs();
        save_bottle(&manifest)?;
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: "already_installed".to_string(),
            detail: format!("{} is already present in this bottle", component_id),
            asset_path: None,
            log_path: None,
            pid: None,
        });
    }

    if matches!(component_id, "wine-mono" | "gecko") {
        let log_path = bottle_logs_dir(id).join(format!("component-{}-{}.log", component_id, timestamp_secs()));
        if dry_run {
            return Ok(ComponentRepairReport {
                id: component_id.to_string(),
                status: "builtin_available".to_string(),
                detail: format!("{} can be repaired with MetalSharp Wine bootstrapping", component_id),
                asset_path: None,
                log_path: Some(log_path.to_string_lossy().to_string()),
                pid: None,
            });
        }
        let pid = launch_wineboot_repair(&prefix, component_id, &log_path)?;
        mark_component_state(&mut manifest, component_id, ComponentState::NeedsRepair);
        mark_manifest_launch_started(&mut manifest, pid, &log_path);
        manifest.health = BottleHealth::NeedsRepair;
        save_bottle(&manifest)?;
        watch_bottle_launch(id.to_string(), pid);
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: "started".to_string(),
            detail: format!("Started {} bootstrap repair in bottle {}", component_id, id),
            asset_path: None,
            log_path: Some(log_path.to_string_lossy().to_string()),
            pid: Some(pid),
        });
    }

    if component_id == "corefonts" {
        let log_path = bottle_logs_dir(id).join(format!("component-{}-{}.log", component_id, timestamp_secs()));
        let sources = host_core_font_sources();
        if dry_run {
            let available = sources.len() >= 4;
            return Ok(ComponentRepairReport {
                id: component_id.to_string(),
                status: if available { "host_fonts_available" } else { "asset_missing" }.to_string(),
                detail: if available {
                    format!("{} host font files can be mapped into this bottle", sources.len())
                } else {
                    "No usable local host font set found for corefonts".to_string()
                },
                asset_path: None,
                log_path: Some(log_path.to_string_lossy().to_string()),
                pid: None,
            });
        }

        let installed = install_host_core_fonts(&prefix, &log_path)?;
        mark_component_state(
            &mut manifest,
            component_id,
            if installed { ComponentState::Installed } else { ComponentState::Missing },
        );
        manifest.health = if installed && components_ready(&manifest.installed_components) {
            BottleHealth::Ready
        } else {
            BottleHealth::NeedsRepair
        };
        manifest.updated_at = timestamp_secs();
        save_bottle(&manifest)?;
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: if installed { "installed" } else { "asset_missing" }.to_string(),
            detail: if installed {
                "Mapped host system fonts into the bottle font directory".to_string()
            } else {
                "No usable local host font set found for corefonts".to_string()
            },
            asset_path: None,
            log_path: Some(log_path.to_string_lossy().to_string()),
            pid: None,
        });
    }

    if component_id == "fna" {
        if dry_run {
            let state = inspect_fna_runtime_component().unwrap_or(ComponentState::Unknown);
            return Ok(ComponentRepairReport {
                id: component_id.to_string(),
                status: if matches!(state, ComponentState::Installed | ComponentState::NeedsRepair) {
                    "runtime_repair_available"
                } else {
                    "asset_missing"
                }
                .to_string(),
                detail: if matches!(state, ComponentState::Installed | ComponentState::NeedsRepair) {
                    "FNA native macOS shims can be rebuilt from MetalSharp runtime sources".to_string()
                } else {
                    "FNA runtime assemblies are not staged locally".to_string()
                },
                asset_path: None,
                log_path: None,
                pid: None,
            });
        }

        let repaired = crate::mtsp::launcher::repair_fna_native_runtime_shims()?;
        let state = inspect_fna_runtime_component().unwrap_or(ComponentState::Unknown);
        mark_component_state(&mut manifest, component_id, state);
        manifest.health = if components_ready(&manifest.installed_components) {
            BottleHealth::Ready
        } else {
            BottleHealth::NeedsRepair
        };
        manifest.updated_at = timestamp_secs();
        save_bottle(&manifest)?;
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: if state == ComponentState::Installed { "installed" } else { "needs_repair" }.to_string(),
            detail: format!("Repaired {} FNA native macOS shim(s) in MetalSharp runtime", repaired),
            asset_path: None,
            log_path: None,
            pid: None,
        });
    }

    if matches!(component_id, "gpu_vendor_stubs" | "gptk_amd_stub") {
        let home = dirs::home_dir().unwrap_or_default();
        let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
        let system32 = prefix.join("drive_c").join("windows").join("system32");
        fs::create_dir_all(&system32)?;

        let dxmt_dir = ms_root.join("lib").join("dxmt").join("x86_64-windows");
        let gptk_dir = ms_root.join("lib").join("gptk").join("x86_64-windows");
        let stub_files: &[(&str, bool)] = match component_id {
            "gpu_vendor_stubs" => &[("nvapi64.dll", false), ("nvngx.dll", false)],
            "gptk_amd_stub" => &[("atidxx64.dll", true)],
            _ => &[],
        };
        let mut copied = 0usize;
        for (stub, gptk_only) in stub_files {
            let dst = system32.join(stub);
            if dst.exists() {
                copied += 1;
                continue;
            }
            let src = if *gptk_only {
                if gptk_dir.join(stub).exists() {
                    gptk_dir.join(stub)
                } else {
                    continue;
                }
            } else if dxmt_dir.join(stub).exists() {
                dxmt_dir.join(stub)
            } else if gptk_dir.join(stub).exists() {
                gptk_dir.join(stub)
            } else {
                continue;
            };
            if fs::copy(&src, &dst).is_ok() {
                copied += 1;
            }
        }

        let installed = copied == stub_files.len();
        mark_component_state(
            &mut manifest,
            component_id,
            if installed { ComponentState::Installed } else { ComponentState::Missing },
        );
        manifest.health = if installed && components_ready(&manifest.installed_components) {
            BottleHealth::Ready
        } else {
            BottleHealth::NeedsRepair
        };
        manifest.updated_at = timestamp_secs();
        save_bottle(&manifest)?;
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: if installed { "installed" } else { "asset_missing" }.to_string(),
            detail: if installed {
                format!("Deployed {} GPU vendor stub(s) to prefix system32", copied)
            } else if component_id == "gptk_amd_stub" {
                "GPTK AMD vendor stub not found in GPTK runtime dirs".to_string()
            } else {
                "GPU vendor stubs not found in DXMT/GPTK runtime dirs".to_string()
            },
            asset_path: None,
            log_path: None,
            pid: None,
        });
    }

    if component_id == "d3d12_agility" {
        let home = dirs::home_dir().ok_or("no home dir")?;
        let appid = manifest.steam_app_id.ok_or("D3D12 Agility repair requires a Steam app id")?;
        let game_dir = manifest
            .game_install_path
            .as_deref()
            .map(PathBuf::from)
            .ok_or("D3D12 Agility repair requires a game install path")?;
        if dry_run {
            let state = if game_dir.is_dir() {
                inspect_d3d12_agility_component_for_manifest(&manifest).unwrap_or(ComponentState::Missing)
            } else {
                ComponentState::Missing
            };
            return Ok(ComponentRepairReport {
                id: component_id.to_string(),
                status: if game_dir.is_dir() { "runtime_repair_available" } else { "asset_missing" }.to_string(),
                detail: if state == ComponentState::Installed {
                    "D3D12 Agility SDK payload is already staged for this game".to_string()
                } else if game_dir.is_dir() {
                    "D3D12 Agility SDK payload can be downloaded, staged, and verified for this game".to_string()
                } else {
                    "Game install path is missing, so the Agility SDK payload cannot be staged".to_string()
                },
                asset_path: Some(game_dir.to_string_lossy().to_string()),
                log_path: None,
                pid: None,
            });
        }

        let report = crate::setup::stage_agility_sdk_for_game_report(appid, &game_dir, &home)?;
        let state = inspect_d3d12_agility_component_for_manifest(&manifest).unwrap_or(ComponentState::Missing);
        if state != ComponentState::Installed {
            return Err("D3D12 Agility repair did not verify after staging".into());
        }
        mark_component_state(&mut manifest, component_id, state);
        manifest.health = if components_ready(&manifest.installed_components) {
            BottleHealth::Ready
        } else {
            BottleHealth::NeedsRepair
        };
        manifest.updated_at = timestamp_secs();
        save_bottle(&manifest)?;
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: if state == ComponentState::Installed { "installed" } else { "needs_repair" }.to_string(),
            detail: if report.app_local_sidecars_skipped {
                format!(
                    "Downloaded and verified Microsoft.Direct3D.D3D12 {} shared x64 payload for this title ({})",
                    report.package_version,
                    report
                        .sdk_version
                        .map(|version| format!("D3D12SDKVersion {}", version))
                        .unwrap_or_else(|| "default D3D12SDKVersion".to_string())
                )
            } else {
                format!(
                    "Downloaded and staged Microsoft.Direct3D.D3D12 {} to {} Agility target(s) for {} ({} file writes)",
                    report.package_version,
                    report.target_dirs.len(),
                    report.sdk_path,
                    report.staged_files.len()
                )
            },
            asset_path: Some(game_dir.to_string_lossy().to_string()),
            log_path: None,
            pid: None,
        });
    }

    let Some(installer) = resolve_component_installer(component_id, manifest.arch)
        .or_else(|| resolve_game_runtime_asset_installer(&manifest, component_id))
    else {
        mark_component_state(&mut manifest, component_id, ComponentState::Missing);
        manifest.health = BottleHealth::NeedsRepair;
        manifest.updated_at = timestamp_secs();
        save_bottle(&manifest)?;
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: "asset_missing".to_string(),
            detail: format!("No local installer asset found for {}", component_id),
            asset_path: None,
            log_path: None,
            pid: None,
        });
    };

    let log_path = bottle_logs_dir(id).join(format!("component-{}-{}.log", component_id, timestamp_secs()));
    if dry_run {
        return Ok(ComponentRepairReport {
            id: component_id.to_string(),
            status: "asset_available".to_string(),
            detail: format!("Local installer asset found for {}", component_id),
            asset_path: Some(installer.path.to_string_lossy().to_string()),
            log_path: Some(log_path.to_string_lossy().to_string()),
            pid: None,
        });
    }

    if component_id == "dotnet40" {
        prepare_native_dotnet4_repair(&prefix, &log_path)?;
    }

    let pid = launch_component_installer(&prefix, &installer, &log_path)?;
    mark_component_state(&mut manifest, component_id, ComponentState::NeedsRepair);
    mark_manifest_launch_started(&mut manifest, pid, &log_path);
    manifest.health = BottleHealth::NeedsRepair;
    save_bottle(&manifest)?;
    watch_bottle_launch(id.to_string(), pid);

    Ok(ComponentRepairReport {
        id: component_id.to_string(),
        status: "started".to_string(),
        detail: format!("Started {} repair installer in bottle {}", component_id, id),
        asset_path: Some(installer.path.to_string_lossy().to_string()),
        log_path: Some(log_path.to_string_lossy().to_string()),
        pid: Some(pid),
    })
}

pub fn set_windows_version(id: &str, version: &str) -> Result<ComponentRepairReport, Box<dyn std::error::Error>> {
    let allowed = ["win7", "win10", "win11"];
    if !allowed.contains(&version) {
        return Err("windows version must be win7, win10, or win11".into());
    }

    let mut manifest = load_bottle(id)?;
    let prefix = PathBuf::from(&manifest.prefix_path);
    fs::create_dir_all(&prefix)?;
    fs::create_dir_all(bottle_logs_dir(id))?;
    let log_path = bottle_logs_dir(id).join(format!("windows-version-{}-{}.log", version, timestamp_secs()));
    let pid = run_wine_reg_set_windows_version(&prefix, version, &log_path)?;
    mark_component_state(&mut manifest, &windows_version_component_id(version), ComponentState::NeedsRepair);
    mark_manifest_launch_started(&mut manifest, pid, &log_path);
    manifest.health = BottleHealth::NeedsRepair;
    save_bottle(&manifest)?;
    watch_bottle_launch(id.to_string(), pid);

    Ok(ComponentRepairReport {
        id: windows_version_component_id(version),
        status: "started".to_string(),
        detail: format!("Started Windows version mode update to {}", version),
        asset_path: None,
        log_path: Some(log_path.to_string_lossy().to_string()),
        pid: Some(pid),
    })
}

pub fn handle_list_bottles() -> Value {
    match list_bottles() {
        Ok(bottles) => json!({"ok": true, "bottles": bottles}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_list_runtime_profiles() -> Value {
    json!({
        "ok": true,
        "profiles": runtime_profile_definitions(),
    })
}

pub fn handle_sync_steam_bottles() -> Value {
    match sync_steam_game_bottles() {
        Ok(bottles) => json!({"ok": true, "bottles": bottles, "count": bottles.len()}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_refresh_bottle(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match refresh_app_detections(id) {
        Ok(bottle) => json!({"ok": true, "bottle": bottle}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_get_bottle(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match load_bottle(id) {
        Ok(bottle) => json!({"ok": true, "bottle": bottle}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_diagnose_bottle(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match diagnose_bottle(id) {
        Ok(report) => json!({"ok": true, "report": report}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_prepare_bottle(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match prepare_bottle(id) {
        Ok(report) => json!({"ok": true, "report": report}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_repair_component(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let component = body.get("component").and_then(|v| v.as_str()).unwrap_or("");
    let dry_run = body.get("dryRun").and_then(|v| v.as_bool()).unwrap_or(false);
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    if component.is_empty() {
        return json!({"ok": false, "error": "component required"});
    }
    match repair_component(id, component, dry_run) {
        Ok(report) => json!({"ok": true, "repair": report}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_set_runtime_profile(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let profile = body.get("profile").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() || profile.is_empty() {
        return json!({"ok": false, "error": "id and profile required"});
    }
    let Some(profile) = parse_runtime_profile(profile) else {
        return json!({"ok": false, "error": "unknown runtime profile"});
    };
    match set_runtime_profile(id, profile) {
        Ok(bottle) => json!({"ok": true, "bottle": bottle}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_edit_bottle(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    let name = body.get("name").and_then(|v| v.as_str());
    let preferred_pipeline = body.get("preferredPipeline").and_then(|v| v.as_str());
    if name.is_none() && preferred_pipeline.is_none() {
        return json!({"ok": false, "error": "name or preferredPipeline required"});
    }
    match edit_bottle(id, name, preferred_pipeline) {
        Ok(bottle) => {
            let preflight = preflight_bottle_after_edit(&bottle);
            json!({"ok": true, "bottle": bottle, "preflight": preflight})
        },
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

fn preflight_bottle_after_edit(bottle: &BottleManifest) -> Value {
    let Some(appid) = bottle.steam_app_id else {
        return json!({"ok": true, "skipped": true, "reason": "not_steam_bottle"});
    };
    let pipeline = manifest_preferred_pipeline(bottle).unwrap_or_else(|| crate::mtsp::rules::resolve_pipeline(appid));
    match crate::mtsp::launcher::prepare_steam_pipeline_env(appid, pipeline) {
        Ok((_env, recipe)) => json!({
            "ok": true,
            "pipeline": pipeline_preference_id(pipeline),
            "deployed_dlls": recipe.dlls.len(),
        }),
        Err(e) => json!({
            "ok": false,
            "pipeline": pipeline_preference_id(pipeline),
            "error": e.to_string(),
        }),
    }
}

pub fn handle_set_windows_version(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let version = body.get("version").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    if version.is_empty() {
        return json!({"ok": false, "error": "version required"});
    }
    match set_windows_version(id, version) {
        Ok(report) => json!({"ok": true, "repair": report}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_apply_font_substitutions(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    let manifest = match load_bottle(id) {
        Ok(m) => m,
        Err(e) => return json!({"ok": false, "error": e.to_string()}),
    };
    let prefix = PathBuf::from(&manifest.prefix_path);
    let log_path = bottle_logs_dir(id).join("font-subs.log");
    match apply_font_substitutions(&prefix, &log_path) {
        Ok(pid) => json!({"ok": true, "pid": pid, "id": id}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_seed_post_wineboot(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    let manifest = match load_bottle(id) {
        Ok(m) => m,
        Err(e) => return json!({"ok": false, "error": e.to_string()}),
    };
    let prefix = PathBuf::from(&manifest.prefix_path);
    let log_path = bottle_logs_dir(id).join("post-wineboot.log");
    match seed_post_wineboot_config(&prefix, &log_path) {
        Ok(pid) => json!({"ok": true, "pid": pid, "id": id}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_verify_directx(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    let manifest = match load_bottle(id) {
        Ok(m) => m,
        Err(e) => return json!({"ok": false, "error": e.to_string()}),
    };
    let prefix = PathBuf::from(&manifest.prefix_path);
    let verification = verify_directx_jun2010(&prefix);
    json!({
        "ok": true,
        "id": id,
        "complete": verification.complete,
        "present_count": verification.present.len(),
        "missing_count": verification.missing.len(),
        "present": verification.present,
        "missing": verification.missing,
    })
}

pub fn handle_compatibility_matrix() -> Value {
    json!({
        "ok": true,
        "cases": compatibility_matrix(),
    })
}

pub fn handle_record_compatibility_case(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match record_compatibility_case(id, body) {
        Ok(cases) => json!({"ok": true, "cases": cases}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_redist_sources() -> Value {
    json!({
        "ok": true,
        "sources": redist_source_guides(),
    })
}

pub fn handle_steam_runtime_doctor(body: &serde_json::Map<String, Value>) -> Value {
    let appid = match parse_steam_runtime_doctor_appid(body) {
        Ok(appid) => appid,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let requested_pipeline =
        body.get("pipeline").and_then(|v| v.as_str()).and_then(crate::mtsp::engine::PipelineId::from_str_flexible);
    let pipeline = resolve_steam_pipeline_for_request(appid, requested_pipeline);
    let profile = runtime_profile_for_pipeline(pipeline);
    let dual = crate::scan::resolve_dual_game_dir(appid);
    let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
    let bottle = ensure_steam_game_bottle(appid, &name, dual.wine_dir.as_deref(), pipeline).ok();
    let prefix = bottle.as_ref().map(|b| PathBuf::from(&b.prefix_path)).unwrap_or_else(steam_launch_prefix);
    let default_components = default_components_for(profile);
    let components = bottle
        .as_ref()
        .map(|manifest| inspect_components_for_manifest(manifest, &prefix, &default_components))
        .unwrap_or_else(|| inspect_components(&prefix, &default_components));
    let actions = component_actions(&components);
    let compatdata = load_steam_compatdata(appid).ok();
    let recipe_deps = crate::mtsp::rules::game_missing_dependencies(appid, &prefix);
    let recipe = crate::mtsp::rules::get_game_recipe(appid);
    let missing_check_dlls = recipe
        .as_ref()
        .map(|r| {
            let system32 = prefix.join("drive_c/windows/system32");
            let syswow64 = prefix.join("drive_c/windows/syswow64");
            r.check_dlls
                .iter()
                .filter(|dll| !system32.join(dll).exists() && !syswow64.join(dll).exists())
                .cloned()
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    let recipe_env = recipe.as_ref().map(|r| r.env.clone()).unwrap_or_default();
    let report = SteamRuntimeDiagnostic {
        appid: Some(appid),
        bottle_id: bottle.as_ref().map(|b| b.id.clone()),
        bottle_name: bottle.as_ref().map(|b| b.name.clone()),
        preferred_pipeline: bottle.as_ref().and_then(|b| b.preferred_pipeline.clone()),
        pipeline,
        runtime_profile: profile,
        prefix_path: prefix.to_string_lossy().to_string(),
        game_install_path: bottle.as_ref().and_then(|b| b.game_install_path.clone()),
        runtime_assets: bottle.as_ref().map(|b| b.runtime_assets.clone()).unwrap_or_default(),
        components,
        actions,
        compatdata,
        recipe_missing_components: recipe_deps,
        recipe_name: recipe.map(|r| r.name),
        recipe_missing_dlls: missing_check_dlls,
        recipe_env,
        d3d12_sdk: if pipeline == crate::mtsp::engine::PipelineId::M12 {
            Some(crate::d3d12_runtime_doctor::latest_cached_report(appid).unwrap_or_else(|| {
                json!({
                    "sdkAvailability": crate::d3d12_runtime_doctor::sdk_availability(),
                    "summary": "No cached D3D12 SDK runtime doctor report for this appid yet.",
                })
            }))
        } else {
            None
        },
    };
    json!({"ok": true, "report": report})
}

pub fn handle_steam_compatdata(body: &serde_json::Map<String, Value>) -> Value {
    let appid = match parse_steam_runtime_doctor_appid(body) {
        Ok(appid) => appid,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let requested_pipeline =
        body.get("pipeline").and_then(|v| v.as_str()).and_then(crate::mtsp::engine::PipelineId::from_str_flexible);
    let pipeline = resolve_steam_pipeline_for_request(appid, requested_pipeline);
    let dual = crate::scan::resolve_dual_game_dir(appid);
    let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
    match ensure_steam_game_bottle(appid, &name, dual.wine_dir.as_deref(), pipeline)
        .and_then(|manifest| save_steam_compatdata(&manifest, pipeline))
    {
        Ok(record) => json!({"ok": true, "compatdata": record}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_install_recipe_deps(body: &serde_json::Map<String, Value>) -> Value {
    let appid = match parse_steam_runtime_doctor_appid(body) {
        Ok(appid) => appid,
        Err(error) => return json!({"ok": false, "error": error}),
    };
    let pipeline = resolve_steam_pipeline_for_request(appid, None);
    let dual = crate::scan::resolve_dual_game_dir(appid);
    let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
    let manifest = match ensure_steam_game_bottle(appid, &name, dual.wine_dir.as_deref(), pipeline) {
        Ok(m) => m,
        Err(e) => return json!({"ok": false, "error": e.to_string()}),
    };
    let prefix = PathBuf::from(&manifest.prefix_path);
    let missing = crate::mtsp::rules::game_missing_dependencies(appid, &prefix);
    if missing.is_empty() {
        return json!({"ok": true, "appid": appid, "installed": [], "message": "all recipe dependencies satisfied"});
    }

    let mut reports = Vec::new();
    let mut errors = Vec::new();
    for component_id in &missing {
        match repair_component(&manifest.id, component_id, false) {
            Ok(report) => {
                reports.push(report);
            },
            Err(e) => {
                errors.push(format!("{}: {}", component_id, e));
            },
        }
    }

    let mut manifest = match load_bottle(&manifest.id) {
        Ok(m) => m,
        Err(_) => return json!({"ok": false, "appid": appid, "installed": reports, "errors": errors}),
    };
    manifest.installed_components = inspect_components_for_manifest(&manifest, &prefix, &manifest.installed_components);
    let _ = save_bottle(&manifest);

    let still_missing = crate::mtsp::rules::game_missing_dependencies(appid, &prefix);
    let actually_installed = missing.iter().filter(|c| !still_missing.contains(c)).count();

    if errors.is_empty() && still_missing.is_empty() {
        json!({"ok": true, "appid": appid, "installed": reports, "actually_installed": actually_installed, "message": format!("installed {} components", actually_installed)})
    } else if errors.is_empty() {
        json!({"ok": false, "appid": appid, "installed": reports, "actually_installed": actually_installed, "still_missing": still_missing, "message": format!("{} of {} components installed, {} still missing", actually_installed, missing.len(), still_missing.len())})
    } else {
        json!({"ok": false, "appid": appid, "installed": reports, "actually_installed": actually_installed, "still_missing": still_missing, "errors": errors})
    }
}

fn parse_steam_runtime_doctor_appid(body: &serde_json::Map<String, Value>) -> Result<u32, &'static str> {
    let Some(value) = body.get("appid") else {
        return Err("appid required");
    };
    let Some(raw) = value.as_u64() else {
        return Err("appid must be a positive numeric Steam appid");
    };
    let appid = u32::try_from(raw).map_err(|_| "appid out of range")?;
    if appid == 0 {
        return Err("appid must be greater than zero");
    }
    Ok(appid)
}

fn runtime_profile_definitions() -> Vec<RuntimeProfileDefinition> {
    [
        RuntimeProfile::Plain,
        RuntimeProfile::Launcher,
        RuntimeProfile::GameInstall,
        RuntimeProfile::M9,
        RuntimeProfile::M10,
        RuntimeProfile::M11,
        RuntimeProfile::M12,
        RuntimeProfile::M13,
        RuntimeProfile::Dotnet,
        RuntimeProfile::Win32Dotnet,
        RuntimeProfile::Webview,
        RuntimeProfile::JavaLauncher,
        RuntimeProfile::FnaArm64,
        RuntimeProfile::FnaX86,
    ]
    .into_iter()
    .map(runtime_profile_definition)
    .collect()
}

fn runtime_profile_definition(profile: RuntimeProfile) -> RuntimeProfileDefinition {
    let (name, arch, wineboot, components, launch_pipeline) = match profile {
        RuntimeProfile::Plain => {
            ("Plain Wine", BottleArch::Wow64, true, &[][..], crate::mtsp::engine::PipelineId::WineBare)
        },
        RuntimeProfile::Launcher => (
            "Launcher",
            BottleArch::Wow64,
            true,
            &["gecko", "vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::GameInstall => (
            "Game Installer",
            BottleArch::Wow64,
            true,
            &["vcrun2019", "vcrun2013", "directx_jun2010", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::M9 => (
            "D3D9 Metal",
            BottleArch::Wow64,
            true,
            &["d3d9", "vcrun2019", "directx_jun2010"][..],
            crate::mtsp::engine::PipelineId::M9,
        ),
        RuntimeProfile::M10 => (
            "D3D10 Metal",
            BottleArch::Wow64,
            true,
            &["d3d10", "d3d10_1", "dxgi", "vcrun2019"][..],
            crate::mtsp::engine::PipelineId::M10,
        ),
        RuntimeProfile::M11 => (
            "D3D11 Metal",
            BottleArch::Win64,
            true,
            &["d3d11", "dxgi", "vcrun2019"][..],
            crate::mtsp::engine::PipelineId::M11,
        ),
        RuntimeProfile::M12 => (
            "D3D12 Metal",
            BottleArch::Win64,
            true,
            &["d3d12", "d3d12_agility", "d3d11", "dxgi", "vcrun2019", "gpu_vendor_stubs", "corefonts"][..],
            crate::mtsp::engine::PipelineId::M12,
        ),
        RuntimeProfile::M13 => (
            "GPTK D3DMetal",
            BottleArch::Win64,
            true,
            &["d3d11", "d3d12", "dxgi", "d3d10", "vcrun2019", "gpu_vendor_stubs", "gptk_amd_stub"][..],
            crate::mtsp::engine::PipelineId::M13,
        ),
        RuntimeProfile::Dotnet => (
            ".NET",
            BottleArch::Win64,
            true,
            &["wine-mono", "gecko", "dotnet48", "vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::Win32Dotnet => (
            "32-bit .NET",
            BottleArch::Win32,
            true,
            &["wine-mono", "gecko", "dotnet48", "vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::M9,
        ),
        RuntimeProfile::Webview => (
            "WebView",
            BottleArch::Wow64,
            true,
            &["gecko", "webview2", "dotnet48", "vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::JavaLauncher => (
            "Java Launcher",
            BottleArch::Wow64,
            true,
            &["vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::FnaArm64 => (
            "FNA / Mono ARM64",
            BottleArch::Win64,
            false,
            &["mono-arm64", "fna", "xna"][..],
            crate::mtsp::engine::PipelineId::FnaArm64,
        ),
        RuntimeProfile::FnaX86 => (
            "FNA / Mono x86_64",
            BottleArch::Win64,
            false,
            &["mono-x86", "fna", "xna"][..],
            crate::mtsp::engine::PipelineId::FnaArm64,
        ),
    };
    RuntimeProfileDefinition {
        id: profile,
        name,
        arch,
        wineboot,
        components: components.iter().map(|component| (*component).to_string()).collect(),
        launch_pipeline,
        mono_runtime: mono_runtime_definition(profile),
    }
}

fn mono_runtime_definition(profile: RuntimeProfile) -> Option<MonoRuntimeDefinition> {
    let home = dirs::home_dir().unwrap_or_default();
    match profile {
        RuntimeProfile::FnaArm64 => Some(MonoRuntimeDefinition {
            id: "mono-arm64",
            binary_path: crate::platform::metalsharp_home_dir_for(&home).join("runtime/mono-arm64/bin/mono").to_string_lossy().to_string(),
            expected_arch: "arm64",
            known_version: "6.14.1",
            config_path: Some("configs/terraria-mono.config"),
            launch_wrapper: "native_mono_fna",
            notes: "Used by the Terraria-style FNA lane that worked through native macOS Mono plus dllmaps and shims.",
        }),
        RuntimeProfile::FnaX86 => Some(MonoRuntimeDefinition {
            id: "mono-x86",
            binary_path: crate::platform::metalsharp_home_dir_for(&home).join("runtime/mono-x86/bin/mono").to_string_lossy().to_string(),
            expected_arch: "x86_64",
            known_version: "6.12.0.122",
            config_path: Some("configs/celeste-x86-mono.config"),
            launch_wrapper: "arch -x86_64 native_mono_fna",
            notes: "Used by the Celeste-style legacy lane where x86_64 Mono 6.12 and dllmaps avoid newer ARM64-only assumptions.",
        }),
        _ => None,
    }
}

fn default_components_for(profile: RuntimeProfile) -> Vec<RuntimeComponent> {
    runtime_profile_definition(profile)
        .components
        .into_iter()
        .map(|id| RuntimeComponent { id, state: ComponentState::Unknown })
        .collect()
}

fn runtime_profile_for_pipeline(pipeline: crate::mtsp::engine::PipelineId) -> RuntimeProfile {
    match pipeline {
        crate::mtsp::engine::PipelineId::Dxmt => RuntimeProfile::GameInstall,
        crate::mtsp::engine::PipelineId::M9 => RuntimeProfile::M9,
        crate::mtsp::engine::PipelineId::M10 => RuntimeProfile::M10,
        crate::mtsp::engine::PipelineId::M11 => RuntimeProfile::M11,
        crate::mtsp::engine::PipelineId::M12 => RuntimeProfile::M12,
        crate::mtsp::engine::PipelineId::M13 => RuntimeProfile::M13,
        crate::mtsp::engine::PipelineId::FnaArm64 => RuntimeProfile::FnaArm64,
        _ => RuntimeProfile::Plain,
    }
}

fn parse_runtime_profile(value: &str) -> Option<RuntimeProfile> {
    match value.to_ascii_lowercase().replace('-', "_").as_str() {
        "plain" => Some(RuntimeProfile::Plain),
        "launcher" => Some(RuntimeProfile::Launcher),
        "game_install" | "gameinstall" => Some(RuntimeProfile::GameInstall),
        "m9" => Some(RuntimeProfile::M9),
        "m10" => Some(RuntimeProfile::M10),
        "m11" => Some(RuntimeProfile::M11),
        "m12" => Some(RuntimeProfile::M12),
        "m13" | "gptk" | "d3dmetal" => Some(RuntimeProfile::M13),
        "dotnet" => Some(RuntimeProfile::Dotnet),
        "win32_dotnet" | "win32dotnet" => Some(RuntimeProfile::Win32Dotnet),
        "webview" => Some(RuntimeProfile::Webview),
        "java_launcher" | "javalauncher" => Some(RuntimeProfile::JavaLauncher),
        "fna_arm64" | "xna_fna_arm64" | "native_mono_arm64" => Some(RuntimeProfile::FnaArm64),
        "fna_x86" | "xna_fna_x86" | "native_mono_x86" | "mono_x86" => Some(RuntimeProfile::FnaX86),
        _ => None,
    }
}

fn known_launcher_recipes() -> &'static [KnownLauncherRecipe] {
    &[
        KnownLauncherRecipe {
            id: "minecraft",
            label: "Minecraft Launcher",
            tokens: &["minecraft", "minecraftlauncher", "minecraft installer"],
            installer_kind: InstallerKind::Java,
            runtime_profile: RuntimeProfile::JavaLauncher,
            forced_pipeline: None,
        },
        KnownLauncherRecipe {
            id: "ea_app",
            label: "EA App",
            tokens: &["ea app", "eaappinstaller", "eadesktop", "electronic arts", "originthinsetup", "origin setup"],
            installer_kind: InstallerKind::Webview,
            runtime_profile: RuntimeProfile::Webview,
            forced_pipeline: None,
        },
        KnownLauncherRecipe {
            id: "ubisoft_connect",
            label: "Ubisoft Connect",
            tokens: &["ubisoft connect", "ubisoftconnect", "uplay", "ubisoftgamelauncher"],
            installer_kind: InstallerKind::Webview,
            runtime_profile: RuntimeProfile::Webview,
            forced_pipeline: None,
        },
        KnownLauncherRecipe {
            id: "battle_net",
            label: "Battle.net",
            tokens: &["battle.net", "battlenet", "battle net", "blizzard app", "blizzard launcher"],
            installer_kind: InstallerKind::Webview,
            runtime_profile: RuntimeProfile::Webview,
            forced_pipeline: None,
        },
        KnownLauncherRecipe {
            id: "epic_games",
            label: "Epic Games Launcher",
            tokens: &["epic games launcher", "epicgameslauncher", "epic installer", "epic online services"],
            installer_kind: InstallerKind::Webview,
            runtime_profile: RuntimeProfile::Webview,
            forced_pipeline: None,
        },
        KnownLauncherRecipe {
            id: "rockstar",
            label: "Rockstar Games Launcher",
            tokens: &[
                "rockstar games launcher",
                "rockstar-games-launcher",
                "rockstargameslauncher",
                "social club",
                "rockstar social club",
            ],
            installer_kind: InstallerKind::Webview,
            runtime_profile: RuntimeProfile::Webview,
            forced_pipeline: None,
        },
        KnownLauncherRecipe {
            id: "gog_galaxy",
            label: "GOG Galaxy",
            tokens: &["gog galaxy", "goggalaxy", "galaxyclient", "gog_galaxy"],
            installer_kind: InstallerKind::Electron,
            runtime_profile: RuntimeProfile::Launcher,
            forced_pipeline: None,
        },
    ]
}

fn known_launcher_recipe(source_installer: &Path, lower_strings: &[String]) -> Option<&'static KnownLauncherRecipe> {
    let lower_name =
        source_installer.file_name().map(|name| name.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    known_launcher_recipes().iter().find(|recipe| {
        recipe
            .tokens
            .iter()
            .any(|token| lower_name.contains(token) || lower_strings.iter().any(|string| string.contains(token)))
    })
}

fn classify_installer_kind(source_installer: &Path, lower_strings: &[String], is_msi: bool) -> InstallerKind {
    if is_msi {
        return InstallerKind::Msi;
    }
    if lower_strings.iter().any(|s| s.contains("squirrel") || s.contains("update.exe") || s.contains("releasify")) {
        InstallerKind::Squirrel
    } else if lower_strings.iter().any(|s| s.contains("electron") || s.contains("app.asar")) {
        InstallerKind::Electron
    } else if lower_strings.iter().any(|s| s.contains("webview2") || s.contains("edgeupdate")) {
        InstallerKind::Webview
    } else if lower_strings.iter().any(|s| s.contains("unityplayer.dll") || s.contains("unitycrashhandler")) {
        InstallerKind::Unity
    } else if lower_strings.iter().any(|s| s.contains("inno setup") || s.contains("innosetup")) {
        InstallerKind::Inno
    } else if lower_strings.iter().any(|s| s.contains("nullsoft") || s.contains("nsis")) {
        InstallerKind::Nsis
    } else if lower_strings.iter().any(|s| s.contains("wixbundle") || s.contains("wix toolset") || s.contains("burn")) {
        InstallerKind::Wix
    } else if lower_strings.iter().any(|s| s.contains("java") || s.contains("jre") || s.contains("jdk")) {
        InstallerKind::Java
    } else if source_installer.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("exe")).unwrap_or(false)
    {
        InstallerKind::Exe
    } else {
        InstallerKind::Unknown
    }
}

fn merge_components(mut existing: Vec<RuntimeComponent>, required: Vec<RuntimeComponent>) -> Vec<RuntimeComponent> {
    for component in required {
        if !existing.iter().any(|c| c.id == component.id) {
            existing.push(component);
        }
    }
    existing.sort_by(|a, b| a.id.cmp(&b.id));
    existing
}

fn infer_components_from_runtime_assets(assets: &[BottleRuntimeAsset]) -> Vec<RuntimeComponent> {
    let mut ids = HashSet::new();
    for asset in assets {
        match asset.kind.as_str() {
            "vcredist" => {
                ids.insert("vcrun2019".to_string());
            },
            "vcredist_2013" => {
                ids.insert("vcrun2013".to_string());
            },
            "vcredist_2010" => {
                ids.insert("vcrun2010".to_string());
            },
            "directx" => {
                ids.insert("directx_jun2010".to_string());
            },
            "dotnet" => {
                ids.insert("dotnet48".to_string());
            },
            "webview2" => {
                ids.insert("webview2".to_string());
            },
            "openal" => {
                ids.insert("openal".to_string());
            },
            "xna" => {
                ids.insert("xna".to_string());
            },
            "physx" => {
                ids.insert("physx".to_string());
            },
            "easyanticheat" | "easyanticheat_eos" => {
                ids.insert("easyanticheat_eos".to_string());
            },
            "battleye" => {
                ids.insert("battleye".to_string());
            },
            "installscript" => {
                for id in components_from_installscript(Path::new(&asset.source_path)) {
                    ids.insert(id);
                }
            },
            _ => {},
        }
    }
    let mut components =
        ids.into_iter().map(|id| RuntimeComponent { id, state: ComponentState::Unknown }).collect::<Vec<_>>();
    components.sort_by(|a, b| a.id.cmp(&b.id));
    components
}

fn components_from_installscript(path: &Path) -> Vec<String> {
    let Ok(data) = fs::read_to_string(path) else {
        return Vec::new();
    };
    let lower = data.to_ascii_lowercase();
    let mut ids = Vec::new();
    fn maybe_add(ids: &mut Vec<String>, lower: &str, id: &str, needles: &[&str]) {
        if needles.iter().any(|needle| lower.contains(needle)) && !ids.iter().any(|existing| existing == id) {
            ids.push(id.to_string());
        }
    }
    maybe_add(
        &mut ids,
        &lower,
        "vcrun2010",
        &["vcredist_2010", "msvcr100", "msvcp100", "visual c++ 2010", "vcredist/2010"],
    );
    maybe_add(&mut ids, &lower, "vcrun2013", &["vcredist_2013", "msvcr120", "msvcp120", "visual c++ 2013"]);
    if !ids.iter().any(|id| id == "vcrun2010" || id == "vcrun2013") {
        maybe_add(&mut ids, &lower, "vcrun2019", &["vcredist", "vc_redist", "visual c++", "vc runtime"]);
    }
    maybe_add(
        &mut ids,
        &lower,
        "directx_jun2010",
        &[
            "directx",
            "dxsetup",
            "d3dx9_43",
            "d3dx10_43",
            "d3dx11_43",
            "xinput1_3",
            "xaudio2_7",
            "x3daudio1_7",
            "d3dcompiler_43",
        ],
    );
    maybe_add(&mut ids, &lower, "dotnet48", &["dotnet", ".net framework", "ndp48", "ndp472", "ndp462", "ndp452"]);
    maybe_add(&mut ids, &lower, "webview2", &["webview2", "edgewebview"]);
    maybe_add(&mut ids, &lower, "openal", &["openal", "oalinst"]);
    maybe_add(&mut ids, &lower, "xna", &["xnafx", "xna framework", "xnafx40"]);
    maybe_add(&mut ids, &lower, "physx", &["physx", "nvidia physx"]);
    ids.sort();
    ids
}

fn rebuild_components_for_profile(existing: &[RuntimeComponent], profile: RuntimeProfile) -> Vec<RuntimeComponent> {
    let mut rebuilt = default_components_for(profile)
        .into_iter()
        .map(|mut required| {
            if let Some(current) = existing.iter().find(|component| component.id == required.id) {
                required.state = current.state;
            }
            required
        })
        .collect::<Vec<_>>();
    rebuilt.sort_by(|a, b| a.id.cmp(&b.id));
    rebuilt
}

fn mark_component_state(manifest: &mut BottleManifest, component_id: &str, state: ComponentState) {
    if let Some(component) = manifest.installed_components.iter_mut().find(|component| component.id == component_id) {
        component.state = state;
    } else {
        manifest.installed_components.push(RuntimeComponent { id: component_id.to_string(), state });
        manifest.installed_components.sort_by(|a, b| a.id.cmp(&b.id));
    }
}

fn inspect_components(prefix: &Path, components: &[RuntimeComponent]) -> Vec<RuntimeComponent> {
    components
        .iter()
        .map(|component| RuntimeComponent {
            id: component.id.clone(),
            state: inspect_component_state(prefix, &component.id, component.state),
        })
        .collect()
}

fn inspect_components_for_manifest(
    manifest: &BottleManifest,
    prefix: &Path,
    components: &[RuntimeComponent],
) -> Vec<RuntimeComponent> {
    components
        .iter()
        .map(|component| {
            let fallback = inspect_component_state(prefix, &component.id, component.state);
            let state = if component.id == "d3d12_agility" {
                inspect_d3d12_agility_component_for_manifest(manifest).unwrap_or(fallback)
            } else {
                fallback
            };
            RuntimeComponent { id: component.id.clone(), state }
        })
        .collect()
}

pub fn verify_directx_jun2010(prefix: &Path) -> DirectXVerification {
    let system32 = prefix.join("drive_c/windows/system32");
    let syswow64 = prefix.join("drive_c/windows/syswow64");
    let has = |dll: &str| -> bool { system32.join(dll).exists() || syswow64.join(dll).exists() };

    let mut present = Vec::new();
    let mut missing = Vec::new();

    let expected = [
        ("d3dx9_24.dll", "D3DX9"),
        ("d3dx9_25.dll", "D3DX9"),
        ("d3dx9_26.dll", "D3DX9"),
        ("d3dx9_27.dll", "D3DX9"),
        ("d3dx9_28.dll", "D3DX9"),
        ("d3dx9_29.dll", "D3DX9"),
        ("d3dx9_30.dll", "D3DX9"),
        ("d3dx9_31.dll", "D3DX9"),
        ("d3dx9_32.dll", "D3DX9"),
        ("d3dx9_33.dll", "D3DX9"),
        ("d3dx9_34.dll", "D3DX9"),
        ("d3dx9_35.dll", "D3DX9"),
        ("d3dx9_36.dll", "D3DX9"),
        ("d3dx9_37.dll", "D3DX9"),
        ("d3dx9_38.dll", "D3DX9"),
        ("d3dx9_39.dll", "D3DX9"),
        ("d3dx9_40.dll", "D3DX9"),
        ("d3dx9_41.dll", "D3DX9"),
        ("d3dx9_42.dll", "D3DX9"),
        ("d3dx9_43.dll", "D3DX9"),
        ("d3dx10_33.dll", "D3DX10"),
        ("d3dx10_34.dll", "D3DX10"),
        ("d3dx10_35.dll", "D3DX10"),
        ("d3dx10_36.dll", "D3DX10"),
        ("d3dx10_37.dll", "D3DX10"),
        ("d3dx10_38.dll", "D3DX10"),
        ("d3dx10_39.dll", "D3DX10"),
        ("d3dx10_40.dll", "D3DX10"),
        ("d3dx10_41.dll", "D3DX10"),
        ("d3dx10_42.dll", "D3DX10"),
        ("d3dx10_43.dll", "D3DX10"),
        ("d3dx11_42.dll", "D3DX11"),
        ("d3dx11_43.dll", "D3DX11"),
        ("D3DCompiler_42.dll", "D3DCompiler"),
        ("D3DCompiler_43.dll", "D3DCompiler"),
        ("xinput1_3.dll", "XInput"),
        ("xaudio2_7.dll", "XAudio"),
        ("x3daudio1_7.dll", "X3DAudio"),
        ("XAPOFX1_5.dll", "XAPOFX"),
    ];

    for (dll, _family) in &expected {
        if has(dll) {
            present.push(dll.to_string());
        } else {
            missing.push(dll.to_string());
        }
    }

    let complete = missing.is_empty();
    DirectXVerification { present, missing, complete }
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct DirectXVerification {
    pub present: Vec<String>,
    pub missing: Vec<String>,
    pub complete: bool,
}

fn inspect_component_state(prefix: &Path, id: &str, fallback: ComponentState) -> ComponentState {
    let drive_c = prefix.join("drive_c");
    let windows = drive_c.join("windows");
    let system32 = windows.join("system32");
    let syswow64 = windows.join("syswow64");
    if let Some(version) = id.strip_prefix(WINDOWS_VERSION_COMPONENT_PREFIX) {
        return inspect_windows_version_component(prefix, version).unwrap_or(fallback);
    }

    match id {
        "wine-mono" => {
            if windows.join("mono").exists() {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "mono-arm64" => inspect_host_mono_component("mono-arm64").unwrap_or(fallback),
        "mono-x86" => inspect_host_mono_component("mono-x86").unwrap_or(fallback),
        "fna" => inspect_fna_runtime_component().unwrap_or(fallback),
        "d3d12_agility" => inspect_d3d12_agility_component().unwrap_or(fallback),
        "gecko" => {
            if windows.join("gecko").exists() || system32.join("gecko").exists() || syswow64.join("gecko").exists() {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "dotnet40" | "dotnet48" => {
            let framework = windows.join("Microsoft.NET").join("Framework").join("v4.0.30319");
            let framework64 = windows.join("Microsoft.NET").join("Framework64").join("v4.0.30319");
            if framework.join("clr.dll").exists() || framework64.join("clr.dll").exists() {
                ComponentState::Installed
            } else if framework.exists() || framework64.exists() {
                ComponentState::NeedsRepair
            } else {
                ComponentState::Missing
            }
        },
        "vcrun2019" => {
            let has = |dll: &str| -> bool { system32.join(dll).exists() || syswow64.join(dll).exists() };
            let core = ["vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"];
            let core_count = core.iter().filter(|dll| has(dll)).count();
            if core_count == core.len() {
                ComponentState::Installed
            } else if core_count > 0 {
                ComponentState::NeedsRepair
            } else {
                ComponentState::Missing
            }
        },
        "vcrun2010" => {
            let has = |dll: &str| -> bool { system32.join(dll).exists() || syswow64.join(dll).exists() };
            let core = ["msvcr100.dll", "msvcp100.dll"];
            let core_count = core.iter().filter(|dll| has(dll)).count();
            if core_count == core.len() {
                ComponentState::Installed
            } else if core_count > 0 {
                ComponentState::NeedsRepair
            } else {
                ComponentState::Missing
            }
        },
        "vcrun2013" => {
            let has = |dll: &str| -> bool { system32.join(dll).exists() || syswow64.join(dll).exists() };
            let core = ["msvcr120.dll", "msvcp120.dll"];
            let core_count = core.iter().filter(|dll| has(dll)).count();
            if core_count == core.len() {
                ComponentState::Installed
            } else if core_count > 0 {
                ComponentState::NeedsRepair
            } else {
                ComponentState::Missing
            }
        },
        "corefonts" => {
            if core_fonts_installed(&windows.join("Fonts")) {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "directx_jun2010" => {
            let has = |dll: &str| -> bool { system32.join(dll).exists() || syswow64.join(dll).exists() };
            let core_dlls = ["d3dx9_43.dll", "d3dx10_43.dll", "d3dx11_43.dll", "xinput1_3.dll"];
            let core_ok = core_dlls.iter().all(|dll| has(dll));
            if core_ok {
                ComponentState::Installed
            } else if has("d3dx9_43.dll") || has("xinput1_3.dll") {
                ComponentState::NeedsRepair
            } else {
                ComponentState::Missing
            }
        },
        "d3d9" | "d3d10" | "d3d10_1" | "d3d11" | "d3d12" | "dxgi" => {
            inspect_runtime_dll_component(id).unwrap_or(fallback)
        },
        "webview2" => {
            if drive_c.join("Program Files (x86)").join("Microsoft").join("EdgeWebView").exists()
                || drive_c.join("Program Files").join("Microsoft").join("EdgeWebView").exists()
            {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "openal" => {
            if system32.join("OpenAL32.dll").exists() || syswow64.join("OpenAL32.dll").exists() {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "xna" => {
            if xna_framework_installed(prefix) {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "physx" => {
            if system32.join("PhysXLoader.dll").exists()
                || syswow64.join("PhysXLoader.dll").exists()
                || drive_c.join("Program Files (x86)").join("NVIDIA Corporation").join("PhysX").exists()
            {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "gpu_vendor_stubs" => {
            let has = |dll: &str| -> bool { system32.join(dll).exists() };
            let nv_ok = has("nvapi64.dll") && has("nvngx.dll");
            let nv_partial = has("nvapi64.dll") || has("nvngx.dll");
            if nv_ok {
                ComponentState::Installed
            } else if nv_partial {
                ComponentState::NeedsRepair
            } else {
                ComponentState::Missing
            }
        },
        "gptk_amd_stub" => {
            if system32.join("atidxx64.dll").exists() {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "easyanticheat_eos" => {
            if drive_c.join("Program Files (x86)").join("EasyAntiCheat_EOS").exists()
                || drive_c.join("Program Files").join("EasyAntiCheat_EOS").exists()
            {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "battleye" => {
            if drive_c.join("Program Files (x86)").join("Common Files").join("BattlEye").exists()
                || drive_c.join("Program Files").join("Common Files").join("BattlEye").exists()
            {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        _ => fallback,
    }
}

fn inspect_host_mono_component(runtime_id: &str) -> Option<ComponentState> {
    let home = dirs::home_dir()?;
    let mono =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join(runtime_id).join("bin").join("mono");
    Some(if mono.exists() { ComponentState::Installed } else { ComponentState::Missing })
}

fn inspect_fna_runtime_component() -> Option<ComponentState> {
    let home = dirs::home_dir()?;
    let runtime = crate::platform::metalsharp_home_dir_for(&home).join("runtime");
    let required = [
        runtime.join("fna").join("FNA.dll"),
        runtime.join("shims").join("libFNA3D.dylib"),
        runtime.join("shims").join("libSDL3.dylib"),
        runtime.join("shims").join("libkernel32.dylib"),
        runtime.join("shims").join("libuser32.dylib"),
        runtime.join("shims").join("libCarbon.dylib"),
        runtime.join("shims").join("libmetalsharp_carbon_interpose.dylib"),
    ];
    let present = required.iter().filter(|path| path.exists()).count();
    Some(if present == required.len() {
        ComponentState::Installed
    } else if present > 0 {
        ComponentState::NeedsRepair
    } else {
        ComponentState::Missing
    })
}

fn inspect_d3d12_agility_component() -> Option<ComponentState> {
    let home = dirs::home_dir()?;
    let root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("redist").join("agility");
    let known_versions = ["1.614.1", "1.615.1", "1.619.3"];
    let found = known_versions.iter().any(|version| {
        let bin = root.join(version).join("build").join("native").join("bin").join("x64");
        bin.join("D3D12Core.dll").exists() && bin.join("d3d12SDKLayers.dll").exists()
    });
    Some(if found { ComponentState::Installed } else { ComponentState::Missing })
}

fn inspect_d3d12_agility_component_for_manifest(manifest: &BottleManifest) -> Option<ComponentState> {
    let home = dirs::home_dir()?;
    let appid = manifest.steam_app_id?;
    let game_dir = manifest.game_install_path.as_deref().map(PathBuf::from)?;
    if !game_dir.is_dir() {
        return Some(ComponentState::Missing);
    }
    match crate::setup::inspect_agility_sdk_for_game(appid, &game_dir, &home) {
        Ok(inspection) if inspection.installed() => Some(ComponentState::Installed),
        Ok(inspection) if inspection.partially_staged() => Some(ComponentState::NeedsRepair),
        Ok(_) => Some(ComponentState::Missing),
        Err(_) => Some(ComponentState::Missing),
    }
}

fn core_fonts_installed(fonts_dir: &Path) -> bool {
    const CORE_FONT_FILES: &[&str] = &[
        "andale.ttf",
        "arial.ttf",
        "arialbd.ttf",
        "comic.ttf",
        "cour.ttf",
        "georgia.ttf",
        "impact.ttf",
        "times.ttf",
        "trebuc.ttf",
        "verdana.ttf",
        "webdings.ttf",
    ];

    if !fonts_dir.is_dir() {
        return false;
    }

    let installed = CORE_FONT_FILES.iter().filter(|name| fonts_dir.join(name).is_file()).count();
    installed >= 4
}

fn xna_framework_installed(prefix: &Path) -> bool {
    let drive_c = prefix.join("drive_c");
    let windows = drive_c.join("windows");
    if drive_c.join("Program Files (x86)").join("Microsoft XNA").exists()
        || drive_c.join("Program Files").join("Microsoft XNA").exists()
    {
        return true;
    }

    let framework_roots = [
        windows.join("Microsoft.NET").join("assembly").join("GAC_32").join("Microsoft.Xna.Framework"),
        windows.join("Microsoft.NET").join("assembly").join("GAC_MSIL").join("Microsoft.Xna.Framework"),
        windows.join("assembly").join("GAC_32").join("Microsoft.Xna.Framework"),
        windows.join("assembly").join("GAC_MSIL").join("Microsoft.Xna.Framework"),
        windows.join("mono").join("mono-2.0").join("lib").join("mono").join("gac").join("Microsoft.Xna.Framework"),
    ];
    framework_roots.iter().any(|root| xna_framework_dll_exists(root))
}

fn xna_framework_dll_exists(root: &Path) -> bool {
    if !root.exists() {
        return false;
    }
    if root.join("Microsoft.Xna.Framework.dll").is_file() {
        return true;
    }
    WalkDir::new(root).max_depth(3).into_iter().flatten().any(|entry| {
        entry.file_type().is_file()
            && entry.file_name().to_string_lossy().eq_ignore_ascii_case("Microsoft.Xna.Framework.dll")
    })
}

fn host_core_font_sources() -> Vec<(String, PathBuf)> {
    let candidates = [
        ("arial.ttf", "Arial.ttf"),
        ("arialbd.ttf", "Arial Bold.ttf"),
        ("cour.ttf", "Courier New.ttf"),
        ("georgia.ttf", "Georgia.ttf"),
        ("impact.ttf", "Impact.ttf"),
        ("times.ttf", "Times New Roman.ttf"),
        ("trebuc.ttf", "Trebuchet MS.ttf"),
        ("verdana.ttf", "Verdana.ttf"),
        ("webdings.ttf", "Webdings.ttf"),
    ];
    let search_roots = [
        PathBuf::from("/System/Library/Fonts/Supplemental"),
        PathBuf::from("/System/Library/Fonts"),
        PathBuf::from("/Library/Fonts"),
        dirs::home_dir().unwrap_or_default().join("Library").join("Fonts"),
    ];

    candidates
        .iter()
        .filter_map(|(target, source_name)| {
            search_roots
                .iter()
                .map(|root| root.join(source_name))
                .find(|path| path.is_file())
                .map(|path| ((*target).to_string(), path))
        })
        .collect()
}

fn install_host_core_fonts(prefix: &Path, log_path: &Path) -> Result<bool, Box<dyn std::error::Error>> {
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let fonts_dir = prefix.join("drive_c").join("windows").join("Fonts");
    fs::create_dir_all(&fonts_dir)?;
    let sources = host_core_font_sources();
    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "component=corefonts")?;
    writeln!(log, "prefix={}", prefix.display())?;
    writeln!(log, "fonts_dir={}", fonts_dir.display())?;
    writeln!(log, "source_count={}", sources.len())?;
    for (target, source) in sources {
        let dest = fonts_dir.join(&target);
        match fs::copy(&source, &dest) {
            Ok(_) => writeln!(log, "copied {} -> {}", source.display(), dest.display())?,
            Err(err) => writeln!(log, "copy_failed {} -> {}: {}", source.display(), dest.display(), err)?,
        }
    }
    let installed = core_fonts_installed(&fonts_dir);
    writeln!(log, "installed={}", installed)?;
    Ok(installed)
}

fn inspect_runtime_dll_component(id: &str) -> Option<ComponentState> {
    let filename = format!("{}.dll", id);
    let home = dirs::home_dir()?;
    let runtime_wine = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let candidates = [
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join(&filename),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join(&filename),
        runtime_wine.join("lib").join("dxvk").join("x64-windows").join(&filename),
        runtime_wine.join("lib").join("dxvk").join("i386-windows").join(&filename),
    ];
    Some(if candidates.iter().any(|path| path.exists()) { ComponentState::Installed } else { ComponentState::Missing })
}

fn windows_version_component_id(version: &str) -> String {
    format!("{}{}", WINDOWS_VERSION_COMPONENT_PREFIX, version)
}

fn inspect_windows_version_component(prefix: &Path, expected_version: &str) -> Option<ComponentState> {
    let current = read_wine_windows_version(prefix)?;
    Some(if current == expected_version { ComponentState::Installed } else { ComponentState::Missing })
}

fn read_wine_windows_version(prefix: &Path) -> Option<String> {
    for registry in [prefix.join("user.reg"), prefix.join("system.reg")] {
        let Ok(data) = fs::read_to_string(registry) else {
            continue;
        };
        if let Some(version) = parse_wine_windows_version(&data) {
            return Some(version);
        }
    }
    None
}

fn parse_wine_windows_version(data: &str) -> Option<String> {
    let mut in_wine_section = false;
    for line in data.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            let section = trimmed.trim_start_matches('[').trim_end_matches(']');
            in_wine_section = section == r"Software\\Wine" || section.ends_with(r"\\Software\\Wine");
            continue;
        }
        if !in_wine_section || !trimmed.starts_with("\"Version\"=") {
            continue;
        }
        return trimmed.split_once('=').map(|(_, value)| value.trim().trim_matches('"').replace(r#"\""#, "\""));
    }
    None
}

fn resolve_component_installer(component_id: &str, arch: BottleArch) -> Option<ComponentInstaller> {
    let home = dirs::home_dir()?;
    let redist_root = home
        .join(".metalsharp")
        .join("prefix-steam")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps")
        .join("common")
        .join("Steamworks Shared")
        .join("_CommonRedist");
    let local_redist = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("redist");

    resolve_component_installer_from_roots(component_id, arch, &redist_root, &local_redist)
        .or_else(|| resolve_sharp_library_component_installer(component_id))
}

fn resolve_component_installer_from_roots(
    component_id: &str,
    arch: BottleArch,
    redist_root: &Path,
    local_redist: &Path,
) -> Option<ComponentInstaller> {
    let executable = match component_id {
        "vcrun2019" => {
            let filename = match arch {
                BottleArch::Win32 => "VC_redist.x86.exe",
                BottleArch::Win64 | BottleArch::Wow64 => "VC_redist.x64.exe",
            };
            first_existing(&[
                redist_root.join("vcredist").join("2022").join(filename),
                redist_root.join("vcredist").join("2019").join(filename),
                redist_root.join("vcredist").join("2017").join(filename),
                redist_root.join("vcredist").join("2015").join(filename),
                local_redist.join(filename),
            ])
        },
        "vcrun2013" => {
            let filename = match arch {
                BottleArch::Win32 => "vcredist_x86.exe",
                BottleArch::Win64 | BottleArch::Wow64 => "vcredist_x64.exe",
            };
            first_existing(&[redist_root.join("vcredist").join("2013").join(filename), local_redist.join(filename)])
        },
        "vcrun2010" => {
            let filename = match arch {
                BottleArch::Win32 => "vcredist_x86.exe",
                BottleArch::Win64 | BottleArch::Wow64 => "vcredist_x64.exe",
            };
            first_existing(&[
                redist_root.join("vcredist").join("2010").join(filename),
                local_redist.join("vcredist").join("2010").join(filename),
                local_redist.join(filename),
            ])
        },
        "dotnet48" => first_existing(&[
            redist_root.join("DotNet").join("4.8").join("ndp48-x86-x64-allos-enu.exe"),
            redist_root.join("DotNet").join("4.8").join("NDP48-x86-x64-AllOS-ENU.exe"),
            redist_root.join("DotNet").join("4.7.2").join("NDP472-KB4054530-x86-x64-AllOS-ENU.exe"),
            redist_root.join("DotNet").join("4.6").join("NDP462-KB3151800-x86-x64-AllOS-ENU.exe"),
            redist_root.join("DotNet").join("4.5.2").join("NDP452-KB2901907-x86-x64-AllOS-ENU.exe"),
            local_redist.join("DotNet").join("4.8").join("ndp48-x86-x64-allos-enu.exe"),
            local_redist.join("DotNet").join("4.8").join("NDP48-x86-x64-AllOS-ENU.exe"),
        ]),
        "dotnet40" => first_existing(&[
            redist_root.join("DotNet").join("4.0").join("dotNetFx40_Client_x86_x64.exe"),
            redist_root.join("DotNet").join("4.0").join("dotNetFx40_Full_x86_x64.exe"),
            redist_root.join("DotNet").join("4.0").join("dotNetFx40_Full_setup.exe"),
            local_redist.join("DotNet").join("4.0").join("dotNetFx40_Client_x86_x64.exe"),
            local_redist.join("DotNet").join("4.0").join("dotNetFx40_Full_x86_x64.exe"),
            local_redist.join("DotNet").join("4.0").join("dotNetFx40_Full_setup.exe"),
        ]),
        "webview2" => first_existing(&[
            redist_root.join("WebView2").join("MicrosoftEdgeWebView2RuntimeInstallerX64.exe"),
            redist_root.join("WebView2").join("MicrosoftEdgeWebView2RuntimeInstallerX86.exe"),
            local_redist.join("WebView2").join("MicrosoftEdgeWebView2RuntimeInstallerX64.exe"),
            local_redist.join("WebView2").join("MicrosoftEdgeWebView2RuntimeInstallerX86.exe"),
            local_redist.join("WebView2").join("MicrosoftEdgeWebview2Setup.exe"),
            local_redist.join("MicrosoftEdgeWebView2RuntimeInstallerX64.exe"),
            local_redist.join("MicrosoftEdgeWebView2RuntimeInstallerX86.exe"),
            local_redist.join("MicrosoftEdgeWebview2Setup.exe"),
        ]),
        "directx_jun2010" => first_existing(&[
            redist_root.join("DirectX").join("Jun2010").join("DXSETUP.exe"),
            redist_root.join("DirectX").join("Jun2010").join("dxsetup.exe"),
            local_redist.join("DirectX").join("Jun2010").join("DXSETUP.exe"),
        ]),
        "openal" => first_existing(&[
            redist_root.join("OpenAL").join("2.0.7.0").join("oalinst.exe"),
            redist_root.join("OpenAL").join("oalinst.exe"),
            local_redist.join("OpenAL").join("oalinst.exe"),
            local_redist.join("oalinst.exe"),
        ]),
        "xna" => first_existing(&[
            redist_root.join("XNA").join("4.0").join("xnafx40_redist.msi"),
            redist_root.join("XNA").join("4.0").join("xnafx40_redist.exe"),
            local_redist.join("XNA").join("4.0").join("xnafx40_redist.msi"),
            local_redist.join("XNA").join("4.0").join("xnafx40_redist.exe"),
        ]),
        "physx" => first_existing(&[
            redist_root.join("PhysX").join("9.12.1031").join("PhysX-9.12.1031-SystemSoftware.msi"),
            redist_root.join("PhysX").join("9.13.0604").join("PhysX-9.13.0604-SystemSoftware.msi"),
            redist_root.join("PhysX").join("PhysX-9.12.1031-SystemSoftware.msi"),
            local_redist.join("PhysX").join("PhysX-9.12.1031-SystemSoftware.msi"),
        ]),
        _ => None,
    }?;

    let args = component_installer_args(component_id, &executable);
    Some(ComponentInstaller { path: executable, args })
}

fn component_installer_args(component_id: &str, executable: &Path) -> Vec<String> {
    match component_id {
        "vcrun2019" | "vcrun2013" | "vcrun2010" => vec!["/quiet".to_string(), "/norestart".to_string()],
        "dotnet40" | "dotnet48" => vec!["/q".to_string(), "/norestart".to_string()],
        "webview2" => vec!["/silent".to_string(), "/install".to_string()],
        "directx_jun2010" => vec!["/silent".to_string()],
        "openal" => vec!["/S".to_string()],
        "xna" | "physx" => {
            if executable.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("msi")).unwrap_or(false) {
                vec!["/quiet".to_string(), "/norestart".to_string()]
            } else {
                vec!["/quiet".to_string()]
            }
        },
        _ => Vec::new(),
    }
}

fn resolve_sharp_library_component_installer(component_id: &str) -> Option<ComponentInstaller> {
    resolve_sharp_library_component_installer_from_root(component_id, &bottles_root())
}

fn resolve_sharp_library_component_installer_from_root(component_id: &str, root: &Path) -> Option<ComponentInstaller> {
    if !root.exists() {
        return None;
    }
    let mut bottle_dirs = fs::read_dir(root).ok()?.flatten().map(|entry| entry.path()).collect::<Vec<_>>();
    bottle_dirs.sort();
    for bottle_dir in bottle_dirs {
        let manifest_path = bottle_dir.join(MANIFEST_FILE);
        let Ok(data) = fs::read_to_string(&manifest_path) else {
            continue;
        };
        let Ok(manifest) = serde_json::from_str::<BottleManifest>(&data) else {
            continue;
        };
        if manifest.bottle_type != BottleType::Installer {
            continue;
        }

        if let Some(path) = manifest.source_installer_path.as_deref().map(PathBuf::from) {
            if let Some(installer) = component_installer_from_local_path(component_id, &path) {
                return Some(installer);
            }
        }

        let payload_dir = bottle_dir.join("installers");
        let Ok(read_dir) = fs::read_dir(payload_dir) else {
            continue;
        };
        let mut payloads = read_dir.flatten().map(|entry| entry.path()).collect::<Vec<_>>();
        payloads.sort();
        for path in payloads {
            if let Some(installer) = component_installer_from_local_path(component_id, &path) {
                return Some(installer);
            }
        }
    }
    None
}

fn component_installer_from_local_path(component_id: &str, path: &Path) -> Option<ComponentInstaller> {
    if !path.is_file() || component_kind_from_local_installer(path).as_deref() != Some(component_id) {
        return None;
    }
    Some(ComponentInstaller { path: path.to_path_buf(), args: component_installer_args(component_id, path) })
}

fn component_kind_from_local_installer(path: &Path) -> Option<String> {
    let lower_name = path.file_name()?.to_string_lossy().to_ascii_lowercase();
    classify_redist_asset(&lower_name)
}

fn resolve_game_runtime_asset_installer(manifest: &BottleManifest, component_id: &str) -> Option<ComponentInstaller> {
    if component_id == "easyanticheat_eos" {
        let candidates = game_runtime_installer_candidates(manifest, component_id);
        for asset in candidates.iter().filter(|asset| {
            let lower = asset.source_path.to_ascii_lowercase();
            lower.ends_with(".bat") || lower.ends_with(".cmd")
        }) {
            if let Some(installer) = easyanticheat_eos_installer_from_asset(Path::new(&asset.source_path)) {
                return Some(installer);
            }
        }
        return candidates
            .iter()
            .filter(|asset| asset.source_path.to_ascii_lowercase().ends_with(".exe"))
            .find_map(|asset| easyanticheat_eos_installer_from_asset(Path::new(&asset.source_path)));
    }
    let candidates = game_runtime_installer_candidates(manifest, component_id);
    let preferred = candidates
        .iter()
        .find(|asset| {
            let lower = asset.source_path.to_ascii_lowercase();
            lower.ends_with(".bat") || lower.ends_with(".cmd")
        })
        .or_else(|| candidates.first())?;
    let path = PathBuf::from(&preferred.source_path);
    Some(ComponentInstaller { args: component_installer_args(component_id, &path), path })
}

fn game_runtime_installer_candidates<'a>(
    manifest: &'a BottleManifest,
    component_id: &str,
) -> Vec<&'a BottleRuntimeAsset> {
    manifest
        .runtime_assets
        .iter()
        .filter(|asset| {
            asset.present
                && is_game_runtime_installer_candidate(asset, component_id)
                && match component_id {
                    "easyanticheat_eos" => matches!(asset.kind.as_str(), "easyanticheat" | "easyanticheat_eos"),
                    "battleye" => asset.kind == "battleye",
                    "xna" => asset.kind == "xna",
                    _ => false,
                }
        })
        .collect()
}

fn is_game_runtime_installer_candidate(asset: &BottleRuntimeAsset, component_id: &str) -> bool {
    let path = Path::new(&asset.source_path);
    let lower_name = path.file_name().map(|name| name.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    let Some(extension) = path.extension().map(|ext| ext.to_string_lossy().to_ascii_lowercase()) else {
        return false;
    };
    if matches!(extension.as_str(), "bat" | "cmd" | "msi") {
        return true;
    }
    if extension != "exe" {
        return false;
    }
    match component_id {
        "easyanticheat_eos" => lower_name.contains("setup") || lower_name.contains("install"),
        "battleye" => {
            lower_name.contains("setup")
                || lower_name.contains("install")
                || lower_name == "beservice.exe"
                || lower_name == "beservice_x64.exe"
        },
        "xna" => lower_name.contains("xnafx") || lower_name.contains("xna"),
        _ => false,
    }
}

fn easyanticheat_eos_installer_from_asset(path: &Path) -> Option<ComponentInstaller> {
    let lower = path.file_name()?.to_string_lossy().to_ascii_lowercase();
    if lower.ends_with(".exe") {
        return Some(ComponentInstaller { path: path.to_path_buf(), args: Vec::new() });
    }
    if !(lower.ends_with(".bat") || lower.ends_with(".cmd")) {
        return None;
    }
    let script = fs::read_to_string(path).ok()?;
    for line in script.lines() {
        let trimmed = line.trim();
        let lower_line = trimmed.to_ascii_lowercase();
        if !lower_line.contains("easyanticheat_eos_setup.exe") || !lower_line.contains(" install ") {
            continue;
        }
        let product_id = trimmed.split_whitespace().last()?.trim_matches('"').to_string();
        if let Some(setup) = find_case_insensitive_sibling(path.parent()?, "easyanticheat_eos_setup.exe") {
            return Some(ComponentInstaller { path: setup, args: vec!["install".to_string(), product_id] });
        }
    }
    None
}

fn find_case_insensitive_sibling(parent: &Path, file_name: &str) -> Option<PathBuf> {
    let target = file_name.to_ascii_lowercase();
    fs::read_dir(parent)
        .ok()?
        .flatten()
        .find(|entry| entry.file_name().to_string_lossy().to_ascii_lowercase() == target)
        .map(|entry| entry.path())
}

fn first_existing(paths: &[PathBuf]) -> Option<PathBuf> {
    paths.iter().find(|path| path.exists()).cloned()
}

fn prepare_native_dotnet4_repair(prefix: &Path, log_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    if inspect_component_state(prefix, "dotnet40", ComponentState::Missing) == ComponentState::Installed {
        return Ok(());
    }

    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let reg_file = prefix.join("drive_c").join("metalsharp-dotnet40-native-repair.reg");
    fs::write(&reg_file, build_dotnet4_native_repair_reg())?;

    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "dotnet40_native_repair_preflight={}", reg_file.display())?;
    writeln!(log, "--- dotnet40 native repair registry output ---")?;
    let stdout = log.try_clone()?;

    let mut cmd = Command::new(&wine);
    cmd.arg("regedit")
        .arg(wine_z_drive_path(&reg_file))
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let status = cmd.status()?;
    if !status.success() {
        return Err(format!("failed to clear stale .NET 4 registry markers before native repair: {}", status).into());
    }
    Ok(())
}

fn build_dotnet4_native_repair_reg() -> String {
    let keys = [
        r"HKEY_LOCAL_MACHINE\Software\Microsoft\NET Framework Setup\NDP\v4\Client",
        r"HKEY_LOCAL_MACHINE\Software\Microsoft\NET Framework Setup\NDP\v4\Full",
        r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Microsoft\NET Framework Setup\NDP\v4\Client",
        r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Microsoft\NET Framework Setup\NDP\v4\Full",
    ];
    let mut reg = String::from("REGEDIT4\r\n\r\n");
    for key in keys {
        reg.push_str(&format!("[{}]\r\n", key));
        reg.push_str("\"Install\"=dword:00000000\r\n\r\n");
    }
    reg
}

fn launch_component_installer(
    prefix: &Path,
    installer: &ComponentInstaller,
    log_path: &Path,
) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "component_installer={}", installer.path.display())?;
    writeln!(log, "prefix={}", prefix.display())?;
    writeln!(log, "args={:?}", installer.args)?;
    writeln!(log, "--- wine output ---")?;
    let stdout = log.try_clone()?;

    let mut cmd = Command::new(&wine);
    if installer.path.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("msi")).unwrap_or(false) {
        cmd.arg("msiexec").arg("/i").arg(&installer.path);
    } else if installer
        .path
        .extension()
        .map(|ext| {
            let ext = ext.to_string_lossy();
            ext.eq_ignore_ascii_case("bat") || ext.eq_ignore_ascii_case("cmd")
        })
        .unwrap_or(false)
    {
        cmd.arg("cmd").arg("/c").arg(format!("call \"{}\"", wine_z_drive_path(&installer.path)));
    } else {
        cmd.arg(&installer.path);
    }
    cmd.args(&installer.args)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    if let Some(parent) = installer.path.parent() {
        cmd.current_dir(parent);
    }
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;
    Ok(child.id())
}

fn launch_wineboot_repair(
    prefix: &Path,
    component_id: &str,
    log_path: &Path,
) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wineboot = ms_root.join("bin").join("wineboot");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    let executable = if wineboot.exists() { wineboot } else { wine };
    if !executable.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "builtin_component_repair={}", component_id)?;
    writeln!(log, "prefix={}", prefix.display())?;
    writeln!(log, "--- wineboot output ---")?;
    let stdout = log.try_clone()?;

    let mut cmd = Command::new(&executable);
    if executable.file_name().map(|name| name.to_string_lossy().contains("wine")).unwrap_or(false)
        && !executable.file_name().map(|name| name == "wineboot").unwrap_or(false)
    {
        cmd.arg("wineboot");
    }
    cmd.arg("-u")
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "+loaddll")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;
    Ok(child.id())
}

fn run_wine_reg_set_windows_version(
    prefix: &Path,
    version: &str,
    log_path: &Path,
) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "windows_version={}", version)?;
    writeln!(log, "prefix={}", prefix.display())?;
    writeln!(log, "--- wine output ---")?;
    let stdout = log.try_clone()?;

    let mut cmd = Command::new(&wine);
    cmd.arg("reg")
        .arg("add")
        .arg("HKCU\\Software\\Wine")
        .arg("/v")
        .arg("Version")
        .arg("/d")
        .arg(version)
        .arg("/f")
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;
    Ok(child.id())
}

const FONT_SUBSTITUTIONS: &[(&str, &str)] = &[
    ("Helvetica", "Arial"),
    ("Times", "Times New Roman"),
    ("Helv", "MS Sans Serif"),
    ("Tms Rmn", "Times New Roman"),
    ("MS Shell Dlg", "Tahoma"),
    ("MS Shell Dlg 2", "Tahoma"),
    ("Arial Baltic,186", "Arial,186"),
    ("Arial CE,238", "Arial,238"),
    ("Arial CYR,204", "Arial,204"),
    ("Arial Greek,161", "Arial,161"),
    ("Arial TUR,162", "Arial,162"),
    ("Courier New Baltic,186", "Courier New,186"),
    ("Courier New CE,238", "Courier New,238"),
    ("Courier New CYR,204", "Courier New,204"),
    ("Courier New Greek,161", "Courier New,161"),
    ("Courier New TUR,162", "Courier New,162"),
    ("Times New Roman Baltic,186", "Times New Roman,186"),
    ("Times New Roman CE,238", "Times New Roman,238"),
    ("Times New Roman CYR,204", "Times New Roman,204"),
    ("Times New Roman Greek,161", "Times New Roman,161"),
    ("Times New Roman TUR,162", "Times New Roman,162"),
];

const WINE_FONT_REPLACEMENTS: &[(&str, &str)] = &[
    ("Arial", "Helvetica Neue"),
    ("MS Gothic", "Hiragino Sans"),
    ("MS PGothic", "Hiragino Sans"),
    ("SimSun", "STSong"),
    ("NSimSun", "STSong"),
    ("MingLiU", "LiSong Pro"),
    ("PMingLiU", "LiSong Pro"),
    ("Microsoft Himalaya", "Kailasa"),
    ("Euphemia", "Euphemia UCAS"),
    ("Gulim", "Apple SD Gothic Neo"),
];

pub fn apply_font_substitutions(prefix: &Path, log_path: &Path) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }

    let reg_content = build_font_substitution_reg();
    let reg_file = prefix.join("drive_c").join("metalsharp-fontsubs.reg");
    fs::write(&reg_file, &reg_content)?;

    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "font_substitutions_applied")?;
    writeln!(log, "prefix={}", prefix.display())?;
    let stdout = log.try_clone()?;

    let reg_file_win = wine_z_drive_path(&reg_file);
    let mut cmd = Command::new(&wine);
    cmd.arg("regedit")
        .arg(&reg_file_win)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;
    Ok(child.id())
}

fn build_font_substitution_reg() -> String {
    let mut reg = String::from("REGEDIT4\r\n\r\n");
    reg.push_str("[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]\r\n");
    for (name, value) in FONT_SUBSTITUTIONS {
        reg.push_str(&format!("\"{}\"=\"{}\"\r\n", name, value));
    }
    reg.push_str("\r\n");
    reg.push_str("[HKEY_CURRENT_USER\\Software\\Wine\\Fonts\\Replacements]\r\n");
    for (name, value) in WINE_FONT_REPLACEMENTS {
        reg.push_str(&format!("\"{}\"=\"{}\"\r\n", name, value));
    }
    reg
}

const POST_WINEBOOT_DLL_OVERRIDES: &[(&str, &str)] = &[
    ("atl", "native,builtin"),
    ("msvcirt", "native,builtin"),
    ("msvcrt40", "native,builtin"),
    ("msvcrtd", "native,builtin"),
    ("msxml3", "native,builtin"),
    ("vcruntime140", "native,builtin"),
    ("vcruntime140_1", "native,builtin"),
    ("msvcp140", "native,builtin"),
];

pub fn seed_post_wineboot_config(prefix: &Path, log_path: &Path) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }

    let dosdevices = prefix.join("dosdevices");
    if dosdevices.exists() {
        let y_link = dosdevices.join("y:");
        let needs_link = if y_link.exists() {
            match std::fs::read_link(&y_link) {
                Ok(target) => target != home,
                Err(_) => true,
            }
        } else {
            true
        };
        if needs_link {
            let _ = std::fs::remove_file(&y_link);
            let _ = std::os::unix::fs::symlink(&home, &y_link);
        }
    }

    let ntdll_hook_src =
        ms_root.join("lib").join("metalsharp").join("x86_64-windows").join("metalsharp_ntdll_hook.dll");
    if ntdll_hook_src.exists() {
        let system32 = prefix.join("drive_c").join("windows").join("system32");
        let _ = fs::create_dir_all(&system32);
        let dst = system32.join("metalsharp_ntdll_hook.dll");
        if !dst.exists() {
            let _ = fs::copy(&ntdll_hook_src, &dst);
        }
    }

    let mut reg = build_font_substitution_reg();
    reg.push_str("\r\n[HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides]\r\n");
    for (dll, mode) in POST_WINEBOOT_DLL_OVERRIDES {
        reg.push_str(&format!("\"{}\"=\"{}\"\r\n", dll, mode));
    }

    reg.push_str("\r\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows]\r\n");
    reg.push_str("\"AppInit_DLLs\"=\"metalsharp_ntdll_hook.dll\"\r\n");
    reg.push_str("\"LoadAppInit_DLLs\"=dword:00000001\r\n");
    reg.push_str("\"RequireSignedAppInit_DLLs\"=dword:00000000\r\n");

    let reg_file = prefix.join("drive_c").join("metalsharp-post-wineboot.reg");
    fs::write(&reg_file, &reg)?;

    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "post_wineboot_config_seed")?;
    writeln!(log, "prefix={}", prefix.display())?;
    let stdout = log.try_clone()?;

    let reg_file_win = wine_z_drive_path(&reg_file);
    let mut cmd = Command::new(&wine);
    cmd.arg("regedit")
        .arg(&reg_file_win)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let mut child = cmd.spawn()?;
    let pid = child.id();
    let exit_status = child.wait()?;
    if exit_status.success() {
        let marker = prefix.join("drive_c").join("metalsharp-post-wineboot-seeded");
        let _ = fs::write(&marker, timestamp_secs().as_bytes());
    }
    Ok(pid)
}

fn wine_z_drive_path(path: &Path) -> String {
    if path.is_absolute() {
        format!("Z:{}", path.to_string_lossy().replace('/', "\\"))
    } else {
        path.to_string_lossy().replace('/', "\\")
    }
}

fn normalized_existing_path_string(path: &Path) -> String {
    path.canonicalize().unwrap_or_else(|_| path.to_path_buf()).to_string_lossy().to_string()
}

fn component_actions(components: &[RuntimeComponent]) -> Vec<BottleAction> {
    components
        .iter()
        .filter(|component| !component_ready(component))
        .map(|component| BottleAction {
            id: component.id.clone(),
            status: "needed".to_string(),
            detail: component_action_detail(&component.id),
        })
        .collect()
}

fn component_ready(component: &RuntimeComponent) -> bool {
    component.state == ComponentState::Installed
}

fn components_ready(components: &[RuntimeComponent]) -> bool {
    components.iter().all(component_ready)
}

fn component_source_policies_for_manifest(manifest: &BottleManifest) -> Vec<ComponentSourcePolicy> {
    manifest
        .installed_components
        .iter()
        .map(|component| {
            if matches!(component.id.as_str(), "easyanticheat_eos" | "battleye") {
                if let Some(installer) = resolve_game_runtime_asset_installer(manifest, &component.id) {
                    return ComponentSourcePolicy {
                        id: component.id.clone(),
                        source: "game_runtime_asset".to_string(),
                        available: true,
                        detail: match component.id.as_str() {
                            "easyanticheat_eos" => {
                                "Uses the game-local Easy Anti-Cheat EOS setup asset shipped with this install"
                            },
                            "battleye" => "Uses the game-local BattlEye setup/service asset shipped with this install",
                            _ => "Uses a game-local runtime installer asset",
                        }
                        .to_string(),
                        path: Some(installer.path.to_string_lossy().to_string()),
                    };
                }
            }
            component_source_policy(&component.id, manifest.arch)
        })
        .collect()
}

fn component_source_policy(id: &str, arch: BottleArch) -> ComponentSourcePolicy {
    if matches!(id, "wine-mono" | "gecko") {
        return ComponentSourcePolicy {
            id: id.to_string(),
            source: "metalsharp_wine_bootstrap".to_string(),
            available: dirs::home_dir()
                .map(|home| {
                    crate::platform::runtime_wine_binary(
                        &crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine"),
                    )
                    .exists()
                })
                .unwrap_or(false),
            detail: format!("{} is repaired by running wineboot -u in the bottle prefix", id),
            path: None,
        };
    }
    if id == "corefonts" {
        let available = host_core_font_sources().len() >= 4;
        return ComponentSourcePolicy {
            id: id.to_string(),
            source: if available { "host_system_fonts" } else { "missing_local_asset" }.to_string(),
            available,
            detail: if available {
                "Maps locally installed host fonts into the bottle Windows font directory"
            } else {
                "Requires a local core fonts payload or a mapped font installation strategy"
            }
            .to_string(),
            path: None,
        };
    }
    if matches!(id, "mono-arm64" | "mono-x86" | "fna" | "d3d12_agility") {
        let state = match id {
            "mono-arm64" => inspect_host_mono_component("mono-arm64"),
            "mono-x86" => inspect_host_mono_component("mono-x86"),
            "fna" => inspect_fna_runtime_component(),
            "d3d12_agility" => inspect_d3d12_agility_component(),
            _ => None,
        }
        .unwrap_or(ComponentState::Unknown);
        let path = dirs::home_dir().map(|home| match id {
            "mono-arm64" => crate::platform::metalsharp_home_dir_for(&home).join("runtime/mono-arm64/bin/mono"),
            "mono-x86" => crate::platform::metalsharp_home_dir_for(&home).join("runtime/mono-x86/bin/mono"),
            "fna" => crate::platform::metalsharp_home_dir_for(&home).join("runtime/fna"),
            "d3d12_agility" => crate::platform::metalsharp_home_dir_for(&home).join("runtime/redist/agility"),
            _ => crate::platform::metalsharp_home_dir_for(&home).join("runtime"),
        });
        return ComponentSourcePolicy {
            id: id.to_string(),
            source: "metalsharp_native_runtime".to_string(),
            available: if id == "d3d12_agility" {
                true
            } else if id == "fna" {
                matches!(state, ComponentState::Installed | ComponentState::NeedsRepair)
            } else {
                state == ComponentState::Installed
            },
            detail: match id {
                "mono-arm64" => "Native ARM64 Mono runtime for Terraria/FNA-style macOS launch wrappers",
                "mono-x86" => "Native x86_64 Mono runtime for legacy Celeste/FNA-style launch wrappers under Rosetta",
                "fna" => "FNA/XNA compatibility assemblies and native shims staged in MetalSharp runtime",
                "d3d12_agility" => "D3D12 Agility SDK x64 runtime payload staged for M12/D3D12 games",
                _ => "MetalSharp native runtime component",
            }
            .to_string(),
            path: path.map(|p| p.to_string_lossy().to_string()),
        };
    }
    let installer = resolve_component_installer(id, arch);
    let source = installer
        .as_ref()
        .map(|installer| component_installer_source_label(&installer.path))
        .unwrap_or("missing_local_asset");
    ComponentSourcePolicy {
        id: id.to_string(),
        source: source.to_string(),
        available: installer.is_some(),
        detail: match id {
            "dotnet40" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist .NET 4.0 offline installers",
            "dotnet48" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist .NET 4.x offline installers",
            "vcrun2019" => "Uses Steam CommonRedist VC_redist or compatible local Visual C++ redistributable",
            "vcrun2010" => "Uses Steam CommonRedist or local Visual C++ 2010 redistributable",
            "vcrun2013" => "Uses Steam CommonRedist or local Visual C++ 2013 redistributable",
            "gpu_vendor_stubs" => "DXMT open-source NVAPI/NVNGX stubs from lib/dxmt/x86_64-windows",
            "gptk_amd_stub" => "GPTK AMD vendor stub from lib/gptk/x86_64-windows",
            "corefonts" => "Requires a local core fonts payload or a mapped font installation strategy",
            "webview2" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist WebView2 evergreen installer",
            "directx_jun2010" => "DirectX June 2010 — checks d3dx9_43, d3dx10_43, d3dx11_43, xinput1_3, xaudio2_7, x3daudio1_7, D3DCompiler_43",
            "d3d12_agility" => "Uses NuGet Microsoft.Direct3D.D3D12 x64 runtime payload for the game-declared D3D12SDKVersion",
            "openal" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist OpenAL installer",
            "xna" => {
                "Uses Steam CommonRedist, Sharp Library installer bottles, or ~/.metalsharp/runtime/redist XNA 4.0 installer"
            },
            "physx" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist PhysX installer",
            "easyanticheat_eos" => "Uses game-local Easy Anti-Cheat EOS setup assets when present",
            "battleye" => "Uses game-local BattlEye setup/service assets when present",
            _ => "No external installer source required or source is not yet mapped",
        }
        .to_string(),
        path: installer.map(|installer| installer.path.to_string_lossy().to_string()),
    }
}

fn component_installer_source_label(path: &Path) -> &'static str {
    let normalized = path.to_string_lossy();
    if normalized.contains("/bottles/") && normalized.contains("/installers/") {
        "sharp_library_installer"
    } else {
        "local_redist_asset"
    }
}

fn component_action_detail(id: &str) -> String {
    match id {
        "wine-mono" => "Install or repair Wine Mono inside this bottle prefix".to_string(),
        "mono-arm64" => "Install MetalSharp ARM64 Mono runtime".to_string(),
        "mono-x86" => "Install MetalSharp x86_64 Mono runtime".to_string(),
        "fna" => "Install FNA/XNA compatibility assemblies and native shims".to_string(),
        "d3d12_agility" => "Download and stage the D3D12 Agility SDK payload".to_string(),
        "gecko" => "Install Wine Gecko for embedded browser surfaces".to_string(),
        "dotnet40" => "Install the native .NET Framework 4.0 runtime for CLR v4 titles".to_string(),
        "dotnet48" => "Install a compatible .NET 4.x runtime strategy for this bottle".to_string(),
        "vcrun2019" => "Install Visual C++ 2015-2022 runtime DLLs".to_string(),
        "vcrun2010" => "Install Visual C++ 2010 runtime DLLs (msvcr100, msvcp100)".to_string(),
        "vcrun2013" => "Install Visual C++ 2013 runtime DLLs (msvcr120, msvcp120)".to_string(),
        "gpu_vendor_stubs" => "Deploy NVAPI/NVNGX GPU vendor stubs for NVIDIA API compatibility".to_string(),
        "gptk_amd_stub" => "Deploy GPTK AMD vendor stub for D3DMetal compatibility".to_string(),
        "corefonts" => "Install core Windows fonts".to_string(),
        "webview2" => "Install or emulate Microsoft Edge WebView2 runtime".to_string(),
        "directx_jun2010" => "Install DirectX June 2010 runtime payloads".to_string(),
        "openal" => "Install OpenAL audio runtime".to_string(),
        "xna" => "Install XNA Framework 4.0 runtime".to_string(),
        "physx" => "Install NVIDIA PhysX legacy runtime".to_string(),
        "easyanticheat_eos" => "Run the game-local Easy Anti-Cheat EOS service installer".to_string(),
        "battleye" => "Run the game-local BattlEye service installer".to_string(),
        "d3d10" => "Verify MetalSharp D3D10 runtime DLLs".to_string(),
        "d3d10_1" => "Verify MetalSharp D3D10.1 runtime DLLs".to_string(),
        id if id.starts_with(WINDOWS_VERSION_COMPONENT_PREFIX) => {
            format!("Apply Wine Windows version mode {}", id.trim_start_matches(WINDOWS_VERSION_COMPONENT_PREFIX))
        },
        _ => format!("Prepare component {}", id),
    }
}

fn installer_bottle_id(source_installer: &Path) -> String {
    let mut hasher = DefaultHasher::new();
    "installer".hash(&mut hasher);
    source_installer.to_string_lossy().hash(&mut hasher);
    format!("installer_{:016x}", hasher.finish())
}

fn fresh_installer_bottle_id(source_installer: &Path) -> String {
    let millis = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or_default();
    format!("{}_fresh_{}", installer_bottle_id(source_installer), millis)
}

fn installer_pipeline_from_pe(pe: Option<&crate::mtsp::pe::PeInfo>) -> crate::mtsp::engine::PipelineId {
    let Some(pe) = pe else {
        return crate::mtsp::engine::PipelineId::WineBare;
    };
    if !pe.is_64_bit {
        return crate::mtsp::engine::PipelineId::M9;
    }
    match pe.detected_api {
        crate::mtsp::pe::D3dApi::D3D12 => crate::mtsp::engine::PipelineId::M12,
        crate::mtsp::pe::D3dApi::D3D11 => crate::mtsp::engine::PipelineId::M11,
        crate::mtsp::pe::D3dApi::D3D10 => crate::mtsp::engine::PipelineId::M10,
        crate::mtsp::pe::D3dApi::D3D9 => crate::mtsp::engine::PipelineId::M9,
        crate::mtsp::pe::D3dApi::Unknown => crate::mtsp::engine::PipelineId::WineBare,
    }
}

fn detect_apps_in_prefix(prefix: &Path) -> Vec<AppDetection> {
    let roots = [
        prefix.join("drive_c").join("Program Files"),
        prefix.join("drive_c").join("Program Files (x86)"),
        prefix.join("drive_c").join("users"),
    ];
    let mut seen = HashSet::new();
    let mut detections = Vec::new();
    for root in roots {
        if !root.exists() {
            continue;
        }
        for entry in WalkDir::new(&root).max_depth(5).into_iter().flatten() {
            let path = entry.path();
            if !path.is_file()
                || path.extension().map(|ext| ext.to_string_lossy().to_ascii_lowercase()) != Some("exe".into())
            {
                continue;
            }
            let name = path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
            if !is_probable_app_exe_path(&name, path) {
                continue;
            }
            let key = path.to_string_lossy().to_string();
            if seen.insert(key.clone()) {
                detections.push(AppDetection {
                    name: name.trim_end_matches(".exe").to_string(),
                    exe_path: key,
                    source: "prefix_scan".to_string(),
                });
            }
        }
    }
    detections.sort_by(|a, b| a.name.cmp(&b.name).then_with(|| a.exe_path.cmp(&b.exe_path)));
    detections
}

fn compatibility_matrix() -> Vec<CompatibilityCase> {
    let bottles = list_bottles().unwrap_or_default();
    let overrides = load_compatibility_overrides();
    let mut cases = vec![
        compatibility_case(
            "minecraft-installer",
            "Minecraft Installer",
            "32-bit .NET/WinRT bootstrapper",
            RuntimeProfile::Win32Dotnet,
            "needs_real_trace",
            "pending",
            "pending",
            "Wine Mono/.NET 4.x compatibility",
            find_bottle_for(&bottles, &["minecraft"]),
        ),
        compatibility_case(
            "itch-windows-games",
            "Itch.io Windows Games",
            "indie installer/extracted demo",
            RuntimeProfile::GameInstall,
            "untested",
            "pending",
            "pending",
            "VC runtime or DirectX June 2010 varies by game",
            find_bottle_for(&bottles, &["itch"]),
        ),
        compatibility_case(
            "unity-demos",
            "Unity Demos",
            "Unity player demo",
            RuntimeProfile::M11,
            "untested",
            "pending",
            "pending",
            "VC runtime and Unity launcher handoff",
            find_bottle_for(&bottles, &["unity", "demo"]),
        ),
        compatibility_case(
            "unreal-demos",
            "Unreal Demos",
            "Unreal packaged demo",
            RuntimeProfile::M12,
            "untested",
            "pending",
            "pending",
            "VC runtime, DirectX payloads, D3D12 route",
            find_bottle_for(&bottles, &["unreal"]),
        ),
        compatibility_case(
            "electron-launchers",
            "Electron Launchers",
            "Squirrel/Electron launcher",
            RuntimeProfile::Launcher,
            "untested",
            "pending",
            "pending",
            "WebView/Gecko/browser runtime varies",
            find_bottle_for(&bottles, &["electron", "squirrel", "launcher"]),
        ),
        compatibility_case(
            "gog-offline-installers",
            "GOG Offline Installers",
            "offline game installer",
            RuntimeProfile::GameInstall,
            "untested",
            "pending",
            "pending",
            "VC runtime and DirectX June 2010",
            find_bottle_for(&bottles, &["gog"]),
        ),
        compatibility_case(
            "webview-launchers",
            "Epic/EA/Ubisoft Launchers",
            "store-adjacent launcher",
            RuntimeProfile::Webview,
            "untested",
            "pending",
            "pending",
            "WebView2 under Wine remains risky",
            find_bottle_for(&bottles, &["epic", "ea", "ubisoft", "webview"]),
        ),
        compatibility_case(
            "vc-redists",
            "VC Runtime Redistributables (2015-2022 + 2013)",
            "runtime installer",
            RuntimeProfile::Plain,
            "supported",
            "not_applicable",
            "not_applicable",
            "local redistributable asset required",
            None,
        ),
    ];

    for bottle in bottles {
        if !cases.iter().any(|case| case.bottle_id.as_deref() == Some(bottle.id.as_str())) {
            cases.push(compatibility_case(
                &bottle.id,
                &bottle.name,
                match bottle.bottle_type {
                    BottleType::Steam => "steam game bottle",
                    BottleType::Installer => "installer bottle",
                    BottleType::SharpApp => "sharp app bottle",
                    BottleType::Utility => "utility bottle",
                },
                bottle.runtime_profile,
                bottle.last_launch_status.as_deref().unwrap_or("not_run"),
                if bottle.installed_app_detections.is_empty() { "no" } else { "yes" },
                "unknown",
                &missing_components_summary(&bottle.installed_components),
                Some(bottle.id.clone()),
            ));
        }
    }
    for case in &mut cases {
        case.per_game_prefix_recommendation = per_game_prefix_recommendation(case);
        if let Some(saved) = overrides.get(&case.id) {
            apply_compatibility_override(case, saved);
        }
    }
    cases
}

#[allow(clippy::too_many_arguments)]
fn compatibility_case(
    id: &str,
    name: &str,
    case_type: &str,
    required_profile: RuntimeProfile,
    installer_opens: &str,
    final_app_detected: &str,
    final_app_launches: &str,
    known_missing_runtime: &str,
    bottle_id: Option<String>,
) -> CompatibilityCase {
    CompatibilityCase {
        id: id.to_string(),
        name: name.to_string(),
        case_type: case_type.to_string(),
        required_profile,
        installer_opens: installer_opens.to_string(),
        final_app_detected: final_app_detected.to_string(),
        final_app_launches: final_app_launches.to_string(),
        known_missing_runtime: known_missing_runtime.to_string(),
        bottle_id,
        notes: String::new(),
        evidence_updated_at: None,
        per_game_prefix_recommendation: String::new(),
    }
}

fn load_compatibility_overrides() -> HashMap<String, CompatibilityCase> {
    let path = compatibility_matrix_path();
    let Ok(data) = fs::read_to_string(path) else {
        return HashMap::new();
    };
    serde_json::from_str::<Vec<CompatibilityCase>>(&data)
        .unwrap_or_default()
        .into_iter()
        .map(|case| (case.id.clone(), case))
        .collect()
}

fn save_compatibility_overrides(
    overrides: &HashMap<String, CompatibilityCase>,
) -> Result<(), Box<dyn std::error::Error>> {
    fs::create_dir_all(bottles_root())?;
    let mut values = overrides.values().cloned().collect::<Vec<_>>();
    values.sort_by(|a, b| a.name.cmp(&b.name).then_with(|| a.id.cmp(&b.id)));
    fs::write(compatibility_matrix_path(), serde_json::to_string_pretty(&values)?)?;
    Ok(())
}

fn record_compatibility_case(
    id: &str,
    body: &serde_json::Map<String, Value>,
) -> Result<Vec<CompatibilityCase>, Box<dyn std::error::Error>> {
    let current =
        compatibility_matrix().into_iter().find(|case| case.id == id).ok_or("compatibility case not found")?;
    let mut overrides = load_compatibility_overrides();
    let mut saved = overrides.remove(id).unwrap_or(current);
    if let Some(value) = body.get("installerOpens").and_then(|v| v.as_str()) {
        saved.installer_opens = value.to_string();
    }
    if let Some(value) = body.get("finalAppDetected").and_then(|v| v.as_str()) {
        saved.final_app_detected = value.to_string();
    }
    if let Some(value) = body.get("finalAppLaunches").and_then(|v| v.as_str()) {
        saved.final_app_launches = value.to_string();
    }
    if let Some(value) = body.get("knownMissingRuntime").and_then(|v| v.as_str()) {
        saved.known_missing_runtime = value.to_string();
    }
    if let Some(value) = body.get("notes").and_then(|v| v.as_str()) {
        saved.notes = value.to_string();
    }
    saved.evidence_updated_at = Some(timestamp_secs());
    saved.per_game_prefix_recommendation = per_game_prefix_recommendation(&saved);
    overrides.insert(id.to_string(), saved);
    save_compatibility_overrides(&overrides)?;
    Ok(compatibility_matrix())
}

fn apply_compatibility_override(case: &mut CompatibilityCase, saved: &CompatibilityCase) {
    case.installer_opens = saved.installer_opens.clone();
    case.final_app_detected = saved.final_app_detected.clone();
    case.final_app_launches = saved.final_app_launches.clone();
    case.known_missing_runtime = saved.known_missing_runtime.clone();
    case.notes = saved.notes.clone();
    case.evidence_updated_at = saved.evidence_updated_at.clone();
    case.per_game_prefix_recommendation = per_game_prefix_recommendation(case);
}

fn per_game_prefix_recommendation(case: &CompatibilityCase) -> String {
    if !case.id.starts_with("steam_") && !case.case_type.contains("steam") {
        return "not_applicable".to_string();
    }
    let launch = case.final_app_launches.to_ascii_lowercase();
    let missing = case.known_missing_runtime.to_ascii_lowercase();
    if launch == "yes" && (missing == "none" || missing == "not_applicable") {
        "shared_prefix_ok".to_string()
    } else if launch == "no" || missing.contains("dotnet") || missing.contains("webview") || missing.contains("directx")
    {
        "candidate_for_per_game_prefix".to_string()
    } else {
        "needs_more_evidence".to_string()
    }
}

fn redist_source_guides() -> Vec<RedistSourceGuide> {
    let home = dirs::home_dir().unwrap_or_default();
    let redist = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("redist");
    vec![
        RedistSourceGuide {
            id: "dotnet40".to_string(),
            name: ".NET Framework 4.0 Runtime".to_string(),
            source_url: "https://dotnet.microsoft.com/en-us/download/dotnet-framework/net40".to_string(),
            local_targets: vec![
                redist.join("DotNet").join("4.0").join("dotNetFx40_Client_x86_x64.exe").to_string_lossy().to_string(),
                redist.join("DotNet").join("4.0").join("dotNetFx40_Full_x86_x64.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_user_supplied".to_string(),
            notes: "Use for CLR v4 games that ship C++/CLI launchers, including older UE3 titles such as Goat Simulator.".to_string(),
        },
        RedistSourceGuide {
            id: "dotnet48".to_string(),
            name: ".NET Framework 4.8 Runtime".to_string(),
            source_url: "https://dotnet.microsoft.com/en-us/download/dotnet-framework/net48".to_string(),
            local_targets: vec![
                redist.join("DotNet").join("4.8").join("ndp48-x86-x64-allos-enu.exe").to_string_lossy().to_string(),
                redist.join("DotNet").join("4.8").join("NDP48-x86-x64-AllOS-ENU.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_user_supplied".to_string(),
            notes: "Use the official offline runtime installer; MetalSharp does not vendor it in this PR.".to_string(),
        },
        RedistSourceGuide {
            id: "webview2".to_string(),
            name: "Microsoft Edge WebView2 Evergreen Runtime".to_string(),
            source_url: "https://developer.microsoft.com/en-us/microsoft-edge/webview2/".to_string(),
            local_targets: vec![
                redist.join("MicrosoftEdgeWebView2RuntimeInstallerX64.exe").to_string_lossy().to_string(),
                redist.join("MicrosoftEdgeWebView2RuntimeInstallerX86.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_user_supplied".to_string(),
            notes: "Use the Evergreen Standalone Installer for offline scenarios; Wine compatibility still needs per-installer evidence.".to_string(),
        },
        RedistSourceGuide {
            id: "vcrun2019".to_string(),
            name: "Latest Microsoft Visual C++ Redistributable (2015-2022)".to_string(),
            source_url: "https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist".to_string(),
            local_targets: vec![
                redist.join("VC_redist.x64.exe").to_string_lossy().to_string(),
                redist.join("VC_redist.x86.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Prefer Steam CommonRedist when present; otherwise use Microsoft's latest supported redist links. Installs vcruntime140, vcruntime140_1, msvcp140 family, concrt140, vcomp140.".to_string(),
        },
        RedistSourceGuide {
            id: "vcrun2010".to_string(),
            name: "Microsoft Visual C++ 2010 Redistributable (10.0)".to_string(),
            source_url: "https://www.microsoft.com/download/details.aspx?id=26999".to_string(),
            local_targets: vec![
                redist.join("vcredist").join("2010").join("vcredist_x64.exe").to_string_lossy().to_string(),
                redist.join("vcredist").join("2010").join("vcredist_x86.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Installs msvcr100.dll and msvcp100.dll. Required by older UE3/C++-CLI titles such as Goat Simulator.".to_string(),
        },
        RedistSourceGuide {
            id: "vcrun2013".to_string(),
            name: "Microsoft Visual C++ 2013 Redistributable (12.0)".to_string(),
            source_url: "https://support.microsoft.com/en-us/topic/update-for-visual-c-2013-redistributable-package-d8ccd6a4-4a90-bdbd-a060-8276036c0738".to_string(),
            local_targets: vec![
                redist.join("vcredist_x64.exe").to_string_lossy().to_string(),
                redist.join("vcredist_x86.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Installs msvcr120.dll and msvcp120.dll. Required by some older titles.".to_string(),
        },
        RedistSourceGuide {
            id: "directx_jun2010".to_string(),
            name: "DirectX June 2010 Runtime".to_string(),
            source_url: "https://www.microsoft.com/download/details.aspx?id=8109".to_string(),
            local_targets: vec![redist.join("DirectX").join("Jun2010").join("DXSETUP.exe").to_string_lossy().to_string()],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Prefer Steam CommonRedist game payloads; local offline payload should contain DXSETUP.exe.".to_string(),
        },
        RedistSourceGuide {
            id: "openal".to_string(),
            name: "OpenAL Runtime".to_string(),
            source_url: "https://www.openal.org/downloads/".to_string(),
            local_targets: vec![
                redist.join("OpenAL").join("oalinst.exe").to_string_lossy().to_string(),
                redist.join("oalinst.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Prefer Steam CommonRedist when available; older games often ship oalinst.exe beside installscript.vdf.".to_string(),
        },
        RedistSourceGuide {
            id: "xna".to_string(),
            name: "Microsoft XNA Framework 4.0".to_string(),
            source_url: "https://www.microsoft.com/download/details.aspx?id=20914".to_string(),
            local_targets: vec![
                redist.join("XNA").join("4.0").join("xnafx40_redist.msi").to_string_lossy().to_string(),
                redist.join("XNA").join("4.0").join("xnafx40_redist.exe").to_string_lossy().to_string(),
                crate::platform::metalsharp_home_dir()
                    .join("bottles")
                    .join("*")
                    .join("installers")
                    .join("xnafx40_redist.msi")
                    .to_string_lossy()
                    .to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Use local, Sharp Library staged, or Steam-provided XNA 4.0 redist assets; this stays receipt-driven per bottle.".to_string(),
        },
        RedistSourceGuide {
            id: "physx".to_string(),
            name: "NVIDIA PhysX Legacy Runtime".to_string(),
            source_url: "https://www.nvidia.com/en-us/drivers/physx/physx-9-13-0604-legacy-driver/".to_string(),
            local_targets: vec![
                redist.join("PhysX").join("PhysX-9.12.1031-SystemSoftware.msi").to_string_lossy().to_string(),
                redist.join("PhysX").join("PhysX-9.13.0604-SystemSoftware.msi").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Only install when a game's install script or bundled redist explicitly requires legacy PhysX.".to_string(),
        },
    ]
}

fn find_bottle_for(bottles: &[BottleManifest], needles: &[&str]) -> Option<String> {
    bottles
        .iter()
        .find(|bottle| {
            let haystack = format!(
                "{} {} {}",
                bottle.id,
                bottle.name,
                bottle.source_installer_path.as_deref().unwrap_or_default()
            )
            .to_ascii_lowercase();
            needles.iter().any(|needle| haystack.contains(needle))
        })
        .map(|bottle| bottle.id.clone())
}

fn missing_components_summary(components: &[RuntimeComponent]) -> String {
    let missing = components
        .iter()
        .filter(|component| !component_ready(component))
        .map(|component| component.id.as_str())
        .collect::<Vec<_>>();
    if missing.is_empty() {
        "none".to_string()
    } else {
        missing.join(", ")
    }
}

fn detect_apps_in_game_dir(game_dir: &Path) -> Vec<AppDetection> {
    if !game_dir.exists() {
        return Vec::new();
    }
    let mut seen = HashSet::new();
    let mut detections = Vec::new();
    for entry in WalkDir::new(game_dir).max_depth(4).into_iter().flatten() {
        let path = entry.path();
        if !path.is_file()
            || path.extension().map(|ext| ext.to_string_lossy().to_ascii_lowercase()) != Some("exe".into())
        {
            continue;
        }
        let name = path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
        if !is_probable_app_exe_path(&name, path) {
            continue;
        }
        let key = path.to_string_lossy().to_string();
        if seen.insert(key.clone()) {
            detections.push(AppDetection {
                name: name.trim_end_matches(".exe").to_string(),
                exe_path: key,
                source: "steam_game_scan".to_string(),
            });
        }
    }
    detections.sort_by(|a, b| a.name.cmp(&b.name).then_with(|| a.exe_path.cmp(&b.exe_path)));
    detections
}

fn detect_game_runtime_assets(game_dir: &Path) -> Vec<BottleRuntimeAsset> {
    if !game_dir.exists() {
        return Vec::new();
    }
    let mut seen = HashSet::new();
    let mut assets = Vec::new();
    for entry in WalkDir::new(game_dir).max_depth(5).into_iter().flatten() {
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let name = path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
        let lower_name = name.to_ascii_lowercase();
        let lower_path = path.to_string_lossy().to_ascii_lowercase();
        let kind = classify_game_runtime_asset(&lower_name, &lower_path).or_else(|| {
            if lower_path.contains("_commonredist") || lower_path.contains("commonredist") {
                classify_redist_asset(&lower_name)
            } else if lower_name == "installscript.vdf" {
                Some("installscript".to_string())
            } else {
                None
            }
        });
        let Some(kind) = kind else {
            continue;
        };
        let source_path = normalized_existing_path_string(path);
        if seen.insert(source_path.clone()) {
            assets.push(BottleRuntimeAsset {
                id: format!("{}:{}", kind, name),
                kind,
                source_path,
                present: path.exists(),
            });
        }
    }
    assets.sort_by(|a, b| a.kind.cmp(&b.kind).then_with(|| a.source_path.cmp(&b.source_path)));
    assets
}

fn classify_game_runtime_asset(lower_name: &str, lower_path: &str) -> Option<String> {
    if lower_path.contains("easyanticheat") || lower_name.contains("easyanticheat") {
        if lower_path.contains("easyanticheat_eos") || lower_name.contains("eos") {
            Some("easyanticheat_eos".to_string())
        } else {
            Some("easyanticheat".to_string())
        }
    } else if lower_path.contains("battleye")
        || lower_path.contains("battle-eye")
        || lower_name.contains("beservice")
        || lower_name.contains("beclient")
        || lower_name.contains("bedaisy")
    {
        Some("battleye".to_string())
    } else {
        None
    }
}

fn classify_redist_asset(lower_name: &str) -> Option<String> {
    if lower_name.ends_with(".vdf") {
        Some("installscript".to_string())
    } else if lower_name.contains("vc_redist") || lower_name.contains("vcredist") {
        Some("vcredist".to_string())
    } else if lower_name.contains("dotnet") || lower_name.starts_with("ndp") {
        Some("dotnet".to_string())
    } else if lower_name.contains("directx") || lower_name == "dxsetup.exe" || lower_name.ends_with(".cab") {
        Some("directx".to_string())
    } else if lower_name.contains("webview") {
        Some("webview2".to_string())
    } else if lower_name.contains("openal") || lower_name == "oalinst.exe" {
        Some("openal".to_string())
    } else if lower_name.contains("xnafx") || lower_name.contains("xna") {
        Some("xna".to_string())
    } else if lower_name.contains("physx") {
        Some("physx".to_string())
    } else {
        None
    }
}

fn is_probable_app_exe(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    let builtins = [
        "iexplore.exe",
        "wmplayer.exe",
        "wordpad.exe",
        "notepad.exe",
        "regedit.exe",
        "winebrowser.exe",
        "control.exe",
        "cmd.exe",
        "cookie_exporter.exe",
        "elevated_tracing_service.exe",
        "elevation_service.exe",
        "ie_to_edge_stub.exe",
        "microsoftedgecomregistershellarm64.exe",
        "mscopilot.exe",
        "msedge.exe",
        "msedge_proxy.exe",
        "msedge_pwa_launcher.exe",
        "msedgewebview2.exe",
        "upc.exe",
        "uc_connector.exe",
    ];
    lower.ends_with(".exe")
        && !builtins.contains(&lower.as_str())
        && !lower.starts_with("microsoftedgewebview_")
        && !lower.contains("setup")
        && !lower.contains("install")
        && !lower.contains("unins")
        && !lower.contains("vcredist")
        && !lower.contains("crash")
        && !lower.contains("extension")
        && !lower.contains("helper")
        && !lower.contains("service")
        && !lower.contains("shareplay")
        && !lower.contains("update")
        && !lower.contains("webcore")
}

fn is_probable_app_exe_path(name: &str, path: &Path) -> bool {
    if !is_probable_app_exe(name) {
        return false;
    }
    let lower_path = path.to_string_lossy().to_ascii_lowercase();
    !lower_path.contains("/microsoft/edgecore/")
        && !lower_path.contains("/microsoft/edgeupdate/")
        && !lower_path.contains("/microsoft/edgewebview/")
}

fn read_ascii_strings(path: &Path, max_bytes: usize) -> Vec<String> {
    let Ok(data) = fs::read(path) else {
        return Vec::new();
    };
    let mut strings = Vec::new();
    let mut current = Vec::new();
    for byte in data.into_iter().take(max_bytes) {
        if byte.is_ascii_graphic() || byte == b' ' {
            current.push(byte);
        } else {
            if current.len() >= 4 {
                strings.push(String::from_utf8_lossy(&current).to_string());
            }
            current.clear();
        }
    }
    if current.len() >= 4 {
        strings.push(String::from_utf8_lossy(&current).to_string());
    }
    strings
}

fn timestamp_secs() -> String {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs().to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn installer_bottle_ids_are_stable_for_source_path() {
        let path = Path::new("/tmp/MinecraftInstaller.exe");
        assert_eq!(installer_bottle_id(path), installer_bottle_id(path));
        assert!(installer_bottle_id(path).starts_with("installer_"));
    }

    #[test]
    fn fresh_installer_bottle_ids_keep_source_lineage() {
        let path = Path::new("/tmp/MinecraftInstaller.exe");
        let stable = installer_bottle_id(path);
        let fresh = fresh_installer_bottle_id(path);

        assert_ne!(fresh, stable);
        assert!(fresh.starts_with(&format!("{}_fresh_", stable)));
    }

    #[test]
    fn dxmt_preference_is_treated_as_auto_not_a_saved_override() {
        let manifest = BottleManifest {
            id: "steam_17410".into(),
            name: "Mirror's Edge".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(17410),
            prefix_path: "/tmp/metalsharp-test-prefix".into(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M11,
            preferred_pipeline: Some("dxmt".into()),
            installed_components: Vec::new(),
            source_installer_path: None,
            installer_kind: None,
            game_install_path: None,
            runtime_assets: Vec::new(),
            installed_app_detections: Vec::new(),
            health: BottleHealth::Ready,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: "0".into(),
            updated_at: "0".into(),
        };

        assert_eq!(manifest_preferred_pipeline(&manifest), None);
    }

    #[test]
    fn steam_profile_route_ids_are_precise_for_saved_compatdata() {
        assert_eq!(pipeline_preference_id(crate::mtsp::engine::PipelineId::M11), "m11");
        assert_eq!(pipeline_preference_id(crate::mtsp::engine::PipelineId::M12), "m12");
    }

    #[test]
    fn passive_steam_refresh_respects_saved_pipeline_preference() {
        let manifest = BottleManifest {
            id: steam_game_bottle_id(17410),
            name: "Mirror's Edge".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(17410),
            prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M9,
            preferred_pipeline: Some("m9".into()),
            installed_components: default_components_for(RuntimeProfile::M9),
            source_installer_path: None,
            installer_kind: None,
            game_install_path: None,
            runtime_assets: Vec::new(),
            installed_app_detections: Vec::new(),
            health: BottleHealth::Ready,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: "0".into(),
            updated_at: "0".into(),
        };

        assert_eq!(
            effective_pipeline_for_bottle_refresh(Some(&manifest), crate::mtsp::engine::PipelineId::M11, true),
            crate::mtsp::engine::PipelineId::M9
        );
        assert_eq!(
            effective_pipeline_for_bottle_refresh(Some(&manifest), crate::mtsp::engine::PipelineId::M11, false),
            crate::mtsp::engine::PipelineId::M11
        );
    }

    #[test]
    fn win32_dotnet_profile_tracks_expected_components() {
        let components = default_components_for(RuntimeProfile::Win32Dotnet);
        let ids = components.iter().map(|c| c.id.as_str()).collect::<Vec<_>>();
        assert!(ids.contains(&"wine-mono"));
        assert!(ids.contains(&"dotnet48"));
        assert!(ids.contains(&"vcrun2019"));
    }

    #[test]
    fn runtime_profile_definitions_are_declarative() {
        let win32 = runtime_profile_definition(RuntimeProfile::Win32Dotnet);
        assert_eq!(win32.arch, BottleArch::Win32);
        assert_eq!(win32.launch_pipeline, crate::mtsp::engine::PipelineId::M9);
        assert!(win32.components.contains(&"dotnet48".to_string()));

        let profiles = runtime_profile_definitions();
        assert!(profiles.iter().any(|profile| profile.id == RuntimeProfile::GameInstall));
        assert!(profiles.iter().any(|profile| profile.id == RuntimeProfile::M10));
        assert!(profiles.iter().any(|profile| profile.id == RuntimeProfile::Webview));
        assert!(profiles.iter().any(|profile| profile.id == RuntimeProfile::FnaArm64));
        assert!(profiles.iter().any(|profile| profile.id == RuntimeProfile::FnaX86));

        let webview = runtime_profile_definition(RuntimeProfile::Webview);
        assert_eq!(webview.launch_pipeline, crate::mtsp::engine::PipelineId::WineBare);
        assert!(webview.components.contains(&"dotnet48".to_string()));
    }

    #[test]
    fn fna_profiles_pin_the_known_mono_lanes() {
        let arm64 = runtime_profile_definition(RuntimeProfile::FnaArm64);
        let x86 = runtime_profile_definition(RuntimeProfile::FnaX86);

        assert!(!arm64.wineboot);
        assert!(arm64.components.contains(&"mono-arm64".to_string()));
        assert_eq!(arm64.mono_runtime.as_ref().expect("arm64 mono profile").known_version, "6.14.1");
        assert!(x86.components.contains(&"mono-x86".to_string()));
        assert_eq!(x86.mono_runtime.as_ref().expect("x86 mono profile").known_version, "6.12.0.122");
    }

    #[test]
    fn m10_pipeline_maps_to_d3d10_runtime_profile() {
        let profile = runtime_profile_definition(RuntimeProfile::M10);
        let components = default_components_for(RuntimeProfile::M10);
        let ids = components.iter().map(|component| component.id.as_str()).collect::<Vec<_>>();

        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::M10), RuntimeProfile::M10);
        assert_eq!(profile.launch_pipeline, crate::mtsp::engine::PipelineId::M10);
        assert!(ids.contains(&"d3d10"));
        assert!(ids.contains(&"d3d10_1"));
        assert!(ids.contains(&"dxgi"));
    }

    #[test]
    fn bottle_manifest_atomic_write_replaces_complete_json() {
        let dir = test_dir("atomic-manifest");
        fs::create_dir_all(&dir).expect("create manifest dir");
        let manifest = dir.join(MANIFEST_FILE);

        write_bottle_manifest_atomic(&manifest, br#"{"id":"first"}"#).expect("write first manifest");
        write_bottle_manifest_atomic(&manifest, br#"{"id":"second","health":"ready"}"#).expect("write second manifest");

        assert_eq!(fs::read_to_string(&manifest).expect("read manifest"), r#"{"id":"second","health":"ready"}"#);
        assert!(fs::read_dir(&dir).expect("read manifest dir").all(|entry| !entry
            .expect("manifest entry")
            .file_name()
            .to_string_lossy()
            .contains(".tmp-")));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn game_install_profile_tracks_directx_redist() {
        let components = default_components_for(RuntimeProfile::GameInstall);
        assert!(components.iter().any(|component| component.id == "directx_jun2010"));
    }

    #[test]
    fn classifier_maps_32_bit_clr_installers_to_win32_dotnet() {
        let dir = test_dir("classifier-dotnet");
        fs::create_dir_all(&dir).expect("create test dir");
        let exe = dir.join("DotnetBootstrapper.exe");
        let mut data = test_pe(0x014c, 0x10b);
        data.extend_from_slice(b"System.Runtime.WindowsRuntime mscoree");
        fs::write(&exe, data).expect("write test installer");

        let classification = classify_installer(&exe);

        assert_eq!(classification.arch, BottleArch::Win32);
        assert_eq!(classification.pipeline, crate::mtsp::engine::PipelineId::M9);
        assert_eq!(classification.runtime_profile, RuntimeProfile::Win32Dotnet);
        assert!(classification.hints.contains(&"dotnet_or_clr".to_string()));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn classifier_maps_minecraft_to_java_launcher_before_dotnet_fallback() {
        let dir = test_dir("classifier-minecraft");
        fs::create_dir_all(&dir).expect("create test dir");
        let exe = dir.join("MinecraftInstaller.exe");
        let mut data = test_pe(0x014c, 0x10b);
        data.extend_from_slice(b"System.Runtime.WindowsRuntime mscoree");
        fs::write(&exe, data).expect("write test installer");

        let classification = classify_installer(&exe);

        assert_eq!(classification.arch, BottleArch::Win32);
        assert_eq!(classification.pipeline, crate::mtsp::engine::PipelineId::WineBare);
        assert_eq!(classification.installer_kind, InstallerKind::Java);
        assert_eq!(classification.runtime_profile, RuntimeProfile::JavaLauncher);
        assert!(classification.hints.contains(&"known_launcher:minecraft".to_string()));
        assert!(classification.hints.contains(&"dotnet_or_clr".to_string()));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn classifier_maps_known_store_launchers_to_webview_or_launcher_profiles() {
        let dir = test_dir("classifier-known-launchers");
        fs::create_dir_all(&dir).expect("create test dir");
        let cases = [
            ("EAappInstaller.exe", RuntimeProfile::Webview, "known_launcher:ea_app"),
            ("UbisoftConnectInstaller.exe", RuntimeProfile::Webview, "known_launcher:ubisoft_connect"),
            ("Battle.net-Setup.exe", RuntimeProfile::Webview, "known_launcher:battle_net"),
            ("EpicGamesLauncherInstaller.exe", RuntimeProfile::Webview, "known_launcher:epic_games"),
            ("Rockstar-Games-Launcher.exe", RuntimeProfile::Webview, "known_launcher:rockstar"),
            ("GOG_Galaxy_2.0.exe", RuntimeProfile::Launcher, "known_launcher:gog_galaxy"),
        ];

        for (name, profile, hint) in cases {
            let exe = dir.join(name);
            fs::write(&exe, test_pe(0x8664, 0x20b)).expect("write test launcher");

            let classification = classify_installer(&exe);

            assert_eq!(classification.runtime_profile, profile, "{}", name);
            assert_eq!(classification.pipeline, crate::mtsp::engine::PipelineId::WineBare, "{}", name);
            assert!(classification.hints.contains(&hint.to_string()), "{}", name);
        }
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn classifier_maps_msi_packages_to_game_install_profile() {
        let dir = test_dir("classifier-msi");
        fs::create_dir_all(&dir).expect("create test dir");
        let msi = dir.join("DemoSetup.msi");
        fs::write(&msi, b"Windows Installer").expect("write msi");

        let classification = classify_installer(&msi);

        assert_eq!(classification.installer_kind, InstallerKind::Msi);
        assert_eq!(classification.pipeline, crate::mtsp::engine::PipelineId::WineBare);
        assert_eq!(classification.runtime_profile, RuntimeProfile::GameInstall);
        assert!(classification.hints.contains(&"msi_package".to_string()));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn classifier_detects_squirrel_electron_launchers() {
        let dir = test_dir("classifier-squirrel");
        fs::create_dir_all(&dir).expect("create test dir");
        let exe = dir.join("LauncherSetup.exe");
        let mut data = test_pe(0x8664, 0x20b);
        data.extend_from_slice(b"Squirrel app.asar electron");
        fs::write(&exe, data).expect("write test installer");

        let classification = classify_installer(&exe);

        assert_eq!(classification.installer_kind, InstallerKind::Squirrel);
        assert_eq!(classification.runtime_profile, RuntimeProfile::Launcher);
        assert!(classification.hints.iter().any(|hint| hint.starts_with("installer_kind:")));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn component_merge_preserves_existing_state() {
        let existing = vec![RuntimeComponent { id: "wine-mono".into(), state: ComponentState::NeedsRepair }];
        let merged = merge_components(existing, default_components_for(RuntimeProfile::Win32Dotnet));
        let mono = merged.iter().find(|c| c.id == "wine-mono").expect("wine-mono component");
        assert_eq!(mono.state, ComponentState::NeedsRepair);
        assert!(merged.iter().any(|c| c.id == "dotnet48"));
    }

    #[test]
    fn profile_rebuild_drops_stale_components_but_keeps_overlap_state() {
        let existing = vec![
            RuntimeComponent { id: "d3d12".into(), state: ComponentState::Missing },
            RuntimeComponent { id: "vcrun2019".into(), state: ComponentState::Installed },
        ];

        let rebuilt = rebuild_components_for_profile(&existing, RuntimeProfile::Plain);

        assert!(!rebuilt.iter().any(|component| component.id == "d3d12"));
        assert!(!rebuilt.iter().any(|component| component.id == "vcrun2019"));

        let rebuilt = rebuild_components_for_profile(&existing, RuntimeProfile::M9);
        let vcrun = rebuilt.iter().find(|component| component.id == "vcrun2019").expect("vcrun component");
        assert_eq!(vcrun.state, ComponentState::Installed);
        assert!(!rebuilt.iter().any(|component| component.id == "d3d12"));
    }

    #[test]
    fn missing_dotnet_components_produce_actions() {
        let components = default_components_for(RuntimeProfile::Win32Dotnet);
        let inspected = inspect_components(Path::new("/tmp/definitely-missing-metalsharp-prefix"), &components);
        let actions = component_actions(&inspected);

        assert!(actions.iter().any(|a| a.id == "wine-mono"));
        assert!(actions.iter().any(|a| a.id == "dotnet48"));
        assert!(!components_ready(&inspected));
    }

    #[test]
    fn dotnet4_components_require_native_clr_not_only_framework_folder() {
        let dir = test_dir("dotnet4-native-clr");
        let framework = dir.join("drive_c/windows/Microsoft.NET/Framework/v4.0.30319");
        fs::create_dir_all(&framework).expect("create framework dir");
        fs::write(framework.join("mscorlib.dll"), b"mono-backed facade").expect("write mscorlib");

        assert_eq!(inspect_component_state(&dir, "dotnet40", ComponentState::Unknown), ComponentState::NeedsRepair);
        assert_eq!(inspect_component_state(&dir, "dotnet48", ComponentState::Unknown), ComponentState::NeedsRepair);

        fs::write(framework.join("clr.dll"), b"native clr").expect("write clr");
        assert_eq!(inspect_component_state(&dir, "dotnet40", ComponentState::Unknown), ComponentState::Installed);
        assert_eq!(inspect_component_state(&dir, "dotnet48", ComponentState::Unknown), ComponentState::Installed);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn dotnet40_native_repair_clears_false_install_markers() {
        let reg = build_dotnet4_native_repair_reg();

        assert!(reg.contains(r"[HKEY_LOCAL_MACHINE\Software\Microsoft\NET Framework Setup\NDP\v4\Client]"));
        assert!(reg.contains(r"[HKEY_LOCAL_MACHINE\Software\Microsoft\NET Framework Setup\NDP\v4\Full]"));
        assert!(reg.contains(r"[HKEY_LOCAL_MACHINE\Software\Wow6432Node\Microsoft\NET Framework Setup\NDP\v4\Client]"));
        assert!(reg.contains(r"[HKEY_LOCAL_MACHINE\Software\Wow6432Node\Microsoft\NET Framework Setup\NDP\v4\Full]"));
        assert_eq!(reg.matches("\"Install\"=dword:00000000").count(), 4);
    }

    #[test]
    fn unknown_and_needs_repair_components_are_not_ready() {
        let components = vec![
            RuntimeComponent { id: "vcrun2019".into(), state: ComponentState::Unknown },
            RuntimeComponent { id: "directx_jun2010".into(), state: ComponentState::NeedsRepair },
        ];

        let actions = component_actions(&components);

        assert!(!components_ready(&components));
        assert_eq!(actions.len(), 2);
        assert!(missing_components_summary(&components).contains("vcrun2019"));
        assert!(missing_components_summary(&components).contains("directx_jun2010"));
    }

    #[test]
    fn absent_inspectable_redists_are_missing_not_unknown() {
        let prefix = test_dir("missing-redists-prefix");
        let components = vec![
            RuntimeComponent { id: "vcrun2019".into(), state: ComponentState::Unknown },
            RuntimeComponent { id: "directx_jun2010".into(), state: ComponentState::Unknown },
            RuntimeComponent { id: "corefonts".into(), state: ComponentState::Unknown },
            RuntimeComponent { id: "gecko".into(), state: ComponentState::Unknown },
        ];

        let inspected = inspect_components(&prefix, &components);

        assert!(inspected.iter().all(|component| component.state == ComponentState::Missing));
        assert!(!components_ready(&inspected));
        let _ = fs::remove_dir_all(prefix);
    }

    #[test]
    fn corefonts_requires_installed_font_files_not_just_fonts_directory() {
        let prefix = test_dir("corefonts-inspect");
        let fonts = prefix.join("drive_c").join("windows").join("Fonts");
        fs::create_dir_all(&fonts).expect("create fonts dir");

        assert_eq!(inspect_component_state(&prefix, "corefonts", ComponentState::Unknown), ComponentState::Missing);

        for name in ["arial.ttf", "times.ttf", "cour.ttf", "verdana.ttf"] {
            fs::write(fonts.join(name), b"font").expect("write font marker");
        }

        assert_eq!(inspect_component_state(&prefix, "corefonts", ComponentState::Unknown), ComponentState::Installed);
        let _ = fs::remove_dir_all(prefix);
    }

    #[test]
    fn resolver_uses_advertised_local_redist_targets() {
        let dir = test_dir("local-redist-targets");
        let _ = fs::remove_dir_all(&dir);
        let steam_redist = dir.join("steam-redist");
        let local_redist = dir.join("runtime-redist");
        let dotnet = local_redist.join("DotNet").join("4.8").join("NDP48-x86-x64-AllOS-ENU.exe");
        let dotnet40 = local_redist.join("DotNet").join("4.0").join("dotNetFx40_Client_x86_x64.exe");
        let vc = local_redist.join("VC_redist.x86.exe");
        let xna = local_redist.join("XNA").join("4.0").join("xnafx40_redist.msi");
        let physx = local_redist.join("PhysX").join("PhysX-9.12.1031-SystemSoftware.msi");
        fs::create_dir_all(dotnet.parent().expect("dotnet parent")).expect("create dotnet dir");
        fs::create_dir_all(dotnet40.parent().expect("dotnet40 parent")).expect("create dotnet40 dir");
        fs::create_dir_all(vc.parent().expect("vc parent")).expect("create vc dir");
        fs::create_dir_all(xna.parent().expect("xna parent")).expect("create xna dir");
        fs::create_dir_all(physx.parent().expect("physx parent")).expect("create physx dir");
        fs::write(&dotnet, b"dotnet").expect("write dotnet redist");
        fs::write(&dotnet40, b"dotnet40").expect("write dotnet40 redist");
        fs::write(&vc, b"vc").expect("write vc redist");
        fs::write(&xna, b"xna").expect("write xna redist");
        fs::write(&physx, b"physx").expect("write physx redist");

        let dotnet_installer =
            resolve_component_installer_from_roots("dotnet48", BottleArch::Wow64, &steam_redist, &local_redist)
                .expect("resolve local dotnet");
        let dotnet40_installer =
            resolve_component_installer_from_roots("dotnet40", BottleArch::Wow64, &steam_redist, &local_redist)
                .expect("resolve local dotnet40");
        let vc_installer =
            resolve_component_installer_from_roots("vcrun2019", BottleArch::Win32, &steam_redist, &local_redist)
                .expect("resolve local vc");
        let xna_installer =
            resolve_component_installer_from_roots("xna", BottleArch::Wow64, &steam_redist, &local_redist)
                .expect("resolve local xna");
        let physx_installer =
            resolve_component_installer_from_roots("physx", BottleArch::Wow64, &steam_redist, &local_redist)
                .expect("resolve local physx");

        assert_eq!(
            dotnet_installer.path.to_string_lossy().to_ascii_lowercase(),
            dotnet.to_string_lossy().to_ascii_lowercase()
        );
        assert_eq!(
            dotnet40_installer.path.to_string_lossy().to_ascii_lowercase(),
            dotnet40.to_string_lossy().to_ascii_lowercase()
        );
        assert_eq!(vc_installer.path, vc);
        assert_eq!(xna_installer.path, xna);
        assert_eq!(physx_installer.path, physx);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn xna_installed_state_accepts_wine_mono_gac_layout() {
        let prefix = test_dir("xna-mono-gac");
        let gac = prefix
            .join("drive_c")
            .join("windows")
            .join("mono")
            .join("mono-2.0")
            .join("lib")
            .join("mono")
            .join("gac")
            .join("Microsoft.Xna.Framework")
            .join("4.0.0.0__842cf8be1de50553");
        fs::create_dir_all(&gac).expect("create xna mono gac");
        fs::write(gac.join("Microsoft.Xna.Framework.dll"), b"xna").expect("write xna assembly");

        assert_eq!(inspect_component_state(&prefix, "xna", ComponentState::Missing), ComponentState::Installed);
        let _ = fs::remove_dir_all(prefix);
    }

    #[test]
    fn resolver_uses_sharp_library_xna_installer_bottle_payload() {
        let root = test_dir("sharp-xna-installer-source");
        let bottle_id = "installer_xna_test";
        let payload = root.join(bottle_id).join("installers").join("xnafx40_redist.msi");
        fs::create_dir_all(payload.parent().expect("payload parent")).expect("create payload dir");
        fs::write(&payload, b"xna").expect("write xna installer");
        let manifest = BottleManifest {
            id: bottle_id.into(),
            name: "xnafx40_redist".into(),
            custom_name: None,
            bottle_type: BottleType::Installer,
            steam_app_id: None,
            prefix_path: root.join(bottle_id).join("prefix").to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::Plain,
            preferred_pipeline: None,
            installed_components: Vec::new(),
            source_installer_path: None,
            installer_kind: Some(InstallerKind::Msi),
            game_install_path: None,
            runtime_assets: Vec::new(),
            installed_app_detections: Vec::new(),
            health: BottleHealth::NeedsRepair,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };
        fs::write(
            root.join(bottle_id).join(MANIFEST_FILE),
            serde_json::to_vec_pretty(&manifest).expect("serialize bottle"),
        )
        .expect("write bottle manifest");

        let installer =
            resolve_sharp_library_component_installer_from_root("xna", &root).expect("resolve xna installer");

        assert_eq!(installer.path, payload);
        assert_eq!(installer.args, vec!["/quiet".to_string(), "/norestart".to_string()]);
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn game_runtime_asset_xna_repair_preserves_msi_silent_args() {
        let manifest = BottleManifest {
            id: "steam_105600".into(),
            name: "Terraria".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(105600),
            prefix_path: "/tmp/metalsharp-test-prefix".into(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::FnaX86,
            preferred_pipeline: None,
            installed_components: vec![RuntimeComponent { id: "xna".into(), state: ComponentState::Missing }],
            source_installer_path: None,
            installer_kind: None,
            game_install_path: None,
            runtime_assets: vec![BottleRuntimeAsset {
                id: "xna:xnafx40_redist.msi".into(),
                kind: "xna".into(),
                source_path: "/tmp/_CommonRedist/XNA/4.0/xnafx40_redist.msi".into(),
                present: true,
            }],
            installed_app_detections: Vec::new(),
            health: BottleHealth::NeedsRepair,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };

        let installer = resolve_game_runtime_asset_installer(&manifest, "xna").expect("resolve game xna installer");

        assert_eq!(installer.path, PathBuf::from("/tmp/_CommonRedist/XNA/4.0/xnafx40_redist.msi"));
        assert_eq!(installer.args, vec!["/quiet".to_string(), "/norestart".to_string()]);
    }

    #[test]
    fn windows_version_component_inspects_wine_registry() {
        let prefix = test_dir("windows-version-registry");
        fs::create_dir_all(&prefix).expect("create prefix");
        fs::write(
            prefix.join("user.reg"),
            r#"
[Software\\Wine]
"Version"="win10"
"#,
        )
        .expect("write registry");

        assert_eq!(
            inspect_component_state(&prefix, "windows_version_win10", ComponentState::NeedsRepair),
            ComponentState::Installed
        );
        assert_eq!(
            inspect_component_state(&prefix, "windows_version_win7", ComponentState::NeedsRepair),
            ComponentState::Missing
        );
        let _ = fs::remove_dir_all(prefix);
    }

    #[test]
    fn windows_version_component_stays_unready_until_registry_matches() {
        let prefix = test_dir("windows-version-missing");
        let components =
            vec![RuntimeComponent { id: windows_version_component_id("win11"), state: ComponentState::NeedsRepair }];

        let inspected = inspect_components(&prefix, &components);

        assert_eq!(inspected[0].state, ComponentState::NeedsRepair);
        assert!(!components_ready(&inspected));
        let _ = fs::remove_dir_all(prefix);
    }

    #[test]
    fn steam_pipeline_maps_to_runtime_profile() {
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::Dxmt), RuntimeProfile::GameInstall);
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::M9), RuntimeProfile::M9);
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::M12), RuntimeProfile::M12);
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::FnaArm64), RuntimeProfile::FnaArm64);
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::WineBare), RuntimeProfile::Plain);
    }

    #[test]
    fn steam_runtime_doctor_rejects_missing_or_invalid_appids() {
        let missing = serde_json::Map::new();
        assert_eq!(handle_steam_runtime_doctor(&missing).get("ok").and_then(|v| v.as_bool()), Some(false));

        let mut string_appid = serde_json::Map::new();
        string_appid.insert("appid".into(), json!("620"));
        assert_eq!(handle_steam_runtime_doctor(&string_appid).get("ok").and_then(|v| v.as_bool()), Some(false));

        let mut zero_appid = serde_json::Map::new();
        zero_appid.insert("appid".into(), json!(0));
        assert_eq!(handle_steam_runtime_doctor(&zero_appid).get("ok").and_then(|v| v.as_bool()), Some(false));

        let mut out_of_range = serde_json::Map::new();
        out_of_range.insert("appid".into(), json!(u64::from(u32::MAX) + 1));
        assert_eq!(handle_steam_runtime_doctor(&out_of_range).get("ok").and_then(|v| v.as_bool()), Some(false));
    }

    #[test]
    fn app_detection_rejects_wine_builtins() {
        assert!(!is_probable_app_exe("iexplore.exe"));
        assert!(!is_probable_app_exe("wordpad.exe"));
        assert!(!is_probable_app_exe("msedgewebview2.exe"));
        assert!(!is_probable_app_exe("MicrosoftEdgeWebview_X64_148.0.3967.70.exe"));
        assert!(!is_probable_app_exe("UplayService.exe"));
        assert!(!is_probable_app_exe("UplayWebCore.exe"));
        assert!(!is_probable_app_exe("UpcElevationService.exe"));
        assert!(!is_probable_app_exe("UbisoftExtension.exe"));
        assert!(!is_probable_app_exe("upc.exe"));
        assert!(is_probable_app_exe("MinecraftLauncher.exe"));
        assert!(is_probable_app_exe("UbisoftConnect.exe"));
    }

    #[test]
    fn game_runtime_assets_detect_common_redist_payloads() {
        let dir = test_dir("game-redists");
        let redist = dir.join("_CommonRedist").join("vcredist").join("2019");
        let openal = dir.join("_CommonRedist").join("OpenAL");
        let eac = dir.join("Game").join("EasyAntiCheat");
        let battleye = dir.join("BattlEye");
        fs::create_dir_all(&redist).expect("create redist dir");
        fs::create_dir_all(&openal).expect("create openal dir");
        fs::create_dir_all(&eac).expect("create eac dir");
        fs::create_dir_all(&battleye).expect("create battleye dir");
        fs::write(redist.join("VC_redist.x86.exe"), b"redist").expect("write vcredist");
        fs::write(openal.join("oalinst.exe"), b"openal").expect("write openal");
        fs::write(eac.join("EasyAntiCheat_EOS_Setup.exe"), b"eac").expect("write eac setup");
        fs::write(
            eac.join("install_easyanticheat_eos_setup.bat"),
            b"call EasyAntiCheat_EOS_Setup.exe install 773d3a68f76f4b2ebebc5b4127bbad3e",
        )
        .expect("write eac installer script");
        fs::write(battleye.join("Install_BattlEye.bat"), b"call BEService.exe").expect("write battleye script");
        fs::write(battleye.join("BEService.exe"), b"battleye").expect("write battleye service");
        fs::write(
            redist.join("installscript.vdf"),
            br#"
"InstallScript"
{
  "Run Process"
  {
    "DXSETUP.exe" {}
    "xnafx40_redist.msi" {}
    "PhysX-9.12.1031-SystemSoftware.msi" {}
  }
}
"#,
        )
        .expect("write installscript");

        let assets = detect_game_runtime_assets(&dir);
        let inferred = infer_components_from_runtime_assets(&assets);
        let ids = inferred.iter().map(|component| component.id.as_str()).collect::<Vec<_>>();

        assert!(assets.iter().any(|asset| asset.kind == "vcredist"));
        assert!(assets.iter().any(|asset| asset.kind == "openal"));
        assert!(assets.iter().any(|asset| asset.kind == "installscript"));
        assert!(assets.iter().any(|asset| asset.kind == "easyanticheat_eos"));
        assert!(assets.iter().any(|asset| asset.kind == "battleye"));
        assert!(ids.contains(&"vcrun2019"));
        assert!(ids.contains(&"openal"));
        assert!(ids.contains(&"directx_jun2010"));
        assert!(ids.contains(&"xna"));
        assert!(ids.contains(&"physx"));
        assert!(ids.contains(&"easyanticheat_eos"));
        assert!(ids.contains(&"battleye"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn easyanticheat_eos_installer_uses_game_script_install_args() {
        let dir = test_dir("eac-eos-installer");
        fs::create_dir_all(&dir).expect("create eac dir");
        let setup = dir.join("EasyAntiCheat_EOS_Setup.exe");
        let script = dir.join("install_easyanticheat_eos_setup.bat");
        fs::write(&setup, b"eac").expect("write eac setup");
        fs::write(
            &script,
            b"@echo off\r\ncall EasyAntiCheat_EOS_Setup.exe install 773d3a68f76f4b2ebebc5b4127bbad3e\r\npause\r\n",
        )
        .expect("write eac script");

        let installer = easyanticheat_eos_installer_from_asset(&script).expect("resolve eac installer");

        assert_eq!(installer.path, setup);
        assert_eq!(installer.args, vec!["install".to_string(), "773d3a68f76f4b2ebebc5b4127bbad3e".to_string()]);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn easyanticheat_eos_installer_skips_checker_scripts() {
        let dir = test_dir("eac-eos-multiple-scripts");
        fs::create_dir_all(&dir).expect("create eac dir");
        let setup = dir.join("EasyAntiCheat_EOS_Setup.exe");
        let checker = dir.join("eacchecker.bat");
        let install = dir.join("install_easyanticheat_eos_setup.bat");
        fs::write(&setup, b"eac").expect("write eac setup");
        fs::write(&checker, b"reg query HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\EasyAntiCheat_EOS")
            .expect("write checker");
        fs::write(
            &install,
            b"@echo off\r\ncall EasyAntiCheat_EOS_Setup.exe install 789399aada914e66bb3c3facebc5d709\r\npause\r\n",
        )
        .expect("write install");
        let manifest = BottleManifest {
            id: "steam_1888160".into(),
            name: "ARMORED CORE VI FIRES OF RUBICON".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(1888160),
            prefix_path: "/tmp/metalsharp-test-prefix".into(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M11,
            preferred_pipeline: None,
            installed_components: vec![RuntimeComponent {
                id: "easyanticheat_eos".into(),
                state: ComponentState::Missing,
            }],
            source_installer_path: None,
            installer_kind: None,
            game_install_path: None,
            runtime_assets: vec![
                BottleRuntimeAsset {
                    id: "easyanticheat:eacchecker.bat".into(),
                    kind: "easyanticheat".into(),
                    source_path: checker.to_string_lossy().to_string(),
                    present: true,
                },
                BottleRuntimeAsset {
                    id: "easyanticheat_eos:install_easyanticheat_eos_setup.bat".into(),
                    kind: "easyanticheat_eos".into(),
                    source_path: install.to_string_lossy().to_string(),
                    present: true,
                },
                BottleRuntimeAsset {
                    id: "easyanticheat_eos:easyanticheat_eos_setup.exe".into(),
                    kind: "easyanticheat_eos".into(),
                    source_path: setup.to_string_lossy().to_string(),
                    present: true,
                },
            ],
            installed_app_detections: Vec::new(),
            health: BottleHealth::NeedsRepair,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };

        let installer =
            resolve_game_runtime_asset_installer(&manifest, "easyanticheat_eos").expect("resolve installer");

        assert_eq!(installer.path, setup);
        assert_eq!(installer.args, vec!["install".to_string(), "789399aada914e66bb3c3facebc5d709".to_string()]);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn game_runtime_asset_repair_ignores_non_installer_dlls() {
        let manifest = BottleManifest {
            id: "steam_123".into(),
            name: "Game 123".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(123),
            prefix_path: "/tmp/metalsharp-test-prefix".into(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M11,
            preferred_pipeline: None,
            installed_components: vec![RuntimeComponent { id: "battleye".into(), state: ComponentState::Missing }],
            source_installer_path: None,
            installer_kind: None,
            game_install_path: None,
            runtime_assets: vec![BottleRuntimeAsset {
                id: "battleye:BEClient_x64.dll".into(),
                kind: "battleye".into(),
                source_path: "/tmp/BattlEye/BEClient_x64.dll".into(),
                present: true,
            }],
            installed_app_detections: Vec::new(),
            health: BottleHealth::NeedsRepair,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };

        assert!(resolve_game_runtime_asset_installer(&manifest, "battleye").is_none());
    }

    #[test]
    fn wine_z_drive_path_quotes_unix_script_paths_for_cmd() {
        assert_eq!(
            wine_z_drive_path(Path::new("/Volumes/AverySSD/Game/BattlEye/Install_BattlEye.bat")),
            "Z:\\Volumes\\AverySSD\\Game\\BattlEye\\Install_BattlEye.bat"
        );
    }

    #[test]
    fn steam_bottle_ids_are_appid_scoped() {
        assert_eq!(steam_game_bottle_id(620), "steam_620");
        assert_ne!(steam_game_bottle_id(620), steam_game_bottle_id(504230));
    }

    #[test]
    fn steam_game_bottles_bind_to_shared_steam_launch_prefix() {
        let manifest = BottleManifest {
            id: steam_game_bottle_id(620),
            name: "Game 620".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(620),
            prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M9,
            preferred_pipeline: None,
            installed_components: default_components_for(RuntimeProfile::M9),
            source_installer_path: None,
            installer_kind: None,
            game_install_path: None,
            runtime_assets: Vec::new(),
            installed_app_detections: Vec::new(),
            health: BottleHealth::New,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };

        assert_eq!(PathBuf::from(&manifest.prefix_path), steam_launch_prefix());
        assert!(!should_wait_for_prefix_idle(&manifest));
    }

    #[test]
    fn steam_compatdata_record_is_appid_scoped_and_launch_authoritative() {
        let manifest = BottleManifest {
            id: steam_game_bottle_id(620),
            name: "Portal 2".into(),
            custom_name: None,
            bottle_type: BottleType::Steam,
            steam_app_id: Some(620),
            prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M9,
            preferred_pipeline: None,
            installed_components: default_components_for(RuntimeProfile::M9),
            source_installer_path: None,
            installer_kind: None,
            game_install_path: Some("/games/Portal 2".into()),
            runtime_assets: vec![BottleRuntimeAsset {
                id: "installscript".into(),
                kind: "installscript".into(),
                source_path: "/games/Portal 2/installscript.vdf".into(),
                present: true,
            }],
            installed_app_detections: Vec::new(),
            health: BottleHealth::Ready,
            last_launch_log: Some("/tmp/steam_620.log".into()),
            last_launch_pid: Some(1234),
            last_launch_status: Some("running".into()),
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };

        let record = steam_compatdata_record(&manifest, crate::mtsp::engine::PipelineId::M9);

        assert_eq!(record.appid, 620);
        assert_eq!(record.bottle_id, "steam_620");
        assert!(record.compatdata_path.ends_with("/compatdata/620"));
        assert_eq!(record.launch_pipeline, "m9");
        assert_eq!(record.steam_identity_mode, "wine_steam_background");
        assert_eq!(record.compat_tool_name, "MetalSharp");
        assert!(record.launch_command_template.contains("/steam/launch-game"));
        assert!(record.launch_command_template.contains("620"));
        assert_eq!(record.runtime_assets.len(), 1);
        assert_eq!(record.last_launch_log.as_deref(), Some("/tmp/steam_620.log"));
        assert_eq!(record.last_launch_pid, Some(1234));
        assert_eq!(record.last_launch_status.as_deref(), Some("running"));
    }

    #[test]
    fn installer_bottles_wait_for_prefix_idle_completion() {
        let manifest = BottleManifest {
            id: "installer_demo".into(),
            name: "Demo Installer".into(),
            custom_name: None,
            bottle_type: BottleType::Installer,
            steam_app_id: None,
            prefix_path: bottle_dir("installer_demo").join("prefix").to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::GameInstall,
            preferred_pipeline: None,
            installed_components: default_components_for(RuntimeProfile::GameInstall),
            source_installer_path: None,
            installer_kind: Some(InstallerKind::Exe),
            game_install_path: None,
            runtime_assets: Vec::new(),
            installed_app_detections: Vec::new(),
            health: BottleHealth::New,
            last_launch_log: None,
            last_launch_pid: None,
            last_launch_status: None,
            last_launch_finished_at: None,
            created_at: timestamp_secs(),
            updated_at: timestamp_secs(),
        };

        assert!(should_wait_for_prefix_idle(&manifest));
    }

    #[test]
    fn bottle_ids_reject_path_like_values() {
        assert!(validate_bottle_id("steam_620").is_ok());
        assert!(validate_bottle_id("installer_abcdef1234567890").is_ok());
        assert!(validate_bottle_id("../steam_620").is_err());
        assert!(validate_bottle_id("steam/620").is_err());
        assert!(validate_bottle_id("").is_err());
    }

    #[test]
    fn gecko_inspection_accepts_wine_system_gecko_dirs() {
        let prefix = test_dir("gecko-system-dirs");
        let system32_gecko = prefix.join("drive_c").join("windows").join("system32").join("gecko");
        fs::create_dir_all(&system32_gecko).expect("create gecko dir");

        assert_eq!(inspect_component_state(&prefix, "gecko", ComponentState::Missing), ComponentState::Installed);
        let _ = fs::remove_dir_all(prefix);
    }

    #[test]
    fn corefonts_are_not_reported_as_wineboot_repairable() {
        let policy = component_source_policy("corefonts", BottleArch::Wow64);

        assert_ne!(policy.source, "metalsharp_wine_bootstrap");
        assert!(!policy.detail.contains("wineboot"));
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-bottles-{}-{}-{}", name, std::process::id(), timestamp_secs()));
        dir
    }

    fn test_pe(machine: u16, optional_magic: u16) -> Vec<u8> {
        let mut data = vec![0_u8; 0x200];
        data[0] = b'M';
        data[1] = b'Z';
        data[0x3c..0x40].copy_from_slice(&(0x80_u32).to_le_bytes());
        data[0x80..0x84].copy_from_slice(b"PE\0\0");
        data[0x84..0x86].copy_from_slice(&machine.to_le_bytes());
        data[0x86..0x88].copy_from_slice(&(0_u16).to_le_bytes());
        data[0x94..0x96].copy_from_slice(&(0xf0_u16).to_le_bytes());
        data[0x98..0x9a].copy_from_slice(&optional_magic.to_le_bytes());
        data
    }

    #[test]
    fn vcrun2019_detected_by_any_v14_dll() {
        let dir = test_dir("vcrun2019-detect");
        let system32 = dir.join("drive_c").join("windows").join("system32");
        let syswow64 = dir.join("drive_c").join("windows").join("syswow64");
        fs::create_dir_all(&system32).expect("create system32");
        fs::create_dir_all(&syswow64).expect("create syswow64");

        assert_eq!(inspect_component_state(&dir, "vcrun2019", ComponentState::Unknown), ComponentState::Missing);

        fs::write(system32.join("vcruntime140.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2019", ComponentState::Unknown), ComponentState::NeedsRepair);

        fs::write(system32.join("vcruntime140_1.dll"), b"dll").expect("write dll");
        fs::write(system32.join("msvcp140.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2019", ComponentState::Unknown), ComponentState::Installed);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn vcrun2013_detected_by_msvcr120() {
        let dir = test_dir("vcrun2013-detect");
        let system32 = dir.join("drive_c").join("windows").join("system32");
        let syswow64 = dir.join("drive_c").join("windows").join("syswow64");
        fs::create_dir_all(&system32).expect("create system32");
        fs::create_dir_all(&syswow64).expect("create syswow64");

        assert_eq!(inspect_component_state(&dir, "vcrun2013", ComponentState::Unknown), ComponentState::Missing);

        fs::write(system32.join("msvcr120.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2013", ComponentState::Unknown), ComponentState::NeedsRepair);

        fs::write(system32.join("msvcp120.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2013", ComponentState::Unknown), ComponentState::Installed);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn vcrun2010_detected_by_msvcr100() {
        let dir = test_dir("vcrun2010-detect");
        let system32 = dir.join("drive_c").join("windows").join("system32");
        let syswow64 = dir.join("drive_c").join("windows").join("syswow64");
        fs::create_dir_all(&system32).expect("create system32");
        fs::create_dir_all(&syswow64).expect("create syswow64");

        assert_eq!(inspect_component_state(&dir, "vcrun2010", ComponentState::Unknown), ComponentState::Missing);

        fs::write(system32.join("msvcr100.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2010", ComponentState::Unknown), ComponentState::NeedsRepair);

        fs::write(system32.join("msvcp100.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2010", ComponentState::Unknown), ComponentState::Installed);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn vcrun2019_extended_detection_covers_msvcp140() {
        let dir = test_dir("vcrun2019-extended");
        let system32 = dir.join("drive_c").join("windows").join("system32");
        let syswow64 = dir.join("drive_c").join("windows").join("syswow64");
        fs::create_dir_all(&system32).expect("create system32");
        fs::create_dir_all(&syswow64).expect("create syswow64");

        fs::write(system32.join("msvcp140.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2019", ComponentState::Unknown), ComponentState::NeedsRepair);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn vcrun2013_detected_by_msvcp120_in_syswow64() {
        let dir = test_dir("vcrun2013-syswow64");
        let system32 = dir.join("drive_c").join("windows").join("system32");
        let syswow64 = dir.join("drive_c").join("windows").join("syswow64");
        fs::create_dir_all(&system32).expect("create system32");
        fs::create_dir_all(&syswow64).expect("create syswow64");

        fs::write(syswow64.join("msvcp120.dll"), b"dll").expect("write dll");
        assert_eq!(inspect_component_state(&dir, "vcrun2013", ComponentState::Unknown), ComponentState::NeedsRepair);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn game_install_profile_includes_vcrun2013() {
        let components = default_components_for(RuntimeProfile::GameInstall);
        let ids = components.iter().map(|c| c.id.as_str()).collect::<Vec<_>>();
        assert!(ids.contains(&"vcrun2019"));
        assert!(ids.contains(&"vcrun2013"));
        assert!(ids.contains(&"directx_jun2010"));
    }

    #[test]
    fn gpu_vendor_stubs_do_not_require_gptk_amd_stub() {
        let dir = test_dir("gpu-vendor-stubs");
        let system32 = dir.join("drive_c").join("windows").join("system32");
        fs::create_dir_all(&system32).expect("create system32");

        fs::write(system32.join("nvapi64.dll"), b"dll").expect("write nvapi");
        assert_eq!(
            inspect_component_state(&dir, "gpu_vendor_stubs", ComponentState::Unknown),
            ComponentState::NeedsRepair
        );

        fs::write(system32.join("nvngx.dll"), b"dll").expect("write nvngx");
        assert_eq!(
            inspect_component_state(&dir, "gpu_vendor_stubs", ComponentState::Unknown),
            ComponentState::Installed
        );
        assert_eq!(inspect_component_state(&dir, "gptk_amd_stub", ComponentState::Unknown), ComponentState::Missing);

        fs::write(system32.join("atidxx64.dll"), b"dll").expect("write amd stub");
        assert_eq!(inspect_component_state(&dir, "gptk_amd_stub", ComponentState::Unknown), ComponentState::Installed);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn gptk_profile_splits_amd_stub_from_dxmt_vendor_stubs() {
        let m12 = default_components_for(RuntimeProfile::M12);
        let m12_ids = m12.iter().map(|c| c.id.as_str()).collect::<Vec<_>>();
        assert!(m12_ids.contains(&"gpu_vendor_stubs"));
        assert!(m12_ids.contains(&"corefonts"));
        assert!(m12_ids.contains(&"d3d12_agility"));
        assert!(!m12_ids.contains(&"gptk_amd_stub"));

        let m13 = default_components_for(RuntimeProfile::M13);
        let m13_ids = m13.iter().map(|c| c.id.as_str()).collect::<Vec<_>>();
        assert!(m13_ids.contains(&"gpu_vendor_stubs"));
        assert!(m13_ids.contains(&"gptk_amd_stub"));
    }

    #[test]
    fn redist_source_guides_cover_vcrun2013() {
        let guides = redist_source_guides();
        assert!(guides.iter().any(|g| g.id == "vcrun2010"));
        assert!(guides.iter().any(|g| g.id == "vcrun2013"));
        assert!(guides.iter().any(|g| g.id == "vcrun2019"));
    }

    #[test]
    fn d3d12_agility_component_is_repairable_native_runtime_payload() {
        let policy = component_source_policy("d3d12_agility", BottleArch::Win64);

        assert_eq!(policy.source, "metalsharp_native_runtime");
        assert!(policy.available);
        assert!(policy.detail.contains("Agility SDK"));
    }

    #[test]
    fn installscript_heuristic_detects_vcrun2010() {
        let dir = test_dir("installscript-vcrun2010");
        fs::create_dir_all(&dir).expect("create dir");
        let script = dir.join("installscript.vdf");
        fs::write(&script, br#""vcredist/2010/vcredist_x86.exe""#).expect("write script");
        let components = components_from_installscript(&script);
        assert!(components.iter().any(|c| c == "vcrun2010"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn installscript_heuristic_detects_vcrun2013() {
        let dir = test_dir("installscript-vcrun2013");
        fs::create_dir_all(&dir).expect("create dir");
        let script = dir.join("installscript.vdf");
        fs::write(&script, br#""vcredist_2013/x64/vcredist_x64.exe""#).expect("write script");
        let components = components_from_installscript(&script);
        assert!(components.iter().any(|c| c == "vcrun2013"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn font_substitution_reg_includes_core_entries() {
        let reg = build_font_substitution_reg();
        assert!(reg.contains("REGEDIT4"));
        assert!(reg.contains("FontSubstitutes"));
        assert!(reg.contains("Helvetica"));
        assert!(reg.contains("MS Shell Dlg"));
        assert!(reg.contains("Fonts\\Replacements"));
        assert!(reg.contains("SimSun"));
        assert!(reg.contains("Arial Baltic,186"));
    }

    #[test]
    fn wine_font_replacements_map_to_macos_fonts() {
        let reg = build_font_substitution_reg();
        assert!(reg.contains("Hiragino"));
        assert!(reg.contains("STSong"));
        assert!(reg.contains("LiSong Pro"));
    }
}
