use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::{hash_map::DefaultHasher, HashMap, HashSet};
use std::fs::{self, OpenOptions};
use std::hash::{Hash, Hasher};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::thread;
use std::time::Duration;
use walkdir::WalkDir;

const BOTTLES_DIR: &str = "bottles";
const MANIFEST_FILE: &str = "bottle.json";
const COMPATIBILITY_MATRIX_FILE: &str = "compatibility-matrix.json";
const LAUNCH_WATCH_INTERVAL_SECS: u64 = 5;
const LAUNCH_WATCH_MAX_POLLS: usize = 4320;
const LAUNCH_WATCH_LOG_STABLE_POLLS: usize = 3;

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
    M11,
    M12,
    Dotnet,
    Win32Dotnet,
    Webview,
    JavaLauncher,
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
    pub bottle_type: BottleType,
    pub steam_app_id: Option<u32>,
    pub prefix_path: String,
    pub arch: BottleArch,
    pub runtime_profile: RuntimeProfile,
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
    pub pipeline: crate::mtsp::engine::PipelineId,
    pub runtime_profile: RuntimeProfile,
    pub prefix_path: String,
    pub game_install_path: Option<String>,
    pub runtime_assets: Vec<BottleRuntimeAsset>,
    pub components: Vec<RuntimeComponent>,
    pub actions: Vec<BottleAction>,
}

pub fn bottles_root() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join(BOTTLES_DIR)
}

fn steam_launch_prefix() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-steam")
}

pub fn bottle_dir(id: &str) -> PathBuf {
    bottles_root().join(id)
}

