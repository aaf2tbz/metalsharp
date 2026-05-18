use serde_json::{json, Value};
use std::io::{Read, Seek, SeekFrom};
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

static STEAM_INSTALLING: AtomicBool = AtomicBool::new(false);
const STEAMWEBHELPER_WRAPPER_MAX_BYTES: u64 = 100_000;
const DEFAULT_STEAM_CEF_MODE: &str = "disable_gpu";
const STEAM_CEF_MODES: &[&str] = &["disable_gpu", "swiftshader", "passthrough"];
const WINE_STEAM_DLL_OVERRIDES: &str = "bcrypt=b;ncrypt=b;gameoverlayrenderer,gameoverlayrenderer64=d";

fn ms_wine() -> PathBuf {
    crate::platform::runtime_wine_binary(&crate::platform::wine_runtime_root())
}

fn steam_prefix() -> PathBuf {
    crate::platform::steam_prefix_dir()
}

fn steam_exe_path() -> PathBuf {
    steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe")
}

fn steam_run_url(appid: u32, launch_args: &[String]) -> String {
    if launch_args.is_empty() {
        return format!("steam://run/{}", appid);
    }

    format!("steam://run/{}//{}", appid, launch_args.join(" "))
}

fn steam_gameprocess_log_path() -> PathBuf {
    steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("logs").join("gameprocess_log.txt")
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
        "cef": steam_cef_status(&wine_steam_dir),
        "metalsharp_wine_available": ms_available,
        "installing": installing
    })
}

pub fn set_steam_cef_mode(mode: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let Some(mode) = normalize_steam_cef_mode(mode) else {
        return Err(
            format!("unsupported Steam CEF mode '{}'; expected one of {}", mode, STEAM_CEF_MODES.join(", ")).into()
        );
    };
    let steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");
    if !steam_dir.join("steamui.dll").exists() {
        return Err("Steam is not installed — use the setup wizard to install it first".into());
    }
    let mode_path = steam_cef_mode_path(&steam_dir);
    if let Some(parent) = mode_path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(&mode_path, format!("{}\n", mode))?;
    if !is_wine_steam_running() {
        deploy_steamwebhelper_wrapper(&steam_dir);
    }
    Ok(json!({
        "ok": true,
        "mode": mode,
        "restartRequired": is_wine_steam_running(),
        "cef": steam_cef_status(&steam_dir),
    }))
}

pub fn is_wine_steam_running() -> bool {
    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .any(|(_, command)| is_wine_steam_owner_command(command))
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

fn process_command_matches_exe(command: &str, exe_name: &str) -> bool {
    let lower = command.to_lowercase();
    let exe_lower = exe_name.to_lowercase();

    if is_process_probe_command(command) {
        return false;
    }

    lower.contains(&exe_lower)
}

fn steam_launch_process_names(appid: u32, pipeline: crate::mtsp::engine::PipelineId) -> Vec<String> {
    let mut names = Vec::new();
    let node = crate::mtsp::engine::get_pipeline(pipeline);
    if let Ok(recipe) = crate::mtsp::recipe::build_launch_recipe(appid, node) {
        if let Some(exe_name) = recipe.exe_name {
            names.push(exe_name);
        }
    }
    names.extend(known_launcher_process_names(appid).into_iter().map(str::to_string));
    names.sort_by_key(|name| name.to_lowercase());
    names.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    names
}

fn known_launcher_process_names(appid: u32) -> Vec<&'static str> {
    match appid {
        1669000 => vec!["Paradox Launcher.exe"],
        _ => Vec::new(),
    }
}

#[derive(Debug, Default, PartialEq)]
struct SteamCefProcessTopology {
    wrapper_pids: Vec<u32>,
    real_pids: Vec<u32>,
    renderer_pids: Vec<u32>,
    utility_pids: Vec<u32>,
    gpu_disabled: bool,
    in_process_gpu: bool,
}

fn steam_cef_dir(steam_dir: &Path) -> PathBuf {
    steam_dir.join("bin").join("cef").join("cef.win64")
}

fn steam_cef_mode_path(steam_dir: &Path) -> PathBuf {
    steam_cef_dir(steam_dir).join("steamwebhelper_mode.txt")
}

