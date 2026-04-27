use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::Command;

pub fn status() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let wine_steam_exe = home
        .join(".metalsharp")
        .join("prefix")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("Steam.exe");

    let wine_steam_dir = wine_steam_exe.parent().map(|p| p.to_path_buf());
    let windows_installed = wine_steam_exe.exists();
    let windows_path = if windows_installed {
        wine_steam_dir.as_ref().map(|p| p.to_string_lossy().to_string())
    } else {
        None
    };

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
        "macInstalled": mac_installed,
        "steamCmdPath": steamcmd,
        "running": running
    })
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

    let output = Command::new(&steamcmd)
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
        .output()?;

    if !output.status.success() {
        return Err(format!(
            "steamcmd failed: {}",
            String::from_utf8_lossy(&output.stderr)
        )
        .into());
    }

    Ok(scan_downloaded_dir(&install_dir, appid))
}

fn scan_downloaded_dir(dir: &PathBuf, appid: u32) -> Vec<serde_json::Value> {
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
