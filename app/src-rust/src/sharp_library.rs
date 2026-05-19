use serde_json::{json, Value};
use std::collections::hash_map::DefaultHasher;
use std::fs;
use std::hash::{Hash, Hasher};
use std::path::{Component, Path, PathBuf};
use walkdir::WalkDir;

const LIBRARY_DIR: &str = "sharp-library";
const MANIFEST_FILE: &str = "library.json";

fn base_dir() -> PathBuf {
    let home = dirs::home_dir().unwrap_or_default();
    home.join(".metalsharp").join(LIBRARY_DIR)
}

fn manifest_path() -> PathBuf {
    base_dir().join(MANIFEST_FILE)
}

#[derive(serde::Serialize, serde::Deserialize, Clone)]
pub struct SharpApp {
    pub id: String,
    pub name: String,
    pub exe_path: String,
    pub install_dir: String,
    pub cover: Option<String>,
    pub engine: String,
    #[serde(default)]
    pub launch_args: Vec<String>,
    pub installed_at: String,
    pub size_bytes: u64,
}

#[derive(serde::Serialize)]
pub struct SharpLaunchResult {
    pub pid: u32,
    pub game_type: &'static str,
    pub pipeline: crate::mtsp::engine::PipelineId,
    pub exe_path: String,
    pub warnings: Vec<String>,
}

fn ensure_base_dir() -> Result<(), Box<dyn std::error::Error>> {
    let dir = base_dir();
    if !dir.exists() {
        fs::create_dir_all(&dir)?;
    }
    Ok(())
}

pub fn load_library() -> Result<Vec<SharpApp>, Box<dyn std::error::Error>> {
    ensure_base_dir()?;
    let path = manifest_path();
    let mut apps: Vec<SharpApp> = if path.exists() {
        let data = fs::read_to_string(&path)?;
        serde_json::from_str(&data)?
    } else {
        Vec::new()
    };

    if sync_non_steam_shortcuts(&mut apps) {
        save_library(&apps)?;
    }

    Ok(apps)
}

fn save_library(apps: &[SharpApp]) -> Result<(), Box<dyn std::error::Error>> {
    ensure_base_dir()?;
    let data = serde_json::to_string_pretty(apps)?;
    fs::write(manifest_path(), data)?;
    Ok(())
}

fn sync_non_steam_shortcuts(apps: &mut Vec<SharpApp>) -> bool {
    let mut changed = false;
    for shortcut in crate::scan::scan_non_steam_shortcuts() {
        let id = format!("steam_shortcut_{}", stable_shortcut_id(&shortcut.name, &shortcut.exe_path));
        if apps.iter().any(|app| app.id == id || app_absolute_exe_path(app) == shortcut.exe_path) {
            continue;
        }

        let install_dir = shortcut
            .start_dir
            .clone()
            .filter(|dir| shortcut.exe_path.starts_with(dir))
            .or_else(|| shortcut.exe_path.parent().map(Path::to_path_buf))
            .unwrap_or_else(|| shortcut.exe_path.clone());

        let exe_path = relative_path_string(&install_dir, &shortcut.exe_path)
            .unwrap_or_else(|_| shortcut.exe_path.to_string_lossy().to_string());

        apps.push(SharpApp {
            id,
            name: shortcut.name,
            exe_path,
            install_dir: install_dir.to_string_lossy().to_string(),
            cover: None,
            engine: "auto".to_string(),
            launch_args: shortcut.launch_args,
            installed_at: chrono_now(),
            size_bytes: dir_size(&install_dir),
        });
        changed = true;
    }
    changed
}

fn app_absolute_exe_path(app: &SharpApp) -> PathBuf {
    PathBuf::from(&app.install_dir).join(&app.exe_path)
}

fn stable_shortcut_id(name: &str, exe_path: &Path) -> u32 {
    let mut hasher = DefaultHasher::new();
    name.hash(&mut hasher);
    exe_path.hash(&mut hasher);
    let hash = hasher.finish() as u32;
    if hash == 0 {
        1
    } else {
        hash
    }
}

fn generate_id(name: &str) -> String {
    let clean: String = name.to_lowercase().chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect();
    let timestamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis();
    format!("{}_{}", clean.trim_matches('_'), timestamp)
}

