use serde_json::{json, Value};
use std::path::{Path, PathBuf};
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

#[derive(Debug, Clone)]
pub struct DualGameDir {
    pub appid: u32,
    pub macos_dir: Option<PathBuf>,
    pub wine_dir: Option<PathBuf>,
    pub macos_app: Option<PathBuf>,
    pub has_native_build: bool,
}

pub fn macos_steam_library_paths() -> Vec<PathBuf> {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates = vec![
        home.join("Library/Application Support/Steam/steamapps"),
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
    ];
    let mut paths = Vec::new();
    for mac_path in &candidates {
        if mac_path.exists() {
            paths.push(mac_path.clone());
            paths.extend(parse_library_folders(mac_path));
        }
    }
    paths
}

pub fn resolve_dual_game_dir(appid: u32) -> DualGameDir {
    let manifest_name = format!("appmanifest_{}.acf", appid);
    let mut macos_dir: Option<PathBuf> = None;
    let mut wine_dir: Option<PathBuf> = None;
    let mut install_dir_name: Option<String> = None;

    for steamapps in macos_steam_library_paths() {
        let manifest_path = steamapps.join(&manifest_name);
        if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
            if let Some(dir_name) = parse_installdir_from_acf(&contents) {
                let dir = steamapps.join("common").join(&dir_name);
                if dir.exists() {
                    macos_dir = Some(dir);
                    install_dir_name = Some(dir_name);
                    break;
                }
            }
        }
    }

    for steamapps in wine_steam_library_paths() {
        if let Some(ref dir_name) = install_dir_name {
            let dir = steamapps.join("common").join(dir_name);
            if dir.exists() {
                wine_dir = Some(dir);
                break;
            }
        } else {
            let manifest_path = steamapps.join(&manifest_name);
            if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
                if let Some(dir_name) = parse_installdir_from_acf(&contents) {
                    let dir = steamapps.join("common").join(&dir_name);
                    if dir.exists() {
                        wine_dir = Some(dir);
                        let _ = install_dir_name.insert(dir_name);
                        break;
                    }
                }
            }
        }
    }

    let macos_app = macos_dir.as_ref().and_then(|d| find_macos_app(d));
    let has_native_build = macos_app.is_some();

    DualGameDir { appid, macos_dir, wine_dir, macos_app, has_native_build }
}

pub fn find_macos_app(dir: &Path) -> Option<PathBuf> {
    for entry in std::fs::read_dir(dir).ok()? {
        let entry = entry.ok()?;
        let path = entry.path();
        if path.extension().map(|e| e == "app").unwrap_or(false) && path.is_dir() {
            return Some(path);
        }
    }
    for entry in WalkDir::new(dir).max_depth(2).into_iter().flatten() {
        let path = entry.path();
        if path.extension().map(|e| e == "app").unwrap_or(false) && path.is_dir() {
            return Some(path.to_path_buf());
        }
    }
    None
}

fn parse_installdir_from_acf(contents: &str) -> Option<String> {
    for line in contents.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("\"installdir\"") {
            let parts: Vec<&str> = trimmed.splitn(2, ['\t', ' ']).collect();
            return parts.last().map(|s| s.trim().trim_matches('"').to_string());
        }
    }
    None
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
    let steam_exe =
        crate::platform::steam_prefix_dir().join("drive_c").join("Program Files (x86)").join("Steam").join("steam.exe");

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
    let mut paths = macos_steam_library_paths();
    paths.extend(wine_steam_library_paths());
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
    let unix_path = val.replace('\\', "/");
    Some(resolve_wine_path(&unix_path))
}

pub fn resolve_wine_path(path: &str) -> String {
    let p = path.replace('\\', "/");
    for drive in &["C:", "c:", "D:", "d:", "E:", "e:", "F:", "f:", "G:", "g:", "H:", "h:"] {
        if let Some(rest) = p.strip_prefix(drive) {
            if rest.starts_with('/') || rest.is_empty() {
                return format!("/{}", rest);
            }
            return rest.to_string();
        }
    }
    if p.starts_with("Z:/") || p.starts_with("z:/") || p.starts_with("Z:") || p.starts_with("z:") {
        return p[2..].to_string();
    }
    p
}

pub fn wine_steam_library_paths() -> Vec<PathBuf> {
    let wine_steamapps =
        crate::platform::steam_prefix_dir().join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");
    let legacy_steamapps = dirs::home_dir().map(|home| {
        home.join(".metalsharp")
            .join("prefix-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam")
            .join("steamapps")
    });

    collect_wine_steam_library_paths(wine_steamapps, legacy_steamapps)
}

fn collect_wine_steam_library_paths(wine_steamapps: PathBuf, legacy_steamapps: Option<PathBuf>) -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if wine_steamapps.exists() {
        paths.push(wine_steamapps.clone());
        paths.extend(parse_library_folders(&wine_steamapps));
    }

    if let Some(legacy_steamapps) = legacy_steamapps {
        if legacy_steamapps.exists() && legacy_steamapps != wine_steamapps {
            paths.push(legacy_steamapps.clone());
            paths.extend(parse_library_folders(&legacy_steamapps));
        }
    }

    paths.sort();
    paths.dedup();
    paths
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
                _ => {},
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
                let matches_name = lower.starts_with("rain")
                    || lower.starts_with("terraria")
                    || lower.starts_with("hl2")
                    || lower == "game.exe";
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
    let metalsharp_dir = crate::platform::metalsharp_home().join("games");

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

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn wine_libraries_include_legacy_prefix_when_active_home_moved() {
        let root = test_dir("legacy-wine-library");
        let active_steamapps = root.join("active").join("steamapps");
        let legacy_steamapps = root.join("legacy").join("steamapps");
        let external_library = root.join("external");

        fs::create_dir_all(&legacy_steamapps).expect("create legacy steamapps");
        fs::create_dir_all(external_library.join("steamapps")).expect("create external steamapps");
        fs::write(
            legacy_steamapps.join("libraryfolders.vdf"),
            format!(
                "\"libraryfolders\"\n{{\n  \"1\"\n  {{\n    \"path\"\t\t\"{}\"\n  }}\n}}\n",
                external_library.to_string_lossy()
            ),
        )
        .expect("write libraryfolders");

        let paths = collect_wine_steam_library_paths(active_steamapps, Some(legacy_steamapps.clone()));

        assert!(paths.contains(&legacy_steamapps));
        assert!(paths.contains(&external_library.join("steamapps")));
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn wine_libraries_deduplicate_active_and_legacy_prefix() {
        let root = test_dir("dedupe-wine-library");
        let steamapps = root.join("steamapps");
        fs::create_dir_all(&steamapps).expect("create steamapps");

        let paths = collect_wine_steam_library_paths(steamapps.clone(), Some(steamapps.clone()));

        assert_eq!(paths, vec![steamapps]);
        let _ = fs::remove_dir_all(root);
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-scan-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
