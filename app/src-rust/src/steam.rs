use serde_json::{json, Value};
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

static STEAM_INSTALLING: AtomicBool = AtomicBool::new(false);
static GPTK_STEAM_INSTALLING: AtomicBool = AtomicBool::new(false);
const STEAMWEBHELPER_WRAPPER_MAX_BYTES: u64 = 100_000;
const GPTK_TOOLKIT_URL: &str = "https://developer.apple.com/games/game-porting-toolkit/";

fn ms_wine() -> PathBuf {
    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    crate::platform::runtime_wine_binary(&ms_root)
}

fn ms_wine_root() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine")
}

fn steam_prefix() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-steam")
}

pub fn gptk_steam_prefix() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-gptk-steam")
}

fn steam_exe_path() -> PathBuf {
    steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe")
}

fn gptk_steam_exe_path() -> PathBuf {
    gptk_steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe")
}

fn gptk_root() -> PathBuf {
    PathBuf::from("/Applications/Game Porting Toolkit.app/Contents/Resources/wine")
}

fn gptk_app_wine_path() -> PathBuf {
    gptk_root().join("bin").join("wine64")
}

fn gptk_app_wineserver_path() -> PathBuf {
    gptk_root().join("bin").join("wineserver")
}

fn gptk_local_root() -> PathBuf {
    ms_wine_root().join("lib").join("gptk")
}

fn gptk_wine_path() -> PathBuf {
    if gptk_app_wine_path().exists() {
        gptk_app_wine_path()
    } else {
        ms_wine()
    }
}

fn gptk_wineserver_path() -> PathBuf {
    if gptk_app_wineserver_path().exists() {
        gptk_app_wineserver_path()
    } else {
        ms_wine_root().join("bin").join("wineserver")
    }
}

fn gptk_installed() -> bool {
    (gptk_app_wine_path().exists() && gptk_app_wineserver_path().exists()) || gptk_local_redist_installed()
}

fn gptk_local_redist_installed() -> bool {
    let root = gptk_local_root();
    ms_wine().exists()
        && ms_wine_root().join("bin").join("wineserver").exists()
        && root.join("x86_64-windows").join("d3d12.dll").exists()
        && root.join("x86_64-windows").join("dxgi.dll").exists()
        && root.join("external").join("D3DMetal.framework").exists()
}

fn gptk_runtime_windows_dir(arch: &str) -> PathBuf {
    if gptk_app_wine_path().exists() {
        gptk_root().join("lib").join("wine").join(arch)
    } else {
        gptk_local_root().join(arch)
    }
}

fn gptk_runtime_unix_dir() -> PathBuf {
    if gptk_app_wine_path().exists() {
        gptk_root().join("lib").join("wine").join("x86_64-unix")
    } else {
        gptk_local_root().join("x86_64-unix")
    }
}

fn gptk_runtime_external_dir() -> PathBuf {
    if gptk_app_wine_path().exists() {
        gptk_root().join("lib").join("external")
    } else {
        gptk_local_root().join("external")
    }
}

fn gptk_runtime_status_path() -> PathBuf {
    if gptk_app_wine_path().exists() {
        gptk_root()
    } else {
        gptk_local_root()
    }
}

fn gptk_winedata_dir() -> PathBuf {
    if gptk_app_wine_path().exists() {
        gptk_root().join("share")
    } else {
        ms_wine_root().join("share")
    }
}

fn gptk_steam_install_progress_path() -> PathBuf {
    metalsharp_home().join("gptk_steam_install_progress.json")
}

fn write_gptk_steam_install_progress(phase: &str, message: &str, error: Option<&str>) {
    let progress = json!({
        "phase": phase,
        "message": message,
        "error": error,
        "installing": is_installing_gptk_steam(),
        "toolkit_installed": gptk_installed(),
        "steam_installed": gptk_steam_installed(),
    });
    let path = gptk_steam_install_progress_path();
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let _ = std::fs::write(path, serde_json::to_string_pretty(&progress).unwrap_or_default());
}

fn read_gptk_steam_install_progress() -> Value {
    let path = gptk_steam_install_progress_path();
    if path.exists() {
        if let Ok(contents) = std::fs::read_to_string(path) {
            if let Ok(value) = serde_json::from_str::<Value>(&contents) {
                return value;
            }
        }
    }

    json!({
        "phase": if gptk_steam_installed() { "ready" } else { "idle" },
        "message": if gptk_steam_installed() {
            "GPTK Steam is installed"
        } else {
            "GPTK Steam is not installed"
        },
        "error": null,
        "installing": is_installing_gptk_steam(),
        "toolkit_installed": gptk_installed(),
        "steam_installed": gptk_steam_installed(),
    })
}

fn write_gptk_toolkit_progress(phase: &str, message: &str, error: Option<&str>) {
    let progress = json!({
        "phase": phase,
        "message": message,
        "error": error,
        "installing": is_installing_gptk_steam(),
        "toolkit_installed": gptk_installed(),
        "steam_installed": gptk_steam_installed(),
    });
    let path = gptk_steam_install_progress_path();
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let _ = std::fs::write(path, serde_json::to_string_pretty(&progress).unwrap_or_default());
}

fn gptk_steam_installed() -> bool {
    let steam_dir = steam_dir_for_prefix(&gptk_steam_prefix());
    gptk_steam_exe_path().exists()
        && steam_dir.join("steamui.dll").exists()
        && !prefix_contains_crossover_identity(&gptk_steam_prefix())
}

fn metalsharp_home() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp")
}

fn current_timestamp_secs() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|duration| duration.as_secs()).unwrap_or(0)
}

fn current_account_name() -> String {
    std::env::var("USER")
        .or_else(|_| std::env::var("LOGNAME"))
        .ok()
        .filter(|name| !name.trim().is_empty())
        .unwrap_or_else(|| "metalsharp".to_string())
}

fn steam_dir_for_prefix(prefix: &Path) -> PathBuf {
    prefix.join("drive_c").join("Program Files (x86)").join("Steam")
}

fn windows_user_dir(prefix: &Path) -> PathBuf {
    prefix.join("drive_c").join("users").join(current_account_name())
}

fn gptk_prefix_identity() -> Value {
    let prefix = gptk_steam_prefix();
    let username = current_account_name();
    let has_steam = gptk_steam_exe_path().exists();
    let crossover_detected = prefix_contains_crossover_identity(&prefix);
    let profile_ok = prefix_profile_matches_user(&prefix, &username) && !crossover_detected;

    json!({
        "username": username,
        "profile_ok": profile_ok,
        "crossover_detected": crossover_detected,
        "has_steam": has_steam
    })
}

