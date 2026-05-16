use serde_json::{json, Value};
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

static STEAM_INSTALLING: AtomicBool = AtomicBool::new(false);
const STEAMWEBHELPER_WRAPPER_MAX_BYTES: u64 = 100_000;

fn ms_wine() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("runtime")
        .join("wine")
        .join("bin")
        .join("metalsharp-wine")
}

fn steam_prefix() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-steam")
}

fn steam_exe_path() -> PathBuf {
    steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe")
}

fn macos_steam_app() -> Option<PathBuf> {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates = [
        PathBuf::from("/Applications/Steam.app"),
        home.join("Applications/Steam.app"),
        home.join("Library/Application Support/Steam/Steam.AppBundle/Steam/Steam.app"),
    ];
    candidates.into_iter().find(|p| p.exists())
}

fn macos_steam_install_url() -> &'static str {
    "https://store.steampowered.com/about/"
}

pub fn status() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let wine_steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");
    let wine_steam_exe = wine_steam_dir.join("Steam.exe");
    let windows_installed = wine_steam_exe.exists();
    let windows_path = if windows_installed { Some(wine_steam_dir.to_string_lossy().to_string()) } else { None };

    let login_state = detect_login_state();

    let mac_paths = [
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
        home.join("Library/Application Support/Steam/steamapps"),
    ];
    let mac_app = macos_steam_app();
    let mac_installed = mac_app.is_some() || mac_paths.iter().any(|p| p.exists());
    let mac_running = is_macos_steam_running();

    let running = is_wine_steam_running();
    let ms_available = ms_wine().exists();
    let installing = is_installing_steam();

    json!({
        "installed": windows_installed,
        "path": windows_path,
        "login_state": login_state,
        "mac_installed": mac_installed,
        "mac_path": mac_app.map(|p| p.to_string_lossy().to_string()),
        "mac_install_url": macos_steam_install_url(),
        "mac_running": mac_running,
        "running": running,
        "metalsharp_wine_available": ms_available,
        "installing": installing
    })
}

pub fn is_wine_steam_running() -> bool {
    process_lines().iter().any(|line| is_wine_steam_process_line(line))
}

fn process_lines() -> Vec<String> {
    Command::new("ps")
        .args(["axo", "pid=,command="])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.lines().map(str::to_string).collect())
        .unwrap_or_default()
}

fn is_wine_steam_process_line(line: &str) -> bool {
    if line.contains("Steam.app/Contents/MacOS") || line.contains("steam_osx") {
        return false;
    }

    let prefix = steam_prefix().to_string_lossy().to_string();
    let exe = steam_exe_path().to_string_lossy().to_string();
    let lower = line.to_lowercase();

    line.contains(&exe)
        || (line.contains(&prefix) && (line.contains("Steam.exe") || line.contains("steam.exe")))
        || (lower.contains("c:\\program files (x86)\\steam")
            && (lower.contains("steam.exe")
                || lower.contains("steamwebhelper")
                || lower.contains("steamservice.exe")
                || lower.contains("steamerrorreporter")))
}

pub fn is_macos_steam_running() -> bool {
    process_lines().iter().any(|line| {
        line.contains("Steam.app/Contents/MacOS/steam_osx")
            || line.contains("Steam.AppBundle/Steam/Contents/MacOS/ipcserver")
    })
}

fn latest_macos_steam_pid() -> u32 {
    Command::new("pgrep")
        .args(["-n", "steam_osx"])
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .and_then(|s| s.trim().parse::<u32>().ok())
        .unwrap_or(0)
}

pub fn is_installing_steam() -> bool {
    STEAM_INSTALLING.load(Ordering::SeqCst)
}

fn ensure_steam_launch_ready(steam_dir: &PathBuf) {
    let cef_dir = steam_dir.join("bin").join("cef").join("cef.win64");
    let wrapper = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");

    let wrapper_size = std::fs::metadata(&wrapper).map(|m| m.len()).unwrap_or(0);
    let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);

    let wrapper_missing = wrapper_size == 0;
    let wrapper_overwritten = wrapper_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES;
    let real_missing_or_bad = real_size > 0 && real_size < STEAMWEBHELPER_WRAPPER_MAX_BYTES;

    if wrapper_missing || wrapper_overwritten || real_missing_or_bad {
        deploy_steamwebhelper_wrapper(steam_dir);
    }
}

