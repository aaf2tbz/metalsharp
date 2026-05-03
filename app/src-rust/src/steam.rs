use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::Command;

pub fn status() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let wine_steam_dir = home
        .join(".metalsharp")
        .join("prefix")
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

    let steamcmd = which_steamcmd();

    let running = Command::new("pgrep")
        .args(["-f", "steam"])
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false);

    json!({
        "installed": windows_installed,
        "path": windows_path,
        "login_state": login_state,
        "mac_installed": mac_installed,
        "steam_cmd_path": steamcmd,
        "running": running
    })
}

pub fn steamcmd_status() -> Value {
    let steamcmd = which_steamcmd();
    let home = dirs::home_dir().unwrap_or_default();
    let config_path = home.join(".metalsharp/cache/steam_config.json");

    let logged_in = if config_path.exists() {
        std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str::<serde_json::Map<String, Value>>(&s).ok())
            .and_then(|m| m.get("steamcmd_logged_in").and_then(|v| v.as_bool()))
            .unwrap_or(false)
    } else {
        false
    };

    let username = if config_path.exists() {
        std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str::<serde_json::Map<String, Value>>(&s).ok())
            .and_then(|m| m.get("steam_username").and_then(|v| v.as_str()).map(String::from))
    } else {
        None
    };

    json!({
        "ok": true,
        "steamcmd_path": steamcmd,
        "logged_in": logged_in,
        "username": username,
    })
}