fn prefix_contains_crossover_identity(prefix: &Path) -> bool {
    ["user.reg", "system.reg"]
        .iter()
        .map(|name| prefix.join(name))
        .filter_map(|path| std::fs::read_to_string(path).ok())
        .any(|contents| {
            let lower = contents.to_lowercase();
            lower.contains("c:\\\\users\\\\crossover")
                || lower.contains(r#"username"="crossover""#)
                || lower.contains(r#"profileimagepath"="c:\\users\\crossover""#)
        })
}

fn prefix_profile_matches_user(prefix: &Path, username: &str) -> bool {
    let user_reg = prefix.join("user.reg");
    let system_reg = prefix.join("system.reg");
    if !user_reg.exists() || !system_reg.exists() {
        return false;
    }

    let user = std::fs::read_to_string(user_reg).unwrap_or_default().to_lowercase();
    let system = std::fs::read_to_string(system_reg).unwrap_or_default().to_lowercase();
    let username = username.to_lowercase();
    let profile = format!("c:\\\\users\\\\{}", username);

    user.contains(&format!(r#""username"="{}""#, username))
        || user.contains(&format!(r#""userprofile"="{}""#, profile))
        || system.contains(&format!(r#""profileimagepath"="{}""#, profile))
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
    let gptk_steam_dir = gptk_steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");
    let wine_steam_exe = wine_steam_dir.join("Steam.exe");
    let windows_installed = wine_steam_exe.exists();
    let windows_path = if windows_installed { Some(wine_steam_dir.to_string_lossy().to_string()) } else { None };
    let gptk_steam_installed = gptk_steam_installed();

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
    let gptk_running = is_gptk_steam_running();
    let gptk_identity = gptk_prefix_identity();
    let ms_available = ms_wine().exists();
    let installing = is_installing_steam();
    let gptk_installing = is_installing_gptk_steam();

    json!({
        "installed": windows_installed,
        "path": windows_path,
        "login_state": login_state,
        "mac_installed": mac_installed,
        "mac_path": mac_app.map(|p| p.to_string_lossy().to_string()),
        "mac_install_url": macos_steam_install_url(),
        "mac_running": mac_running,
        "gptk_installed": gptk_installed(),
        "gptk_toolkit_installed": gptk_installed(),
        "gptk_toolkit_url": GPTK_TOOLKIT_URL,
        "gptk_toolkit_downloaded": downloaded_gptk_dmgs().first().map(|p| p.to_string_lossy().to_string()),
        "gptk_runtime_path": gptk_runtime_status_path().to_string_lossy().to_string(),
        "gptk_steam_installed": gptk_steam_installed,
        "gptk_path": gptk_steam_dir.to_string_lossy().to_string(),
        "gptk_prefix": gptk_steam_prefix().to_string_lossy().to_string(),
        "gptk_running": gptk_running,
        "gptk_synced": gptk_steam_installed,
        "gptk_installing": gptk_installing,
        "gptk_install_progress": read_gptk_steam_install_progress(),
        "gptk_profile": gptk_identity,
        "running": running,
        "metalsharp_wine_available": ms_available,
        "installing": installing
    })
}

pub fn is_wine_steam_running() -> bool {
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .any(|(_, command)| is_wine_steam_owner_command(command))
}

pub fn is_gptk_steam_running() -> bool {
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .any(|(_, command)| is_gptk_steam_owner_command(command))
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
    parse_process_line(line).map(|(_, command)| is_wine_steam_owner_command(command)).unwrap_or(false)
}

fn is_wine_steam_owner_command(command: &str) -> bool {
    if command.contains(" rg ") || command.contains("rg -i") || command.contains("ps axo") {
        return false;
    }
    if command.contains("Steam.app/Contents/MacOS") || command.contains("steam_osx") {
        return false;
    }
    if is_gptk_runtime_command(command) {
        return false;
    }

    let prefix = steam_prefix().to_string_lossy().to_string();
    let exe = steam_exe_path().to_string_lossy().to_string();
    let lower = command.to_lowercase();

    command.contains(&exe)
        || (command.contains(&prefix) && (command.contains("Steam.exe") || command.contains("steam.exe")))
        || (lower.contains("c:\\program files (x86)\\steam")
            && (lower.contains("steam.exe")
                || lower.contains("steamservice.exe")
                || lower.contains("steamerrorreporter")))
}

fn is_wine_steam_cleanup_command(command: &str) -> bool {
    if is_wine_steam_owner_command(command) {
        return true;
    }
    if command.contains(" rg ") || command.contains("rg -i") || command.contains("ps axo") {
        return false;
    }
    if command.contains("Steam.app/Contents/MacOS") || command.contains("steam_osx") {
        return false;
    }
    if is_gptk_runtime_command(command) {
        return false;
    }

    let prefix = steam_prefix().to_string_lossy().to_string();
    let lower = command.to_lowercase();

    command.contains(&prefix)
        || lower.contains("c:\\program files (x86)\\steam")
        || lower.contains("steamwebhelper.exe")
        || lower.contains("steamwebhelper_real.exe")
        || lower.contains("winedevice.exe")
        || lower.contains("wineserver")
        || lower.contains("wineloader")
}

fn is_gptk_runtime_command(command: &str) -> bool {
    command.contains("/Applications/Game Porting Toolkit.app/Contents/Resources/wine/")
        || command.contains(&gptk_steam_prefix().to_string_lossy().to_string())
        || command.contains(&gptk_local_root().to_string_lossy().to_string())
}

fn is_gptk_steam_owner_command(command: &str) -> bool {
    if command.contains(" rg ") || command.contains("rg -i") || command.contains("ps axo") {
        return false;
    }
    let lower = command.to_lowercase();
    let prefix = gptk_steam_prefix().to_string_lossy().to_lowercase();
    let exe = gptk_steam_exe_path().to_string_lossy().to_lowercase();
    let steam_process =
        lower.contains("steam.exe") || lower.contains("steamservice.exe") || lower.contains("steamerrorreporter");
    let windows_steam_path = lower.contains("c:\\program files (x86)\\steam") && steam_process;
    let gptk_prefix_steam_path = lower.contains(&exe) || (lower.contains(&prefix) && steam_process);

    (is_gptk_runtime_command(command) && windows_steam_path) || gptk_prefix_steam_path
}

fn is_gptk_steam_cleanup_command(command: &str) -> bool {
    if command.contains(" rg ") || command.contains("rg -i") || command.contains("ps axo") {
        return false;
    }

    let lower = command.to_lowercase();
    let prefix = gptk_steam_prefix().to_string_lossy().to_lowercase();
    let steam_cleanup_process = lower.contains("steam.exe")
        || lower.contains("steamservice.exe")
        || lower.contains("steamerrorreporter")
        || lower.contains("steamwebhelper.exe")
        || lower.contains("steamwebhelper_real.exe")
        || lower.contains("winedevice.exe")
        || lower.contains("wineloader");

    is_gptk_steam_owner_command(command)
        || (lower.contains(&prefix) && steam_cleanup_process)
        || (is_gptk_runtime_command(command)
            && lower.contains("c:\\program files (x86)\\steam")
            && steam_cleanup_process)
}

fn gptk_steam_cleanup_pids() -> Vec<u32> {
    let this_pid = std::process::id();
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .filter_map(|(pid, command)| (pid != this_pid && is_gptk_steam_cleanup_command(command)).then_some(pid))
        .collect()
}

fn wine_steam_cleanup_pids() -> Vec<u32> {
    let this_pid = std::process::id();
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .filter_map(|(pid, command)| (pid != this_pid && is_wine_steam_cleanup_command(command)).then_some(pid))
        .collect()
}

pub fn is_macos_steam_running() -> bool {
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .any(|(_, command)| is_macos_steam_active_command(command))
}

fn latest_macos_steam_pid() -> u32 {
    macos_steam_process_pids().into_iter().max().unwrap_or(0)
}

fn parse_process_line(line: &str) -> Option<(u32, &str)> {
    let line = line.trim_start();
    let mut parts = line.splitn(2, char::is_whitespace);
    let pid = parts.next()?.parse::<u32>().ok()?;
    let command = parts.next().unwrap_or("").trim_start();
    Some((pid, command))
}

fn is_macos_steam_search_command(command: &str) -> bool {
    if command.contains(" rg ") || command.contains("rg -i") || command.contains("ps axo") {
        return true;
    }

    false
}

fn is_macos_steam_active_command(command: &str) -> bool {
    if is_macos_steam_search_command(command) {
        return false;
    }

    command.contains("/Steam.app/Contents/MacOS/steam_osx")
        || command.ends_with("/steam_osx")
        || command.contains("Steam Helper.app/Contents/MacOS")
}

fn is_macos_steam_cleanup_command(command: &str) -> bool {
    if is_macos_steam_search_command(command) {
        return false;
    }

    is_macos_steam_active_command(command) || command.contains("Steam.AppBundle/Steam/Contents/MacOS/ipcserver")
}

fn macos_steam_process_pids() -> Vec<u32> {
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .filter_map(|(pid, command)| is_macos_steam_cleanup_command(command).then_some(pid))
        .collect()
}

pub fn is_installing_steam() -> bool {
    STEAM_INSTALLING.load(Ordering::SeqCst)
}

pub fn is_installing_gptk_steam() -> bool {
    GPTK_STEAM_INSTALLING.load(Ordering::SeqCst)
}

fn ensure_steam_launch_ready(steam_dir: &PathBuf) {
    ensure_steam_launch_ready_with_wrapper(steam_dir, find_bundled_steamwebhelper_wrapper, ".ms_wrapper_deployed");
}

fn ensure_gptk_steam_launch_ready(steam_dir: &PathBuf) {
    ensure_steam_launch_ready_with_wrapper(
        steam_dir,
        find_bundled_gptk_steamwebhelper_wrapper,
        ".ms_gptk_wrapper_deployed",
    );
}

fn ensure_steam_launch_ready_with_wrapper(
    steam_dir: &PathBuf,
    wrapper_finder: fn() -> Option<PathBuf>,
    marker_name: &str,
) {
    let cef_dir = steam_dir.join("bin").join("cef").join("cef.win64");
    let wrapper = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");

    let wrapper_size = std::fs::metadata(&wrapper).map(|m| m.len()).unwrap_or(0);
    let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);

    let wrapper_missing = wrapper_size == 0;
    let wrapper_overwritten = wrapper_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES;
    let real_missing_or_bad = real_size > 0 && real_size < STEAMWEBHELPER_WRAPPER_MAX_BYTES;

    if wrapper_missing || wrapper_overwritten || real_missing_or_bad {
        deploy_steamwebhelper_wrapper_with(steam_dir, wrapper_finder, marker_name);
    }
}

fn spawn_wine_steam(args: &[&str]) -> Result<u32, Box<dyn std::error::Error>> {
    spawn_wine_steam_with_env(args, &[])
}

fn apply_wine_wrapper_env(cmd: &mut Command, ms_root: &Path) {
    let home = dirs::home_dir().unwrap_or_default();
    let lib = ms_root.join("lib");
    let unix_lib = lib.join("wine").join("x86_64-unix");
    cmd.env("MS_ROOT", ms_root)
        .env("CX_ROOT", ms_root)
        .env("WINEDATADIR", ms_root.join("share"))
        .env("DYLD_FALLBACK_LIBRARY_PATH", format!("{}:{}", lib.display(), unix_lib.display()))
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env("MVK_PRESENT_MODE", "1")
        .env("DXVK_STATE_CACHE_PATH", home.join(".metalsharp").join("dxvk-cache"))
        .env("DXVK_LOG_PATH", home.join(".metalsharp").join("dxvk-logs"));

    let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
    if moltenvk_icd.exists() {
        cmd.env("VK_ICD_FILENAMES", moltenvk_icd);
    }
}

fn apply_gptk_prefix_user_env(cmd: &mut Command) {
    let username = current_account_name();
    let user_profile = format!(r"C:\users\{}", username);
    let local_app_data = format!(r"{}\AppData\Local", user_profile);
    let app_data = format!(r"{}\AppData\Roaming", user_profile);
    let temp = format!(r"{}\Temp", local_app_data);

    cmd.env("USER", &username)
        .env("LOGNAME", &username)
        .env("USERNAME", &username)
        .env("USERPROFILE", &user_profile)
        .env("HOMEPATH", format!(r"\users\{}", username))
        .env("APPDATA", &app_data)
        .env("LOCALAPPDATA", &local_app_data)
        .env("TEMP", &temp)
        .env("TMP", &temp);
}

fn apply_gptk_env(cmd: &mut Command) {
    let home = dirs::home_dir().unwrap_or_default();
    let ms_root = ms_wine_root();
    let base_dyld =
        format!("{}:{}", ms_root.join("lib").display(), ms_root.join("lib").join("wine").join("x86_64-unix").display());
    let gptk_unix = gptk_runtime_unix_dir();
    let gptk_external = gptk_runtime_external_dir();
    cmd.env("WINESERVER", gptk_wineserver_path())
        .env("WINELOADER", gptk_wine_path())
        .env("WINEDATADIR", gptk_winedata_dir())
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env("MVK_PRESENT_MODE", "1")
        .env("DXVK_STATE_CACHE_PATH", home.join(".metalsharp").join("dxvk-cache-gptk"))
        .env("DXVK_LOG_PATH", home.join(".metalsharp").join("dxvk-logs-gptk"))
        .env(
            "WINEDLLPATH",
            format!(
                "{}:{}",
                gptk_runtime_windows_dir("x86_64-windows").display(),
                gptk_runtime_windows_dir("i386-windows").display()
            ),
        )
        .env(
            "DYLD_FALLBACK_LIBRARY_PATH",
            format!(
                "{}:{}:{}:{}",
                gptk_unix.display(),
                gptk_external.display(),
                gptk_local_root().display(),
                base_dyld
            ),
        );
    let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
    if moltenvk_icd.exists() {
        cmd.env("VK_ICD_FILENAMES", moltenvk_icd);
    }
    apply_gptk_prefix_user_env(cmd);
}

fn stop_gptk_prefix_wineserver(prefix: &Path) {
    if !gptk_wineserver_path().exists() {
        return;
    }
    let prefix_str = prefix.to_string_lossy().to_string();
    let _ = Command::new(gptk_wineserver_path())
        .arg("-k")
        .env("WINEPREFIX", &prefix_str)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
    std::thread::sleep(std::time::Duration::from_secs(2));
}

fn spawn_gptk_steam(args: &[&str]) -> Result<u32, Box<dyn std::error::Error>> {
    if !gptk_installed() {
        return Err("Game Porting Toolkit is not installed at /Applications/Game Porting Toolkit.app".into());
    }

    let exe = gptk_steam_exe_path();
    let steam_dir = steam_dir_for_prefix(&gptk_steam_prefix());

    if !exe.exists() || !steam_dir.join("steamui.dll").exists() {
        return Err("GPTK Steam is not installed — run the GPTK Steam setup first".into());
    }

    ensure_gptk_steam_launch_ready(&steam_dir);

    let prefix_str = gptk_steam_prefix().to_string_lossy().to_string();
    let mut cmd = Command::new("arch");
    cmd.arg("-x86_64")
        .arg(gptk_wine_path())
        .arg(&exe)
        .args(args)
        .current_dir(&steam_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("STEAM_RUNTIME", "0")
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env(
            "WINEDLLOVERRIDES",
            "dxgi,d3d11,d3d10core,d3d12=n,b;bcrypt=b;ncrypt=b;gameoverlayrenderer,gameoverlayrenderer64=d",
        )
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    apply_gptk_env(&mut cmd);

    let child = cmd.spawn()?;
    Ok(child.id())
}

pub fn sync_gptk_steam_prefix() -> Result<Value, Box<dyn std::error::Error>> {
    install_gptk_steam()
}

fn downloaded_gptk_dmgs() -> Vec<PathBuf> {
    let home = dirs::home_dir().unwrap_or_default();
    let mut candidates = Vec::new();
    for dir in [home.join("Downloads"), home.join("Desktop")] {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("").to_lowercase();
                if path.extension().and_then(|e| e.to_str()).map(|e| e.eq_ignore_ascii_case("dmg")).unwrap_or(false)
                    && (name.contains("game_porting_toolkit")
                        || name.contains("game porting toolkit")
                        || name.contains("gptk"))
                {
                    candidates.push(path);
                }
            }
        }
    }
    candidates.sort_by(|a, b| {
        let a_time = std::fs::metadata(a).and_then(|m| m.modified()).unwrap_or(std::time::UNIX_EPOCH);
        let b_time = std::fs::metadata(b).and_then(|m| m.modified()).unwrap_or(std::time::UNIX_EPOCH);
        b_time.cmp(&a_time)
    });
    candidates
}

fn unique_temp_dir(name: &str) -> PathBuf {
    std::env::temp_dir().join(format!("metalsharp-{}-{}-{}", name, std::process::id(), current_timestamp_secs()))
}

fn attach_dmg(dmg: &Path, mountpoint: &Path) -> Result<(), Box<dyn std::error::Error>> {
    std::fs::create_dir_all(mountpoint)?;
    let status = Command::new("hdiutil")
        .arg("attach")
        .arg(dmg)
        .arg("-readonly")
        .arg("-nobrowse")
        .arg("-mountpoint")
        .arg(mountpoint)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("Could not mount {}", dmg.display()).into())
    }
}

fn detach_dmg(mountpoint: &Path) {
    let _ = Command::new("hdiutil")
        .arg("detach")
        .arg(mountpoint)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
    let _ = std::fs::remove_dir_all(mountpoint);
}

fn find_child_by_name_contains(root: &Path, needle: &str, extension: Option<&str>) -> Option<PathBuf> {
    let entries = std::fs::read_dir(root).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("").to_lowercase();
        let extension_ok = extension
            .map(|ext| path.extension().and_then(|e| e.to_str()).map(|e| e.eq_ignore_ascii_case(ext)).unwrap_or(false))
            .unwrap_or(true);
        if name.contains(needle) && extension_ok {
            return Some(path);
        }
    }
    None
}

fn ditto_copy(src: &Path, dst: &Path) -> Result<(), Box<dyn std::error::Error>> {
    if !src.exists() {
        return Ok(());
    }
    if let Some(parent) = dst.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let status = Command::new("ditto").arg(src).arg(dst).status()?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("Could not copy {} to {}", src.display(), dst.display()).into())
    }
}

fn command_output_text(mut command: Command) -> Result<(bool, String), Box<dyn std::error::Error>> {
    let output = command.output()?;
    let mut text = String::from_utf8_lossy(&output.stdout).to_string();
    text.push_str(&String::from_utf8_lossy(&output.stderr));
    Ok((output.status.success(), text))
}

fn apple_signature_output_is_accepted(text: &str) -> bool {
    let accepted = text.lines().any(|line| {
        let trimmed = line.trim().to_ascii_lowercase();
        trimmed == "accepted" || trimmed.ends_with(": accepted")
    });
    let apple_source = text.lines().any(|line| {
        let trimmed = line.trim().to_ascii_lowercase();
        trimmed == "source=apple" || trimmed.starts_with("source=apple ")
    });
    accepted && apple_source
}

fn codesign_output_has_apple_authority(text: &str) -> bool {
    text.lines().any(|line| {
        let trimmed = line.trim();
        trimmed == "Authority=Software Signing"
            || trimmed == "Authority=Apple Code Signing Certification Authority"
            || trimmed == "TeamIdentifier=59GAB85EFG"
    })
}

fn verify_signed_disk_image(dmg: &Path, label: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut command = Command::new("spctl");
    command.arg("-a").arg("-t").arg("open").arg("--context").arg("context:primary-signature").arg("-v").arg(dmg);
    let (success, output) = command_output_text(command)?;
    if success && apple_signature_output_is_accepted(&output) {
        Ok(())
    } else {
        Err(format!("Refusing to install GPTK runtime: {} is not an accepted Apple-signed disk image", label).into())
    }
}

fn verify_gptk_redist_identity(redist: &Path) -> Result<(), Box<dyn std::error::Error>> {
    for relative in [
        Path::new("wine").join("x86_64-windows").join("d3d12.dll"),
        Path::new("wine").join("x86_64-windows").join("dxgi.dll"),
        Path::new("wine").join("x86_64-unix"),
        Path::new("external").join("D3DMetal.framework"),
    ] {
        let candidate = redist.join(relative);
        if !candidate.exists() {
            return Err(format!(
                "Refusing to install GPTK runtime: expected signed redist component is missing at {}",
                candidate.display()
            )
            .into());
        }
    }

    let framework = redist.join("external").join("D3DMetal.framework");
    let mut verify = Command::new("codesign");
    verify.arg("--verify").arg("--deep").arg("--strict").arg(&framework);
    let (verified, verify_output) = command_output_text(verify)?;
    if !verified {
        return Err(format!(
            "Refusing to install GPTK runtime: D3DMetal.framework failed code signature verification ({})",
            verify_output.trim()
        )
        .into());
    }

    let mut describe = Command::new("codesign");
    describe.arg("-dv").arg("--verbose=4").arg(&framework);
    let (described, describe_output) = command_output_text(describe)?;
    if !described || !codesign_output_has_apple_authority(&describe_output) {
        return Err(
            "Refusing to install GPTK runtime: D3DMetal.framework is not signed by an Apple-controlled identity".into(),
        );
    }

    Ok(())
}

fn install_gptk_redist_from_dmg(dmg: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let outer_mount = unique_temp_dir("gptk-outer");
    let inner_mount = unique_temp_dir("gptk-inner");
    verify_signed_disk_image(dmg, "outer Game Porting Toolkit DMG")?;
    attach_dmg(dmg, &outer_mount)?;
    let result = (|| -> Result<(), Box<dyn std::error::Error>> {
        let inner_dmg =
            find_child_by_name_contains(&outer_mount, "evaluation environment for windows games", Some("dmg")).ok_or(
                "Downloaded Game Porting Toolkit DMG did not contain the Windows games evaluation environment DMG",
            )?;
        verify_signed_disk_image(&inner_dmg, "inner Game Porting Toolkit DMG")?;
        attach_dmg(&inner_dmg, &inner_mount)?;
        let inner_result = (|| -> Result<(), Box<dyn std::error::Error>> {
            let redist = inner_mount.join("redist").join("lib");
            if !redist.exists() {
                return Err("Mounted Game Porting Toolkit image did not contain redist/lib".into());
            }
            verify_gptk_redist_identity(&redist)?;
            let target = gptk_local_root();
            if target.exists() {
                let backup =
                    metalsharp_home().join("backups").join(format!("gptk-redist-{}", current_timestamp_secs()));
                if let Some(parent) = backup.parent() {
                    std::fs::create_dir_all(parent)?;
                }
                std::fs::rename(&target, backup)?;
            }
            ditto_copy(&redist.join("wine").join("x86_64-windows"), &target.join("x86_64-windows"))?;
            ditto_copy(&redist.join("wine").join("x86_64-unix"), &target.join("x86_64-unix"))?;
            ditto_copy(&redist.join("wine").join("i386-windows"), &target.join("i386-windows"))?;
            ditto_copy(&redist.join("external"), &target.join("external"))?;
            Ok(())
        })();
        detach_dmg(&inner_mount);
        inner_result
    })();
    detach_dmg(&outer_mount);
    result
}

fn ensure_gptk_toolkit_installed_from_download() -> Result<Option<PathBuf>, Box<dyn std::error::Error>> {
    if gptk_installed() {
        return Ok(None);
    }
    let dmg = downloaded_gptk_dmgs().into_iter().next();
    if let Some(dmg) = dmg {
        write_gptk_toolkit_progress(
            "installing_toolkit",
            &format!("Installing Game Porting Toolkit runtime from {}...", dmg.display()),
            None,
        );
        install_gptk_redist_from_dmg(&dmg)?;
        if gptk_installed() {
            write_gptk_toolkit_progress("toolkit_ready", "Game Porting Toolkit runtime is installed.", None);
            return Ok(Some(dmg));
        }
        return Err("Game Porting Toolkit files were copied, but the runtime was not detected".into());
    }
    Ok(None)
}

pub fn open_gptk_toolkit_download() -> Result<Value, Box<dyn std::error::Error>> {
    if let Some(dmg) = ensure_gptk_toolkit_installed_from_download()? {
        return Ok(json!({
            "ok": true,
            "installed": true,
            "source": dmg.to_string_lossy().to_string(),
            "runtime_path": gptk_runtime_status_path().to_string_lossy().to_string(),
            "progress": read_gptk_steam_install_progress()
        }));
    }
    if gptk_installed() {
        return Ok(json!({
            "ok": true,
            "installed": true,
            "runtime_path": gptk_runtime_status_path().to_string_lossy().to_string(),
            "progress": read_gptk_steam_install_progress()
        }));
    }

    let child = Command::new("open")
        .arg(GPTK_TOOLKIT_URL)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({
        "ok": true,
        "installed": false,
        "download_required": true,
        "pid": child.id(),
        "url": GPTK_TOOLKIT_URL
    }))
}

