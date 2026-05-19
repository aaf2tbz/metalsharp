use serde_json::{json, Value};
use std::collections::HashSet;
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NonSteamShortcut {
    pub name: String,
    pub exe_path: PathBuf,
    pub start_dir: Option<PathBuf>,
    pub launch_args: Vec<String>,
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

    for shortcut in scan_non_steam_shortcuts() {
        games.push(Game {
            id: format!("non_steam_{}", shortcut_id(&shortcut.name, &shortcut.exe_path)),
            name: shortcut.name,
            exe_path: shortcut.exe_path.to_string_lossy().to_string(),
            platform: "non_steam".into(),
            steam_app_id: None,
            size_bytes: shortcut.start_dir.as_ref().and_then(dir_size),
            metalsharp_compatible: true,
        });
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
        .join("prefix-steam")
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
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return Vec::new(),
    };

    let wine_steamapps = home
        .join(".metalsharp")
        .join("prefix-steam")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

    if !wine_steamapps.exists() {
        return Vec::new();
    }

    let mut paths = vec![wine_steamapps.clone()];
    paths.extend(parse_library_folders(&wine_steamapps));
    paths
}

pub fn scan_non_steam_shortcuts() -> Vec<NonSteamShortcut> {
    let mut shortcuts = Vec::new();
    let mut seen = HashSet::new();

    for path in shortcut_vdf_paths() {
        let Ok(data) = std::fs::read(&path) else {
            continue;
        };
        for shortcut in parse_shortcuts_vdf(&data) {
            let key = shortcut.exe_path.to_string_lossy().to_ascii_lowercase();
            if shortcut.exe_path.exists() && seen.insert(key) {
                shortcuts.push(shortcut);
            }
        }
    }

    shortcuts
}

fn shortcut_vdf_paths() -> Vec<PathBuf> {
    let home = dirs::home_dir().unwrap_or_default();
    let mut roots = vec![
        home.join("Library/Application Support/Steam/userdata"),
        home.join(".steam/steam/userdata"),
        home.join(".local/share/Steam/userdata"),
        home.join(".metalsharp")
            .join("prefix-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam")
            .join("userdata"),
    ];

    roots.retain(|root| root.exists());

    let mut paths = Vec::new();
    for root in roots {
        let Ok(users) = std::fs::read_dir(root) else {
            continue;
        };
        for user in users.flatten() {
            let path = user.path().join("config").join("shortcuts.vdf");
            if path.exists() {
                paths.push(path);
            }
        }
    }
    paths
}

fn parse_shortcuts_vdf(data: &[u8]) -> Vec<NonSteamShortcut> {
    let mut offset = 0usize;
    let mut current_name: Option<String> = None;
    let mut current_exe: Option<String> = None;
    let mut current_start_dir: Option<String> = None;
    let mut current_launch_options: Option<String> = None;
    let mut shortcut_depth: Option<usize> = None;
    let mut depth = 0usize;
    let mut shortcuts = Vec::new();

    while offset < data.len() {
        let field_type = data[offset];
        offset += 1;

        if field_type == 0x08 {
            if shortcut_depth == Some(depth) {
                if let Some(shortcut) = build_non_steam_shortcut(
                    current_name.take(),
                    current_exe.take(),
                    current_start_dir.take(),
                    current_launch_options.take(),
                ) {
                    shortcuts.push(shortcut);
                }
                shortcut_depth = None;
            }
            depth = depth.saturating_sub(1);
            continue;
        }

        let Some(key) = read_c_string(data, &mut offset) else {
            break;
        };

        match field_type {
            0x00 => {
                depth += 1;
                if depth == 2 && key.as_bytes().iter().all(u8::is_ascii_digit) {
                    shortcut_depth = Some(depth);
                    current_name = None;
                    current_exe = None;
                    current_start_dir = None;
                    current_launch_options = None;
                }
            },
            0x01 => {
                let Some(value) = read_c_string(data, &mut offset) else {
                    break;
                };
                if shortcut_depth == Some(depth) {
                    match key.as_str() {
                        "appname" => current_name = Some(value),
                        "exe" => current_exe = Some(value),
                        "StartDir" => current_start_dir = Some(value),
                        "LaunchOptions" => current_launch_options = Some(value),
                        _ => {},
                    }
                }
            },
            0x02 => {
                offset = offset.saturating_add(4);
            },
            _ => break,
        }
    }

    shortcuts
}