fn steam_runtime_dyld_path() -> String {
    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    )
}

fn spawn_wine_steam(args: &[&str]) -> Result<u32, Box<dyn std::error::Error>> {
    spawn_wine_steam_with_env(args, &[])
}

fn spawn_wine_steam_with_env(args: &[&str], extra_env: &[(String, String)]) -> Result<u32, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let exe = steam_exe_path();
    let steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");

    if !exe.exists() || !steam_dir.join("steamui.dll").exists() {
        return Err("Steam is not installed — use the setup wizard to install it first".into());
    }

    ensure_steam_launch_ready(&steam_dir);

    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let dyld = steam_runtime_dyld_path();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&steam_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("STEAM_RUNTIME", "0")
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .env(
            "WINEDLLOVERRIDES",
            "dxgi,d3d11,d3d10core=n,b;bcrypt=b;ncrypt=b;gameoverlayrenderer,gameoverlayrenderer64=d",
        )
        .arg(&exe)
        .args(args)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

    for (key, val) in extra_env {
        cmd.env(key, val);
    }

    let child = cmd.spawn()?;

    Ok(child.id())
}

pub fn launch_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    launch_wine_steam_with_env(&[])
}

pub fn launch_wine_steam_with_env(extra_env: &[(String, String)]) -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let exe = steam_exe_path();
    let steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");

    if !exe.exists() || !steam_dir.join("steamui.dll").exists() {
        return Err("Steam is not installed — use the setup wizard to install it first".into());
    }

    if is_wine_steam_running() && extra_env.is_empty() {
        return Ok(json!({"ok": true, "message": "Steam already running"}));
    }

    if is_wine_steam_running() {
        stop_wine_steam()?;
        for _ in 0..20 {
            std::thread::sleep(std::time::Duration::from_millis(500));
            if !is_wine_steam_running() {
                break;
            }
        }
    }

    ensure_steam_launch_ready(&steam_dir);

    let pid = spawn_wine_steam_with_env(
        &["-no-cef-sandbox", "-cef-single-process", "-noverifyfiles", "-no-dwrite"],
        extra_env,
    )?;

    let _ = open_wine_steam_library();

    Ok(json!({"ok": true, "pid": pid}))
}

pub fn open_wine_steam_library() -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    if !is_wine_steam_running() {
        return Err("Wine Steam is not running".into());
    }

    let _ = spawn_wine_steam(&["steam://open/library"])?;

    Ok(json!({"ok": true}))
}

pub fn launch_macos_steam() -> Result<Value, Box<dyn std::error::Error>> {
    if macos_steam_app().is_none() {
        return Err("macOS Steam is not installed".into());
    }

    let child = Command::new("open")
        .args(["-a", "Steam", "steam://open/library"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": latest_macos_steam_pid().max(child.id())}))
}

pub fn install_macos_steam() -> Result<Value, Box<dyn std::error::Error>> {
    if let Some(app) = macos_steam_app() {
        return Ok(json!({"ok": true, "installed": true, "path": app.to_string_lossy()}));
    }

    let child = Command::new("open")
        .arg(macos_steam_install_url())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "installed": false, "pid": child.id(), "url": macos_steam_install_url()}))
}