pub fn install_gptk_steam() -> Result<Value, Box<dyn std::error::Error>> {
    if !gptk_installed() {
        let _ = ensure_gptk_toolkit_installed_from_download()?;
    }

    if !gptk_installed() {
        write_gptk_steam_install_progress(
            "toolkit_missing",
            "Game Porting Toolkit is not installed. Download it, then press Install GPTK Steam again.",
            Some("Game Porting Toolkit was not found in /Applications or Downloads"),
        );
        return Ok(json!({
            "ok": false,
            "error": "Game Porting Toolkit was not found in /Applications or Downloads",
            "toolkit_url": GPTK_TOOLKIT_URL,
            "progress": read_gptk_steam_install_progress()
        }));
    }

    if gptk_steam_installed() {
        ensure_gptk_steam_launch_ready(&steam_dir_for_prefix(&gptk_steam_prefix()));
        write_gptk_steam_install_progress("ready", "GPTK Steam is installed and ready to launch.", None);
        return Ok(json!({
            "ok": true,
            "installed": true,
            "prefix": gptk_steam_prefix().to_string_lossy().to_string(),
            "steam_path": steam_dir_for_prefix(&gptk_steam_prefix()).to_string_lossy().to_string(),
            "progress": read_gptk_steam_install_progress(),
            "profile": gptk_prefix_identity()
        }));
    }

    if GPTK_STEAM_INSTALLING.load(Ordering::SeqCst) {
        return Ok(json!({
            "ok": true,
            "installing": true,
            "message": "GPTK Steam installation already in progress",
            "progress": read_gptk_steam_install_progress()
        }));
    }

    if GPTK_STEAM_INSTALLING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok(json!({
            "ok": true,
            "installing": true,
            "message": "GPTK Steam installation already in progress",
            "progress": read_gptk_steam_install_progress()
        }));
    }

    write_gptk_steam_install_progress("starting", "Starting GPTK Steam setup...", None);
    std::thread::spawn(move || {
        let result = run_install_gptk_steam();
        GPTK_STEAM_INSTALLING.store(false, Ordering::SeqCst);
        match result {
            Ok(message) => write_gptk_steam_install_progress("ready", &message, None),
            Err(error) => write_gptk_steam_install_progress(
                "error",
                &format!("GPTK Steam setup failed: {}", error),
                Some(&error.to_string()),
            ),
        }
    });

    Ok(json!({
        "ok": true,
        "installing": true,
        "message": "GPTK Steam installation started",
        "prefix": gptk_steam_prefix().to_string_lossy().to_string(),
        "progress": read_gptk_steam_install_progress()
    }))
}