fn read_c_string(data: &[u8], offset: &mut usize) -> Option<String> {
    let start = *offset;
    while *offset < data.len() && data[*offset] != 0 {
        *offset += 1;
    }
    if *offset >= data.len() {
        return None;
    }
    let value = String::from_utf8_lossy(&data[start..*offset]).to_string();
    *offset += 1;
    Some(value)
}

fn build_non_steam_shortcut(
    name: Option<String>,
    exe: Option<String>,
    start_dir: Option<String>,
    launch_options: Option<String>,
) -> Option<NonSteamShortcut> {
    let name = name?.trim().to_string();
    if name.is_empty() {
        return None;
    }

    let exe = exe?;
    let exe_path = clean_shortcut_path(&exe)?;
    if exe_path.extension().map(|ext| ext.to_string_lossy().eq_ignore_ascii_case("exe")) != Some(true) {
        return None;
    }

    let start_dir = start_dir.and_then(|dir| clean_shortcut_path(&dir));
    let mut launch_args = extract_shortcut_args(&exe);
    if let Some(options) = launch_options {
        launch_args.extend(split_shortcut_args(&options));
    }
    Some(NonSteamShortcut { name, exe_path, start_dir, launch_args })
}

fn clean_shortcut_path(path: &str) -> Option<PathBuf> {
    let trimmed = extract_shortcut_path(path)?;
    if trimmed.is_empty() {
        return None;
    }

    let normalized = trimmed.replace('\\', "/");
    let home = dirs::home_dir().unwrap_or_default();

    if normalized.len() > 2 && normalized.as_bytes().get(1) == Some(&b':') {
        let drive = normalized.chars().next()?.to_ascii_lowercase();
        let rest = normalized[2..].trim_start_matches('/');
        return match drive {
            'z' => Some(PathBuf::from("/").join(rest)),
            'c' => Some(home.join(".metalsharp").join("prefix-steam").join("drive_c").join(rest)),
            _ => Some(PathBuf::from("/").join(rest)),
        };
    }

    Some(PathBuf::from(normalized))
}

fn extract_shortcut_path(path: &str) -> Option<String> {
    let trimmed = path.trim();
    if trimmed.is_empty() {
        return None;
    }

    if let Some(rest) = trimmed.strip_prefix('"') {
        let end = rest.find('"').unwrap_or(rest.len());
        return Some(rest[..end].trim().to_string());
    }
    if let Some(rest) = trimmed.strip_prefix('\'') {
        let end = rest.find('\'').unwrap_or(rest.len());
        return Some(rest[..end].trim().to_string());
    }

    let lower = trimmed.to_ascii_lowercase();
    if let Some(idx) = lower.find(".exe") {
        return Some(trimmed[..idx + 4].trim().to_string());
    }

    Some(trimmed.to_string())
}

fn extract_shortcut_args(command: &str) -> Vec<String> {
    let Some(rest) = shortcut_args_suffix(command) else {
        return Vec::new();
    };
    split_shortcut_args(rest)
}

fn shortcut_args_suffix(command: &str) -> Option<&str> {
    let trimmed = command.trim();
    if trimmed.is_empty() {
        return None;
    }

    if let Some(rest) = trimmed.strip_prefix('"') {
        let end = rest.find('"')?;
        return Some(rest[end + 1..].trim());
    }
    if let Some(rest) = trimmed.strip_prefix('\'') {
        let end = rest.find('\'')?;
        return Some(rest[end + 1..].trim());
    }

    let lower = trimmed.to_ascii_lowercase();
    let idx = lower.find(".exe")?;
    Some(trimmed[idx + 4..].trim())
}

fn split_shortcut_args(args: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut current = String::new();
    let mut quote: Option<char> = None;

    for ch in args.chars() {
        match (quote, ch) {
            (Some(q), c) if c == q => quote = None,
            (None, '"' | '\'') => quote = Some(ch),
            (None, c) if c.is_whitespace() => {
                if !current.is_empty() {
                    out.push(std::mem::take(&mut current));
                }
            },
            (_, c) => current.push(c),
        }
    }

    if !current.is_empty() {
        out.push(current);
    }

    out
}