fn normalize_steam_cef_mode(mode: &str) -> Option<&'static str> {
    let mode = mode.trim();
    STEAM_CEF_MODES.iter().copied().find(|candidate| *candidate == mode)
}

fn active_steam_cef_mode(steam_dir: &Path) -> &'static str {
    let Ok(mode) = std::fs::read_to_string(steam_cef_mode_path(steam_dir)) else {
        return DEFAULT_STEAM_CEF_MODE;
    };
    normalize_steam_cef_mode(&mode).unwrap_or(DEFAULT_STEAM_CEF_MODE)
}

fn steam_cef_status(steam_dir: &Path) -> Value {
    let cef_dir = steam_cef_dir(steam_dir);
    let wrapper = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");
    let marker = cef_dir.join(".ms_wrapper_deployed");
    let mode_path = steam_cef_mode_path(steam_dir);
    let active_mode = active_steam_cef_mode(steam_dir);

    let dir_present = cef_dir.exists();
    let wrapper_size = std::fs::metadata(&wrapper).map(|m| m.len()).unwrap_or(0);
    let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);
    let bundled_wrapper = find_bundled_steamwebhelper_wrapper();
    let bundled_path = bundled_wrapper.as_ref().map(|path| path.to_string_lossy().to_string());
    let bundled_size =
        bundled_wrapper.as_ref().and_then(|path| std::fs::metadata(path).ok()).map(|m| m.len()).unwrap_or(0);
    let marker_present = marker.exists();
    let wrapper_present = wrapper_size > 0;
    let real_present = real_size > 0;
    let wrapper_overwritten = wrapper_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES;
    let wrapper_matches_bundled = bundled_wrapper.as_ref().is_some_and(|path| files_equal(&wrapper, path));
    let pending_wrapper_update = bundled_size > 0 && wrapper_present && !wrapper_matches_bundled;
    let real_missing_or_bad = real_size == 0 || real_size < STEAMWEBHELPER_WRAPPER_MAX_BYTES;
    let wrapper_deployed =
        marker_present && wrapper_present && !wrapper_overwritten && real_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES;

    let process_topology = steam_cef_process_topology_from_lines(&process_lines());
    let real_process_running = !process_topology.real_pids.is_empty();
    let renderer_running = !process_topology.renderer_pids.is_empty();
    let ready = wrapper_deployed && real_process_running && renderer_running;
    let reason = steam_cef_status_reason(SteamCefReadiness {
        dir_present,
        wrapper_present,
        wrapper_overwritten,
        real_present,
        real_missing_or_bad,
        wrapper_deployed,
        real_process_running,
        renderer_running,
    });

    json!({
        "ready": ready,
        "reason": reason,
        "cef_dir": cef_dir.to_string_lossy().to_string(),
        "wrapper_path": wrapper.to_string_lossy().to_string(),
        "real_path": real.to_string_lossy().to_string(),
        "marker_path": marker.to_string_lossy().to_string(),
        "mode_path": mode_path.to_string_lossy().to_string(),
        "mode": active_mode,
        "supported_modes": STEAM_CEF_MODES,
        "bundled_wrapper_path": bundled_path,
        "bundled_wrapper_size": bundled_size,
        "dir_present": dir_present,
        "wrapper_present": wrapper_present,
        "real_present": real_present,
        "marker_present": marker_present,
        "wrapper_size": wrapper_size,
        "wrapper_matches_bundled": wrapper_matches_bundled,
        "pending_wrapper_update": pending_wrapper_update,
        "real_size": real_size,
        "wrapper_deployed": wrapper_deployed,
        "wrapper_overwritten": wrapper_overwritten,
        "real_missing_or_bad": real_missing_or_bad,
        "wrapper_pids": process_topology.wrapper_pids,
        "real_pids": process_topology.real_pids,
        "renderer_pids": process_topology.renderer_pids,
        "utility_pids": process_topology.utility_pids,
        "real_process_running": real_process_running,
        "renderer_running": renderer_running,
        "gpu_disabled": process_topology.gpu_disabled,
        "in_process_gpu": process_topology.in_process_gpu,
    })
}