fn ensure_clean_gptk_prefix(prefix: &Path) -> Result<(), Box<dyn std::error::Error>> {
    if !gptk_installed() {
        return Err("Game Porting Toolkit is not installed at /Applications/Game Porting Toolkit.app".into());
    }

    std::fs::create_dir_all(prefix)?;

    let windows_dir = prefix.join("drive_c").join("windows").join("system32");
    if !windows_dir.exists() {
        let prefix_str = prefix.to_string_lossy().to_string();
        let mut cmd = Command::new("arch");
        cmd.arg("-x86_64")
            .arg(gptk_wine_path())
            .arg("wineboot")
            .arg("--init")
            .env("WINEPREFIX", &prefix_str)
            .env("WINEDEBUG", "-all")
            .env("WINEDLLOVERRIDES", "mscoree=d;mshtml=d")
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null());
        apply_gptk_env(&mut cmd);

        let status = cmd.status()?;
        if !status.success() {
            return Err("GPTK wineboot --init failed while creating the MetalSharp GPTK prefix".into());
        }

        let mut ready = false;
        for _ in 0..30 {
            if windows_dir.exists() {
                ready = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_secs(1));
        }
        if !ready {
            return Err("GPTK wineboot --init timed out before the prefix became usable".into());
        }
        stop_gptk_prefix_wineserver(prefix);
    }

    let user_dir = windows_user_dir(prefix);
    std::fs::create_dir_all(user_dir.join("AppData").join("Local").join("Temp"))?;
    std::fs::create_dir_all(user_dir.join("AppData").join("Local").join("Steam"))?;
    std::fs::create_dir_all(user_dir.join("AppData").join("Roaming"))?;

    repair_fresh_gptk_identity(prefix, &current_account_name())?;
    Ok(())
}

