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

    let login_state = detect_login_state(&wine_steam_dir);

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
        "loginState": login_state,
        "macInstalled": mac_installed,
        "steamCmdPath": steamcmd,
        "running": running
    })
}

fn detect_login_state(steam_dir: &PathBuf) -> Value {
    let loginusers_path = steam_dir.join("config").join("loginusers.vdf");

    if !loginusers_path.exists() {
        return json!({"state": "unknown", "account": null});
    }

    let contents = match std::fs::read_to_string(&loginusers_path) {
        Ok(c) => c,
        Err(_) => return json!({"state": "unknown", "account": null}),
    };

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

pub fn download_game(appid: u32) -> Result<Vec<serde_json::Value>, Box<dyn std::error::Error>> {
    let steamcmd = which_steamcmd().ok_or("steamcmd not found")?;

    let home = dirs::home_dir().ok_or("no home dir")?;
    let install_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    std::fs::create_dir_all(&install_dir)?;

    let progress_file = home.join(".metalsharp").join("download_progress.json");

    let mut child = Command::new(&steamcmd)
        .args([
            "+@sSteamCmdForcePlatformType",
            "windows",
            "+force_install_dir",
            install_dir.to_str().unwrap_or(""),
            "+login",
            "anonymous",
            "+app_update",
            &appid.to_string(),
            "validate",
            "+quit",
        ])
        .stderr(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()?;

    if let Some(stdout) = child.stdout.take() {
        use std::io::{BufRead, BufReader};
        let reader = BufReader::new(stdout);
        let progress = progress_file.clone();
        let aid = appid;

        std::thread::spawn(move || {
            for line in reader.lines().flatten() {
                let pct = parse_progress_line(&line);
                if pct.is_some() {
                    let json = serde_json::json!({
                        "appId": aid,
                        "progress": pct,
                        "line": line,
                    });
                    let _ = std::fs::write(&progress, json.to_string());
                }
            }
        });
    }

    let output = child.wait_with_output()?;
    let _ = std::fs::remove_file(&progress_file);

    if !output.status.success() {
        return Err(format!(
            "steamcmd failed: {}",
            String::from_utf8_lossy(&output.stderr)
        )
        .into());
    }

    Ok(scan_downloaded_dir(&install_dir, appid))
}

fn parse_progress_line(line: &str) -> Option<f64> {
    let lower = line.to_lowercase();
    if !lower.contains("progress:") {
        return None;
    }
    let start = lower.find("progress:")? + "progress:".len();
    let rest = &lower[start..].trim_start();
    let end = rest.find(|c: char| !c.is_ascii_digit() && c != '.')?;
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