struct SteamCefReadiness {
    dir_present: bool,
    wrapper_present: bool,
    wrapper_overwritten: bool,
    real_present: bool,
    real_missing_or_bad: bool,
    wrapper_deployed: bool,
    real_process_running: bool,
    renderer_running: bool,
}

fn steam_cef_status_reason(readiness: SteamCefReadiness) -> &'static str {
    if !readiness.dir_present {
        "cef_dir_missing"
    } else if readiness.wrapper_overwritten {
        "wrapper_overwritten"
    } else if !readiness.wrapper_present {
        "wrapper_missing"
    } else if !readiness.real_present {
        "real_helper_missing"
    } else if readiness.real_missing_or_bad {
        "real_helper_too_small"
    } else if !readiness.wrapper_deployed {
        "wrapper_marker_missing"
    } else if !readiness.real_process_running {
        "real_helper_not_running"
    } else if !readiness.renderer_running {
        "renderer_not_running"
    } else {
        "ready"
    }
}

fn steam_cef_process_topology_from_lines(lines: &[String]) -> SteamCefProcessTopology {
    let mut topology = SteamCefProcessTopology::default();
    for line in lines {
        let Some((pid, command)) = parse_process_line(line) else {
            continue;
        };
        if is_process_probe_command(command) {
            continue;
        }
        if is_steamwebhelper_real_command(command) {
            topology.real_pids.push(pid);
            let lower = command.to_lowercase();
            if lower.contains("--type=renderer") {
                topology.renderer_pids.push(pid);
            }
            if lower.contains("--type=utility") {
                topology.utility_pids.push(pid);
            }
            if lower.contains("--disable-gpu") {
                topology.gpu_disabled = true;
            }
            if lower.contains("--in-process-gpu") {
                topology.in_process_gpu = true;
            }
        } else if is_steamwebhelper_wrapper_command(command) {
            topology.wrapper_pids.push(pid);
        }
    }
    topology
}

fn is_process_probe_command(command: &str) -> bool {
    let lower = command.to_lowercase();
    if lower.contains(" rg ")
        || lower.contains("rg -i")
        || lower.contains("ps axo")
        || lower.contains("ps -axo")
        || lower.contains("pgrep -afil")
    {
        return true;
    }

    let shell_invocation = lower.contains("/bin/zsh")
        || lower.contains("/bin/bash")
        || lower.contains("/bin/sh")
        || lower.starts_with("zsh ")
        || lower.starts_with("bash ")
        || lower.starts_with("sh ");

    shell_invocation
        && (lower.contains("curl")
            || lower.contains("/steam/pickup-game")
            || lower.contains("processnames")
            || lower.contains("steam://run/"))
}

fn is_steamwebhelper_wrapper_command(command: &str) -> bool {
    let lower = command.to_lowercase();
    lower.contains("steamwebhelper.exe") && !lower.contains("steamwebhelper_real.exe")
}

fn is_steamwebhelper_real_command(command: &str) -> bool {
    command.to_lowercase().contains("steamwebhelper_real.exe")
}

fn steam_game_process_pids(appid: u32, pipeline: crate::mtsp::engine::PipelineId) -> Vec<u32> {
    let process_names = steam_launch_process_names(appid, pipeline);
    if process_names.is_empty() {
        return Vec::new();
    }

    process_lines()
        .iter()
        .filter_map(|line| parse_process_line(line))
        .filter_map(|(pid, command)| {
            process_names.iter().any(|exe_name| process_command_matches_exe(command, exe_name)).then_some(pid)
        })
        .collect()
}

fn parse_steam_gameprocess_added_pid(line: &str, appid: u32) -> Option<u32> {
    if !line.contains(&format!("AppID {} adding PID ", appid)) {
        return None;
    }

    let pid_text = line.split(" adding PID ").nth(1)?.split_whitespace().next()?;
    pid_text.parse::<u32>().ok()
}

