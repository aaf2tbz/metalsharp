use serde_json::{json, Value};
use std::collections::{hash_map::DefaultHasher, HashSet};
use std::fs::{self, OpenOptions};
use std::hash::{Hash, Hasher};
use std::io::Write;
use std::path::{Component, Path, PathBuf};
use std::process::{Command, Stdio};
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

fn steam_prefix() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-steam")
}

#[derive(serde::Serialize, serde::Deserialize, Clone)]
pub struct SharpApp {
    pub id: String,
    pub name: String,
    pub exe_path: String,
    pub install_dir: String,
    pub cover: Option<String>,
    #[serde(default = "default_cover_position")]
    pub cover_position_x: u8,
    #[serde(default = "default_cover_position")]
    pub cover_position_y: u8,
    pub engine: String,
    #[serde(default)]
    pub launch_args: Vec<String>,
    #[serde(default)]
    pub user_launch_args: Vec<String>,
    #[serde(default)]
    pub bottle_id: Option<String>,
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

pub enum SharpInstallOutcome {
    Imported(Box<SharpApp>),
    InstallerStarted { pid: u32, message: String },
}

fn ensure_base_dir() -> Result<(), Box<dyn std::error::Error>> {
    let dir = base_dir();
    if !dir.exists() {
        fs::create_dir_all(&dir)?;
    }
    Ok(())
}

fn default_cover_position() -> u8 {
    50
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

    let before_prune = apps.len();
    apps.retain(|app| !is_unwanted_wine_prefix_app(app));
    let mut changed = apps.len() != before_prune;
    changed |= sync_non_steam_shortcuts(&mut apps);
    changed |= sync_wine_prefix_apps(&mut apps);
    if changed {
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
        changed |= sync_non_steam_shortcut(apps, shortcut);
    }
    changed
}

#[derive(Clone)]
struct WinePrefixApp {
    id: String,
    name: String,
    exe_path: PathBuf,
    install_dir: PathBuf,
}

fn sync_wine_prefix_apps(apps: &mut Vec<SharpApp>) -> bool {
    let mut changed = false;
    for candidate in scan_wine_prefix_apps() {
        changed |= sync_wine_prefix_app(apps, candidate);
    }
    changed
}

fn sync_wine_prefix_app(apps: &mut Vec<SharpApp>, candidate: WinePrefixApp) -> bool {
    let install_dir_string = candidate.install_dir.to_string_lossy().to_string();
    let exe_path_string = candidate.exe_path.to_string_lossy().to_string();
    let absolute_exe = candidate.install_dir.join(&candidate.exe_path);

    if let Some(app) = apps.iter_mut().find(|app| app.id == candidate.id || app_absolute_exe_path(app) == absolute_exe)
    {
        if !app.id.starts_with("wine_app_") {
            return false;
        }

        let size_bytes = dir_size(&candidate.install_dir);
        let mut changed = false;
        if app.name != candidate.name {
            app.name = candidate.name.clone();
            changed = true;
        }
        if app.install_dir != install_dir_string {
            app.install_dir = install_dir_string.clone();
            changed = true;
        }
        if app.exe_path != exe_path_string {
            app.exe_path = exe_path_string.clone();
            changed = true;
        }
        if app.size_bytes != size_bytes {
            app.size_bytes = size_bytes;
            changed = true;
        }
        return changed;
    }

    apps.push(SharpApp {
        id: candidate.id,
        name: candidate.name,
        exe_path: exe_path_string,
        install_dir: install_dir_string,
        cover: None,
        cover_position_x: default_cover_position(),
        cover_position_y: default_cover_position(),
        engine: "auto".to_string(),
        launch_args: Vec::new(),
        user_launch_args: Vec::new(),
        bottle_id: None,
        installed_at: chrono_now(),
        size_bytes: dir_size(&candidate.install_dir),
    });
    true
}

fn scan_wine_prefix_apps() -> Vec<WinePrefixApp> {
    let drive_c = steam_prefix().join("drive_c");
    scan_wine_prefix_apps_under(&drive_c)
}

fn scan_wine_prefix_apps_under(drive_c: &Path) -> Vec<WinePrefixApp> {
    let mut candidates = Vec::new();
    let mut seen_exes = HashSet::new();

    for root in wine_program_roots(drive_c) {
        scan_program_root(&root, &mut seen_exes, &mut candidates);
    }
    for root in wine_desktop_roots(drive_c) {
        scan_desktop_root(&root, &mut seen_exes, &mut candidates);
    }

    candidates
}

fn wine_program_roots(drive_c: &Path) -> Vec<PathBuf> {
    let mut roots = vec![drive_c.join("Program Files"), drive_c.join("Program Files (x86)")];
    let users = drive_c.join("users");
    if let Ok(entries) = fs::read_dir(users) {
        for entry in entries.flatten() {
            roots.push(entry.path().join("AppData").join("Local").join("Programs"));
        }
    }
    roots
}

fn wine_desktop_roots(drive_c: &Path) -> Vec<PathBuf> {
    let mut roots = vec![drive_c.join("users").join("Public").join("Desktop")];
    let users = drive_c.join("users");
    if let Ok(entries) = fs::read_dir(users) {
        for entry in entries.flatten() {
            roots.push(entry.path().join("Desktop"));
        }
    }
    roots
}

fn scan_program_root(root: &Path, seen_exes: &mut HashSet<PathBuf>, candidates: &mut Vec<WinePrefixApp>) {
    let Ok(entries) = fs::read_dir(root) else {
        return;
    };

    for entry in entries.flatten() {
        let app_dir = entry.path();
        if !app_dir.is_dir() || should_skip_wine_app_dir(&app_dir) {
            continue;
        }
        add_wine_prefix_candidate(&app_dir, None, seen_exes, candidates);
    }
}

fn scan_desktop_root(root: &Path, seen_exes: &mut HashSet<PathBuf>, candidates: &mut Vec<WinePrefixApp>) {
    if !root.exists() {
        return;
    }

    for entry in WalkDir::new(root).max_depth(4).into_iter().flatten() {
        let path = entry.path();
        if !path.is_file() || !is_valid_app_exe(&path.to_string_lossy()) || should_skip_wine_app_dir(path) {
            continue;
        }

        let install_dir = import_root_for_exe(path);
        add_wine_prefix_candidate(&install_dir, Some(path.to_path_buf()), seen_exes, candidates);
    }
}

fn add_wine_prefix_candidate(
    install_dir: &Path,
    preferred_exe: Option<PathBuf>,
    seen_exes: &mut HashSet<PathBuf>,
    candidates: &mut Vec<WinePrefixApp>,
) {
    if should_skip_wine_app_dir(install_dir) {
        return;
    }

    let Some(exe) = preferred_exe.or_else(|| find_real_exe(&install_dir.to_path_buf())) else {
        return;
    };
    if should_skip_wine_app_exe(&exe) {
        return;
    }
    if !seen_exes.insert(exe.clone()) {
        return;
    }

    let name = display_name_for_wine_app(install_dir, &exe);
    if should_skip_wine_app_name(&name) {
        return;
    }
    let exe_path = relative_path_string(install_dir, &exe).unwrap_or_else(|_| {
        exe.file_name()
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(exe.to_string_lossy().to_string()))
            .to_string_lossy()
            .to_string()
    });

