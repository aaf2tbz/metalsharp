use serde_json::{json, Value};
use std::fs;
use std::path::{Component, Path, PathBuf};
use std::process::Command;
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
    pub installed_at: String,
    pub size_bytes: u64,
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
    if !path.exists() {
        return Ok(vec![]);
    }
    let data = fs::read_to_string(&path)?;
    let apps: Vec<SharpApp> = serde_json::from_str(&data)?;
    Ok(apps)
}

fn save_library(apps: &[SharpApp]) -> Result<(), Box<dyn std::error::Error>> {
    ensure_base_dir()?;
    let data = serde_json::to_string_pretty(apps)?;
    fs::write(manifest_path(), data)?;
    Ok(())
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

    let dst = app_dir.join(&file_name);
    fs::copy(&src, &dst)?;

    let metadata = fs::metadata(&dst)?;
    let size_bytes = metadata.len();
    let install_dir = app_dir.to_string_lossy().to_string();

    let installed_at = chrono_now();

    let app = SharpApp {
        id,
        name: app_name,
        exe_path: file_name,
        install_dir,
        cover: None,
        engine: "wine_bare".to_string(),
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

pub fn launch_app(id: &str, engine: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let mut library = load_library()?;
    let idx = library.iter().position(|a| a.id == id).ok_or("App not found")?;
    let changed = refresh_setup_reference(&mut library[idx]);
    if changed {
        save_library(&library)?;
    }
    let app = library.get(idx).ok_or("App not found")?.clone();

    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let work_dir = PathBuf::from(&app.install_dir);
    let exe_path = work_dir.join(&app.exe_path);

    if !exe_path.exists() {
        return Err(format!("EXE not found: {}", exe_path.display()).into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let pipeline =
        crate::mtsp::engine::PipelineId::from_str_flexible(engine).unwrap_or(crate::mtsp::engine::PipelineId::WineBare);
    let node = crate::mtsp::engine::get_pipeline(pipeline);
    crate::mtsp::launcher::deploy_dlls_for_pipeline(&work_dir, node);

    let runtime_lib_path = if node.dyld_paths.is_empty() {
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string()
    } else {
        node.dyld_paths.iter().map(|p| ms_root.join(p).to_string_lossy().to_string()).collect::<Vec<_>>().join(":")
    };
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&work_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env(runtime_lib_key, &runtime_lib_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }
    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }
    cmd.arg(&app.exe_path);
    cmd.args(&node.launch_args);

    let child = cmd.spawn()?;
    Ok(child.id())
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
    let valid = ["wine_bare", "m64", "m9", "m10", "m11", "m12", "m32"];
    if !valid.contains(&engine) {
        return Err(format!("Unknown engine: {}. Valid: {}", engine, valid.join(", ")).into());
    }

    let mut library = load_library()?;
    if let Some(app) = library.iter_mut().find(|a| a.id == id) {
        app.engine = engine.to_string();
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

    app.exe_path = file_name;
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
        if lower == "game.exe" || lower == "launcher.exe" || lower.contains("shipping") {
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
    let engine = body.get("engine").and_then(|v| v.as_str()).unwrap_or("wine_bare");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match launch_app(id, engine) {
        Ok(pid) => json!({"ok": true, "pid": pid}),
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
}
