use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::Command;

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
    dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("prefix-steam")
}

fn steam_exe_path() -> PathBuf {
    steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("Steam.exe")
}

pub fn status() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let wine_steam_dir = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam");
    let wine_steam_exe = wine_steam_dir.join("Steam.exe");
    let windows_installed = wine_steam_exe.exists();
    let windows_path = if windows_installed {
        Some(wine_steam_dir.to_string_lossy().to_string())
    } else {
        None
    };

    let login_state = detect_login_state();

    let mac_paths = vec![
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
        home.join("Library/Application Support/Steam/steamapps"),
    ];
    let mac_installed = mac_paths.iter().any(|p| p.exists());

    let running = is_wine_steam_running();
    let ms_available = ms_wine().exists();

    json!({
        "installed": windows_installed,
        "path": windows_path,
        "login_state": login_state,
        "mac_installed": mac_installed,
        "running": running,
        "metalsharp_wine_available": ms_available
    })
}

pub fn is_wine_steam_running() -> bool {
    Command::new("pgrep")
        .args(["-f", "Steam.exe"])
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
        || Command::new("pgrep")
            .args(["-f", "steam.exe"])
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false)
}

pub fn launch_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let wine = ms_wine();
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let exe = steam_exe_path();
    let steam_dir = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam");

    if !exe.exists() || !steam_dir.join("steamui.dll").exists() {
        return Err("Steam is not installed — use the setup wizard to install it first".into());
    }

    if is_wine_steam_running() {
        return Ok(json!({"ok": true, "message": "Steam already running"}));
    }

    let prefix_str = steam_prefix().to_string_lossy().to_string();

    let ms_root = dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("runtime")
        .join("wine");

    let child = Command::new(&wine)
        .current_dir(&steam_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", ms_root.join("lib").to_string_lossy().to_string())
        .env("STEAM_RUNTIME", "0")
        .arg(&exe)
        .args(["-no-cef-sandbox", "-noverifyfiles", "-no-dwrite"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": child.id()}))
}

pub fn stop_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let targets = [
        "Steam.exe", "steam.exe", "steamservice.exe",
        "steamwebhelper.exe", "winedevice.exe",
    ];

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

    let still_running = Command::new("pgrep")
        .args(["-f", "Steam.exe"])
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false);

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

    let ms_root = dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("runtime")
        .join("wine");

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", ms_root.join("lib").to_string_lossy().to_string())
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

    let ms_root = dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("runtime")
        .join("wine");

    Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", ms_root.join("lib").to_string_lossy().to_string())
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "appid": appid}))
}

pub fn get_wine_steam_installed_games() -> Vec<u32> {
    let steamapps = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

    let mut appids = Vec::new();
    if let Ok(entries) = std::fs::read_dir(&steamapps) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.starts_with("appmanifest_") && name.ends_with(".acf") {
                if let Some(id_str) = name
                    .strip_prefix("appmanifest_")
                    .and_then(|s| s.strip_suffix(".acf"))
                {
                    if let Ok(id) = id_str.parse::<u32>() {
                        appids.push(id);
                    }
                }
            }
        }
    }
    appids
}

fn get_game_name_from_manifest(appid: u32) -> Option<String> {
    let manifest_path = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps")
        .join(format!("appmanifest_{}.acf", appid));

    let contents = std::fs::read_to_string(&manifest_path).ok()?;
    for line in contents.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("\"name\"") {
            if let Some((_, val)) = trimmed.split_once('\t') {
                return Some(val.trim().trim_matches('"').to_string());
            }
        }
    }
    None
}