pub fn stop_macos_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let _ = Command::new("osascript")
        .args(["-e", "tell application \"Steam\" to quit"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    std::thread::sleep(std::time::Duration::from_secs(2));

    if is_macos_steam_running() {
        let _ = Command::new("pkill")
            .args(["-TERM", "-x", "steam_osx"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
        let _ = Command::new("pkill")
            .args(["-TERM", "-f", "Steam.AppBundle/Steam/Contents/MacOS/ipcserver"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    if is_macos_steam_running() {
        let _ = Command::new("pkill")
            .args(["-KILL", "-f", "Steam.AppBundle/Steam/Contents/MacOS/ipcserver"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    Ok(json!({"ok": true}))
}

pub fn launch_macos_steam_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    launch_macos_steam_game_with_env(appid, &[])
}

pub fn launch_macos_steam_game_with_env(
    appid: u32,
    extra_env: &[(String, String)],
) -> Result<Value, Box<dyn std::error::Error>> {
    if !extra_env.is_empty() {
        for (key, val) in extra_env {
            let _ = Command::new("launchctl")
                .args(["setenv", key, val])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
        if is_macos_steam_running() {
            stop_macos_steam()?;
        }
    }

    if !is_macos_steam_running() {
        launch_macos_steam()?;
        for _ in 0..20 {
            std::thread::sleep(std::time::Duration::from_millis(500));
            if is_macos_steam_running() {
                break;
            }
        }
    }

    let url = format!("steam://run/{}", appid);
    let mut cmd = Command::new("open");
    cmd.arg(&url).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null());
    for (key, val) in extra_env {
        cmd.env(key, val);
    }
    let child = cmd.spawn()?;

    Ok(json!({"ok": true, "pid": latest_macos_steam_pid().max(child.id()), "appid": appid}))
}

pub fn stop_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let targets = ["Steam.exe", "steam.exe", "steamservice.exe", "steamwebhelper.exe", "winedevice.exe"];

    for target in &targets {
        let _ = Command::new("pkill")
            .args(["-9", "-f", target])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    std::thread::sleep(std::time::Duration::from_secs(2));

    let _ = Command::new("pkill")
        .args(["-9", "-f", "wineserver"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-f", "wineloader"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    std::thread::sleep(std::time::Duration::from_secs(2));

    let still_running =
        Command::new("pgrep").args(["-f", "Steam.exe"]).output().map(|o| o.status.success()).unwrap_or(false);

    if still_running {
        for target in &targets {
            let _ = Command::new("killall")
                .args(["-9", target])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
        std::thread::sleep(std::time::Duration::from_secs(2));
    }

    Ok(json!({"ok": true}))
}

pub fn install_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    if !is_wine_steam_running() {
        launch_wine_steam()?;
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    Ok(json!({"ok": true, "appid": appid, "method": "steam_ui"}))
}

pub fn launch_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    if !is_wine_steam_running() {
        launch_wine_steam()?;
        for _ in 0..30 {
            std::thread::sleep(std::time::Duration::from_secs(2));
            if is_wine_steam_running() {
                break;
            }
        }
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    let url = format!("steam://run/{}", appid);
    let pid = spawn_wine_steam(&[&url])?;

    Ok(json!({"ok": true, "pid": pid, "appid": appid}))
}

pub fn view_game_in_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    if !is_wine_steam_running() {
        return Err("Steam is not running".into());
    }

    let url = format!("steam://nav/games/details/{}", appid);
    let _ = spawn_wine_steam(&[&url])?;

    Ok(json!({"ok": true, "appid": appid}))
}

pub fn get_wine_steam_installed_games() -> Vec<u32> {
    let mut appids = Vec::new();

    for steamapps in crate::scan::wine_steam_library_paths() {
        if is_macos_steamapps_path(&steamapps) {
            continue;
        }
        if let Ok(entries) = std::fs::read_dir(&steamapps) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_string();
                if name.starts_with("appmanifest_") && name.ends_with(".acf") {
                    if let Some(id_str) = name.strip_prefix("appmanifest_").and_then(|s| s.strip_suffix(".acf")) {
                        if let Ok(id) = id_str.parse::<u32>() {
                            if !appids.contains(&id) {
                                appids.push(id);
                            }
                        }
                    }
                }
            }
        }
    }

    appids
}

pub fn deploy_steamwebhelper_wrapper(steam_dir: &PathBuf) {
    let cef_dir = steam_dir.join("bin").join("cef").join("cef.win64");
    let original = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");
    let wrapper_marker = cef_dir.join(".ms_wrapper_deployed");

    let wrapper = match find_bundled_steamwebhelper_wrapper() {
        Some(wrapper) => wrapper,
        None => return,
    };

    let original_size = std::fs::metadata(&original).map(|m| m.len()).unwrap_or(0);
    let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);
    let bundled_size = std::fs::metadata(&wrapper).map(|m| m.len()).unwrap_or(0);

    if bundled_size == 0 || bundled_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES {
        return;
    }

    if original_size > 0 && original_size <= STEAMWEBHELPER_WRAPPER_MAX_BYTES {
        if !wrapper_marker.exists() {
            let _ = std::fs::write(&wrapper_marker, "deployed");
        }
        return;
    }

    if real_size < STEAMWEBHELPER_WRAPPER_MAX_BYTES {
        if original_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES {
            let _ = std::fs::remove_file(&real);
            let _ = std::fs::rename(&original, &real);
        } else {
            return;
        }
    } else if original.exists() {
        let _ = std::fs::remove_file(&original);
    }

    if std::fs::copy(&wrapper, &original).is_ok() {
        let _ = std::fs::write(&wrapper_marker, "deployed");
    }
}

fn find_bundled_steamwebhelper_wrapper() -> Option<PathBuf> {
    if let Ok(exe) = std::env::current_exe() {
        let resources = exe.parent()?.parent()?.join("Resources");
        let wrapper = resources.join("bundles").join("steamwebhelper.exe");
        if wrapper.exists() {
            return Some(wrapper);
        }
    }

    let dev = PathBuf::from("app/bundles/steamwebhelper.exe");
    if dev.exists() {
        return Some(dev);
    }

    let home = dirs::home_dir()?;
    let cache_dir = home.join(".metalsharp").join("cache").join("bundles");
    let _ = std::fs::create_dir_all(&cache_dir);
    let cached = cache_dir.join("steamwebhelper.exe");

    if cached.exists() {
        let size = std::fs::metadata(&cached).map(|m| m.len()).unwrap_or(0);
        if size > 0 && size <= STEAMWEBHELPER_WRAPPER_MAX_BYTES {
            return Some(cached);
        }
    }

    let url = "https://github.com/aaf2tbz/metalsharp/releases/download/bundles/steamwebhelper.exe";
    let output = Command::new("curl").args(["-sL", "-o"]).arg(&cached).arg(url).output().ok()?;

    if output.status.success() && cached.exists() {
        let size = std::fs::metadata(&cached).map(|m| m.len()).unwrap_or(0);
        if size > 0 && size <= STEAMWEBHELPER_WRAPPER_MAX_BYTES {
            return Some(cached);
        }
    }

    let _ = std::fs::remove_file(&cached);
    None
}

fn get_game_name_from_manifest(appid: u32) -> Option<String> {
    let manifest_name = format!("appmanifest_{}.acf", appid);

    for steamapps in crate::scan::wine_steam_library_paths() {
        let manifest_path = steamapps.join(&manifest_name);
        if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
            if let Some(name) = parse_acf_field(&contents, "name") {
                return Some(name);
            }
        }
    }

    None
}

fn parse_acf_field(contents: &str, key: &str) -> Option<String> {
    for line in contents.lines() {
        let mut quoted = line.trim().split('"');
        let _ = quoted.next();
        let Some(parsed_key) = quoted.next() else {
            continue;
        };
        if parsed_key == key {
            let _ = quoted.next();
            if let Some(value) = quoted.next().map(str::trim).filter(|value| !value.is_empty()) {
                return Some(value.to_string());
            }
        }
    }
    None
}

fn is_single_path_component(name: &str) -> bool {
    let mut components = Path::new(name).components();
    matches!(components.next(), Some(Component::Normal(_))) && components.next().is_none()
}

fn canonical_path(path: &Path) -> Option<PathBuf> {
    std::fs::canonicalize(path).ok()
}

fn path_is_under(path: &Path, root: &Path) -> bool {
    match (canonical_path(path), canonical_path(root)) {
        (Some(path), Some(root)) => path != root && path.starts_with(root),
        _ => false,
    }
}

fn is_macos_steamapps_path(path: &Path) -> bool {
    crate::scan::macos_steam_library_paths().iter().any(|macos_path| {
        match (canonical_path(path), canonical_path(macos_path)) {
            (Some(path), Some(macos_path)) => path == macos_path,
            _ => false,
        }
    })
}

fn remove_dir_all_under(target: &Path, root: &Path) -> Result<bool, Box<dyn std::error::Error>> {
    if !target.exists() {
        return Ok(false);
    }
    if !path_is_under(target, root) {
        return Err(format!("Refusing to remove path outside uninstall root: {}", target.display()).into());
    }
    std::fs::remove_dir_all(target)?;
    Ok(true)
}

fn remove_file_under(target: &Path, root: &Path) -> Result<bool, Box<dyn std::error::Error>> {
    if !target.exists() {
        return Ok(false);
    }
    if !path_is_under(target, root) {
        return Err(format!("Refusing to remove file outside uninstall root: {}", target.display()).into());
    }
    std::fs::remove_file(target)?;
    Ok(true)
}

pub fn uninstall_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let manifest_name = format!("appmanifest_{}.acf", appid);
    let mut removed_wine = false;
    let mut removed_local = false;

    let _ = crate::launch::kill_game_with_pid(appid, 0);

    for steamapps in crate::scan::wine_steam_library_paths() {
        if is_macos_steamapps_path(&steamapps) {
            eprintln!("Refusing to uninstall appid {} from macOS Steam library {}", appid, steamapps.display());
            continue;
        }

        let manifest_path = steamapps.join(&manifest_name);
        if manifest_path.exists() {
            let contents = std::fs::read_to_string(&manifest_path).unwrap_or_default();
            let install_dir = parse_acf_field(&contents, "installdir");

            if let Some(dir_name) = install_dir {
                if !is_single_path_component(&dir_name) {
                    return Err(format!("Refusing unsafe Steam install dir for appid {}: {}", appid, dir_name).into());
                }
                let game_dir = steamapps.join("common").join(&dir_name);
                if remove_dir_all_under(&game_dir, &steamapps.join("common"))? {
                    removed_wine = true;
                }
            }
            if remove_file_under(&manifest_path, &steamapps)? {
                removed_wine = true;
            }
            break;
        }
    }

    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    if remove_dir_all_under(&local_dir, &home.join(".metalsharp").join("games"))? {
        removed_local = true;
    }

    if !removed_wine && !removed_local && macos_manifest_exists(appid) {
        return Err("This game is installed in macOS Steam. Uninstall it from macOS Steam, or install the Windows copy before using MetalSharp uninstall.".into());
    }

    if !removed_wine && !removed_local {
        return Err("No Windows Steam or MetalSharp local install was found to uninstall.".into());
    }

    Ok(json!({"ok": true, "appid": appid, "wine_removed": removed_wine, "local_removed": removed_local}))
}

fn macos_manifest_exists(appid: u32) -> bool {
    let manifest_name = format!("appmanifest_{}.acf", appid);
    crate::scan::macos_steam_library_paths().iter().any(|steamapps| steamapps.join(&manifest_name).exists())
}

pub fn get_api_key() -> Value {
    let (key, _) = read_steam_config();
    json!({
        "ok": true,
        "key": key.unwrap_or_default(),
    })
}

pub fn save_api_key(key: &str) -> Result<(), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let config_dir = home.join(".metalsharp/cache");
    std::fs::create_dir_all(&config_dir)?;
    let config_path = config_dir.join("steam_config.json");

    let steam_id = get_steam_id().unwrap_or_default();

    let config = json!({
        "steam_api_key": key,
        "steam_id": steam_id,
    });

    std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;

    let cache_path = config_dir.join("owned_games.json");
    let _ = std::fs::remove_file(cache_path);

    Ok(())
}

pub fn get_steam_id() -> Option<String> {
    let home = dirs::home_dir()?;

    let paths = vec![
        home.join("Library/Application Support/Steam/config/loginusers.vdf"),
        home.join(".metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/config/loginusers.vdf"),
    ];

    for mac_path in &paths {
        if let Ok(contents) = std::fs::read_to_string(mac_path) {
            for line in contents.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with('"') && trimmed.chars().filter(|c| *c == '"').count() == 2 {
                    let id = trimmed.trim_matches('"').trim();
                    if id.starts_with("7656") {
                        return Some(id.to_string());
                    }
                }
            }
        }
    }

    None
}

pub fn library() -> Value {
    let installed_appids = get_installed_appids();
    let downloaded_appids = get_downloaded_appids();
    let wine_steam_appids = get_wine_steam_installed_games();

    let owned: Vec<(u32, String)> = match fetch_owned_games(get_steam_id().as_deref()) {
        Ok(games) if !games.is_empty() => games,
        _ => {
            let mut fallback: Vec<(u32, String)> = Vec::new();
            for &appid in &wine_steam_appids {
                let name = get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
                fallback.push((appid, name));
            }
            for &appid in &downloaded_appids {
                if !fallback.iter().any(|(id, _)| *id == appid) {
                    fallback.push((appid, format!("Game {}", appid)));
                }
            }
            fallback
        },
    };

    let owned_appids: Vec<u32> = owned.iter().map(|(id, _)| *id).collect();
    let mut all_games = owned;
    for &appid in &wine_steam_appids {
        if !owned_appids.contains(&appid) {
            let name = get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
            all_games.push((appid, name));
        }
    }
    for &appid in &downloaded_appids {
        if !all_games.iter().any(|(id, _)| *id == appid) {
            all_games.push((appid, format!("Game {}", appid)));
        }
    }

    let games: Vec<Value> = all_games
        .iter()
        .map(|(appid, name)| {
            let is_installed = installed_appids.contains(appid)
                || downloaded_appids.contains(appid)
                || wine_steam_appids.contains(appid);
            let can_uninstall = downloaded_appids.contains(appid) || wine_steam_appids.contains(appid);
            let dual = crate::scan::resolve_dual_game_dir(*appid);
            let pipeline_id = crate::mtsp::rules::resolve_pipeline(*appid);
            let recommended = pipeline_id.to_legacy_method();
            let node = crate::mtsp::engine::get_pipeline(pipeline_id);
            let available_pipelines: Vec<serde_json::Value> = std::iter::once(serde_json::json!({
                "id": node.id,
                "name": node.name,
                "recommended": true,
            }))
            .chain(node.alternatives.iter().map(|alt| {
                let alt_node = crate::mtsp::engine::get_pipeline(*alt);
                serde_json::json!({
                    "id": alt_node.id,
                    "name": alt_node.name,
                    "recommended": false,
                })
            }))
            .collect();
            json!({
                "appid": appid,
                "name": name,
                "installed": is_installed,
                "state": if is_installed { "installed" } else { "not_installed" },
                "can_uninstall": can_uninstall,
                "launch_method": recommended,
                "available_pipelines": available_pipelines,
                "has_native_build": dual.has_native_build,
                "native_app_path": dual.macos_app.map(|p| p.to_string_lossy().to_string()),
                "wine_game_path": dual.wine_dir.map(|p| p.to_string_lossy().to_string()),
                "cover_url": format!("https://steamcdn-a.akamaihd.net/steam/apps/{}/library_600x900.jpg", appid),
                "header_url": format!("https://steamcdn-a.akamaihd.net/steam/apps/{}/header.jpg", appid),
            })
        })
        .collect();

    json!({
        "ok": true,
        "total": games.len(),
        "installed_count": games.iter().filter(|g| g["installed"].as_bool().unwrap_or(false)).count(),
        "games": games,
    })
}

fn get_downloaded_appids() -> Vec<u32> {
    let home = dirs::home_dir().unwrap_or_default();
    let games_dir = home.join(".metalsharp").join("games");
    let mut appids = Vec::new();

    if let Ok(entries) = std::fs::read_dir(&games_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if !path.is_dir() {
                continue;
            }
            if let Some(name) = path.file_name() {
                if let Ok(id) = name.to_string_lossy().parse::<u32>() {
                    let has_exe = walkdir::WalkDir::new(&path)
                        .max_depth(3)
                        .into_iter()
                        .flatten()
                        .any(|e| e.path().extension().map(|ext| ext == "exe").unwrap_or(false));
                    if has_exe {
                        appids.push(id);
                    }
                }
            }
        }
    }

    appids
}

fn read_steam_config() -> (Option<String>, Option<String>) {
    let home = dirs::home_dir().unwrap_or_default();
    let config_path = home.join(".metalsharp/cache/steam_config.json");
    if let Ok(contents) = std::fs::read_to_string(&config_path) {
        if let Ok(cfg) = serde_json::from_str::<serde_json::Map<String, Value>>(&contents) {
            let key = cfg.get("steam_api_key").and_then(|v| v.as_str()).map(String::from);
            let sid = cfg.get("steam_id").and_then(|v| v.as_str()).map(String::from);
            return (key, sid);
        }
    }
    (None, get_steam_id())
}

fn fetch_owned_games(_steam_id: Option<&str>) -> Result<Vec<(u32, String)>, Box<dyn std::error::Error>> {
    let (api_key, steam_id) = read_steam_config();
    let key = api_key.as_deref().unwrap_or("");
    let sid = steam_id.as_deref().or(_steam_id).unwrap_or("");

    if key.is_empty() || sid.is_empty() {
        return Ok(vec![]);
    }

    let cache_path = dirs::home_dir().map(|h| h.join(".metalsharp/cache/owned_games.json")).unwrap_or_default();

    if cache_path.exists() {
        if let Ok(contents) = std::fs::read_to_string(&cache_path) {
            if let Ok(cached) = serde_json::from_str::<serde_json::Map<String, Value>>(&contents) {
                if let Some(ts) = cached.get("timestamp").and_then(|t| t.as_u64()) {
                    let age = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_secs();
                    if age - ts < 3600 {
                        if let Some(arr) = cached.get("games").and_then(|g| g.as_array()) {
                            return Ok(arr
                                .iter()
                                .filter_map(|g| {
                                    let id = g.get("appid")?.as_u64()? as u32;
                                    let name = g.get("name")?.as_str()?.to_string();
                                    Some((id, name))
                                })
                                .collect());
                        }
                    }
                }
            }
        }
    }

    let url = format!(
        "https://api.steampowered.com/IPlayerService/GetOwnedGames/v0001/?key={}&steamid={}&include_appinfo=1&include_played_free_games=1&format=json",
        key, sid
    );

    let output = Command::new("curl").args(["-sL", "-m", "15", &url]).output()?;

    if !output.status.success() {
        return Err("curl failed".into());
    }

    let body: Value = serde_json::from_slice(&output.stdout)?;

    let games_arr = body.get("response").and_then(|r| r.get("games")).and_then(|g| g.as_array());

    let result: Vec<(u32, String)> = match games_arr {
        Some(arr) => arr
            .iter()
            .filter_map(|g| {
                let id = g.get("appid")?.as_u64()? as u32;
                let name = g.get("name")?.as_str()?.to_string();
                Some((id, name))
            })
            .collect(),
        None => vec![],
    };

    let _ = save_cache(&cache_path, &result);

    Ok(result)
}

fn save_cache(path: &PathBuf, games: &[(u32, String)]) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let arr: Vec<Value> = games.iter().map(|(id, name)| json!({"appid": id, "name": name})).collect();
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs();
    std::fs::write(path, serde_json::to_string_pretty(&json!({"timestamp": now, "games": arr}))?)?;
    Ok(())
}

fn get_installed_appids() -> Vec<u32> {
    let home = dirs::home_dir().unwrap_or_default();
    let mut appids = Vec::new();

    let mac_dirs = vec![
        home.join("Library/Application Support/Steam/steamapps"),
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
    ];

    let mut all_dirs: Vec<PathBuf> = mac_dirs.into_iter().filter(|d| d.exists()).collect();
    all_dirs.extend(crate::scan::wine_steam_library_paths());

    for dir in all_dirs {
        if let Ok(entries) = std::fs::read_dir(&dir) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_string();
                if name.starts_with("appmanifest_") && name.ends_with(".acf") {
                    if let Some(id_str) = name.strip_prefix("appmanifest_").and_then(|s| s.strip_suffix(".acf")) {
                        if let Ok(id) = id_str.parse::<u32>() {
                            if !appids.contains(&id) {
                                appids.push(id);
                            }
                        }
                    }
                }
            }
        }
    }

    appids
}