pub fn install_exe(src_path: &str, custom_name: Option<&str>) -> Result<SharpApp, Box<dyn std::error::Error>> {
    let src = PathBuf::from(src_path);
    if !src.exists() {
        return Err("Source EXE not found".into());
    }
    if src.extension().map(|e| e.to_string_lossy().to_lowercase()) != Some("exe".to_string()) {
        return Err("Only .exe files are supported".into());
    }

    let file_name = src.file_name().unwrap_or_default().to_string_lossy().to_string();
    let app_name = custom_name.map(|n| n.to_string()).unwrap_or_else(|| file_name.trim_end_matches(".exe").to_string());
    let id = generate_id(&app_name);

    let app_dir = base_dir().join(&id);
    fs::create_dir_all(&app_dir)?;

    let import_root = import_root_for_exe(&src);
    copy_app_tree(&import_root, &app_dir)?;
    let exe_rel = src.strip_prefix(&import_root).unwrap_or_else(|_| Path::new(&file_name));
    let copied_exe = app_dir.join(exe_rel);
    if !copied_exe.exists() {
        return Err(format!("Copied app is missing selected EXE: {}", copied_exe.display()).into());
    }

    let exe_path = relative_path_string(&app_dir, &copied_exe)?;
    let size_bytes = dir_size(&app_dir);
    let install_dir = app_dir.to_string_lossy().to_string();

    let installed_at = chrono_now();

    let app = SharpApp {
        id,
        name: app_name,
        exe_path,
        install_dir,
        cover: None,
        engine: "auto".to_string(),
        launch_args: Vec::new(),
        installed_at,
        size_bytes,
    };

    let mut library = load_library()?;
    library.push(app.clone());
    save_library(&library)?;

    Ok(app)
}

pub fn uninstall_app(id: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut library = load_library()?;
    let idx = library.iter().position(|a| a.id == id).ok_or("App not found")?;

    if !is_safe_library_id(id) {
        return Err("Invalid app id".into());
    }

    let app_dir = base_dir().join(id);
    if app_dir.exists() {
        remove_dir_all_under(&app_dir, &base_dir())?;
    }

    let cover_path = base_dir().join(format!("{}.cover", id));
    if cover_path.exists() {
        let _ = fs::remove_file(&cover_path);
    }

    library.remove(idx);
    save_library(&library)?;
    Ok(())
}

fn is_safe_library_id(id: &str) -> bool {
    let mut components = Path::new(id).components();
    matches!(components.next(), Some(Component::Normal(_))) && components.next().is_none()
}

fn remove_dir_all_under(target: &Path, root: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let target = fs::canonicalize(target)?;
    let root = fs::canonicalize(root)?;
    if target == root || !target.starts_with(&root) {
        return Err(format!("Refusing to remove path outside Sharp library: {}", target.display()).into());
    }
    fs::remove_dir_all(target)?;
    Ok(())
}

pub fn launch_app(id: &str, engine: &str) -> Result<SharpLaunchResult, Box<dyn std::error::Error>> {
    let mut library = load_library()?;
    let idx = library.iter().position(|a| a.id == id).ok_or("App not found")?;
    let changed = refresh_setup_reference(&mut library[idx]);
    if changed {
        save_library(&library)?;
    }
    let app = library.get(idx).ok_or("App not found")?.clone();

    let work_dir = PathBuf::from(&app.install_dir);
    let exe_path = work_dir.join(&app.exe_path);

    if !exe_path.exists() {
        return Err(format!("EXE not found: {}", exe_path.display()).into());
    }

    let pipeline = resolve_sharp_pipeline(engine, &exe_path);
    let launch_id = stable_launch_id(&app.id);
    let (pid, game_type, recipe) = crate::mtsp::launcher::launch_custom_with_pipeline(
        launch_id,
        &work_dir,
        &exe_path,
        pipeline,
        &app.launch_args,
    )?;

    Ok(SharpLaunchResult {
        pid,
        game_type,
        pipeline,
        exe_path: exe_path.to_string_lossy().to_string(),
        warnings: recipe.warnings,
    })
}