    candidates.push(WinePrefixApp {
        id: format!("wine_app_{}", stable_wine_app_id(&name, install_dir, &exe)),
        name,
        exe_path: PathBuf::from(exe_path),
        install_dir: install_dir.to_path_buf(),
    });
}

fn display_name_for_wine_app(install_dir: &Path, exe: &Path) -> String {
    let dir_name = install_dir.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let lower = dir_name.to_lowercase();
    if dir_name.is_empty() || lower == "desktop" || lower == "programs" {
        return exe.file_stem().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "Windows App".to_string());
    }
    dir_name
}

fn should_skip_wine_app_dir(path: &Path) -> bool {
    path.components().any(|component| {
        let Component::Normal(value) = component else {
            return false;
        };
        let lower = value.to_string_lossy().to_lowercase();
        matches!(
            lower.as_str(),
            "steam"
                | "windows"
                | "windows media player"
                | "windows nt"
                | "internet explorer"
                | "common files"
                | "microsoft"
                | "microsoft shared"
                | "wine mono"
                | "wine gecko"
                | "$recycle.bin"
        )
    })
}

fn is_unwanted_wine_prefix_app(app: &SharpApp) -> bool {
    app.id.starts_with("wine_app_")
        && (should_skip_wine_app_name(&app.name)
            || should_skip_wine_app_dir(Path::new(&app.install_dir))
            || should_skip_wine_app_exe(&PathBuf::from(&app.exe_path)))
}

fn should_skip_wine_app_name(name: &str) -> bool {
    let lower = name.trim().to_lowercase();
    matches!(
        lower.as_str(),
        "windows media player" | "windows nt" | "wine internet explorer" | "internet explorer" | "notepad" | "wordpad"
    )
}

fn should_skip_wine_app_exe(path: &Path) -> bool {
    let name = path.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
    matches!(
        name.as_str(),
        "wmplayer.exe"
            | "mplayer2.exe"
            | "iexplore.exe"
            | "notepad.exe"
            | "wordpad.exe"
            | "write.exe"
            | "regedit.exe"
            | "winecfg.exe"
            | "winver.exe"
    )
}

fn sync_non_steam_shortcut(apps: &mut Vec<SharpApp>, shortcut: crate::scan::NonSteamShortcut) -> bool {
    let id = format!("steam_shortcut_{}", stable_shortcut_id(&shortcut.name, &shortcut.exe_path));
    let (install_dir, exe_path) = shortcut_library_paths(&shortcut);
    let install_dir_string = install_dir.to_string_lossy().to_string();
    let exe_path_string = exe_path.to_string_lossy().to_string();

    if let Some(app) = apps.iter_mut().find(|app| {
        app.id == id
            || app_absolute_exe_path(app) == shortcut.exe_path
            || (app.id.starts_with("steam_shortcut_") && app.name == shortcut.name)
    }) {
        let size_bytes = dir_size(&install_dir);
        let mut changed = false;
        if app.id != id {
            app.id = id;
            changed = true;
        }
        if app.name != shortcut.name {
            app.name = shortcut.name.clone();
            changed = true;
        }
        if app.install_dir != install_dir_string {
            app.install_dir = install_dir_string.clone();
            changed = true;
        }
        if app.exe_path != exe_path_string {
            app.exe_path = exe_path_string.clone();
            changed = true;
        }
        if app.launch_args != shortcut.launch_args {
            app.launch_args = shortcut.launch_args;
            changed = true;
        }
        if app.size_bytes != size_bytes {
            app.size_bytes = size_bytes;
            changed = true;
        }
        return changed;
    }

    apps.push(SharpApp {
        id,
        name: shortcut.name,
        exe_path: exe_path_string,
        install_dir: install_dir_string,
        cover: None,
        cover_position_x: default_cover_position(),
        cover_position_y: default_cover_position(),
        engine: "auto".to_string(),
        launch_args: shortcut.launch_args,
        user_launch_args: Vec::new(),
        bottle_id: None,
        installed_at: chrono_now(),
        size_bytes: dir_size(&install_dir),
    });
    true
}