pub fn steamcmd_login(username: &str, password: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let steamcmd = which_steamcmd().ok_or("steamcmd not found")?;
    let home = dirs::home_dir().ok_or("no home dir")?;

    use std::io::BufRead;
    use std::process::Stdio;

    let mut child = Command::new(&steamcmd)
        .args(["+login", username, password, "+quit"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;

    let stdout = child.stdout.take().ok_or("no stdout")?;
    let stderr = child.stderr.take().ok_or("no stderr")?;

    let stderr_handle = std::thread::spawn(move || {
        let reader = std::io::BufReader::new(stderr);
        let mut combined = String::new();
        for line in reader.lines().flatten() {
            combined.push_str(&line);
            combined.push('\n');
        }
        combined
    });

    let reader = std::io::BufReader::new(stdout);
    let mut combined_output = String::new();
    let mut logged_in = false;
    let mut login_failed = false;
    let mut failure_reason = String::new();

    for line in reader.lines().flatten() {
        combined_output.push_str(&line);
        combined_output.push('\n');

        let lower = line.to_lowercase();

        if lower.contains("logged in ok") || lower.contains("waiting for user info...ok") {
            logged_in = true;
            break;
        }

        if lower.contains("invalid password") {
            login_failed = true;
            failure_reason = "Invalid password".into();
            break;
        }
        if lower.contains("invalid login") {
            login_failed = true;
            failure_reason = "Invalid login credentials".into();
            break;
        }
    }

    drop(child.stdin.take());
    let _ = child.wait();

    let stderr_combined = stderr_handle.join().unwrap_or_default();
    let combined = format!("{}{}", combined_output, stderr_combined);

    let logged_in = logged_in
        || combined.contains("Logged in OK")
        || combined.contains("Logged in user")
        || combined.contains("Waiting for user info...OK");

    let login_failed = login_failed
        || combined.contains("Invalid Password")
        || combined.contains("Invalid Login");

    if logged_in && !login_failed {
        let config_dir = home.join(".metalsharp/cache");
        std::fs::create_dir_all(&config_dir)?;
        let config_path = config_dir.join("steam_config.json");

        let mut config: serde_json::Map<String, Value> = if config_path.exists() {
            std::fs::read_to_string(&config_path)
                .ok()
                .and_then(|s| serde_json::from_str(&s).ok())
                .unwrap_or_default()
        } else {
            serde_json::Map::new()
        };

        config.insert("steamcmd_logged_in".into(), json!(true));
        config.insert("steam_username".into(), json!(username));
        config.insert("steam_password".into(), json!(password));

        if let Some(steam_id) = get_steam_id() {
            config.insert("steam_id".into(), json!(steam_id));
        }

        std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;

        Ok(json!({"ok": true, "username": username}))
    } else {
        let reason = if login_failed && !failure_reason.is_empty() {
            failure_reason
        } else if combined.contains("Invalid Password") {
            "Invalid password".into()
        } else if combined.contains("Invalid Login") {
            "Invalid login credentials".into()
        } else if combined.contains("Steam Guard") || combined.contains("verification") {
            "Steam Guard verification failed — try again and approve the prompt quickly".into()
        } else {
            "Login failed — check your credentials".into()
        };
        Ok(json!({"ok": false, "error": reason}))
    }
}

pub fn steamcmd_logout() -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let config_path = home.join(".metalsharp/cache/steam_config.json");

    if config_path.exists() {
        let mut config: serde_json::Map<String, Value> = std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or_default();
        config.insert("steamcmd_logged_in".into(), json!(false));
        config.remove("steam_username");
        std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;
    }

    Ok(json!({"ok": true}))
}

pub fn get_steamcmd_username() -> Option<String> {
    let home = dirs::home_dir()?;
    let config_path = home.join(".metalsharp/cache/steam_config.json");
    let config = std::fs::read_to_string(&config_path).ok()?;
    let map: serde_json::Map<String, Value> = serde_json::from_str(&config).ok()?;
    let logged_in = map.get("steamcmd_logged_in").and_then(|v| v.as_bool())?;
    if !logged_in { return None; }
    map.get("steam_username").and_then(|v| v.as_str()).map(String::from)
}

fn get_steamcmd_password() -> Option<String> {
    let home = dirs::home_dir()?;
    let config_path = home.join(".metalsharp/cache/steam_config.json");
    let config = std::fs::read_to_string(&config_path).ok()?;
    let map: serde_json::Map<String, Value> = serde_json::from_str(&config).ok()?;
    map.get("steam_password").and_then(|v| v.as_str()).map(String::from)
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

    let owned: Vec<(u32, String)> = match fetch_owned_games(get_steam_id().as_deref()) {
        Ok(games) => games,
        Err(_) => vec![],
    };

    let games: Vec<Value> = owned
        .iter()
        .map(|(appid, name)| {
            let is_installed = installed_appids.contains(appid) || downloaded_appids.contains(appid);
            json!({
                "appid": appid,
                "name": name,
                "installed": is_installed,
                "state": if is_installed { "installed" } else { "not_installed" },
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
            if !path.is_dir() { continue; }
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
                            return Ok(arr.iter().filter_map(|g| {
                                let id = g.get("appid")?.as_u64()? as u32;
                                let name = g.get("name")?.as_str()?.to_string();
                                Some((id, name))
                            }).collect());
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
        Some(arr) => arr.iter().filter_map(|g| {
            let id = g.get("appid")?.as_u64()? as u32;
            let name = g.get("name")?.as_str()?.to_string();
            Some((id, name))
        }).collect(),
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
    std::fs::write(path, serde_json::to_string_pretty(&json!({"timestamp": now, "games": arr}))?)?;
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
    let wine_path = home
        .join(".metalsharp/prefix/drive_c/Program Files (x86)/Steam/config/loginusers.vdf");

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

fn which_steamcmd() -> Option<String> {
    let home = dirs::home_dir()?;

    let candidates = vec![
        home.join(".steam/steamcmd/steamcmd.sh"),
        home.join("steamcmd/steamcmd.sh"),
        PathBuf::from("/usr/local/bin/steamcmd"),
        PathBuf::from("/opt/homebrew/bin/steamcmd"),
    ];

    for c in candidates {
        if c.exists() {
            return Some(c.to_string_lossy().to_string());
        }
    }

    Command::new("which")
        .arg("steamcmd")
        .output()
        .ok()
        .and_then(|o| {
            if o.status.success() {
                Some(String::from_utf8_lossy(&o.stdout).trim().to_string())
            } else {
                None
            }
        })
}

pub fn install_steam() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let metalsharp_dir = home.join(".metalsharp");
    std::fs::create_dir_all(&metalsharp_dir)?;

    let installer = metalsharp_dir.join("SteamSetup.exe");

    if !installer.exists() {
        let url = "https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe";
        let mut resp = reqwest_https_get(url)?;
        let mut file = std::fs::File::create(&installer)?;
        std::io::copy(&mut resp, &mut file)?;
    }

    let metalsharp_bin = find_metalsharp_launcher()?;

    let child = Command::new(metalsharp_bin)
        .arg(&installer)
        .spawn()?;

    Ok(format!("Launched Steam installer (pid {})", child.id()))
}

fn reqwest_https_get(url: &str) -> Result<Box<dyn std::io::Read>, Box<dyn std::error::Error>> {
    let output = Command::new("curl")
        .args(["-sL", "-o", "-", url])
        .stdout(std::process::Stdio::piped())
        .spawn()?;

    match output.stdout {
        Some(out) => Ok(Box::new(out)),
        None => Err("curl failed to start".into()),
    }
}

fn find_metalsharp_launcher() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        home.join("metalsharp/build/metalsharp_launcher"),
        home.join("metalsharp/build/tools/launcher/metalsharp_launcher"),
        PathBuf::from("/usr/local/bin/metalsharp_launcher"),
        PathBuf::from("/opt/homebrew/bin/metalsharp_launcher"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    let which = Command::new("which")
        .arg("metalsharp_launcher")
        .output()?;

    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("metalsharp_launcher not found".into())
}

pub fn download_game(appid: u32, password: Option<&str>) -> Result<Value, Box<dyn std::error::Error>> {
    let steamcmd = which_steamcmd().ok_or("steamcmd not found")?;

    let home = dirs::home_dir().ok_or("no home dir")?;
    let install_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    std::fs::create_dir_all(&install_dir)?;

    let progress_file = home.join(".metalsharp").join("download_progress.json");

    let _ = std::fs::write(&progress_file, serde_json::json!({
        "appId": appid,
        "progress": 0.0,
        "status": "downloading",
    }).to_string());

    let cmd = steamcmd;
    let dir = install_dir.clone();
    let pf = progress_file.clone();
    let pw = password.map(String::from).or_else(|| get_steamcmd_password());

    std::thread::spawn(move || {
        let username = get_steamcmd_username().unwrap_or_else(|| "anonymous".into());
        let login_args: Vec<String> = if let Some(ref p) = pw {
            vec!["+login".into(), username.clone(), p.clone()]
        } else {
            vec!["+login".into(), username.clone()]
        };
        let mut child = match Command::new(&cmd)
            .args([
                "+@sSteamCmdForcePlatformType",
                "windows",
                "+force_install_dir",
                dir.to_str().unwrap_or(""),
            ])
            .args(&login_args)
            .args([
                "+app_update",
                &appid.to_string(),
                "validate",
                "+quit",
            ])
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
        {
            Ok(c) => c,
            Err(_) => {
                let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                return;
            }
        };

        if let Some(stdout) = child.stdout.take() {
            use std::io::BufRead;
            let reader = std::io::BufReader::new(stdout);
            for line in reader.lines().flatten() {
                if let Some(pct) = parse_progress_line(&line) {
                    let _ = std::fs::write(&pf, json!({
                        "appId": appid,
                        "progress": pct,
                        "status": "downloading",
                    }).to_string());
                }
            }
        }

        let _ = child.wait();

        let _ = std::fs::write(&pf, json!({
            "appId": appid,
            "progress": 100.0,
            "status": "setting_up",
        }).to_string());

        let has_exe = walkdir::WalkDir::new(&dir)
            .max_depth(3)
            .into_iter()
            .flatten()
            .any(|e| e.path().extension().map(|ext| ext == "exe").unwrap_or(false));

        if has_exe {
            let _ = crate::setup::prepare_game(appid);
            let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 100.0, "status": "complete"}).to_string());
        } else {
            let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
        }
    });

    Ok(json!({"ok": true, "appId": appid, "status": "started"}))
}

fn parse_progress_line(line: &str) -> Option<f64> {
    let lower = line.to_lowercase();
    if lower.contains("fully installed") || lower.contains("success") {
        return Some(100.0);
    }
    if !lower.contains("progress") {
        return None;
    }
    let idx = lower.find("progress")?;
    let rest = &lower[idx + 8..].trim_start_matches(|c: char| c == ':' || c == ' ' || c == '=');
    let end = rest.find(|c: char| !c.is_ascii_digit() && c != '.').unwrap_or(rest.len());
    if end == 0 {
        return None;
    }
    rest[..end].parse().ok()
}

fn scan_downloaded_dir(dir: &PathBuf, _appid: u32) -> Vec<serde_json::Value> {
    let mut results = Vec::new();
    let mut exe_name = String::new();

    for entry in walkdir::WalkDir::new(dir).max_depth(4).into_iter().flatten() {
        if let Some(ext) = entry.path().extension() {
            if ext == "exe" {
                let name = entry.file_name().to_string_lossy().to_string();
                results.push(json!({
                    "name": name,
                    "exePath": entry.path().to_string_lossy().to_string()
                }));
                if exe_name.is_empty() {
                    exe_name = name;
                }
            }
        }
    }

    results
}