fn repair_fresh_gptk_identity(prefix: &Path, username: &str) -> Result<(), Box<dyn std::error::Error>> {
    let users_dir = prefix.join("drive_c").join("users");
    let crossover_dir = users_dir.join("crossover");
    let user_dir = users_dir.join(username);
    if crossover_dir.exists() {
        if !user_dir.exists() {
            std::fs::rename(&crossover_dir, &user_dir)?;
        } else {
            let backup = users_dir.join(format!("crossover.archived.{}", current_timestamp_secs()));
            std::fs::rename(&crossover_dir, backup)?;
        }
    }

    for name in ["user.reg", "system.reg"] {
        let path = prefix.join(name);
        if !path.exists() {
            continue;
        }
        let contents = std::fs::read_to_string(&path)?;
        let repaired = contents
            .replace(r"C:\\users\\crossover", &format!(r"C:\\users\\{}", username))
            .replace(r"C:\\Users\\crossover", &format!(r"C:\\Users\\{}", username))
            .replace(r"\\users\\crossover", &format!(r"\\users\\{}", username))
            .replace(r#""USERNAME"="crossover""#, &format!(r#""USERNAME"="{}""#, username))
            .replace(r#""USERPROFILE"="C:\\users\\crossover""#, &format!(r#""USERPROFILE"="C:\\users\\{}""#, username))
            .replace(
                r#""LOCALAPPDATA"="C:\\users\\crossover\\AppData\\Local""#,
                &format!(r#""LOCALAPPDATA"="C:\\users\\{}\\AppData\\Local""#, username),
            )
            .replace(
                r#""APPDATA"="C:\\users\\crossover\\AppData\\Roaming""#,
                &format!(r#""APPDATA"="C:\\users\\{}\\AppData\\Roaming""#, username),
            )
            .replace(
                r#""ProfileImagePath"="C:\\users\\crossover""#,
                &format!(r#""ProfileImagePath"="C:\\users\\{}""#, username),
            )
            .replace(r#""TEMP"="C:\\users\\crossover\\Temp""#, &format!(r#""TEMP"="C:\\users\\{}\\Temp""#, username))
            .replace(r#""TMP"="C:\\users\\crossover\\Temp""#, &format!(r#""TMP"="C:\\users\\{}\\Temp""#, username))
            .replace(r#""HOMEPATH"="\\users\\crossover""#, &format!(r#""HOMEPATH"="\\users\\{}""#, username));
        if repaired != contents {
            std::fs::write(path, repaired)?;
        }
    }
    Ok(())
}

fn archive_contaminated_gptk_prefix(prefix: &Path) -> Result<Option<PathBuf>, Box<dyn std::error::Error>> {
    if !prefix.exists() || !prefix_contains_crossover_identity(prefix) {
        return Ok(None);
    }

    let backup_root = metalsharp_home().join("backups");
    std::fs::create_dir_all(&backup_root)?;
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .unwrap_or(0);
    let mut target = backup_root.join(format!("prefix-gptk-steam-contaminated-{}", stamp));
    let mut counter = 1;
    while target.exists() {
        target = backup_root.join(format!("prefix-gptk-steam-contaminated-{}-{}", stamp, counter));
        counter += 1;
    }
    std::fs::rename(prefix, &target)?;
    Ok(Some(target))
}

fn run_install_gptk_steam() -> Result<String, Box<dyn std::error::Error>> {
    write_gptk_steam_install_progress("checking_toolkit", "Checking Game Porting Toolkit installation...", None);
    if !gptk_installed() {
        return Err("Game Porting Toolkit is not installed at /Applications/Game Porting Toolkit.app".into());
    }

    let metalsharp_dir = metalsharp_home();
    std::fs::create_dir_all(&metalsharp_dir)?;

    write_gptk_steam_install_progress("downloading_steam", "Downloading SteamSetup.exe for GPTK Steam...", None);
    let installer = prepare_steam_installer(&metalsharp_dir)?;
    let prefix = gptk_steam_prefix();
    write_gptk_steam_install_progress("preparing_prefix", "Preparing a dedicated GPTK Steam prefix...", None);
    let archived_prefix = archive_contaminated_gptk_prefix(&prefix)?;
    std::fs::create_dir_all(&prefix)?;

    ensure_clean_gptk_prefix(&prefix)?;

    write_gptk_steam_install_progress("running_installer", "Running SteamSetup.exe inside the GPTK prefix...", None);
    let prefix_str = prefix.to_string_lossy().to_string();
    let mut install_cmd = Command::new("arch");
    install_cmd
        .arg("-x86_64")
        .arg(gptk_wine_path())
        .arg(&installer)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDLLOVERRIDES", "mscoree=d;mshtml=d")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    apply_gptk_env(&mut install_cmd);
    let _child = install_cmd.spawn()?;

    write_gptk_steam_install_progress(
        "waiting_for_steam",
        "Waiting for Steam installer to finish writing the GPTK Steam files...",
        None,
    );
    let steam_dir = steam_dir_for_prefix(&prefix);
    let steam_exe = steam_dir.join("Steam.exe");
    let steam_ui_dll = steam_dir.join("steamui.dll");
    for _ in 0..180 {
        if steam_exe.exists() && steam_ui_dll.exists() {
            break;
        }
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    if !steam_exe.exists() || !steam_ui_dll.exists() {
        return Err("SteamSetup.exe did not finish writing Steam.exe and steamui.dll into the GPTK prefix".into());
    }

    stop_gptk_prefix_wineserver(&prefix);
    write_gptk_steam_install_progress("deploying_wrapper", "Deploying the GPTK Steam CEF wrapper...", None);
    ensure_gptk_steam_launch_ready(&steam_dir);
    repair_fresh_gptk_identity(&prefix, &current_account_name())?;

    if !gptk_steam_installed() {
        return Err("GPTK Steam files were created, but the prefix is not launch-ready".into());
    }

    Ok(format!(
        "GPTK Steam is installed and ready{}",
        archived_prefix.map(|path| format!("; archived contaminated prefix to {}", path.display())).unwrap_or_default()
    ))
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
    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&steam_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .env("STEAM_RUNTIME", "0")
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env(
            "WINEDLLOVERRIDES",
            "dxgi,d3d11,d3d10core=n,b;bcrypt=b;ncrypt=b;gameoverlayrenderer,gameoverlayrenderer64=d",
        )
        .arg(&exe)
        .args(args)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

    apply_wine_wrapper_env(&mut cmd, &ms_root);
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    for (key, val) in extra_env {
        cmd.env(key, val);
    }

    let child = cmd.spawn()?;

    Ok(child.id())
}

pub fn launch_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    launch_wine_steam_with_env(&[])
}

pub fn launch_gptk_steam() -> Result<Value, Box<dyn std::error::Error>> {
    if is_gptk_steam_running() {
        return Ok(json!({
            "ok": true,
            "message": "GPTK Steam already running",
            "running": true,
            "installed": gptk_steam_installed()
        }));
    }
    if is_wine_steam_running() {
        stop_wine_steam()?;
    }
    let pid = spawn_gptk_steam(&["-no-cef-sandbox", "-cef-single-process", "-noverifyfiles", "-no-dwrite"])?;
    Ok(json!({"ok": true, "pid": pid, "running": true, "installed": gptk_steam_installed()}))
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

    if is_wine_steam_running() {
        if !extra_env.is_empty() {
            return Err(
                "Wine Steam is already running; per-game environment cannot be inherited without restarting Steam"
                    .into(),
            );
        }
        return Ok(json!({
            "ok": true,
            "message": "Steam already running"
        }));
    }

    if is_gptk_steam_running() {
        stop_gptk_steam()?;
    }

    ensure_steam_launch_ready(&steam_dir);

    let pid = spawn_wine_steam_with_env(
        &["-no-cef-sandbox", "-cef-single-process", "-noverifyfiles", "-no-dwrite"],
        extra_env,
    )?;

    Ok(json!({"ok": true, "pid": pid, "running": true}))
}

pub fn launch_macos_steam() -> Result<Value, Box<dyn std::error::Error>> {
    if macos_steam_app().is_none() {
        return Err("macOS Steam is not installed".into());
    }
    if is_wine_steam_running() || is_gptk_steam_running() {
        return Err("Wine Steam is running. Stop Wine or GPTK Steam before launching macOS Steam.".into());
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

    let term_pids = macos_steam_process_pids();
    if !term_pids.is_empty() {
        for pid in term_pids {
            let _ = Command::new("kill")
                .args(["-TERM", &pid.to_string()])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    let kill_pids = macos_steam_process_pids();
    if !kill_pids.is_empty() {
        for pid in kill_pids {
            let _ = Command::new("kill")
                .args(["-KILL", &pid.to_string()])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }

    Ok(json!({"ok": true, "running": is_macos_steam_running()}))
}

pub fn launch_macos_steam_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    if is_wine_steam_running() {
        return Err("Wine Steam is running. Stop Wine Steam before launching through MacOS Steam.".into());
    }
    if !is_macos_game_installed(appid) {
        return Err(
            "This game is not installed in macOS Steam. Download it through macOS Steam before using the MacOS Steam engine."
                .into(),
        );
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
    let child = Command::new("open")
        .arg(&url)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": latest_macos_steam_pid().max(child.id()), "appid": appid}))
}

pub fn stop_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let term_pids = wine_steam_cleanup_pids();
    for pid in term_pids {
        let _ = Command::new("kill")
            .args(["-TERM", &pid.to_string()])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }
    std::thread::sleep(std::time::Duration::from_secs(1));

    let kill_pids = wine_steam_cleanup_pids();
    for pid in kill_pids {
        let _ = Command::new("kill")
            .args(["-KILL", &pid.to_string()])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }
    if !wine_steam_cleanup_pids().is_empty() {
        std::thread::sleep(std::time::Duration::from_millis(500));
    }

    Ok(json!({"ok": true, "running": is_wine_steam_running()}))
}

pub fn stop_gptk_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let prefix_str = gptk_steam_prefix().to_string_lossy().to_string();
    let _ = Command::new(gptk_wineserver_path())
        .arg("-k")
        .env("WINEPREFIX", &prefix_str)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let term_pids = gptk_steam_cleanup_pids();
    for pid in term_pids {
        let _ = Command::new("kill")
            .args(["-TERM", &pid.to_string()])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }
    std::thread::sleep(std::time::Duration::from_secs(1));

    let kill_pids = gptk_steam_cleanup_pids();
    for pid in kill_pids {
        let _ = Command::new("kill")
            .args(["-KILL", &pid.to_string()])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    Ok(json!({"ok": true, "running": is_gptk_steam_running()}))
}

pub fn ensure_gptk_steam_ready_for_game_launch() -> Result<bool, Box<dyn std::error::Error>> {
    if is_gptk_steam_running() {
        return Ok(false);
    }

    launch_gptk_steam()?;
    for _ in 0..20 {
        if is_gptk_steam_running() {
            std::thread::sleep(std::time::Duration::from_secs(2));
            return Ok(true);
        }
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    Err("GPTK Steam was started but did not become ready for game launch".into())
}

pub fn install_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    if !is_wine_steam_running() {
        launch_wine_steam()?;
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    let name = get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
    let pipeline = crate::mtsp::rules::resolve_pipeline(appid);
    let bottle = crate::bottles::ensure_steam_game_bottle(appid, &name, None, pipeline).ok();

    Ok(json!({"ok": true, "appid": appid, "method": "steam_ui", "bottle_id": bottle.map(|b| b.id)}))
}

pub fn launch_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    launch_game_via_steam_with_env(appid, &[])
}

pub fn ensure_wine_steam_ready_for_game_launch() -> Result<bool, Box<dyn std::error::Error>> {
    if is_wine_steam_running() {
        return Ok(false);
    }

    launch_wine_steam()?;
    for _ in 0..12 {
        if is_wine_steam_running() {
            std::thread::sleep(std::time::Duration::from_secs(2));
            return Ok(true);
        }
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    Err("Wine Steam was started but did not become ready for game launch".into())
}

pub fn launch_game_via_steam_with_env(
    appid: u32,
    extra_env: &[(String, String)],
) -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    let steam_running = is_wine_steam_running();
    if steam_running && !extra_env.is_empty() {
        return Err(
            "Wine Steam is already running; route-specific environment cannot be inherited without restarting Steam"
                .into(),
        );
    }
    if !steam_running {
        launch_wine_steam_with_env(extra_env)?;
        let mut ready = false;
        for _ in 0..12 {
            if is_wine_steam_running() {
                std::thread::sleep(std::time::Duration::from_secs(2));
                ready = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_secs(1));
        }
        if !ready {
            return Err("Wine Steam was started but did not become ready for game launch".into());
        }
    }

    let url = format!("steam://run/{}", appid);
    let pid = spawn_wine_steam_with_env(&[&url], extra_env)?;

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
    installed_games_in_steamapps(crate::scan::wine_steam_library_paths(), true)
}

pub fn get_gptk_steam_installed_games() -> Vec<u32> {
    installed_games_in_steamapps(crate::scan::gptk_steam_library_paths(), false)
}

fn installed_games_in_steamapps(paths: Vec<PathBuf>, skip_macos: bool) -> Vec<u32> {
    let mut appids = Vec::new();

    for steamapps in paths {
        if skip_macos && is_macos_steamapps_path(&steamapps) {
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

fn resolve_game_dir_in_steamapps(appid: u32, paths: Vec<PathBuf>) -> Option<PathBuf> {
    let manifest_name = format!("appmanifest_{}.acf", appid);
    for steamapps in paths {
        let manifest_path = steamapps.join(&manifest_name);
        if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
            if let Some(dir_name) = crate::scan::parse_installdir_from_acf(&contents) {
                let dir = steamapps.join("common").join(dir_name);
                if dir.exists() {
                    return Some(dir);
                }
            }
        }
    }
    None
}

fn get_game_name_from_manifest_in_paths(appid: u32, paths: Vec<PathBuf>) -> Option<String> {
    let manifest_name = format!("appmanifest_{}.acf", appid);

    for steamapps in paths {
        if is_macos_steamapps_path(&steamapps) {
            continue;
        }
        let manifest_path = steamapps.join(&manifest_name);
        if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
            if let Some(name) = parse_acf_field(&contents, "name") {
                return Some(name);
            }
        }
    }

    None
}

pub fn deploy_steamwebhelper_wrapper(steam_dir: &PathBuf) {
    deploy_steamwebhelper_wrapper_with(steam_dir, find_bundled_steamwebhelper_wrapper, ".ms_wrapper_deployed");
}

fn deploy_steamwebhelper_wrapper_with(steam_dir: &PathBuf, wrapper_finder: fn() -> Option<PathBuf>, marker_name: &str) {
    let cef_dir = steam_dir.join("bin").join("cef").join("cef.win64");
    let original = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");
    let wrapper_marker = cef_dir.join(marker_name);

    let wrapper = match wrapper_finder() {
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

fn find_bundled_gptk_steamwebhelper_wrapper() -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
        let wrapper = resources.join("bundles").join("steamwebhelper-gptk.exe");
        if wrapper.exists() {
            return Some(wrapper);
        }
    }

    let dev = PathBuf::from("app/bundles/steamwebhelper-gptk.exe");
    if dev.exists() {
        return Some(dev);
    }

    find_bundled_steamwebhelper_wrapper()
}

fn find_bundled_steamwebhelper_wrapper() -> Option<PathBuf> {
    if let Some(resources) = crate::platform::app_resources_dir() {
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

pub fn get_game_name_from_manifest(appid: u32) -> Option<String> {
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

    if !removed_wine && !removed_local && is_macos_game_installed(appid) {
        return Err("This game is installed in macOS Steam. Uninstall it from macOS Steam, or install the Windows copy before using MetalSharp uninstall.".into());
    }

    if !removed_wine && !removed_local {
        return Err("No Windows Steam or MetalSharp local install was found to uninstall.".into());
    }

    Ok(json!({"ok": true, "appid": appid, "wine_removed": removed_wine, "local_removed": removed_local}))
}

pub fn is_macos_game_installed(appid: u32) -> bool {
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
    let gptk_steam_appids = get_gptk_steam_installed_games();

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
            for &appid in &gptk_steam_appids {
                if !fallback.iter().any(|(id, _)| *id == appid) {
                    let name = get_game_name_from_manifest_in_paths(appid, crate::scan::gptk_steam_library_paths())
                        .unwrap_or_else(|| format!("Game {}", appid));
                    fallback.push((appid, name));
                }
            }
            fallback
        },
    };

    let owned_appids: Vec<u32> = owned.iter().map(|(id, _)| *id).collect();
    let mut all_games = owned.clone();
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
    for &appid in &gptk_steam_appids {
        if !all_games.iter().any(|(id, _)| *id == appid) {
            let name = get_game_name_from_manifest_in_paths(appid, crate::scan::gptk_steam_library_paths())
                .unwrap_or_else(|| format!("Game {}", appid));
            all_games.push((appid, name));
        }
    }

    let mut games: Vec<Value> = Vec::new();
    for (appid, name) in &all_games {
        let dual = crate::scan::resolve_dual_game_dir(*appid);
        let has_metalsharp_install = downloaded_appids.contains(appid) || wine_steam_appids.contains(appid);
        let has_any_install =
            installed_appids.contains(appid) || has_metalsharp_install || gptk_steam_appids.contains(appid);

        if has_metalsharp_install {
            let resolved = crate::mtsp::rules::resolve_pipeline(*appid);
            let pipeline_id = if matches!(resolved, crate::mtsp::engine::PipelineId::M13) {
                crate::mtsp::engine::PipelineId::M12
            } else {
                resolved
            };
            let bottle =
                crate::bottles::ensure_steam_game_bottle(*appid, name, dual.wine_dir.as_deref(), pipeline_id).ok();
            games.push(steam_library_card(SteamLibraryCardData {
                appid: *appid,
                name,
                source: "metalsharp",
                source_label: "MetalSharp",
                installed: true,
                can_uninstall: downloaded_appids.contains(appid) || wine_steam_appids.contains(appid),
                pipeline_id,
                has_native_build: dual.has_native_build,
                native_app_path: dual.macos_app.as_ref(),
                wine_game_path: dual.wine_dir.as_ref(),
                bottle: bottle.as_ref(),
            }));
        }

        if gptk_steam_appids.contains(appid) {
            let gptk_game_dir = resolve_game_dir_in_steamapps(*appid, crate::scan::gptk_steam_library_paths());
            let bottle = crate::bottles::ensure_steam_game_bottle(
                *appid,
                name,
                gptk_game_dir.as_deref(),
                crate::mtsp::engine::PipelineId::M13,
            )
            .ok();
            games.push(steam_library_card(SteamLibraryCardData {
                appid: *appid,
                name,
                source: "gptk",
                source_label: "GPTK",
                installed: true,
                can_uninstall: true,
                pipeline_id: crate::mtsp::engine::PipelineId::M13,
                has_native_build: false,
                native_app_path: None,
                wine_game_path: gptk_game_dir.as_ref(),
                bottle: bottle.as_ref(),
            }));
        }

        if !has_metalsharp_install && !gptk_steam_appids.contains(appid) {
            let resolved = crate::mtsp::rules::resolve_pipeline(*appid);
            let pipeline_id = if matches!(resolved, crate::mtsp::engine::PipelineId::M13) {
                crate::mtsp::engine::PipelineId::M12
            } else {
                resolved
            };
            games.push(steam_library_card(SteamLibraryCardData {
                appid: *appid,
                name,
                source: "steam",
                source_label: "Steam",
                installed: has_any_install,
                can_uninstall: false,
                pipeline_id,
                has_native_build: dual.has_native_build,
                native_app_path: dual.macos_app.as_ref(),
                wine_game_path: dual.wine_dir.as_ref(),
                bottle: None,
            }));
        }
    }

    json!({
        "ok": true,
        "total": games.len(),
        "installed_count": games.iter().filter(|g| g["installed"].as_bool().unwrap_or(false)).count(),
        "games": games,
    })
}

struct SteamLibraryCardData<'a> {
    appid: u32,
    name: &'a str,
    source: &'a str,
    source_label: &'a str,
    installed: bool,
    can_uninstall: bool,
    pipeline_id: crate::mtsp::engine::PipelineId,
    has_native_build: bool,
    native_app_path: Option<&'a PathBuf>,
    wine_game_path: Option<&'a PathBuf>,
    bottle: Option<&'a crate::bottles::BottleManifest>,
}

fn steam_library_card(card: SteamLibraryCardData<'_>) -> Value {
    let available_pipelines: Vec<serde_json::Value> = if card.source == "gptk" {
        vec![serde_json::json!({
            "id": "d3dmetal",
            "name": "D3DMetal",
            "recommended": true,
        })]
    } else {
        let node = crate::mtsp::engine::get_pipeline(card.pipeline_id);
        std::iter::once(serde_json::json!({
            "id": node.id,
            "name": node.name,
            "recommended": true,
        }))
        .chain(node.alternatives.iter().filter(|alt| !matches!(alt, crate::mtsp::engine::PipelineId::M13)).map(|alt| {
            let alt_node = crate::mtsp::engine::get_pipeline(*alt);
            serde_json::json!({
                "id": alt_node.id,
                "name": alt_node.name,
                "recommended": false,
            })
        }))
        .collect()
    };
    let launch_method = if card.source == "gptk" { "d3dmetal" } else { card.pipeline_id.to_legacy_method() };

    json!({
        "library_id": format!("{}:{}", card.source, card.appid),
        "library_source": card.source,
        "library_source_label": card.source_label,
        "appid": card.appid,
        "name": card.name,
        "installed": card.installed,
        "state": if card.installed { "installed" } else { "not_installed" },
        "can_uninstall": card.can_uninstall,
        "launch_method": launch_method,
        "available_pipelines": available_pipelines,
        "has_native_build": card.has_native_build,
        "native_app_path": card.native_app_path.map(|p| p.to_string_lossy().to_string()),
        "wine_game_path": card.wine_game_path.map(|p| p.to_string_lossy().to_string()),
        "bottle_id": card.bottle.map(|b| b.id.clone()),
        "bottle_health": card.bottle.map(|b| json!(b.health)),
        "bottle_runtime_assets": card.bottle.map(|b| b.runtime_assets.len()).unwrap_or(0),
        "cover_url": format!("https://steamcdn-a.akamaihd.net/steam/apps/{}/library_600x900.jpg", card.appid),
        "header_url": format!("https://steamcdn-a.akamaihd.net/steam/apps/{}/header.jpg", card.appid),
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

    let installer = prepare_steam_installer(&metalsharp_dir)?;

    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = steam_prefix();
    std::fs::create_dir_all(&prefix)?;

    let prefix_str = prefix.to_string_lossy().to_string();

    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");

    let mut wineboot_cmd = Command::new(&wine);
    wineboot_cmd
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut wineboot_cmd, &ms_root);
    let wineboot_result = wineboot_cmd.status();

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

    let mut install_cmd = Command::new(&wine);
    install_cmd
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .arg(&installer)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut install_cmd, &ms_root);
    let _child = install_cmd.spawn()?;

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

fn prepare_steam_installer(metalsharp_dir: &Path) -> Result<PathBuf, Box<dyn std::error::Error>> {
    std::fs::create_dir_all(metalsharp_dir)?;
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

    Ok(installer)
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

    #[test]
    fn detects_macos_steam_process_lines_without_matching_shell_searches() {
        let line = "26398 /Users/alex/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/ipcserver";
        let (pid, command) = parse_process_line(line).expect("process line should parse");
        assert_eq!(pid, 26398);
        assert!(!is_macos_steam_active_command(command));
        assert!(is_macos_steam_cleanup_command(command));
        assert!(!is_macos_steam_active_command(
            "/bin/zsh -lc ps axo pid=,command= | rg -i \"Steam.app|steam_osx|ipcserver\"",
        ));
        assert!(!is_macos_steam_cleanup_command(
            "/bin/zsh -lc ps axo pid=,command= | rg -i \"Steam.app|steam_osx|ipcserver\"",
        ));
    }

    #[test]
    fn detects_gptk_steam_posix_launch_command() {
        let command =
            format!("arch -x86_64 {} {} -no-cef-sandbox", gptk_wine_path().display(), gptk_steam_exe_path().display());

        assert!(is_gptk_steam_owner_command(&command));
        assert!(!is_wine_steam_owner_command(&command));
    }

    #[test]
    fn detects_gptk_steam_windows_path_command() {
        let command = format!("{} C:\\Program Files (x86)\\Steam\\steam.exe -silent", gptk_app_wine_path().display());

        assert!(is_gptk_steam_owner_command(&command));
        assert!(!is_wine_steam_owner_command(&command));
    }

    #[test]
    fn detects_downloaded_gptk_dmg_names() {
        assert!("Game_Porting_Toolkit_3.0.dmg".to_lowercase().contains("game_porting_toolkit"));
        assert!("Game Porting Toolkit 3.0.dmg".to_lowercase().contains("game porting toolkit"));
    }

    #[test]
    fn gptk_disk_image_signature_requires_apple_acceptance() {
        assert!(apple_signature_output_is_accepted(
            "/Users/alex/Downloads/Game_Porting_Toolkit.dmg: accepted\nsource=Apple System"
        ));
        assert!(!apple_signature_output_is_accepted(
            "/Users/alex/Downloads/gptk.dmg: accepted\nsource=Notarized Developer ID"
        ));
        assert!(!apple_signature_output_is_accepted(
            "/Users/alex/Downloads/Game_Porting_Toolkit.dmg: rejected\nsource=Apple System"
        ));
    }

    #[test]
    fn gptk_codesign_identity_requires_apple_controlled_authority() {
        assert!(codesign_output_has_apple_authority(
            "Authority=Software Signing\nAuthority=Apple Code Signing Certification Authority\nAuthority=Apple Root CA"
        ));
        assert!(codesign_output_has_apple_authority("TeamIdentifier=59GAB85EFG"));
        assert!(!codesign_output_has_apple_authority(
            "Authority=Developer ID Application: Example Corp\nAuthority=Developer ID Certification Authority\nAuthority=Apple Root CA"
        ));
    }

    #[test]
    fn ignores_gptk_steam_shell_searches() {
        assert!(!is_gptk_steam_owner_command(
            "/bin/zsh -lc ps axo pid=,command= | rg -i \"prefix-gptk-steam|Steam.exe\"",
        ));
    }

    #[test]
    fn gptk_steam_cleanup_stays_scoped_to_steam_prefix() {
        let game_command =
            format!("arch -x86_64 {} /Volumes/Games/EldenRing/Game/eldenring.exe", gptk_wine_path().display());
        let helper_command = format!(
            "{} {}/drive_c/Program Files (x86)/Steam/bin/cef/cef.win64/steamwebhelper.exe",
            gptk_wine_path().display(),
            gptk_steam_prefix().display()
        );

        assert!(!is_gptk_steam_cleanup_command(&game_command));
        assert!(is_gptk_steam_cleanup_command(&helper_command));
    }

    #[test]
    fn gptk_identity_repair_removes_crossover_profile() {
        let root = std::env::temp_dir().join(format!("metalsharp-gptk-repair-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let users = root.join("drive_c").join("users");
        std::fs::create_dir_all(users.join("crossover")).expect("create crossover profile");
        std::fs::write(
            root.join("user.reg"),
            r#""TEMP"="C:\\users\\crossover\\Temp"
"USERNAME"="crossover"
"USERPROFILE"="C:\\users\\crossover"
"HOMEPATH"="\\users\\crossover"
"AppData"="C:\\users\\crossover\\AppData\\Roaming"
"#,
        )
        .expect("write user.reg");
        std::fs::write(root.join("system.reg"), r#""ProfileImagePath"="C:\\users\\crossover""#)
            .expect("write system.reg");

        repair_fresh_gptk_identity(&root, "metalsharp").expect("repair identity");

        assert!(users.join("metalsharp").exists());
        assert!(!users.join("crossover").exists());
        assert!(!prefix_contains_crossover_identity(&root));
        let user_reg = std::fs::read_to_string(root.join("user.reg")).expect("read user.reg");
        assert!(user_reg.contains(r#""USERNAME"="metalsharp""#));
        assert!(user_reg.contains(r#""HOMEPATH"="\\users\\metalsharp""#));
        let _ = std::fs::remove_dir_all(root);
    }
}