fn detect_login_state() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    let mac_path = home.join("Library/Application Support/Steam/config/loginusers.vdf");
    let wine_path = home.join(".metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/config/loginusers.vdf");

    let contents =
        std::fs::read_to_string(&mac_path).or_else(|_| std::fs::read_to_string(&wine_path)).unwrap_or_default();

    if contents.is_empty() {
        return json!({"state": "unknown", "account": null});
    }

    let mut accounts: Vec<Value> = Vec::new();

    for line in contents.lines() {
        let trimmed = line.trim();
        if let Some(name) = parse_vdf_value(trimmed, "PersonaName") {
            let remembered = contents.lines().any(|l| l.contains("RememberPassword") && l.contains("1"));
            accounts.push(json!({
                "name": name,
                "remembered": remembered,
            }));
        }
    }

    if accounts.is_empty() {
        json!({"state": "logged_out", "account": null})
    } else {
        json!({"state": "logged_in", "account": accounts})
    }
}

fn parse_vdf_value(line: &str, key: &str) -> Option<String> {
    let prefix = format!("\"{}\"", key);
    if !line.starts_with(&prefix) {
        return None;
    }
    let rest = line.trim_start_matches(&prefix).trim();
    let rest = rest.trim_start_matches('\t').trim_start_matches(' ');
    Some(rest.trim_matches('"').to_string())
}