pub fn diagnose_app(
    id: &str,
    engine: &str,
) -> Result<crate::mtsp::recipe::LaunchDoctorReport, Box<dyn std::error::Error>> {
    let library = load_library()?;
    let app = library.iter().find(|a| a.id == id).ok_or("App not found")?.clone();

    let work_dir = PathBuf::from(&app.install_dir);
    let exe_path = work_dir.join(&app.exe_path);
    let pipeline = resolve_sharp_pipeline(engine, &exe_path);
    let node = crate::mtsp::engine::get_pipeline(pipeline);
    let launch_id = stable_launch_id(&app.id);
    let mut recipe = crate::mtsp::recipe::build_custom_launch_recipe(launch_id, node, &work_dir, Some(&exe_path))?;
    recipe.launch_args.extend(app.launch_args.iter().cloned());
    Ok(crate::mtsp::recipe::diagnose_recipe(recipe))
}

pub fn set_cover(id: &str, cover_path: &str) -> Result<(), Box<dyn std::error::Error>> {
    let src = PathBuf::from(cover_path);
    if !src.exists() {
        return Err("Cover image not found".into());
    }

    let metadata = fs::metadata(&src)?;
    if metadata.len() > 5 * 1024 * 1024 {
        return Err("Cover image must be under 5MB".into());
    }

    let ext = src.extension().map(|e| e.to_string_lossy().to_lowercase()).unwrap_or_else(|| "jpg".to_string());

    let cover_filename = format!("{}.{}", id, ext);
    let dst = base_dir().join(&cover_filename);
    fs::copy(&src, &dst)?;

    let mut library = load_library()?;
    if let Some(app) = library.iter_mut().find(|a| a.id == id) {
        app.cover = Some(cover_filename);
        save_library(&library)?;
    }

    Ok(())
}

pub fn set_engine(id: &str, engine: &str) -> Result<(), Box<dyn std::error::Error>> {
    let valid = ["auto", "wine_bare", "m64", "m9", "m10", "m11", "m12", "m32", "d3d9", "d3d10", "d3d11", "d3d12"];
    if engine != "auto" && crate::mtsp::engine::PipelineId::from_str_flexible(engine).is_none() {
        return Err(format!("Unknown engine: {}. Valid: {}", engine, valid.join(", ")).into());
    }

    let mut library = load_library()?;
    if let Some(app) = library.iter_mut().find(|a| a.id == id) {
        app.engine = normalize_engine(engine);
        save_library(&library)?;
    } else {
        return Err("App not found".into());
    }

    Ok(())
}

pub fn get_cover_path(id: &str) -> Option<PathBuf> {
    let library = load_library().ok()?;
    let app = library.iter().find(|a| a.id == id)?;
    let cover_name = app.cover.as_ref()?;
    let path = base_dir().join(cover_name);
    if path.exists() {
        Some(path)
    } else {
        None
    }
}

fn chrono_now() -> String {
    let dur = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    format!("{}", dur.as_secs())
}

fn import_root_for_exe(exe_path: &Path) -> PathBuf {
    let exe_dir = exe_path.parent().unwrap_or_else(|| Path::new("."));
    let dir_name = exe_dir.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
    let parent_name =
        exe_dir.parent().and_then(|p| p.file_name()).map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();

    if matches!(dir_name.as_str(), "win64" | "win32" | "x64" | "x86") && parent_name == "binaries" {
        if let Some(project_dir) = exe_dir.parent().and_then(Path::parent) {
            if let Some(root) = project_dir.parent() {
                return root.to_path_buf();
            }
            return project_dir.to_path_buf();
        }
    }

    exe_dir.to_path_buf()
}

fn copy_app_tree(src_root: &Path, dest_root: &Path) -> Result<(), Box<dyn std::error::Error>> {
    fs::create_dir_all(dest_root)?;
    for entry in WalkDir::new(src_root).into_iter().flatten() {
        let path = entry.path();
        let rel = path.strip_prefix(src_root)?;
        if rel.as_os_str().is_empty() {
            continue;
        }
        let dest = dest_root.join(rel);
        if entry.file_type().is_dir() {
            fs::create_dir_all(&dest)?;
        } else if entry.file_type().is_file() {
            if let Some(parent) = dest.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::copy(path, dest)?;
        }
    }
    Ok(())
}