fn shortcut_library_paths(shortcut: &crate::scan::NonSteamShortcut) -> (PathBuf, PathBuf) {
    let install_dir = shortcut
        .start_dir
        .clone()
        .filter(|dir| shortcut.exe_path.starts_with(dir))
        .or_else(|| shortcut.exe_path.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| shortcut.exe_path.clone());

    let exe_path = relative_path_string(&install_dir, &shortcut.exe_path)
        .unwrap_or_else(|_| shortcut.exe_path.to_string_lossy().to_string());

    (install_dir, PathBuf::from(exe_path))
}

fn app_absolute_exe_path(app: &SharpApp) -> PathBuf {
    PathBuf::from(&app.install_dir).join(&app.exe_path)
}

fn stable_shortcut_id(name: &str, exe_path: &Path) -> String {
    stable_hash_hex(name, Some(exe_path), None)
}

fn stable_wine_app_id(name: &str, install_dir: &Path, exe_path: &Path) -> String {
    stable_hash_hex(name, Some(install_dir), Some(exe_path))
}

fn stable_hash_hex(name: &str, first_path: Option<&Path>, second_path: Option<&Path>) -> String {
    let mut hash = 0xcbf29ce484222325_u64;
    for bytes in [
        Some(name.as_bytes().to_vec()),
        first_path.map(|path| path.to_string_lossy().as_bytes().to_vec()),
        second_path.map(|path| path.to_string_lossy().as_bytes().to_vec()),
    ]
    .into_iter()
    .flatten()
    {
        for byte in bytes.iter().chain(b"\0") {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
    }
    format!("{:016x}", hash)
}

fn generate_id(name: &str) -> String {
    let clean: String = name.to_lowercase().chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect();
    let timestamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis();
    format!("{}_{}", clean.trim_matches('_'), timestamp)
}

pub fn install_exe(
    src_path: &str,
    custom_name: Option<&str>,
) -> Result<SharpInstallOutcome, Box<dyn std::error::Error>> {
    install_exe_with_options(src_path, custom_name, false)
}

fn install_exe_with_options(
    src_path: &str,
    custom_name: Option<&str>,
    fresh_bottle: bool,
) -> Result<SharpInstallOutcome, Box<dyn std::error::Error>> {
    let src = PathBuf::from(src_path);
    if !src.exists() {
        return Err("Source EXE not found".into());
    }
    if !is_supported_windows_program(&src) {
        return Err("Only .exe and .msi Windows program installers are supported".into());
    }

    if should_run_as_wine_installer(&src) {
        return start_wine_installer(&src, fresh_bottle);
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
        cover_position_x: default_cover_position(),
        cover_position_y: default_cover_position(),
        engine: "auto".to_string(),
        launch_args: Vec::new(),
        user_launch_args: Vec::new(),
        bottle_id: None,
        installed_at,
        size_bytes,
    };

    let mut library = load_library()?;
    library.push(app.clone());
    save_library(&library)?;

    Ok(SharpInstallOutcome::Imported(Box::new(app)))
}

fn should_run_as_wine_installer(src: &Path) -> bool {
    let name = src.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
    src.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("msi")).unwrap_or(false)
        || name.contains("setup")
        || name.contains("install")
        || name.contains("installer")
        || name.contains("launcher")
        || name.contains("bootstrap")
        || name.contains("update")
}

fn is_supported_windows_program(src: &Path) -> bool {
    src.extension()
        .map(|ext| {
            let ext = ext.to_string_lossy();
            ext.eq_ignore_ascii_case("exe") || ext.eq_ignore_ascii_case("msi")
        })
        .unwrap_or(false)
}

fn start_wine_installer(src: &Path, fresh_bottle: bool) -> Result<SharpInstallOutcome, Box<dyn std::error::Error>> {
    let classification = crate::bottles::classify_installer(src);
    let pipeline = classification.pipeline;
    let bottle = if fresh_bottle {
        crate::bottles::create_fresh_installer_bottle(src, &classification)?
    } else {
        crate::bottles::ensure_installer_bottle(src, &classification)?
    };
    start_wine_installer_in_bottle(src, &classification, &bottle, pipeline)
}

fn start_wine_installer_in_bottle(
    src: &Path,
    classification: &crate::bottles::InstallerClassification,
    bottle: &crate::bottles::BottleManifest,
    pipeline: crate::mtsp::engine::PipelineId,
) -> Result<SharpInstallOutcome, Box<dyn std::error::Error>> {
    let staged_exe = stage_installer_exe(src, bottle)?;
    let work_dir = staged_exe.parent().ok_or("installer staging folder not found")?.to_path_buf();
    let launch_id = installer_launch_id(src, pipeline);
    let log_path = crate::bottles::next_launch_log_path(&bottle.id);
    let prefix_path = PathBuf::from(&bottle.prefix_path);

    let pid = if classification.installer_kind == crate::bottles::InstallerKind::Msi {
        launch_msi_installer(&staged_exe, &prefix_path, &log_path)?
    } else {
        let (pid, _, _) = crate::mtsp::launcher::launch_custom_with_options(
            launch_id,
            &work_dir,
            &staged_exe,
            pipeline,
            &[],
            crate::mtsp::launcher::CustomLaunchOptions {
                prefix_path: Some(prefix_path),
                log_path: Some(log_path.clone()),
                ..Default::default()
            },
        )?;
        pid
    };
    let _ = crate::bottles::set_launch_started(&bottle.id, pid, &log_path);
    crate::bottles::watch_bottle_launch(bottle.id.clone(), pid);

    Ok(SharpInstallOutcome::InstallerStarted {
        pid,
        message: format!(
            "Installer started with the {} pipeline in bottle {}. MetalSharp will watch for completion and refresh detected apps.",
            pipeline_engine_id(pipeline),
            bottle.id
        ),
    })
}

