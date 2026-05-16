use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

static STEAM_INSTALLING: AtomicBool = AtomicBool::new(false);

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
    let mac_installed = mac_paths.iter().any(|p| p.exists());

    let running = is_wine_steam_running();
    let ms_available = ms_wine().exists();
    let installing = is_installing_steam();

    json!({
        "installed": windows_installed,
        "path": windows_path,
        "login_state": login_state,
        "mac_installed": mac_installed,
        "running": running,
        "metalsharp_wine_available": ms_available,
        "installing": installing
    })
}

pub fn is_wine_steam_running() -> bool {
    Command::new("pgrep").args(["-f", "Steam.exe"]).output().map(|o| o.status.success()).unwrap_or(false)
        || Command::new("pgrep").args(["-f", "steam.exe"]).output().map(|o| o.status.success()).unwrap_or(false)
}

pub fn is_installing_steam() -> bool {
    STEAM_INSTALLING.load(Ordering::SeqCst)
}

fn ensure_steam_launch_ready(steam_dir: &PathBuf) {
    let cef_dir = steam_dir.join("bin").join("cef").join("cef.win64");
    let wrapper = cef_dir.join("steamwebhelper.exe");
    let real = cef_dir.join("steamwebhelper_real.exe");

    if !wrapper.exists() {
        return;
    }

    let needs_redeploy = if real.exists() {
        let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);
        real_size < 100_000
    } else {
        let wrapper_size = std::fs::metadata(&wrapper).map(|m| m.len()).unwrap_or(0);
        wrapper_size > 100_000
    };

    if needs_redeploy {
        deploy_steamwebhelper_wrapper(steam_dir);
    }
}

pub fn launch_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
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
        return Ok(json!({"ok": true, "message": "Steam already running"}));
    }

    ensure_steam_launch_ready(&steam_dir);

    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    let dyld = format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    );

    let prefix_str = steam_prefix().to_string_lossy().to_string();

    let child = Command::new(&wine)
        .current_dir(&steam_dir)
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
        .args(["-no-cef-sandbox", "-cef-single-process", "-noverifyfiles", "-no-dwrite"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": child.id()}))
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

    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let url = format!("steam://run/{}", appid);

    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    let dyld = format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    );

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": child.id(), "appid": appid}))
}

pub fn view_game_in_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }
    if !is_wine_steam_running() {
        return Err("Steam is not running".into());
    }

    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let url = format!("steam://nav/games/details/{}", appid);

    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    let dyld = format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    );

    Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "appid": appid}))
}

pub fn get_wine_steam_installed_games() -> Vec<u32> {
    let mut appids = Vec::new();

    for steamapps in crate::scan::wine_steam_library_paths() {
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

    if wrapper_marker.exists() && real.exists() {
        let real_size = std::fs::metadata(&real).map(|m| m.len()).unwrap_or(0);
        if real_size > 100_000 {
            return;
        }
    }

    if !original.exists() {
        return;
    }

    let real_size = std::fs::metadata(&original).map(|m| m.len()).unwrap_or(0);
    if real_size < 100_000 {
        return;
    }

    if real.exists() {
        let _ = std::fs::remove_file(&real);
    }
    let _ = std::fs::rename(&original, &real);

    let wrapper = find_bundled_steamwebhelper_wrapper();
    if let Some(wrapper_src) = wrapper {
        let _ = std::fs::copy(&wrapper_src, &original);
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
        if size > 100_000 {
            return Some(cached);
        }
    }

    let url = "https://github.com/aaf2tbz/metalsharp/releases/download/bundles/steamwebhelper.exe";
    let output = Command::new("curl").args(["-sL", "-o"]).arg(&cached).arg(url).output().ok()?;

    if output.status.success() && cached.exists() {
        let size = std::fs::metadata(&cached).map(|m| m.len()).unwrap_or(0);
        if size > 100_000 {
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
            for line in contents.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with("\"name\"") {
                    if let Some((_, val)) = trimmed.split_once('\t') {
                        return Some(val.trim().trim_matches('"').to_string());
                    }
                }
            }
        }
    }

    None
}

pub fn uninstall_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let manifest_name = format!("appmanifest_{}.acf", appid);

    let _ = crate::launch::kill_game_with_pid(appid, 0);

    for steamapps in crate::scan::wine_steam_library_paths() {
        let manifest_path = steamapps.join(&manifest_name);
        if manifest_path.exists() {
            let contents = std::fs::read_to_string(&manifest_path).unwrap_or_default();
            let install_dir = contents.lines().find(|l| l.contains("\"installdir\"")).and_then(|l| {
                let parts: Vec<&str> = l.splitn(2, ['\t', ' ']).collect();
                parts.last().map(|s| s.trim().trim_matches('"').to_string())
            });

            if let Some(dir_name) = install_dir {
                let game_dir = steamapps.join("common").join(&dir_name);
                if game_dir.exists() {
                    let _ = std::fs::remove_dir_all(&game_dir);
                }
            }
            let _ = std::fs::remove_file(&manifest_path);
            break;
        }
    }

    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    if local_dir.exists() {
        let _ = std::fs::remove_dir_all(&local_dir);
    }

    Ok(json!({"ok": true, "appid": appid}))
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
