use serde_json::{json, Value};
use std::path::PathBuf;
use walkdir::WalkDir;

#[derive(serde::Serialize, Clone)]
pub struct Game {
    pub id: String,
    pub name: String,
    pub exe_path: String,
    pub platform: String,
    pub steam_app_id: Option<u32>,
    pub size_bytes: Option<u64>,
    pub metalsharp_compatible: bool,
}

pub fn scan_all() -> Result<Value, Box<dyn std::error::Error>> {
    let mut games: Vec<Game> = Vec::new();

    if let Some(steam) = detect_windows_steam() {
        games.push(steam);
    }

    if let Ok(steam_games) = scan_steam_library() {
        games.extend(steam_games);
    }

    if let Ok(local_games) = scan_local_exes() {
        games.extend(local_games);
    }

    let steam_status = super::steam::status();
    Ok(json!({
        "ok": true,
        "data": {
            "games": games,
            "steam": steam_status
        }
    }))
}

fn detect_windows_steam() -> Option<Game> {
    let home = dirs::home_dir()?;
    let steam_exe = home
        .join(".metalsharp")
        .join("prefix")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steam.exe");

    if !steam_exe.exists() {
        return None;
    }

    Some(Game {
        id: "windows_steam".into(),
        name: "Steam (Windows)".into(),
        exe_path: steam_exe.to_string_lossy().to_string(),
        platform: "steam".into(),
        steam_app_id: None,
        size_bytes: dir_size(&steam_exe.parent()?.to_path_buf()),
        metalsharp_compatible: true,
    })
}

fn steam_library_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    let home = dirs::home_dir().unwrap_or_default();

    let mac_candidates = vec![
        home.join("Library/Application Support/Steam/steamapps"),
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
    ];

    for mac_path in &mac_candidates {
        if mac_path.exists() {
            paths.push(mac_path.clone());
            paths.extend(parse_library_folders(mac_path));
        }
    }

    let wine_steam_dir = home
        .join(".metalsharp")
        .join("prefix")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam");

    let wine_steamapps = wine_steam_dir.join("steamapps");
    if wine_steamapps.exists() {
        paths.push(wine_steamapps.clone());
        paths.extend(parse_library_folders(&wine_steamapps));
    }

    paths
}

fn parse_library_folders(steamapps: &PathBuf) -> Vec<PathBuf> {
    let lf_path = steamapps.join("libraryfolders.vdf");
    let contents = match std::fs::read_to_string(&lf_path) {
        Ok(c) => c,
        Err(_) => return Vec::new(),
    };

    let mut extra = Vec::new();
    for line in contents.lines() {
        let trimmed = line.trim();
        if let Some(val) = parse_vdf_path(trimmed, "path") {
            let sa = PathBuf::from(val).join("steamapps");
            if sa.exists() && sa != *steamapps {
                extra.push(sa);
            }
        }
    }
    extra
}

fn parse_vdf_path(line: &str, key: &str) -> Option<String> {
    let prefix = format!("\"{}\"", key);
    if !line.starts_with(&prefix) {
        return None;
    }
    let rest = line.trim_start_matches(&prefix).trim();
    let rest = rest.trim_start_matches('\t').trim_start_matches(' ');
    let val = rest.trim_matches('"');
    if val.is_empty() {
        return None;
    }
    Some(val.replace("\\\\", "/"))
}

fn parse_acf(contents: &str) -> Option<(u32, String, String)> {
    let mut appid: Option<u32> = None;
    let mut name: Option<String> = None;
    let mut install_dir: Option<String> = None;

    for line in contents.lines() {
        let trimmed = line.trim();
        if let Some((k, v)) = parse_kv(trimmed) {
            match k {
                "appid" => appid = v.parse().ok(),
                "name" => name = Some(v.to_string()),
                "installdir" => install_dir = Some(v.to_string()),
                _ => {}
            }
        }
    }

    match (appid, name, install_dir) {
        (Some(a), Some(n), Some(d)) => Some((a, n, d)),
        _ => None,
    }
}