fn launch_msi_installer(
    staged_msi: &Path,
    prefix_path: &Path,
    log_path: &Path,
) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }
    fs::create_dir_all(prefix_path)?;
    if let Some(parent) = log_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "installer_kind=msi")?;
    writeln!(log, "prefix={}", prefix_path.display())?;
    writeln!(log, "msi={}", staged_msi.display())?;
    writeln!(log, "--- wine output ---")?;
    let stdout = log.try_clone()?;

    let mut cmd = Command::new(&wine);
    cmd.arg("msiexec")
        .arg("/i")
        .arg(staged_msi)
        .env("WINEPREFIX", prefix_path.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(log));
    if let Some(parent) = staged_msi.parent() {
        cmd.current_dir(parent);
    }
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;
    Ok(child.id())
}

fn installer_pipeline(src: &Path) -> crate::mtsp::engine::PipelineId {
    crate::bottles::classify_installer(src).pipeline
}

fn stage_installer_exe(
    src: &Path,
    bottle: &crate::bottles::BottleManifest,
) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let file_name = src.file_name().ok_or("installer filename not found")?;
    let dir = crate::bottles::installer_payload_dir(&bottle.id);
    fs::create_dir_all(&dir)?;
    let dest = dir.join(file_name);
    fs::copy(src, &dest)?;
    Ok(dest)
}

fn installer_launch_id(src: &Path, pipeline: crate::mtsp::engine::PipelineId) -> u32 {
    let key = format!("installer:{}:{}", pipeline_engine_id(pipeline), src.to_string_lossy());
    stable_launch_id(&key)
}

pub fn import_bottle_app(
    bottle_id: &str,
    exe_path: &str,
    name: Option<&str>,
) -> Result<SharpApp, Box<dyn std::error::Error>> {
    let bottle = crate::bottles::load_bottle(bottle_id)?;
    let exe = PathBuf::from(exe_path);
    if !exe.exists() || exe.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("exe")) != Some(true) {
        return Err("Bottle app executable not found".into());
    }
    let install_dir = exe.parent().ok_or("Bottle app install directory not found")?.to_path_buf();
    let app_name = name
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_string)
        .or_else(|| exe.file_stem().map(|stem| stem.to_string_lossy().to_string()))
        .unwrap_or_else(|| bottle.name.clone());
    let id = format!("bottle_app_{}", stable_shortcut_id(&format!("{}:{}", bottle_id, app_name), &exe));
    let exe_path = relative_path_string(&install_dir, &exe)?;
    let install_dir_string = install_dir.to_string_lossy().to_string();

    let app = SharpApp {
        id,
        name: app_name,
        exe_path,
        install_dir: install_dir_string,
        cover: None,
        cover_position_x: default_cover_position(),
        cover_position_y: default_cover_position(),
        engine: "auto".to_string(),
        launch_args: Vec::new(),
        user_launch_args: Vec::new(),
        bottle_id: Some(bottle.id),
        installed_at: chrono_now(),
        size_bytes: dir_size(&install_dir),
    };

    let mut library = load_library()?;
    let absolute = app_absolute_exe_path(&app);
    if let Some(existing) =
        library.iter_mut().find(|existing| existing.id == app.id || app_absolute_exe_path(existing) == absolute)
    {
        existing.name = app.name.clone();
        existing.exe_path = app.exe_path.clone();
        existing.install_dir = app.install_dir.clone();
        existing.bottle_id = app.bottle_id.clone();
        existing.size_bytes = app.size_bytes;
        let updated = existing.clone();
        save_library(&library)?;
        return Ok(updated);
    }

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

    let cef_compat_deployed = maybe_deploy_cef_compat_wrapper(&app, &exe_path)?;
    let pipeline = resolve_sharp_pipeline(engine, &exe_path);
    let launch_id = stable_launch_id(&app.id);
    let launch_args = combined_launch_args(&app);
    let (pid, game_type, recipe) = if let Some(bottle_id) = app.bottle_id.as_deref() {
        let bottle = crate::bottles::load_bottle(bottle_id)?;
        let log_path = crate::bottles::next_launch_log_path(bottle_id);
        crate::mtsp::launcher::launch_custom_with_options(
            launch_id,
            &work_dir,
            &exe_path,
            pipeline,
            &launch_args,
            crate::mtsp::launcher::CustomLaunchOptions {
                prefix_path: Some(PathBuf::from(bottle.prefix_path)),
                log_path: Some(log_path.clone()),
                ..Default::default()
            },
        )
        .inspect(|result| {
            let _ = crate::bottles::set_launch_started(bottle_id, result.0, &log_path);
            crate::bottles::watch_bottle_launch(bottle_id.to_string(), result.0);
        })?
    } else {
        crate::mtsp::launcher::launch_custom_with_pipeline(launch_id, &work_dir, &exe_path, pipeline, &launch_args)?
    };

    let mut warnings = recipe.warnings;
    if cef_compat_deployed {
        warnings.push("CEF compatibility wrapper active for this launcher.".to_string());
    }

    Ok(SharpLaunchResult { pid, game_type, pipeline, exe_path: exe_path.to_string_lossy().to_string(), warnings })
}