pub fn uninstall_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let steamapps = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

    let _ = crate::launch::kill_game(appid);

    let manifest_path = steamapps.join(format!("appmanifest_{}.acf", appid));
    if manifest_path.exists() {
        let contents = std::fs::read_to_string(&manifest_path).unwrap_or_default();
        let install_dir = contents
            .lines()
            .find(|l| l.contains("\"installdir\""))
            .and_then(|l| {
                let parts: Vec<&str> = l.splitn(2, |c: char| c == '\t' || c == ' ').collect();
                parts.last().map(|s| s.trim().trim_matches('"').to_string())
            });

        if let Some(dir_name) = install_dir {
            let game_dir = steamapps.join("common").join(&dir_name);
            if game_dir.exists() {
                let _ = std::fs::remove_dir_all(&game_dir);
            }
        }
        let _ = std::fs::remove_file(&manifest_path);
    }

    let local_dir = home
        .join(".metalsharp")
        .join("games")
        .join(appid.to_string());
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
    let mac_path = home.join("Library/Application Support/Steam/config/loginusers.vdf");
    let contents = std::fs::read_to_string(&mac_path).ok()?;
    for line in contents.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('"') && trimmed.chars().filter(|c| *c == '"').count() == 2 {
            let id = trimmed.trim_matches('"').trim();
            if id.starts_with("7656") {
                return Some(id.to_string());
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
                let name =
                    get_game_name_from_manifest(appid).unwrap_or_else(|| format!("Game {}", appid));
                fallback.push((appid, name));
            }
            for &appid in &downloaded_appids {
                if !fallback.iter().any(|(id, _)| *id == appid) {
                    fallback.push((appid, format!("Game {}", appid)));
                }
            }
            fallback
        }
    };

    let games: Vec<Value> = owned
        .iter()
        .map(|(appid, name)| {
            let is_installed = installed_appids.contains(appid)
                || downloaded_appids.contains(appid)
                || wine_steam_appids.contains(appid);
            json!({
                "appid": appid,
                "name": name,
                "installed": is_installed,
                "state": if is_installed { "installed" } else { "not_installed" },
                "launch_method": crate::launch::recommended_method_for_appid(*appid),
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
                        .any(|e| {
                            e.path()
                                .extension()
                                .map(|ext| ext == "exe")
                                .unwrap_or(false)
                        });
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
            let key = cfg
                .get("steam_api_key")
                .and_then(|v| v.as_str())
                .map(String::from);
            let sid = cfg
                .get("steam_id")
                .and_then(|v| v.as_str())
                .map(String::from);
            return (key, sid);
        }
    }
    (None, get_steam_id())
}

fn fetch_owned_games(
    _steam_id: Option<&str>,
) -> Result<Vec<(u32, String)>, Box<dyn std::error::Error>> {
    let (api_key, steam_id) = read_steam_config();
    let key = api_key.as_deref().unwrap_or("");
    let sid = steam_id.as_deref().or(_steam_id).unwrap_or("");

    if key.is_empty() || sid.is_empty() {
        return Ok(vec![]);
    }

    let cache_path = dirs::home_dir()
        .map(|h| h.join(".metalsharp/cache/owned_games.json"))
        .unwrap_or_default();

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

    let output = Command::new("curl")
        .args(["-sL", "-m", "15", &url])
        .output()?;

    if !output.status.success() {
        return Err("curl failed".into());
    }

    let body: Value = serde_json::from_slice(&output.stdout)?;

    let games_arr = body
        .get("response")
        .and_then(|r| r.get("games"))
        .and_then(|g| g.as_array());

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
    let arr: Vec<Value> = games
        .iter()
        .map(|(id, name)| json!({"appid": id, "name": name}))
        .collect();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    std::fs::write(
        path,
        serde_json::to_string_pretty(&json!({"timestamp": now, "games": arr}))?,
    )?;
    Ok(())
}

fn get_installed_appids() -> Vec<u32> {
    let home = dirs::home_dir().unwrap_or_default();
    let mut appids = Vec::new();

    let steamapps_dirs = vec![
        home.join("Library/Application Support/Steam/steamapps"),
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
    ];

    for dir in steamapps_dirs {
        if let Ok(entries) = std::fs::read_dir(&dir) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_string();
                if name.starts_with("appmanifest_") && name.ends_with(".acf") {
                    if let Some(id_str) = name
                        .strip_prefix("appmanifest_")
                        .and_then(|s| s.strip_suffix(".acf"))
                    {
                        if let Ok(id) = id_str.parse::<u32>() {
                            appids.push(id);
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
    let wine_path =
        home.join(".metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/config/loginusers.vdf");

    let contents = std::fs::read_to_string(&mac_path)
        .or_else(|_| std::fs::read_to_string(&wine_path))
        .unwrap_or_default();

    if contents.is_empty() {
        return json!({"state": "unknown", "account": null});
    }

    let mut accounts: Vec<Value> = Vec::new();

    for line in contents.lines() {
        let trimmed = line.trim();
        if let Some(name) = parse_vdf_value(trimmed, "PersonaName") {
            let remembered = contents
                .lines()
                .any(|l| l.contains("RememberPassword") && l.contains("1"));
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
    let home = dirs::home_dir().ok_or("no home dir")?;
    let metalsharp_dir = home.join(".metalsharp");
    std::fs::create_dir_all(&metalsharp_dir)?;

    let steam_dir = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam");

    if steam_dir.join("steamui.dll").exists() && steam_exe_path().exists() {
        return Ok("Steam already installed".into());
    }

    let _ = std::fs::remove_dir_all(steam_prefix());

    let installer = metalsharp_dir.join("SteamSetup.exe");
    let _ = std::fs::remove_file(&installer);

    let url = "https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe";
    let output = Command::new("curl")
        .args(["-sL", "-o", &installer.to_string_lossy(), url])
        .status()?;
    if !output.success() {
        let bundled = PathBuf::from("app/bundles/SteamSetup.exe");
        if bundled.exists() {
            let _ = std::fs::copy(&bundled, &installer);
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

    let ms_root = dirs::home_dir()
        .unwrap_or_default()
        .join(".metalsharp")
        .join("runtime")
        .join("wine");
    let dyld = ms_root.join("lib").to_string_lossy().to_string();

    let _ = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg(&installer)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(format!("Launched Steam installer via MetalSharp Wine (pid {}) — complete the setup wizard, then launch Steam again", child.id()))
}

pub fn watch_steamapps() -> Option<String> {
    let steamapps = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

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

    let _ = std::fs::write(
        &cached_path,
        current.iter().map(|id| id.to_string()).collect::<Vec<_>>().join("\n"),
    );

    if new_appids.is_empty() {
        None
    } else {
        Some(serde_json::to_string(&new_appids).unwrap_or_default())
    }
}