fn relative_path_string(root: &Path, path: &Path) -> Result<String, Box<dyn std::error::Error>> {
    Ok(path.strip_prefix(root)?.to_string_lossy().to_string())
}

fn normalize_engine(engine: &str) -> String {
    if engine.trim().eq_ignore_ascii_case("auto") {
        "auto".to_string()
    } else {
        pipeline_engine_id(
            crate::mtsp::engine::PipelineId::from_str_flexible(engine)
                .unwrap_or(crate::mtsp::engine::PipelineId::WineBare),
        )
        .to_string()
    }
}

fn pipeline_engine_id(pipeline: crate::mtsp::engine::PipelineId) -> &'static str {
    match pipeline {
        crate::mtsp::engine::PipelineId::M9 => "m9",
        crate::mtsp::engine::PipelineId::M10 => "m10",
        crate::mtsp::engine::PipelineId::M11 => "m11",
        crate::mtsp::engine::PipelineId::M12 => "m12",
        crate::mtsp::engine::PipelineId::M32 => "m32",
        crate::mtsp::engine::PipelineId::WineBare => "wine_bare",
        crate::mtsp::engine::PipelineId::FnaArm64 => "fna_arm64",
        crate::mtsp::engine::PipelineId::Steam => "steam",
        crate::mtsp::engine::PipelineId::MacSteam => "macos_steam",
    }
}

fn resolve_sharp_pipeline(engine: &str, exe_path: &Path) -> crate::mtsp::engine::PipelineId {
    if !engine.trim().eq_ignore_ascii_case("auto") {
        return crate::mtsp::engine::PipelineId::from_str_flexible(engine)
            .unwrap_or(crate::mtsp::engine::PipelineId::WineBare);
    }

    let Ok(data) = fs::read(exe_path) else {
        return crate::mtsp::engine::PipelineId::WineBare;
    };
    let Some(pe) = crate::mtsp::pe::parse_pe_imports(&data) else {
        return crate::mtsp::engine::PipelineId::WineBare;
    };

    match pe.detected_api {
        crate::mtsp::pe::D3dApi::D3D12 => {
            if pe.is_64_bit {
                crate::mtsp::engine::PipelineId::M12
            } else {
                crate::mtsp::engine::PipelineId::M11
            }
        },
        crate::mtsp::pe::D3dApi::D3D11 => crate::mtsp::engine::PipelineId::M11,
        crate::mtsp::pe::D3dApi::D3D10 => {
            if pe.is_64_bit {
                crate::mtsp::engine::PipelineId::M10
            } else {
                crate::mtsp::engine::PipelineId::M32
            }
        },
        crate::mtsp::pe::D3dApi::D3D9 => crate::mtsp::engine::PipelineId::M9,
        crate::mtsp::pe::D3dApi::Unknown => crate::mtsp::engine::PipelineId::WineBare,
    }
}

fn stable_launch_id(id: &str) -> u32 {
    let mut hasher = DefaultHasher::new();
    id.hash(&mut hasher);
    let hash = hasher.finish() as u32;
    if hash == 0 {
        1
    } else {
        hash
    }
}

fn refresh_setup_reference(app: &mut SharpApp) -> bool {
    let install_dir = PathBuf::from(&app.install_dir);
    let current = install_dir.join(&app.exe_path);
    let current_is_setup = is_setup_exe(&app.exe_path);
    if current.exists() && !current_is_setup {
        return false;
    }

    let Some(real_exe) = find_real_exe(&install_dir) else {
        return false;
    };

    let Some(file_name) = real_exe.file_name().map(|n| n.to_string_lossy().to_string()) else {
        return false;
    };

    if file_name == app.exe_path {
        return false;
    }

    app.exe_path = relative_path_string(&install_dir, &real_exe).unwrap_or(file_name);
    app.size_bytes = dir_size(&install_dir);
    true
}

fn is_setup_exe(name: &str) -> bool {
    let lower = name.to_lowercase();
    lower.contains("setup") || lower.contains("installer") || lower.contains("install")
}