fn maybe_deploy_cef_compat_wrapper(app: &SharpApp, exe_path: &Path) -> Result<bool, Box<dyn std::error::Error>> {
    if !should_apply_cef_compat(app, exe_path) {
        return Ok(false);
    }

    let stem = exe_path.file_stem().and_then(|stem| stem.to_str()).ok_or("CEF app executable has no stem")?;
    if stem.ends_with("_real") {
        return Ok(false);
    }
    let real_path = exe_path.with_file_name(format!("{}_real.exe", stem));
    let marker = exe_path.with_file_name(format!(".ms_cef_compat_{}", stem));

    let original_data = fs::read(exe_path).or_else(|_| fs::read(&real_path))?;
    let is_64_bit = crate::mtsp::pe::parse_pe_imports(&original_data).map(|info| info.is_64_bit).unwrap_or(false);
    let wrapper = find_bundled_cef_compat_wrapper(is_64_bit).ok_or("CEF compatibility wrapper is missing")?;
    let wrapper_size = fs::metadata(&wrapper).map(|metadata| metadata.len()).unwrap_or(0);
    if wrapper_size == 0 || wrapper_size > 512 * 1024 {
        return Err("CEF compatibility wrapper asset is invalid".into());
    }
    let hook = find_bundled_cef_child_hook(is_64_bit).ok_or("CEF child-process hook is missing")?;
    deploy_cef_child_hook(exe_path, &hook)?;

    if real_path.exists() {
        redeploy_cef_compat_wrapper_if_needed(exe_path, &real_path, &wrapper)?;
        fs::write(&marker, "deployed")?;
        return Ok(true);
    }

    fs::rename(exe_path, &real_path)?;
    if let Err(error) = fs::copy(&wrapper, exe_path) {
        let _ = fs::rename(&real_path, exe_path);
        return Err(error.into());
    }
    fs::write(&marker, "deployed")?;
    Ok(true)
}

fn redeploy_cef_compat_wrapper_if_needed(
    exe_path: &Path,
    real_path: &Path,
    wrapper_path: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    let Ok(current_exe) = fs::read(exe_path) else {
        fs::copy(wrapper_path, exe_path)?;
        return Ok(());
    };
    let wrapper_data = fs::read(wrapper_path)?;
    if current_exe == wrapper_data {
        return Ok(());
    }

    if current_exe.len() > 512 * 1024 {
        fs::copy(exe_path, real_path)?;
    }
    fs::copy(wrapper_path, exe_path)?;
    Ok(())
}

fn deploy_cef_child_hook(exe_path: &Path, hook_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let hook_size = fs::metadata(hook_path).map(|metadata| metadata.len()).unwrap_or(0);
    if hook_size == 0 || hook_size > 1024 * 1024 {
        return Err("CEF child-process hook asset is invalid".into());
    }
    let dest = exe_path.with_file_name("metalsharp-cefchildhook.dll");
    let needs_copy = match (fs::read(&dest), fs::read(hook_path)) {
        (Ok(current), Ok(source)) => current != source,
        _ => true,
    };
    if needs_copy {
        fs::copy(hook_path, dest)?;
    }
    Ok(())
}

fn should_apply_cef_compat(app: &SharpApp, exe_path: &Path) -> bool {
    if app.bottle_id.is_none() {
        return false;
    }
    let name = app.name.to_ascii_lowercase();
    let exe_name = exe_path.file_name().map(|name| name.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    if exe_name.ends_with("_real.exe") || exe_name.contains("unins") || exe_name.contains("setup") {
        return false;
    }
    let likely_launcher = crate::mtsp::recipe::is_likely_launcher_exe(exe_path)
        || name.contains("launcher")
        || name.contains("ea app")
        || name.contains("ubisoft")
        || name.contains("battle.net")
        || name.contains("epic games")
        || name.contains("rockstar");
    likely_launcher && (executable_contains_cef_markers(exe_path) || install_dir_contains_cef_markers(exe_path))
}

fn executable_contains_cef_markers(exe_path: &Path) -> bool {
    let Ok(data) = fs::read(exe_path) else {
        return false;
    };
    let haystack = String::from_utf8_lossy(&data).to_ascii_lowercase();
    [
        "cef initialized",
        "cef version",
        "chromium",
        "chrome-runtime",
        "launcherappbrowser",
        "cefclient",
        "libcef",
        "electron",
        "app.asar",
        "webview2",
    ]
    .iter()
    .any(|needle| haystack.contains(needle))
}

fn install_dir_contains_cef_markers(exe_path: &Path) -> bool {
    let Some(dir) = exe_path.parent() else {
        return false;
    };
    WalkDir::new(dir).max_depth(3).into_iter().filter_map(Result::ok).any(|entry| {
        let name = entry.file_name().to_string_lossy().to_ascii_lowercase();
        matches!(
            name.as_str(),
            "libcef.dll"
                | "chrome_elf.dll"
                | "chrome_100_percent.pak"
                | "chrome_200_percent.pak"
                | "vk_swiftshader.dll"
                | "v8_context_snapshot.bin"
                | "resources.pak"
                | "app.asar"
                | "msedgewebview2.exe"
        )
    })
}

fn find_bundled_cef_compat_wrapper(is_64_bit: bool) -> Option<PathBuf> {
    let filename = if is_64_bit { "cefcompat-wrapper64.exe" } else { "cefcompat-wrapper32.exe" };
    find_bundled_cef_asset(filename)
}

fn find_bundled_cef_child_hook(is_64_bit: bool) -> Option<PathBuf> {
    let filename = if is_64_bit { "cefchildhook64.dll" } else { "cefchildhook32.dll" };
    find_bundled_cef_asset(filename)
}

fn find_bundled_cef_asset(filename: &str) -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let wrapper = resources.join("bundles").join(filename);
        if wrapper.exists() {
            return Some(wrapper);
        }
    }

    let dev = PathBuf::from("app").join("bundles").join(filename);
    if dev.exists() {
        return Some(dev);
    }

    let src_rust_dev = PathBuf::from("..").join("bundles").join(filename);
    if src_rust_dev.exists() {
        return Some(src_rust_dev);
    }

    None
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
    recipe.launch_args.extend(combined_launch_args(&app));
    Ok(crate::mtsp::recipe::diagnose_recipe(recipe))
}