fn read_steam_gameprocess_pid_since(appid: u32, previous_len: u64) -> Option<u32> {
    let log_path = steam_gameprocess_log_path();
    let mut file = std::fs::File::open(&log_path).ok()?;
    let len = file.metadata().map(|m| m.len()).unwrap_or(0);
    let start = previous_len.min(len);
    let _ = file.seek(SeekFrom::Start(start));

    let mut text = String::new();
    file.read_to_string(&mut text).ok()?;

    text.lines().filter_map(|line| parse_steam_gameprocess_added_pid(line, appid)).next_back()
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

fn ensure_steam_launch_ready(steam_dir: &PathBuf) {
    let cef_dir = steam_dir.join("bin").join("cef").join("cef.win64");
    let wrapper = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");

    let wrapper_size = std::fs::metadata(&wrapper).map(|m| m.len()).unwrap_or(0);
    let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);
    let bundled_wrapper = find_bundled_steamwebhelper_wrapper();
    let wrapper_differs_from_bundle = bundled_wrapper.as_ref().is_some_and(|path| !files_equal(&wrapper, path));

    let wrapper_missing = wrapper_size == 0;
    let wrapper_overwritten = wrapper_size > STEAMWEBHELPER_WRAPPER_MAX_BYTES;
    let real_missing_or_bad = real_size > 0 && real_size < STEAMWEBHELPER_WRAPPER_MAX_BYTES;

    if wrapper_missing || wrapper_overwritten || real_missing_or_bad || wrapper_differs_from_bundle {
        deploy_steamwebhelper_wrapper(steam_dir);
    }
    configure_steam_appdefault_overrides();
}

fn configure_steam_appdefault_overrides() {
    for exe_name in ["Steam.exe", "steamwebhelper.exe", "steamwebhelper_real.exe"] {
        for (dll, mode) in [("dxgi", "builtin"), ("d3d11", "builtin"), ("d3d10core", "builtin")] {
            let _ = set_wine_appdefault_dll_override(exe_name, dll, mode);
        }
    }
}

fn set_wine_appdefault_dll_override(exe_name: &str, dll: &str, mode: &str) -> Result<(), Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let ms_root = crate::platform::wine_runtime_root();
    let key = format!("HKCU\\Software\\Wine\\AppDefaults\\{}\\DllOverrides", exe_name);
    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", steam_prefix().to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .args(["reg", "add", &key, "/v", dll, "/t", "REG_SZ", "/d", mode, "/f"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    let status = cmd.status()?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("failed to set Wine AppDefaults override {}={} for {}", dll, mode, exe_name).into())
    }
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
    let cef_mode = active_steam_cef_mode(&steam_dir);

    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let ms_root = crate::platform::wine_runtime_root();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&steam_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("STEAM_RUNTIME", "0")
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env("METALSHARP_STEAM_CEF_MODE", cef_mode)
        .env("WINEDLLOVERRIDES", WINE_STEAM_DLL_OVERRIDES)
        .arg(&exe)
        .args(args)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    for (key, val) in extra_env {
        cmd.env(key, val);
    }

    let child = cmd.spawn()?;

    Ok(child.id())
}