fn parse_kv(line: &str) -> Option<(&str, &str)> {
    let line = line.trim_start_matches('"');
    let parts: Vec<&str> = line.splitn(2, "\"\t\t\"").collect();
    if parts.len() == 2 {
        let key = parts[0].trim();
        let val = parts[1].trim_end_matches('"');
        Some((key, val))
    } else {
        None
    }
}

fn scan_steam_library() -> Result<Vec<Game>, Box<dyn std::error::Error>> {
    let mut games = Vec::new();

    for lib in steam_library_paths() {
        let entries = std::fs::read_dir(&lib)?;
        for entry in entries.flatten() {
            let path = entry.path();
            let fname = path.file_name().unwrap_or_default().to_string_lossy();
            if !fname.starts_with("appmanifest_") || !fname.ends_with(".acf") {
                continue;
            }

            let contents = std::fs::read_to_string(&path)?;
            if let Some((appid, name, install_dir)) = parse_acf(&contents) {
                let game_path = lib.join("common").join(&install_dir);
                let exe = find_exe_in_dir(&game_path);

                games.push(Game {
                    id: format!("steam_{}", appid),
                    name,
                    exe_path: exe.unwrap_or_default(),
                    platform: "steam".into(),
                    steam_app_id: Some(appid),
                    size_bytes: dir_size(&game_path),
                    metalsharp_compatible: true,
                });
            }
        }
    }

    Ok(games)
}

fn is_valid_game_exe(name: &str) -> bool {
    let lower = name.to_lowercase();
    !lower.contains("setup")
        && !lower.contains("redist")
        && !lower.contains("dotnet")
        && !lower.contains("installer")
        && !lower.contains("uninstall")
        && !lower.contains("vcredist")
        && !lower.contains("crashhandler")
        && !lower.contains("server")
}

fn find_exe_in_dir(dir: &PathBuf) -> Option<String> {
    let mut best: Option<String> = None;
    for entry in WalkDir::new(dir).max_depth(3).into_iter().flatten() {
        if let Some(ext) = entry.path().extension() {
            if ext == "exe" {
                let name = entry.file_name().to_string_lossy().to_string();
                if !is_valid_game_exe(&name) {
                    continue;
                }
                let lower = name.to_lowercase();
                let matches_name = lower.starts_with("rain") || lower.starts_with("terraria") || lower.starts_with("hl2") || lower == "game.exe";
                if matches_name {
                    return Some(entry.path().to_string_lossy().to_string());
                }
                if best.is_none() {
                    best = Some(entry.path().to_string_lossy().to_string());
                }
            }
        }
    }
    best
}

fn dir_size(dir: &PathBuf) -> Option<u64> {
    let mut total: u64 = 0;
    for entry in WalkDir::new(dir).into_iter().flatten() {
        if let Ok(m) = entry.metadata() {
            if m.is_file() {
                total += m.len();
            }
        }
    }
    Some(total)
}

fn scan_local_exes() -> Result<Vec<Game>, Box<dyn std::error::Error>> {
    let mut games = Vec::new();
    let home = dirs::home_dir().unwrap_or_default();
    let metalsharp_dir = home.join(".metalsharp").join("games");

    if !metalsharp_dir.exists() {
        return Ok(games);
    }

    for entry in std::fs::read_dir(&metalsharp_dir)?.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let name = path.file_name().unwrap_or_default().to_string_lossy().to_string();
        if let Some(exe) = find_exe_in_dir(&path) {
            games.push(Game {
                id: format!("local_{}", name),
                name,
                exe_path: exe,
                platform: "local".into(),
                steam_app_id: None,
                size_bytes: dir_size(&path),
                metalsharp_compatible: true,
            });
        }
    }

    Ok(games)
}