fn combined_launch_args(app: &SharpApp) -> Vec<String> {
    app.launch_args.iter().chain(app.user_launch_args.iter()).cloned().collect()
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

pub fn set_cover_position(id: &str, x: u8, y: u8) -> Result<(), Box<dyn std::error::Error>> {
    let mut library = load_library()?;
    if let Some(app) = library.iter_mut().find(|a| a.id == id) {
        app.cover_position_x = x.min(100);
        app.cover_position_y = y.min(100);
        save_library(&library)?;
        Ok(())
    } else {
        Err("App not found".into())
    }
}

pub fn set_launch_args(id: &str, args: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    let mut library = load_library()?;
    if let Some(app) = library.iter_mut().find(|a| a.id == id) {
        app.user_launch_args =
            args.into_iter().map(|arg| arg.trim().to_string()).filter(|arg| !arg.is_empty()).collect();
        save_library(&library)?;
        Ok(())
    } else {
        Err("App not found".into())
    }
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
        && !lower.contains("update")
        && !lower.contains("vcredist")
        && !lower.contains("crashhandler")
        && !lower.contains("server")
}

fn find_real_exe(dir: &PathBuf) -> Option<PathBuf> {
    let mut best: Option<(i32, PathBuf)> = None;
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
        let depth = path.strip_prefix(dir).map(|relative| relative.components().count()).unwrap_or(4) as i32;
        let mut score = 100 - depth;
        if lower.contains("launcher") {
            score += 50;
        }
        if lower.ends_with("_real.exe") {
            score += 30;
        }
        if lower.contains("helper") || lower.contains("crash") || lower.contains("service") {
            score -= 50;
        }
        if best.as_ref().map(|(best_score, _)| score > *best_score).unwrap_or(true) {
            best = Some((score, path.to_path_buf()));
        }
    }
    best.map(|(_, path)| path)
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
    let fresh_bottle = body.get("freshBottle").and_then(|v| v.as_bool()).unwrap_or(false);
    if src_path.is_empty() {
        return json!({"ok": false, "error": "srcPath required"});
    }
    match install_exe_with_options(src_path, custom_name, fresh_bottle) {
        Ok(SharpInstallOutcome::Imported(app)) => json!({"ok": true, "app": *app}),
        Ok(SharpInstallOutcome::InstallerStarted { pid, message }) => {
            json!({"ok": true, "installing": true, "pid": pid, "message": message})
        },
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_import_bottle_app(body: &serde_json::Map<String, Value>) -> Value {
    let bottle_id = body.get("bottleId").and_then(|v| v.as_str()).unwrap_or("");
    let exe_path = body.get("exePath").and_then(|v| v.as_str()).unwrap_or("");
    let name = body.get("name").and_then(|v| v.as_str());
    if bottle_id.is_empty() || exe_path.is_empty() {
        return json!({"ok": false, "error": "bottleId and exePath required"});
    }
    match import_bottle_app(bottle_id, exe_path, name) {
        Ok(app) => json!({"ok": true, "app": app}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn relaunch_bottle_installer(id: &str) -> Result<SharpInstallOutcome, Box<dyn std::error::Error>> {
    let bottle = crate::bottles::load_bottle(id)?;
    if bottle.bottle_type != crate::bottles::BottleType::Installer {
        return Err("Only installer bottles can relaunch their source installer".into());
    }
    let source = bottle.source_installer_path.clone().ok_or("Bottle has no source installer path")?;
    let source = Path::new(&source);
    let classification = crate::bottles::classify_installer(source);
    start_wine_installer_in_bottle(source, &classification, &bottle, classification.pipeline)
}

pub fn handle_relaunch_bottle_installer(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match relaunch_bottle_installer(id) {
        Ok(SharpInstallOutcome::InstallerStarted { pid, message }) => {
            json!({"ok": true, "installing": true, "pid": pid, "message": message})
        },
        Ok(SharpInstallOutcome::Imported(app)) => json!({"ok": true, "app": *app}),
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

pub fn handle_set_cover_position(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let x = body.get("x").and_then(|v| v.as_u64()).unwrap_or(50).min(100) as u8;
    let y = body.get("y").and_then(|v| v.as_u64()).unwrap_or(50).min(100) as u8;
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match set_cover_position(id, x, y) {
        Ok(()) => json!({"ok": true}),
        Err(e) => json!({"ok": false, "error": e.to_string()}),
    }
}

pub fn handle_set_launch_args(body: &serde_json::Map<String, Value>) -> Value {
    let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("");
    let args = body
        .get("args")
        .and_then(|v| v.as_array())
        .map(|values| values.iter().filter_map(|v| v.as_str().map(str::to_string)).collect())
        .unwrap_or_default();
    if id.is_empty() {
        return json!({"ok": false, "error": "id required"});
    }
    match set_launch_args(id, args) {
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

    #[test]
    fn non_steam_shortcut_sync_updates_existing_launch_args_by_path() {
        let root = test_dir("shortcut-args");
        let exe = root.join("Game.exe");
        let mut apps = vec![SharpApp {
            id: "steam_shortcut_old_runtime_hash".into(),
            name: "Game".into(),
            exe_path: "Game.exe".into(),
            install_dir: root.to_string_lossy().to_string(),
            cover: None,
            cover_position_x: default_cover_position(),
            cover_position_y: default_cover_position(),
            engine: "auto".into(),
            launch_args: vec!["-dx11".into()],
            user_launch_args: vec!["-user".into()],
            bottle_id: None,
            installed_at: chrono_now(),
            size_bytes: 0,
        }];

        let changed = sync_non_steam_shortcut(
            &mut apps,
            crate::scan::NonSteamShortcut {
                name: "Game".into(),
                exe_path: exe,
                start_dir: Some(root),
                launch_args: vec!["-d3d12".into(), "-windowed".into()],
            },
        );

        assert!(changed);
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].launch_args, vec!["-d3d12", "-windowed"]);
        assert_eq!(apps[0].user_launch_args, vec!["-user"]);
    }

    #[test]
    fn non_steam_shortcut_sync_refreshes_existing_metadata_by_name() {
        let old_root = test_dir("shortcut-old");
        let new_root = test_dir("shortcut-new");
        let new_exe = new_root.join("Renamed.exe");
        let mut apps = vec![SharpApp {
            id: "steam_shortcut_old_target_hash".into(),
            name: "Game".into(),
            exe_path: "Old.exe".into(),
            install_dir: old_root.to_string_lossy().to_string(),
            cover: Some("cover.png".into()),
            cover_position_x: 34,
            cover_position_y: 67,
            engine: "m11".into(),
            launch_args: vec!["-dx11".into()],
            user_launch_args: vec!["-custom".into()],
            bottle_id: None,
            installed_at: chrono_now(),
            size_bytes: 123,
        }];

        let changed = sync_non_steam_shortcut(
            &mut apps,
            crate::scan::NonSteamShortcut {
                name: "Game".into(),
                exe_path: new_exe,
                start_dir: Some(new_root.clone()),
                launch_args: vec!["-d3d12".into()],
            },
        );

        assert!(changed);
        assert_eq!(apps.len(), 1);
        assert!(apps[0].id.starts_with("steam_shortcut_"));
        assert_ne!(apps[0].id, "steam_shortcut_old_target_hash");
        assert_eq!(apps[0].name, "Game");
        assert_eq!(apps[0].install_dir, new_root.to_string_lossy());
        assert_eq!(apps[0].exe_path, "Renamed.exe");
        assert_eq!(apps[0].cover.as_deref(), Some("cover.png"));
        assert_eq!(apps[0].cover_position_x, 34);
        assert_eq!(apps[0].cover_position_y, 67);
        assert_eq!(apps[0].engine, "m11");
        assert_eq!(apps[0].launch_args, vec!["-d3d12"]);
        assert_eq!(apps[0].user_launch_args, vec!["-custom"]);
    }

    #[test]
    fn non_steam_shortcut_ids_use_explicit_stable_hex_hashes() {
        let path = PathBuf::from("/tmp/Game/Game.exe");

        let first = stable_shortcut_id("Game", &path);
        let second = stable_shortcut_id("Game", &path);
        let changed = stable_shortcut_id("Game", Path::new("/tmp/Game/Other.exe"));

        assert_eq!(first, second);
        assert_eq!(first.len(), 16);
        assert!(first.chars().all(|ch| ch.is_ascii_hexdigit()));
        assert_ne!(first, changed);
    }

    #[test]
    fn wine_prefix_scan_finds_program_files_launcher_apps() {
        let drive_c = test_dir("wine-prefix").join("drive_c");
        let app_dir = drive_c.join("Program Files").join("Minecraft Launcher");
        fs::create_dir_all(&app_dir).expect("create app dir");
        fs::write(app_dir.join("MinecraftLauncher.exe"), b"not pe").expect("write launcher");

        let apps = scan_wine_prefix_apps_under(&drive_c);

        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].name, "Minecraft Launcher");
        assert_eq!(apps[0].exe_path, PathBuf::from("MinecraftLauncher.exe"));
        assert!(apps[0].id.starts_with("wine_app_"));
        let _ = fs::remove_dir_all(drive_c);
    }

    #[test]
    fn wine_prefix_scan_ignores_builtin_windows_apps() {
        let drive_c = test_dir("wine-prefix-builtins").join("drive_c");
        let media_dir = drive_c.join("Program Files").join("Windows Media Player");
        let nt_dir = drive_c.join("Program Files").join("Windows NT");
        fs::create_dir_all(&media_dir).expect("create media dir");
        fs::create_dir_all(&nt_dir).expect("create nt dir");
        fs::write(media_dir.join("wmplayer.exe"), b"not pe").expect("write wmplayer");
        fs::write(nt_dir.join("wordpad.exe"), b"not pe").expect("write wordpad");

        let apps = scan_wine_prefix_apps_under(&drive_c);

        assert!(apps.is_empty());
        let _ = fs::remove_dir_all(drive_c);
    }

    #[test]
    fn wine_prefix_prune_removes_persisted_builtin_apps_only() {
        let media_dir = PathBuf::from("/tmp/prefix/drive_c/Program Files/Windows Media Player");
        let game_dir = PathBuf::from("/tmp/prefix/drive_c/Program Files/Minecraft Launcher");
        let media = SharpApp {
            id: "wine_app_media".into(),
            name: "Windows Media Player".into(),
            exe_path: "wmplayer.exe".into(),
            install_dir: media_dir.to_string_lossy().to_string(),
            cover: None,
            cover_position_x: default_cover_position(),
            cover_position_y: default_cover_position(),
            engine: "auto".into(),
            launch_args: Vec::new(),
            user_launch_args: Vec::new(),
            bottle_id: None,
            installed_at: chrono_now(),
            size_bytes: 0,
        };
        let game = SharpApp {
            id: "wine_app_minecraft".into(),
            name: "Minecraft Launcher".into(),
            exe_path: "MinecraftLauncher.exe".into(),
            install_dir: game_dir.to_string_lossy().to_string(),
            cover: None,
            cover_position_x: default_cover_position(),
            cover_position_y: default_cover_position(),
            engine: "auto".into(),
            launch_args: Vec::new(),
            user_launch_args: Vec::new(),
            bottle_id: None,
            installed_at: chrono_now(),
            size_bytes: 0,
        };

        assert!(is_unwanted_wine_prefix_app(&media));
        assert!(!is_unwanted_wine_prefix_app(&game));
    }

    #[test]
    fn wine_prefix_sync_does_not_duplicate_existing_steam_shortcut_path() {
        let drive_c = test_dir("wine-prefix-shortcut").join("drive_c");
        let app_dir = drive_c.join("users").join("alex").join("Desktop").join("The Joy of Creation Story Mode");
        fs::create_dir_all(&app_dir).expect("create app dir");
        let exe = app_dir.join("TJoC_SM.exe");
        fs::write(&exe, b"not pe").expect("write exe");

        let mut apps = vec![SharpApp {
            id: "steam_shortcut_existing".into(),
            name: "The Joy Of Creation".into(),
            exe_path: "TJoC_SM.exe".into(),
            install_dir: app_dir.to_string_lossy().to_string(),
            cover: None,
            cover_position_x: default_cover_position(),
            cover_position_y: default_cover_position(),
            engine: "auto".into(),
            launch_args: Vec::new(),
            user_launch_args: Vec::new(),
            bottle_id: None,
            installed_at: chrono_now(),
            size_bytes: 0,
        }];

        let candidate = scan_wine_prefix_apps_under(&drive_c).pop().expect("find desktop game");
        let changed = sync_wine_prefix_app(&mut apps, candidate);

        assert!(!changed);
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].id, "steam_shortcut_existing");
        let _ = fs::remove_dir_all(drive_c);
    }

    #[test]
    fn bottle_app_import_records_bottle_id() {
        let dir = test_dir("bottle-app-import");
        let exe = dir.join("Demo.exe");
        fs::create_dir_all(&dir).expect("create app dir");
        fs::write(&exe, b"not pe").expect("write exe");
        let app = SharpApp {
            id: "bottle_app_demo".into(),
            name: "Demo".into(),
            exe_path: relative_path_string(&dir, &exe).expect("relative exe"),
            install_dir: dir.to_string_lossy().to_string(),
            cover: None,
            cover_position_x: default_cover_position(),
            cover_position_y: default_cover_position(),
            engine: "auto".into(),
            launch_args: Vec::new(),
            user_launch_args: Vec::new(),
            bottle_id: Some("installer_demo".into()),
            installed_at: chrono_now(),
            size_bytes: 0,
        };

        assert_eq!(app.bottle_id.as_deref(), Some("installer_demo"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn refresh_setup_reference_prefers_launcher_over_updater_tools() {
        let dir = test_dir("launcher-refresh");
        fs::create_dir_all(dir.join("tools")).expect("create tools dir");
        fs::write(dir.join("MinecraftLauncher_real.exe"), b"not pe").expect("write launcher");
        fs::write(dir.join("tools").join("NativeUpdater.exe"), b"not pe").expect("write updater");

        let found = find_real_exe(&dir).expect("find launcher exe");

        assert_eq!(found, dir.join("MinecraftLauncher_real.exe"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn launcher_exes_run_as_wine_installers() {
        assert!(should_run_as_wine_installer(Path::new("/tmp/MinecraftInstaller.exe")));
        assert!(should_run_as_wine_installer(Path::new("/tmp/MinecraftLauncher.exe")));
        assert!(should_run_as_wine_installer(Path::new("/tmp/DemoSetup.msi")));
        assert!(!should_run_as_wine_installer(Path::new("/tmp/TJoC_SM.exe")));
    }

    #[test]
    fn install_windows_program_accepts_exe_and_msi_only() {
        assert!(is_supported_windows_program(Path::new("/tmp/MinecraftInstaller.exe")));
        assert!(is_supported_windows_program(Path::new("/tmp/DemoSetup.msi")));
        assert!(is_supported_windows_program(Path::new("/tmp/DEMOSETUP.MSI")));
        assert!(!is_supported_windows_program(Path::new("/tmp/readme.txt")));
        assert!(!is_supported_windows_program(Path::new("/tmp/installer")));
    }

    #[test]
    fn generic_pe32_installers_use_m9_pipeline() {
        let dir = test_dir("installer-pe32");
        fs::create_dir_all(&dir).expect("create test dir");
        let exe = dir.join("DemoInstaller.exe");
        write_test_pe(&exe, 0x014c, 0x10b);

        assert_eq!(installer_pipeline(&exe), crate::mtsp::engine::PipelineId::M9);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn known_launcher_installers_use_plain_wine_pipeline() {
        let dir = test_dir("installer-known-launcher");
        fs::create_dir_all(&dir).expect("create test dir");
        let exe = dir.join("MinecraftInstaller.exe");
        write_test_pe(&exe, 0x014c, 0x10b);

        assert_eq!(installer_pipeline(&exe), crate::mtsp::engine::PipelineId::WineBare);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn pe64_installers_without_graphics_imports_use_plain_wine() {
        let dir = test_dir("installer-pe64");
        fs::create_dir_all(&dir).expect("create test dir");
        let exe = dir.join("Setup.exe");
        write_test_pe(&exe, 0x8664, 0x20b);

        assert_eq!(installer_pipeline(&exe), crate::mtsp::engine::PipelineId::WineBare);
        let _ = fs::remove_dir_all(dir);
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-sharp-library-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }

    fn write_test_pe(path: &Path, machine: u16, optional_magic: u16) {
        let mut data = vec![0_u8; 0x200];
        data[0] = b'M';
        data[1] = b'Z';
        data[0x3c..0x40].copy_from_slice(&(0x80_u32).to_le_bytes());
        data[0x80..0x84].copy_from_slice(b"PE\0\0");
        data[0x84..0x86].copy_from_slice(&machine.to_le_bytes());
        data[0x86..0x88].copy_from_slice(&(0_u16).to_le_bytes());
        data[0x94..0x96].copy_from_slice(&(0xf0_u16).to_le_bytes());
        data[0x98..0x9a].copy_from_slice(&optional_magic.to_le_bytes());
        fs::write(path, data).expect("write test PE");
    }
}