pub fn install_steam() -> Result<String, Box<dyn std::error::Error>> {
    let steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");

    if steam_dir.join("steamui.dll").exists() && steam_exe_path().exists() {
        return Ok("Steam already installed".into());
    }

    if STEAM_INSTALLING.load(Ordering::SeqCst) {
        return Ok("Steam installation already in progress".into());
    }

    if STEAM_INSTALLING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok("Steam installation already in progress".into());
    }

    std::thread::spawn(move || {
        let _ = run_install_steam();
        STEAM_INSTALLING.store(false, Ordering::SeqCst);
    });

    Ok("Steam installation started — polling /steam/status for completion".into())
}

fn run_install_steam() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let metalsharp_dir = home.join(".metalsharp");
    std::fs::create_dir_all(&metalsharp_dir)?;

    let steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");

    let _ = std::fs::remove_dir_all(steam_prefix());

    let installer = metalsharp_dir.join("SteamSetup.exe");
    let _ = std::fs::remove_file(&installer);

    let url = "https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe";
    let output = Command::new("curl").args(["-sL", "-o", &installer.to_string_lossy(), url]).status()?;
    if !output.success() {
        let mut found = false;
        if let Ok(exe) = std::env::current_exe() {
            if let Some(resources) = exe.parent().and_then(|p| p.parent()) {
                let bundled = resources.join("bundles").join("SteamSetup.exe");
                if bundled.exists() {
                    let _ = std::fs::copy(&bundled, &installer);
                    found = true;
                }
            }
        }
        if !found {
            let bundled = PathBuf::from("app/bundles/SteamSetup.exe");
            if bundled.exists() {
                let _ = std::fs::copy(&bundled, &installer);
            }
        }
    }
    if !installer.exists() {
        return Err("Failed to download Steam installer".into());
    }

    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = steam_prefix();
    std::fs::create_dir_all(&prefix)?;

    let prefix_str = prefix.to_string_lossy().to_string();

    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    let dyld = format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    );

    let wineboot_result = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    if wineboot_result.is_err() {
        return Err("wineboot --init failed — MetalSharp Wine may not be properly installed".into());
    }

    let windows_dir = prefix.join("drive_c").join("windows").join("system32");
    let mut wineboot_ok = false;
    for _ in 0..30 {
        if windows_dir.exists() {
            wineboot_ok = true;
            break;
        }
        std::thread::sleep(std::time::Duration::from_secs(2));
    }
    if !wineboot_ok {
        return Err("wineboot --init timed out — Wine prefix was not created within 60 seconds".into());
    }

    let _child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg(&installer)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    let steam_exe = steam_dir.join("Steam.exe");
    let steam_ui_dll = steam_dir.join("steamui.dll");
    for _ in 0..70 {
        if steam_exe.exists() && steam_ui_dll.exists() {
            break;
        }
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    if steam_exe.exists() && steam_ui_dll.exists() {
        deploy_steamwebhelper_wrapper(&steam_dir);
    }

    Ok("Steam install thread complete".into())
}