fn is_valid_app_exe(name: &str) -> bool {
    let lower = name.to_lowercase();
    lower.ends_with(".exe")
        && !lower.contains("setup")
        && !lower.contains("redist")
        && !lower.contains("dotnet")
        && !lower.contains("installer")
        && !lower.contains("uninstall")
        && !lower.contains("vcredist")
        && !lower.contains("crashhandler")
        && !lower.contains("server")
}

fn find_real_exe(dir: &PathBuf) -> Option<PathBuf> {
    let mut best: Option<PathBuf> = None;
    for entry in WalkDir::new(dir).max_depth(4).into_iter().flatten() {
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let name = entry.file_name().to_string_lossy().to_string();
        if !is_valid_app_exe(&name) {
            continue;
        }
        let lower = name.to_lowercase();
        if lower == "game.exe" || lower.contains("shipping") {
            return Some(path.to_path_buf());
        }
        if best.is_none() {
            best = Some(path.to_path_buf());
        }
    }
    best
}

fn dir_size(dir: &PathBuf) -> u64 {
    let mut total = 0;
    for entry in WalkDir::new(dir).into_iter().flatten() {
        if let Ok(meta) = entry.metadata() {
            if meta.is_file() {
                total += meta.len();
            }
        }
    }
    total
}

pub fn handle_get_library() -> Value {
    match load_library() {
        Ok(apps) => json!({"ok": true, "apps": apps}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_install(body: &serde_json::Map<String, Value>) -> Value {
    let src_path = body.get("srcPath").and_then(|v| v.as_str()).unwrap_or("");
    let custom_name = body.get("name").and_then(|v| v.as_str());
    if src_path.is_empty() {
        return json!({"ok": false, "error": "srcPath required"});
    }
    match install_exe(src_path, custom_name) {
        Ok(app) => json!({"ok": true, "app": app}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_uninstall(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match uninstall_app(id) {
        Ok(()) => json!({"ok": true}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_launch(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let engine = body.get("engine").and_then(|v| v.as_str()).unwrap_or("auto");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match launch_app(id, engine) {
        Ok(result) => {
            json!({"ok": true, "pid": result.pid, "gameType": result.game_type, "pipeline": result.pipeline, "exePath": result.exe_path, "warnings": result.warnings})
        },
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_doctor(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let engine = body.get("engine").and_then(|v| v.as_str()).unwrap_or("auto");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match diagnose_app(id, engine) {
        Ok(report) => json!({"ok": true, "report": report}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_set_cover(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let cover_path = body.get("coverPath").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() || cover_path.is_empty() {
        return json!({"ok": false, "error": "id and coverPath required"});
    }
    match set_cover(id, cover_path) {
        Ok(()) => json!({"ok": true}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_set_engine(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let engine = body.get("engine").and_then(|v| v.as_str()).unwrap_or("wine_bare");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match set_engine(id, engine) {
        Ok(()) => json!({"ok": true}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_path_like_library_ids() {
        assert!(is_safe_library_id("game_123"));
        assert!(!is_safe_library_id("../runtime"));
        assert!(!is_safe_library_id("nested/game"));
        assert!(!is_safe_library_id("/tmp/game"));
    }

    #[test]
    fn unreal_binary_import_root_uses_game_root() {
        let root = PathBuf::from("/tmp/SharpGame");
        let exe = root.join("Game").join("Binaries").join("Win64").join("Game-Win64-Shipping.exe");

        assert_eq!(import_root_for_exe(&exe), root);
    }

    #[test]
    fn selected_exe_relative_path_survives_nested_layout() {
        let root = test_dir("relative");
        let exe_dir = root.join("bin").join("win64");
        fs::create_dir_all(&exe_dir).expect("create test dir");
        let exe = exe_dir.join("Tool.exe");
        fs::write(&exe, b"not pe").expect("write exe");

        assert_eq!(relative_path_string(&root, &exe).unwrap(), "bin/win64/Tool.exe");
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn auto_unknown_windows_app_uses_plain_wine() {
        let exe = test_dir("unknown").join("Tool.exe");

        assert_eq!(resolve_sharp_pipeline("auto", &exe), crate::mtsp::engine::PipelineId::WineBare);
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-sharp-library-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