fn shortcut_id(name: &str, exe_path: &Path) -> u64 {
    use std::hash::{Hash, Hasher};
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    name.hash(&mut hasher);
    exe_path.hash(&mut hasher);
    hasher.finish()
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_binary_non_steam_shortcuts() {
        let exe = if cfg!(windows) { "C:\\Games\\Joy\\Joy.exe" } else { "Z:\\tmp\\Joy\\Joy.exe" };
        let data = test_shortcuts_vdf("Joy of Creation", exe, "Z:\\tmp\\Joy", None, false);

        let shortcuts = parse_shortcuts_vdf(&data);

        assert_eq!(shortcuts.len(), 1);
        assert_eq!(shortcuts[0].name, "Joy of Creation");
        assert!(shortcuts[0].exe_path.ends_with("tmp/Joy/Joy.exe"));
        assert!(shortcuts[0].launch_args.is_empty());
    }

    #[test]
    fn ignores_shortcuts_without_exe_targets() {
        let data = test_shortcuts_vdf("Native App", "/Applications/Foo.app", "/Applications", None, false);

        assert!(parse_shortcuts_vdf(&data).is_empty());
    }

    #[test]
    fn strips_shortcut_launch_args_from_quoted_exe_path() {
        let data =
            test_shortcuts_vdf("DX12 Game", "\"Z:\\tmp\\DxGame\\Game.exe\" -d3d12", "Z:\\tmp\\DxGame", None, false);

        let shortcuts = parse_shortcuts_vdf(&data);

        assert_eq!(shortcuts.len(), 1);
        assert!(shortcuts[0].exe_path.ends_with("tmp/DxGame/Game.exe"));
        assert_eq!(shortcuts[0].launch_args, vec!["-d3d12"]);
    }

    #[test]
    fn preserves_multiple_shortcut_launch_args() {
        let data = test_shortcuts_vdf(
            "DX12 Game",
            "\"Z:\\tmp\\DxGame\\Game.exe\" -d3d12 -windowed \"-profile=high perf\"",
            "Z:\\tmp\\DxGame",
            None,
            false,
        );

        let shortcuts = parse_shortcuts_vdf(&data);

        assert_eq!(shortcuts[0].launch_args, vec!["-d3d12", "-windowed", "-profile=high perf"]);
    }

    #[test]
    fn preserves_launch_options_field() {
        let data = test_shortcuts_vdf(
            "Joy of Creation",
            "\"Z:\\tmp\\Joy\\Joy.exe\"",
            "Z:\\tmp\\Joy",
            Some("-d3d12 -windowed"),
            false,
        );

        let shortcuts = parse_shortcuts_vdf(&data);

        assert_eq!(shortcuts.len(), 1);
        assert_eq!(shortcuts[0].launch_args, vec!["-d3d12", "-windowed"]);
    }

    #[test]
    fn nested_tags_do_not_cancel_shortcut_parsing() {
        let data = test_shortcuts_vdf("Tagged Game", "Z:\\tmp\\Tagged\\Game.exe", "Z:\\tmp\\Tagged", None, true);

        let shortcuts = parse_shortcuts_vdf(&data);

        assert_eq!(shortcuts.len(), 1);
        assert_eq!(shortcuts[0].name, "Tagged Game");
        assert!(shortcuts[0].exe_path.ends_with("tmp/Tagged/Game.exe"));
    }

    fn test_shortcuts_vdf(
        name: &str,
        exe: &str,
        start_dir: &str,
        launch_options: Option<&str>,
        include_tags: bool,
    ) -> Vec<u8> {
        let mut data = Vec::new();
        object(&mut data, "shortcuts");
        object(&mut data, "0");
        string_field(&mut data, "appname", name);
        string_field(&mut data, "exe", exe);
        string_field(&mut data, "StartDir", start_dir);
        if let Some(options) = launch_options {
            string_field(&mut data, "LaunchOptions", options);
        }
        if include_tags {
            object(&mut data, "tags");
            string_field(&mut data, "0", "favorite");
            data.push(0x08);
        }
        data.push(0x08);
        data.push(0x08);
        data
    }

    fn object(data: &mut Vec<u8>, key: &str) {
        data.push(0x00);
        data.extend_from_slice(key.as_bytes());
        data.push(0);
    }

    fn string_field(data: &mut Vec<u8>, key: &str, value: &str) {
        data.push(0x01);
        data.extend_from_slice(key.as_bytes());
        data.push(0);
        data.extend_from_slice(value.as_bytes());
        data.push(0);
    }
}