pub fn watch_steamapps() -> Option<String> {
    let steamapps = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");

    if !steamapps.exists() {
        return None;
    }

    let current = get_wine_steam_installed_games();
    let cached_path = dirs::home_dir()?.join(".metalsharp").join("cache").join("wine_steam_appids.cache");

    let cached: Vec<u32> = if cached_path.exists() {
        std::fs::read_to_string(&cached_path)
            .ok()
            .map(|s| s.lines().filter_map(|l| l.parse::<u32>().ok()).collect())
            .unwrap_or_default()
    } else {
        vec![]
    };

    let mut new_appids = Vec::new();
    for &id in &current {
        if !cached.contains(&id) {
            new_appids.push(id);
        }
    }

    let _ = std::fs::write(&cached_path, current.iter().map(|id| id.to_string()).collect::<Vec<_>>().join("\n"));

    if new_appids.is_empty() {
        None
    } else {
        Some(serde_json::to_string(&new_appids).unwrap_or_default())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_acf_quoted_fields() {
        let acf = r#"
"AppState"
{
    "appid"        "123"
    "name"         "Example Game"
    "installdir"   "Example Game"
}
"#;

        assert_eq!(parse_acf_field(acf, "name"), Some("Example Game".to_string()));
        assert_eq!(parse_acf_field(acf, "installdir"), Some("Example Game".to_string()));
    }

    #[test]
    fn rejects_nested_steam_install_dirs() {
        assert!(is_single_path_component("Example Game"));
        assert!(!is_single_path_component("../Steam"));
        assert!(!is_single_path_component("nested/game"));
        assert!(!is_single_path_component("/Applications/Steam.app"));
    }
}