pub fn bottle_manifest_path(id: &str) -> PathBuf {
    bottle_dir(id).join(MANIFEST_FILE)
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
    let dir = bottle_dir(&manifest.id);
    fs::create_dir_all(dir.join("prefix"))?;
    fs::create_dir_all(dir.join("installers"))?;
    fs::create_dir_all(dir.join("logs"))?;
    fs::create_dir_all(dir.join("assets"))?;
    let data = serde_json::to_string_pretty(manifest)?;
    fs::write(bottle_manifest_path(&manifest.id), data)?;
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
    let id = installer_bottle_id(source_installer);
    let now = timestamp_secs();
    let name = source_installer
        .file_stem()
        .map(|n| n.to_string_lossy().to_string())
        .filter(|n| !n.trim().is_empty())
        .unwrap_or_else(|| "Windows Installer".to_string());

    let mut manifest = load_bottle(&id).unwrap_or_else(|_| BottleManifest {
        id: id.clone(),
        name,
        bottle_type: BottleType::Installer,
        steam_app_id: None,
        prefix_path: bottle_dir(&id).join("prefix").to_string_lossy().to_string(),
        arch: classification.arch,
        runtime_profile: classification.runtime_profile,
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
    let id = steam_game_bottle_id(appid);
    let now = timestamp_secs();
    let runtime_profile = runtime_profile_for_pipeline(pipeline);
    let mut manifest = load_bottle(&id).unwrap_or_else(|_| BottleManifest {
        id: id.clone(),
        name: name.to_string(),
        bottle_type: BottleType::Steam,
        steam_app_id: Some(appid),
        prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
        arch: BottleArch::Wow64,
        runtime_profile,
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

    manifest.name = name.to_string();
    manifest.bottle_type = BottleType::Steam;
    manifest.steam_app_id = Some(appid);
    manifest.prefix_path = steam_launch_prefix().to_string_lossy().to_string();
    manifest.runtime_profile = runtime_profile;
    manifest.installed_components =
        merge_components(manifest.installed_components, default_components_for(runtime_profile));
    manifest.game_install_path = game_dir.map(|dir| dir.to_string_lossy().to_string());
    manifest.runtime_assets = game_dir.map(detect_game_runtime_assets).unwrap_or_default();
    manifest.installed_app_detections = game_dir.map(detect_apps_in_game_dir).unwrap_or_default();
    manifest.health =
        if game_dir.map(|dir| dir.exists()).unwrap_or(false) { BottleHealth::Ready } else { BottleHealth::New };
    manifest.updated_at = now;
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn prepare_steam_game_launch(
    appid: u32,
    pipeline: crate::mtsp::engine::PipelineId,
) -> Result<BottleManifest, Box<dyn std::error::Error>> {
    let dual = crate::scan::resolve_dual_game_dir(appid);
    let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
    let mut manifest = ensure_steam_game_bottle(appid, &name, dual.wine_dir.as_deref(), pipeline)?;
    let prefix = PathBuf::from(&manifest.prefix_path);
    fs::create_dir_all(&prefix)?;
    manifest.installed_components = inspect_components(&prefix, &manifest.installed_components);
    refresh_manifest_runtime_views(&mut manifest);
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn sync_steam_game_bottles() -> Result<Vec<BottleManifest>, Box<dyn std::error::Error>> {
    let mut bottles = Vec::new();
    for appid in crate::steam::get_wine_steam_installed_games() {
        let dual = crate::scan::resolve_dual_game_dir(appid);
        let name = crate::steam::get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
        let pipeline = crate::mtsp::rules::resolve_pipeline(appid);
        bottles.push(ensure_steam_game_bottle(appid, &name, dual.wine_dir.as_deref(), pipeline)?);
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
    let installer_kind = classify_installer_kind(source_installer, &lower_strings, is_msi);

    if is_msi {
        hints.push("msi_package".to_string());
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

    let pipeline =
        if is_msi { crate::mtsp::engine::PipelineId::WineBare } else { installer_pipeline_from_pe(pe.as_ref()) };
    let runtime_profile = if imports_mscoree || strings_dotnet {
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
            crate::mtsp::engine::PipelineId::M9 => RuntimeProfile::M9,
            crate::mtsp::engine::PipelineId::M11 => RuntimeProfile::M11,
            crate::mtsp::engine::PipelineId::M12 => RuntimeProfile::M12,
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
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
    Ok(manifest)
}

pub fn diagnose_bottle(id: &str) -> Result<BottleDiagnostic, Box<dyn std::error::Error>> {
    let mut manifest = load_bottle(id)?;
    refresh_manifest_launch_state(&mut manifest);
    let prefix = PathBuf::from(&manifest.prefix_path);
    manifest.installed_components = inspect_components(&prefix, &manifest.installed_components);
    let log_ok = manifest.last_launch_log.as_ref().map(|p| Path::new(p).exists()).unwrap_or(false);
    refresh_manifest_runtime_views(&mut manifest);
    let detections = manifest.installed_app_detections.clone();
    let runtime_assets = manifest.runtime_assets.clone();

    let mut checks = Vec::new();
    checks.push(BottleCheck {
        id: "prefix".to_string(),
        ok: prefix.exists(),
        detail: prefix.to_string_lossy().to_string(),
    });
    checks.push(BottleCheck {
        id: "components".to_string(),
        ok: manifest.installed_components.iter().all(|c| c.state != ComponentState::Missing),
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
    }

    let actions = component_actions(&manifest.installed_components);
    let component_sources = component_source_policies(&manifest.installed_components, manifest.arch);
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
    manifest.installed_components =
        inspect_components(&PathBuf::from(&manifest.prefix_path), &manifest.installed_components);
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
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
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
    manifest.installed_components = inspect_components(&prefix, &manifest.installed_components);
    manifest.health = BottleHealth::NeedsRepair;
    manifest.updated_at = timestamp_secs();
    save_bottle(&manifest)?;
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
    manifest.installed_components = inspect_components(&prefix, &manifest.installed_components);

    if manifest
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

    if matches!(component_id, "wine-mono" | "gecko" | "corefonts") {
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

    let Some(installer) = resolve_component_installer(component_id, manifest.arch) else {
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
    mark_component_state(&mut manifest, &format!("windows_version_{}", version), ComponentState::NeedsRepair);
    mark_manifest_launch_started(&mut manifest, pid, &log_path);
    manifest.health = BottleHealth::NeedsRepair;
    save_bottle(&manifest)?;
    watch_bottle_launch(id.to_string(), pid);

    Ok(ComponentRepairReport {
        id: format!("windows_version_{}", version),
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
    let appid = body.get("appid").and_then(|v| v.as_u64()).and_then(|v| u32::try_from(v).ok());
    let pipeline = body
        .get("pipeline")
        .and_then(|v| v.as_str())
        .and_then(crate::mtsp::engine::PipelineId::from_str_flexible)
        .unwrap_or(crate::mtsp::engine::PipelineId::M12);
    let profile = runtime_profile_for_pipeline(pipeline);
    let bottle = appid.and_then(|id| {
        let dual = crate::scan::resolve_dual_game_dir(id);
        let name = crate::steam::get_game_name_from_manifest(id).unwrap_or_else(|| format!("Game {}", id));
        ensure_steam_game_bottle(id, &name, dual.wine_dir.as_deref(), pipeline).ok()
    });
    let prefix = bottle.as_ref().map(|b| PathBuf::from(&b.prefix_path)).unwrap_or_else(steam_launch_prefix);
    let components = inspect_components(&prefix, &default_components_for(profile));
    let actions = component_actions(&components);
    let report = SteamRuntimeDiagnostic {
        appid,
        bottle_id: bottle.as_ref().map(|b| b.id.clone()),
        pipeline,
        runtime_profile: profile,
        prefix_path: prefix.to_string_lossy().to_string(),
        game_install_path: bottle.as_ref().and_then(|b| b.game_install_path.clone()),
        runtime_assets: bottle.as_ref().map(|b| b.runtime_assets.clone()).unwrap_or_default(),
        components,
        actions,
    };
    json!({"ok": true, "report": report})
}

fn runtime_profile_definitions() -> Vec<RuntimeProfileDefinition> {
    [
        RuntimeProfile::Plain,
        RuntimeProfile::Launcher,
        RuntimeProfile::GameInstall,
        RuntimeProfile::M9,
        RuntimeProfile::M11,
        RuntimeProfile::M12,
        RuntimeProfile::Dotnet,
        RuntimeProfile::Win32Dotnet,
        RuntimeProfile::Webview,
        RuntimeProfile::JavaLauncher,
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
            &["vcrun2019", "directx_jun2010", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::M9 => (
            "D3D9 Metal",
            BottleArch::Wow64,
            true,
            &["d3d9", "vcrun2019", "directx_jun2010"][..],
            crate::mtsp::engine::PipelineId::M9,
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
            &["d3d12", "d3d11", "dxgi", "vcrun2019"][..],
            crate::mtsp::engine::PipelineId::M12,
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
            &["gecko", "webview2", "vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
        RuntimeProfile::JavaLauncher => (
            "Java Launcher",
            BottleArch::Wow64,
            true,
            &["vcrun2019", "corefonts"][..],
            crate::mtsp::engine::PipelineId::WineBare,
        ),
    };
    RuntimeProfileDefinition {
        id: profile,
        name,
        arch,
        wineboot,
        components: components.iter().map(|component| (*component).to_string()).collect(),
        launch_pipeline,
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
        crate::mtsp::engine::PipelineId::M9 => RuntimeProfile::M9,
        crate::mtsp::engine::PipelineId::M11 => RuntimeProfile::M11,
        crate::mtsp::engine::PipelineId::M12 => RuntimeProfile::M12,
        crate::mtsp::engine::PipelineId::FnaArm64 => RuntimeProfile::JavaLauncher,
        _ => RuntimeProfile::Plain,
    }
}

fn parse_runtime_profile(value: &str) -> Option<RuntimeProfile> {
    match value.to_ascii_lowercase().replace('-', "_").as_str() {
        "plain" => Some(RuntimeProfile::Plain),
        "launcher" => Some(RuntimeProfile::Launcher),
        "game_install" | "gameinstall" => Some(RuntimeProfile::GameInstall),
        "m9" => Some(RuntimeProfile::M9),
        "m11" => Some(RuntimeProfile::M11),
        "m12" => Some(RuntimeProfile::M12),
        "dotnet" => Some(RuntimeProfile::Dotnet),
        "win32_dotnet" | "win32dotnet" => Some(RuntimeProfile::Win32Dotnet),
        "webview" => Some(RuntimeProfile::Webview),
        "java_launcher" | "javalauncher" => Some(RuntimeProfile::JavaLauncher),
        _ => None,
    }
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

fn inspect_component_state(prefix: &Path, id: &str, fallback: ComponentState) -> ComponentState {
    let drive_c = prefix.join("drive_c");
    let windows = drive_c.join("windows");
    let system32 = windows.join("system32");
    let syswow64 = windows.join("syswow64");
    match id {
        "wine-mono" => {
            if windows.join("mono").exists() {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "gecko" => {
            if drive_c.join("windows").join("gecko").exists() {
                ComponentState::Installed
            } else {
                fallback
            }
        },
        "dotnet48" => {
            if windows.join("Microsoft.NET").join("Framework").join("v4.0.30319").exists() {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        "vcrun2019" => {
            if system32.join("vcruntime140.dll").exists() || syswow64.join("vcruntime140.dll").exists() {
                ComponentState::Installed
            } else {
                fallback
            }
        },
        "corefonts" => {
            if windows.join("Fonts").exists() {
                ComponentState::Installed
            } else {
                fallback
            }
        },
        "directx_jun2010" => {
            if system32.join("d3dx9_43.dll").exists()
                || syswow64.join("d3dx9_43.dll").exists()
                || system32.join("xinput1_3.dll").exists()
                || syswow64.join("xinput1_3.dll").exists()
            {
                ComponentState::Installed
            } else {
                fallback
            }
        },
        "d3d9" | "d3d11" | "d3d12" | "dxgi" => fallback,
        "webview2" => {
            if drive_c.join("Program Files (x86)").join("Microsoft").join("EdgeWebView").exists()
                || drive_c.join("Program Files").join("Microsoft").join("EdgeWebView").exists()
            {
                ComponentState::Installed
            } else {
                ComponentState::Missing
            }
        },
        _ => fallback,
    }
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
            ])
        },
        "dotnet48" => first_existing(&[
            redist_root.join("DotNet").join("4.8").join("ndp48-x86-x64-allos-enu.exe"),
            redist_root.join("DotNet").join("4.8").join("NDP48-x86-x64-AllOS-ENU.exe"),
            redist_root.join("DotNet").join("4.7.2").join("NDP472-KB4054530-x86-x64-AllOS-ENU.exe"),
            redist_root.join("DotNet").join("4.6").join("NDP462-KB3151800-x86-x64-AllOS-ENU.exe"),
            redist_root.join("DotNet").join("4.5.2").join("NDP452-KB2901907-x86-x64-AllOS-ENU.exe"),
        ]),
        "webview2" => first_existing(&[
            redist_root.join("WebView2").join("MicrosoftEdgeWebView2RuntimeInstallerX64.exe"),
            redist_root.join("WebView2").join("MicrosoftEdgeWebView2RuntimeInstallerX86.exe"),
            home.join(".metalsharp")
                .join("runtime")
                .join("redist")
                .join("MicrosoftEdgeWebView2RuntimeInstallerX64.exe"),
            home.join(".metalsharp")
                .join("runtime")
                .join("redist")
                .join("MicrosoftEdgeWebView2RuntimeInstallerX86.exe"),
        ]),
        "directx_jun2010" => first_existing(&[
            redist_root.join("DirectX").join("Jun2010").join("DXSETUP.exe"),
            redist_root.join("DirectX").join("Jun2010").join("dxsetup.exe"),
            home.join(".metalsharp").join("runtime").join("redist").join("DirectX").join("Jun2010").join("DXSETUP.exe"),
        ]),
        _ => None,
    }?;

    let args = match component_id {
        "vcrun2019" => vec!["/quiet".to_string(), "/norestart".to_string()],
        "dotnet48" => vec!["/q".to_string(), "/norestart".to_string()],
        "webview2" => vec!["/silent".to_string(), "/install".to_string()],
        "directx_jun2010" => vec!["/silent".to_string()],
        _ => Vec::new(),
    };
    Some(ComponentInstaller { path: executable, args })
}

fn first_existing(paths: &[PathBuf]) -> Option<PathBuf> {
    paths.iter().find(|path| path.exists()).cloned()
}

fn launch_component_installer(
    prefix: &Path,
    installer: &ComponentInstaller,
    log_path: &Path,
) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
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
    cmd.arg(&installer.path)
        .args(&installer.args)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
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
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
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
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
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
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;
    Ok(child.id())
}

fn component_actions(components: &[RuntimeComponent]) -> Vec<BottleAction> {
    components
        .iter()
        .filter(|component| matches!(component.state, ComponentState::Missing | ComponentState::NeedsRepair))
        .map(|component| BottleAction {
            id: component.id.clone(),
            status: "needed".to_string(),
            detail: component_action_detail(&component.id),
        })
        .collect()
}

fn component_source_policies(components: &[RuntimeComponent], arch: BottleArch) -> Vec<ComponentSourcePolicy> {
    components.iter().map(|component| component_source_policy(&component.id, arch)).collect()
}

fn component_source_policy(id: &str, arch: BottleArch) -> ComponentSourcePolicy {
    if matches!(id, "wine-mono" | "gecko" | "corefonts") {
        return ComponentSourcePolicy {
            id: id.to_string(),
            source: "metalsharp_wine_bootstrap".to_string(),
            available: dirs::home_dir()
                .map(|home| {
                    crate::platform::runtime_wine_binary(&home.join(".metalsharp").join("runtime").join("wine"))
                        .exists()
                })
                .unwrap_or(false),
            detail: format!("{} is repaired by running wineboot -u in the bottle prefix", id),
            path: None,
        };
    }
    let installer = resolve_component_installer(id, arch);
    ComponentSourcePolicy {
        id: id.to_string(),
        source: if installer.is_some() { "local_redist_asset" } else { "missing_local_asset" }.to_string(),
        available: installer.is_some(),
        detail: match id {
            "dotnet48" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist .NET 4.x offline installers",
            "vcrun2019" => "Uses Steam CommonRedist VC_redist or compatible local Visual C++ redistributable",
            "webview2" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist WebView2 evergreen installer",
            "directx_jun2010" => "Uses Steam CommonRedist or ~/.metalsharp/runtime/redist DirectX June 2010 payload",
            _ => "No external installer source required or source is not yet mapped",
        }
        .to_string(),
        path: installer.map(|installer| installer.path.to_string_lossy().to_string()),
    }
}

fn component_action_detail(id: &str) -> String {
    match id {
        "wine-mono" => "Install or repair Wine Mono inside this bottle prefix".to_string(),
        "gecko" => "Install Wine Gecko for embedded browser surfaces".to_string(),
        "dotnet48" => "Install a compatible .NET 4.x runtime strategy for this bottle".to_string(),
        "vcrun2019" => "Install Visual C++ 2015-2022 runtime DLLs".to_string(),
        "corefonts" => "Install core Windows fonts".to_string(),
        "webview2" => "Install or emulate Microsoft Edge WebView2 runtime".to_string(),
        "directx_jun2010" => "Install DirectX June 2010 runtime payloads".to_string(),
        _ => format!("Prepare component {}", id),
    }
}

fn installer_bottle_id(source_installer: &Path) -> String {
    let mut hasher = DefaultHasher::new();
    "installer".hash(&mut hasher);
    source_installer.to_string_lossy().hash(&mut hasher);
    format!("installer_{:016x}", hasher.finish())
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
            if !is_probable_app_exe(&name) {
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
            "VC Runtime Redistributables",
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
    let redist = home.join(".metalsharp").join("runtime").join("redist");
    vec![
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
            name: "Latest Microsoft Visual C++ Redistributable".to_string(),
            source_url: "https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist".to_string(),
            local_targets: vec![
                redist.join("VC_redist.x64.exe").to_string_lossy().to_string(),
                redist.join("VC_redist.x86.exe").to_string_lossy().to_string(),
            ],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Prefer Steam CommonRedist when present; otherwise use Microsoft's latest supported redist links.".to_string(),
        },
        RedistSourceGuide {
            id: "directx_jun2010".to_string(),
            name: "DirectX June 2010 Runtime".to_string(),
            source_url: "https://www.microsoft.com/download/details.aspx?id=8109".to_string(),
            local_targets: vec![redist.join("DirectX").join("Jun2010").join("DXSETUP.exe").to_string_lossy().to_string()],
            policy: "official_download_or_steam_commonredist".to_string(),
            notes: "Prefer Steam CommonRedist game payloads; local offline payload should contain DXSETUP.exe.".to_string(),
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
        .filter(|component| matches!(component.state, ComponentState::Missing | ComponentState::NeedsRepair))
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
        if !is_probable_app_exe(&name) {
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
        let kind = if lower_path.contains("_commonredist") || lower_path.contains("commonredist") {
            classify_redist_asset(&lower_name)
        } else if lower_name == "installscript.vdf" {
            Some("installscript".to_string())
        } else {
            None
        };
        let Some(kind) = kind else {
            continue;
        };
        let source_path = path.to_string_lossy().to_string();
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
    ];
    lower.ends_with(".exe")
        && !builtins.contains(&lower.as_str())
        && !lower.contains("setup")
        && !lower.contains("install")
        && !lower.contains("unins")
        && !lower.contains("vcredist")
        && !lower.contains("crash")
        && !lower.contains("helper")
        && !lower.contains("update")
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
        assert!(profiles.iter().any(|profile| profile.id == RuntimeProfile::Webview));
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
        let exe = dir.join("MinecraftInstaller.exe");
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
    }

    #[test]
    fn steam_pipeline_maps_to_runtime_profile() {
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::M9), RuntimeProfile::M9);
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::M12), RuntimeProfile::M12);
        assert_eq!(runtime_profile_for_pipeline(crate::mtsp::engine::PipelineId::WineBare), RuntimeProfile::Plain);
    }

    #[test]
    fn app_detection_rejects_wine_builtins() {
        assert!(!is_probable_app_exe("iexplore.exe"));
        assert!(!is_probable_app_exe("wordpad.exe"));
        assert!(is_probable_app_exe("MinecraftLauncher.exe"));
    }

    #[test]
    fn game_runtime_assets_detect_common_redist_payloads() {
        let dir = test_dir("game-redists");
        let redist = dir.join("_CommonRedist").join("vcredist").join("2019");
        fs::create_dir_all(&redist).expect("create redist dir");
        fs::write(redist.join("VC_redist.x86.exe"), b"redist").expect("write vcredist");
        fs::write(redist.join("installscript.vdf"), b"script").expect("write installscript");

        let assets = detect_game_runtime_assets(&dir);

        assert!(assets.iter().any(|asset| asset.kind == "vcredist"));
        assert!(assets.iter().any(|asset| asset.kind == "installscript"));
        let _ = fs::remove_dir_all(dir);
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
            bottle_type: BottleType::Steam,
            steam_app_id: Some(620),
            prefix_path: steam_launch_prefix().to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::M9,
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
    fn installer_bottles_wait_for_prefix_idle_completion() {
        let manifest = BottleManifest {
            id: "installer_demo".into(),
            name: "Demo Installer".into(),
            bottle_type: BottleType::Installer,
            steam_app_id: None,
            prefix_path: bottle_dir("installer_demo").join("prefix").to_string_lossy().to_string(),
            arch: BottleArch::Wow64,
            runtime_profile: RuntimeProfile::GameInstall,
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
}