fn handoff_steam_url(url: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");
    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let ms_root = crate::platform::wine_runtime_root();

    ensure_steam_launch_ready(&steam_dir);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&steam_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("STEAM_RUNTIME", "0")
        .env("MS_FWD_COMPAT_GL_CTX", "1")
        .env("WINEDLLOVERRIDES", WINE_STEAM_DLL_OVERRIDES)
        .args(["start", url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    let mut child = cmd.spawn()?;
    let pid = child.id();
    let _ = std::thread::Builder::new().name("steam-url-handoff-reaper".to_string()).spawn(move || {
        let _ = child.wait();
    });

    Ok(pid)
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

    ensure_steam_launch_ready(&steam_dir);

    let pid = spawn_wine_steam_with_env(
        &["-no-cef-sandbox", "-cef-single-process", "-noverifyfiles", "-no-dwrite"],
        extra_env,
    )?;

    Ok(json!({"ok": true, "pid": pid}))
}

pub fn launch_macos_steam() -> Result<Value, Box<dyn std::error::Error>> {
    if macos_steam_app().is_none() {
        return Err("macOS Steam is not installed".into());
    }
    if is_wine_steam_running() {
        return Err("Wine Steam is running. Stop Wine Steam before launching macOS Steam.".into());
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

pub fn install_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    if !is_wine_steam_running() {
        launch_wine_steam()?;
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    Ok(json!({"ok": true, "appid": appid, "method": "steam_ui"}))
}

pub fn launch_game_via_steam(
    appid: u32,
    requested_pipeline: Option<crate::mtsp::engine::PipelineId>,
) -> Result<Value, Box<dyn std::error::Error>> {
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

    let pipeline = requested_pipeline.unwrap_or_else(|| crate::mtsp::rules::resolve_pipeline(appid));
    let prepare_result = if pipeline == crate::mtsp::engine::PipelineId::M12 {
        Some(crate::mtsp::launcher::prepare_pipeline_with_pipeline(appid, pipeline)?)
    } else {
        None
    };
    let launch_args: Vec<String> = prepare_result
        .as_ref()
        .and_then(|result| result.get("recipe"))
        .and_then(|recipe| recipe.get("launch_args"))
        .and_then(|args| args.as_array())
        .map(|args| args.iter().filter_map(|arg| arg.as_str().map(str::to_string)).collect())
        .unwrap_or_default();
    let gameprocess_log_len = std::fs::metadata(steam_gameprocess_log_path()).map(|m| m.len()).unwrap_or(0);
    let url = steam_run_url(appid, &launch_args);
    let thread_url = url.clone();
    std::thread::Builder::new().name(format!("steam-handoff-{}", appid)).spawn(move || {
        if let Err(err) = handoff_steam_url(thread_url.as_str()) {
            eprintln!("steam handoff failed for appid {}: {}", appid, err);
        }
    })?;

    Ok(json!({
        "ok": true,
        "pid": 0,
        "hostGamePid": null,
        "steamGamePid": null,
        "steamConfirmed": false,
        "launchPending": true,
        "steamLogOffset": gameprocess_log_len,
        "steamHandoffPid": null,
        "appid": appid,
        "pipeline": pipeline,
        "launchArgs": launch_args,
        "prepared": prepare_result
    }))
}

pub fn pickup_game_via_steam(
    appid: u32,
    requested_pipeline: Option<crate::mtsp::engine::PipelineId>,
    steam_log_offset: u64,
) -> Result<Value, Box<dyn std::error::Error>> {
    let pipeline = requested_pipeline.unwrap_or_else(|| crate::mtsp::rules::resolve_pipeline(appid));
    let host_game_pid = steam_game_process_pids(appid, pipeline).into_iter().next();
    let steam_game_pid = read_steam_gameprocess_pid_since(appid, steam_log_offset);
    let steam_confirmed = host_game_pid.is_some() || steam_game_pid.is_some();

    Ok(json!({
        "ok": true,
        "appid": appid,
        "pid": host_game_pid.unwrap_or(0),
        "hostGamePid": host_game_pid,
        "steamGamePid": steam_game_pid,
        "steamConfirmed": steam_confirmed,
        "launchPending": !steam_confirmed,
        "pipeline": pipeline,
        "steamLogOffset": steam_log_offset,
    }))
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
    let _ = handoff_steam_url(&url)?;

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
        if !files_equal(&original, &wrapper) {
            let _ = std::fs::copy(&wrapper, &original);
        }
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

fn files_equal(a: &Path, b: &Path) -> bool {
    let Ok(a_bytes) = std::fs::read(a) else {
        return false;
    };
    let Ok(b_bytes) = std::fs::read(b) else {
        return false;
    };
    a_bytes == b_bytes
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

    let cache_dir = crate::platform::metalsharp_home().join("cache").join("bundles");
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

    let local_games_dir = crate::platform::metalsharp_home().join("games");
    let local_dir = local_games_dir.join(appid.to_string());
    if remove_dir_all_under(&local_dir, &local_games_dir)? {
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
    let config_dir = crate::platform::metalsharp_home().join("cache");
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
        crate::platform::steam_prefix_dir().join("drive_c/Program Files (x86)/Steam/config/loginusers.vdf"),
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
    let games_dir = crate::platform::metalsharp_home().join("games");
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
    let config_path = crate::platform::metalsharp_home().join("cache").join("steam_config.json");
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

    let cache_path = crate::platform::metalsharp_home().join("cache").join("owned_games.json");

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
    let wine_path = crate::platform::steam_prefix_dir().join("drive_c/Program Files (x86)/Steam/config/loginusers.vdf");

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
    let metalsharp_dir = crate::platform::metalsharp_home();
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

    let ms_root = crate::platform::wine_runtime_root();

    let mut wineboot_cmd = Command::new(&wine);
    wineboot_cmd
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
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

pub fn watch_steamapps() -> Option<String> {
    let steamapps = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");

    if !steamapps.exists() {
        return None;
    }

    let current = get_wine_steam_installed_games();
    let cached_path = crate::platform::metalsharp_home().join("cache").join("wine_steam_appids.cache");

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
    fn wine_steam_uses_ui_safe_dll_overrides() {
        assert!(!WINE_STEAM_DLL_OVERRIDES.contains("dxgi"));
        assert!(!WINE_STEAM_DLL_OVERRIDES.contains("d3d11"));
        assert!(!WINE_STEAM_DLL_OVERRIDES.contains("d3d12"));
        assert!(!WINE_STEAM_DLL_OVERRIDES.contains("winemetal"));
        assert!(WINE_STEAM_DLL_OVERRIDES.contains("gameoverlayrenderer"));
    }

    #[test]
    fn steam_run_url_carries_launch_args_for_m12_games() {
        let args = vec!["-force-d3d12".to_string()];
        assert_eq!(steam_run_url(1326470, &args), "steam://run/1326470//-force-d3d12");
    }

    #[test]
    fn steam_run_url_omits_empty_launch_arg_separator() {
        assert_eq!(steam_run_url(1326470, &[]), "steam://run/1326470");
    }

    #[test]
    fn game_process_pickup_matches_real_windows_exe_command() {
        assert!(process_command_matches_exe(
            r#""Z:\Volumes\AverySSD\SteamLibrary\steamapps\common\Ghostrunner\Ghostrunner\Binaries\Win64\Ghostrunner-Win64-Shipping.exe" Ghostrunner -STEAM -dx12"#,
            "Ghostrunner-Win64-Shipping.exe",
        ));
    }

    #[test]
    fn game_process_pickup_rejects_polling_shell_command() {
        assert!(!process_command_matches_exe(
            r#"/bin/zsh -lc for i in {1..30}; do curl -sS http://127.0.0.1:9274/steam/pickup-game --data '{"processNames":["Ghostrunner-Win64-Shipping.exe"]}'; sleep 1; done"#,
            "Ghostrunner-Win64-Shipping.exe",
        ));
    }

    #[test]
    fn game_process_pickup_rejects_steam_url_handoff_shell_command() {
        assert!(!process_command_matches_exe(
            r#"/bin/zsh -lc /Users/alexmondello/.metalsharp/runtime/wine/bin/wine start steam://run/1139900//-dx12"#,
            "Ghostrunner-Win64-Shipping.exe",
        ));
    }

    #[test]
    fn aow4_process_pickup_includes_paradox_launcher_stage() {
        let names = steam_launch_process_names(1669000, crate::mtsp::engine::PipelineId::M12);
        assert!(names.iter().any(|name| name == "AOW4.exe"));
        assert!(names.iter().any(|name| name == "Paradox Launcher.exe"));
        assert!(process_command_matches_exe(
            "49883 Z:\\Volumes\\AverySSD\\SteamLibrary\\steamapps\\common\\Age of Wonders 4\\launcher-se\\Paradox Launcher.exe --pdxlEnableSteamDeckCompatibilityMode -dx12",
            "Paradox Launcher.exe",
        ));
    }

    #[test]
    fn steam_gameprocess_added_pid_matches_appid() {
        let line = r#"[2026-05-18 00:58:11] AppID 1326470 adding PID 1552 as a tracked process ""Z:\Volumes\AverySSD\SteamLibrary\steamapps\common\Sons Of The Forest\SonsOfTheForest.exe" -force-d3d12""#;
        assert_eq!(parse_steam_gameprocess_added_pid(line, 1326470), Some(1552));
        assert_eq!(parse_steam_gameprocess_added_pid(line, 848450), None);
    }

    #[test]
    fn steam_gameprocess_added_pid_rejects_non_launch_lines() {
        let removed = r#"[2026-05-18 00:58:31] AppID 1326470 no longer tracking PID 1552, exit code 0"#;
        assert_eq!(parse_steam_gameprocess_added_pid(removed, 1326470), None);
    }

    #[test]
    fn cef_process_topology_distinguishes_wrapper_and_real_helpers() {
        let lines = vec![
            "26896 C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe -nocrashdialog".to_string(),
            "26898 C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper_real.exe -nocrashdialog --in-process-gpu --disable-gpu".to_string(),
            "26912 C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper_real.exe --type=renderer --disable-gpu-compositing".to_string(),
            "26905 C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper_real.exe --type=utility --utility-sub-type=network.mojom.NetworkService".to_string(),
            "45737 pgrep -afil steamwebhelper".to_string(),
        ];

        let topology = steam_cef_process_topology_from_lines(&lines);
        assert_eq!(topology.wrapper_pids, vec![26896]);
        assert_eq!(topology.real_pids, vec![26898, 26912, 26905]);
        assert_eq!(topology.renderer_pids, vec![26912]);
        assert_eq!(topology.utility_pids, vec![26905]);
        assert!(topology.gpu_disabled);
        assert!(topology.in_process_gpu);
    }

    #[test]
    fn cef_mode_accepts_only_supported_profiles() {
        assert_eq!(normalize_steam_cef_mode("disable_gpu"), Some("disable_gpu"));
        assert_eq!(normalize_steam_cef_mode("swiftshader\n"), Some("swiftshader"));
        assert_eq!(normalize_steam_cef_mode("passthrough"), Some("passthrough"));
        assert_eq!(normalize_steam_cef_mode("native_gpu"), None);
    }

    #[test]
    fn files_equal_detects_wrapper_drift() {
        let base = std::env::temp_dir().join(format!(
            "metalsharp-wrapper-drift-{}-{}",
            std::process::id(),
            std::time::UNIX_EPOCH.elapsed().unwrap().as_nanos()
        ));
        std::fs::create_dir_all(&base).unwrap();
        let a = base.join("a.exe");
        let b = base.join("b.exe");
        std::fs::write(&a, b"wrapper-v1").unwrap();
        std::fs::write(&b, b"wrapper-v1").unwrap();
        assert!(files_equal(&a, &b));

        std::fs::write(&b, b"wrapper-v2").unwrap();
        assert!(!files_equal(&a, &b));
        std::fs::remove_dir_all(&base).ok();
    }

    #[test]
    fn cef_status_reason_flags_wrapper_drift_before_process_state() {
        assert_eq!(
            steam_cef_status_reason(SteamCefReadiness {
                dir_present: true,
                wrapper_present: true,
                wrapper_overwritten: true,
                real_present: true,
                real_missing_or_bad: false,
                wrapper_deployed: false,
                real_process_running: true,
                renderer_running: true,
            }),
            "wrapper_overwritten"
        );
        assert_eq!(
            steam_cef_status_reason(SteamCefReadiness {
                dir_present: true,
                wrapper_present: true,
                wrapper_overwritten: false,
                real_present: false,
                real_missing_or_bad: true,
                wrapper_deployed: false,
                real_process_running: true,
                renderer_running: true,
            }),
            "real_helper_missing"
        );
        assert_eq!(
            steam_cef_status_reason(SteamCefReadiness {
                dir_present: true,
                wrapper_present: true,
                wrapper_overwritten: false,
                real_present: true,
                real_missing_or_bad: false,
                wrapper_deployed: true,
                real_process_running: false,
                renderer_running: false,
            }),
            "real_helper_not_running"
        );
        assert_eq!(
            steam_cef_status_reason(SteamCefReadiness {
                dir_present: true,
                wrapper_present: true,
                wrapper_overwritten: false,
                real_present: true,
                real_missing_or_bad: false,
                wrapper_deployed: true,
                real_process_running: true,
                renderer_running: true,
            }),
            "ready"
        );
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
}
