use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HostPlatform {
    Macos,
    Linux,
    Other,
}

pub fn current() -> HostPlatform {
    if cfg!(target_os = "macos") {
        HostPlatform::Macos
    } else if cfg!(target_os = "linux") {
        HostPlatform::Linux
    } else {
        HostPlatform::Other
    }
}

pub fn name() -> &'static str {
    match current() {
        HostPlatform::Macos => "macos",
        HostPlatform::Linux => "linux",
        HostPlatform::Other => "other",
    }
}

pub fn app_resources_dir() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    app_resources_dir_for_exe(&exe)
}

pub fn metalsharp_home_dir() -> PathBuf {
    metalsharp_home_dir_for(&dirs::home_dir().unwrap_or_default())
}

pub fn metalsharp_home_dir_for<T: AsRef<std::path::Path> + ?Sized>(home: &T) -> PathBuf {
    if let Ok(value) = std::env::var("METALSHARP_HOME") {
        let trimmed = value.trim();
        if !trimmed.is_empty() {
            return PathBuf::from(trimmed);
        }
    }
    home.as_ref().join(".metalsharp")
}

fn app_resources_dir_for_exe(exe: &std::path::Path) -> Option<PathBuf> {
    let exe_dir = exe.parent()?;

    if cfg!(target_os = "macos") {
        return macos_resources_dir_for_exe(exe);
    }

    if exe_dir.file_name().and_then(|n| n.to_str()) == Some("resources") {
        return Some(exe_dir.to_path_buf());
    }

    Some(exe_dir.to_path_buf())
}

fn macos_resources_dir_for_exe(exe: &std::path::Path) -> Option<PathBuf> {
    let mut dir = exe.parent()?;
    loop {
        if dir.file_name().and_then(|n| n.to_str()) == Some("Resources") {
            return Some(dir.to_path_buf());
        }

        let contents = dir.parent()?;
        if contents.file_name().and_then(|n| n.to_str()) == Some("Contents") {
            return Some(contents.join("Resources"));
        }

        dir = contents;
    }
}

pub fn runtime_library_env(ms_root: &std::path::Path) -> Option<(&'static str, String)> {
    let value = format!(
        "{}:{}",
        ms_root.join("lib").to_string_lossy(),
        ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy()
    );

    match current() {
        HostPlatform::Macos => Some(("DYLD_FALLBACK_LIBRARY_PATH", value)),
        HostPlatform::Linux => Some(("LD_LIBRARY_PATH", value)),
        HostPlatform::Other => None,
    }
}

pub fn set_runtime_library_env(cmd: &mut Command, ms_root: &std::path::Path) {
    if let Some((key, value)) = runtime_library_env(ms_root) {
        cmd.env(key, value);
    }
}

pub fn runtime_wine_binary(ms_root: &std::path::Path) -> PathBuf {
    let wrapped = ms_root.join("bin").join("metalsharp-wine");
    if wrapped.exists() {
        return wrapped;
    }

    ms_root.join("bin").join("wine")
}

pub fn gptk_wine_root() -> PathBuf {
    PathBuf::from("/Applications/Game Porting Toolkit.app/Contents/Resources/wine")
}

pub fn gptk_prefix_path(home: &Path) -> PathBuf {
    metalsharp_home_dir_for(home).join("prefix-gptk")
}

pub fn gptk_prefix_ready(home: &Path) -> bool {
    let prefix = gptk_prefix_path(home);
    if !prefix.join(".gptk-ready").is_file() {
        return false;
    }
    if prefix.join(".gptk-seeding").exists() {
        let _ = std::fs::remove_file(prefix.join(".gptk-seeding"));
    }
    let steam_exe = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe");
    let dosdevices = prefix.join("dosdevices");
    steam_exe.is_file() && dosdevices.is_dir()
}

pub fn gptk_prefix_seeding(home: &Path) -> bool {
    let prefix = gptk_prefix_path(home);
    let marker = prefix.join(".gptk-seeding");
    if !marker.exists() {
        return false;
    }
    if !prefix.join("drive_c").is_dir() {
        eprintln!("gptk: stale seeding marker found with no drive_c — clearing");
        let _ = std::fs::remove_file(&marker);
        return false;
    }
    if let Ok(meta) = std::fs::metadata(&marker) {
        if let Ok(modified) = meta.modified() {
            if let Ok(age) = modified.elapsed() {
                if age > std::time::Duration::from_secs(1800) {
                    eprintln!("gptk: seeding marker is {}s old — clearing as stale", age.as_secs());
                    let _ = std::fs::remove_file(&marker);
                    return false;
                }
            }
        }
    }
    true
}

pub fn ensure_gptk_dosdevices(home: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let gptk_prefix = gptk_prefix_path(home);
    let dosdevices = gptk_prefix.join("dosdevices");
    if !dosdevices.is_dir() {
        std::fs::create_dir_all(&dosdevices)?;
    }

    let c_link = dosdevices.join("c:");
    if c_link.exists() {
        if let Ok(target) = std::fs::read_link(&c_link) {
            if target != *Path::new("../drive_c") {
                let _ = std::fs::remove_file(&c_link);
                let _ = std::os::unix::fs::symlink("../drive_c", &c_link);
            }
        }
    } else {
        let _ = std::os::unix::fs::symlink("../drive_c", &c_link);
    }

    ensure_dosdevice(&dosdevices, "y:", home);
    ensure_dosdevice(&dosdevices, "z:", Path::new("/"));

    let steam_prefix = metalsharp_home_dir_for(home).join("prefix-steam");
    let wine_steamapps = steam_prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");

    if let Ok(lf_data) = std::fs::read_to_string(wine_steamapps.join("libraryfolders.vdf")) {
        for library_path in parse_library_paths_from_vdf(&lf_data) {
            if library_path.is_absolute() && library_path.is_dir() {
                ensure_dosdevice_for_path(&dosdevices, &library_path);
            }
        }
    }

    if let Ok(mac_lf_paths) = std::fs::read_dir("/Volumes") {
        for entry in mac_lf_paths {
            let Ok(entry) = entry else { continue };
            let vol_path = entry.path();
            if vol_path.is_dir() {
                ensure_dosdevice_for_path(&dosdevices, &vol_path);
            }
        }
    }

    Ok(())
}

fn ensure_dosdevice(dosdevices: &Path, letter: &str, target: &Path) {
    let link = dosdevices.join(letter);
    if link.exists() {
        if let Ok(current) = std::fs::read_link(&link) {
            if current == target {
                return;
            }
        }
        let _ = std::fs::remove_file(&link);
    }
    if let Err(e) = std::os::unix::fs::symlink(target, &link) {
        eprintln!("gptk dosdevices: failed to link {} -> {}: {}", letter, target.display(), e);
    } else {
        eprintln!("gptk dosdevices: {} -> {}", letter, target.display());
    }
}

fn ensure_dosdevice_for_path(dosdevices: &Path, target: &Path) {
    if target.starts_with("/Volumes") {
        let Ok(rd) = std::fs::read_dir(dosdevices) else {
            return;
        };
        for entry in rd.flatten() {
            let name_str = entry.file_name().to_string_lossy().to_string();
            if name_str.len() == 2 && name_str.ends_with(':') {
                if let Ok(current) = std::fs::read_link(entry.path()) {
                    if current == target {
                        return;
                    }
                }
            }
        }
    }

    let existing: std::collections::HashSet<String> = match std::fs::read_dir(dosdevices) {
        Ok(rd) => rd
            .filter_map(|e| e.ok())
            .filter_map(|e| {
                let name = e.file_name().to_string_lossy().to_string();
                if name.len() == 2 && name.ends_with(':') {
                    Some(name)
                } else {
                    None
                }
            })
            .collect(),
        Err(_) => std::collections::HashSet::new(),
    };

    for ch in b'd'..=b'z' {
        let letter = format!("{}:", ch as char);
        if existing.contains(&letter) {
            continue;
        }
        let link = dosdevices.join(&letter);
        match std::os::unix::fs::symlink(target, &link) {
            Ok(()) => {
                eprintln!("gptk dosdevices: {} -> {} (steam library)", letter, target.display());
                return;
            },
            Err(e) => {
                eprintln!("gptk dosdevices: failed {} -> {}: {}", letter, target.display(), e);
                return;
            },
        }
    }
}

fn parse_library_paths_from_vdf(data: &str) -> Vec<PathBuf> {
    let mut paths = Vec::new();
    for line in data.lines() {
        let trimmed = line.trim();
        if let Some(rest) = trimmed.strip_prefix("\"path\"") {
            if let Some(value) = rest.split('"').nth(1) {
                let p = PathBuf::from(value);
                if !paths.contains(&p) {
                    paths.push(p);
                }
            }
        }
    }
    paths
}

fn is_game_storage_volume(name: &str) -> bool {
    if name == "Macintosh HD" {
        return false;
    }
    if name.starts_with("MetalSharp") {
        return false;
    }
    if name.starts_with("Game Porting Toolkit") {
        return false;
    }
    if name.contains("Evaluation environment") {
        return false;
    }
    true
}

pub fn find_external_game_storage() -> Option<PathBuf> {
    let entries = std::fs::read_dir("/Volumes").ok()?;
    for entry in entries {
        let entry = entry.ok()?;
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        if !is_game_storage_volume(&name_str) {
            continue;
        }
        let vol_path = entry.path();
        if !vol_path.is_dir() {
            continue;
        }
        let games_dir = vol_path.join("MetalSharp").join("games");
        if std::fs::create_dir_all(&games_dir).is_ok() {
            return Some(games_dir);
        }
    }
    None
}

pub fn is_path_on_external_volume(path: &Path) -> bool {
    let canonical = match std::fs::canonicalize(path) {
        Ok(c) => c,
        Err(_) => return false,
    };
    let canon_str = canonical.to_string_lossy();
    if !canon_str.starts_with("/Volumes/") {
        return false;
    }
    let rest = canon_str.strip_prefix("/Volumes/").unwrap_or("");
    let vol_name = rest.split('/').next().unwrap_or("");
    is_game_storage_volume(vol_name)
}

pub fn migrate_game_to_external(
    home: &Path,
    game_dir: &Path,
    appid: u32,
) -> Result<Option<PathBuf>, Box<dyn std::error::Error>> {
    if !game_dir.is_dir() {
        return Ok(None);
    }

    if is_path_on_external_volume(game_dir) {
        eprintln!("gptk migrate: game already on external volume ({})", game_dir.display());
        return Ok(None);
    }

    let storage = match find_external_game_storage() {
        Some(s) => s,
        None => {
            eprintln!("gptk migrate: no writable external SSD found, using internal path");
            return Ok(None);
        },
    };

    let game_name = match game_dir.file_name() {
        Some(n) => n.to_string_lossy().to_string(),
        None => return Ok(None),
    };

    let external_game_dir = storage.join(format!("{}", appid)).join(&game_name);

    if external_game_dir.is_dir() && std::fs::read_dir(&external_game_dir).map(|r| r.count()).unwrap_or(0) > 0 {
        eprintln!("gptk migrate: external copy already exists at {}", external_game_dir.display());
        return Ok(Some(external_game_dir));
    }

    eprintln!("gptk migrate: copying game '{}' (appid {}) from internal to external SSD ...", game_name, appid);

    let external_base = storage.join(format!("{}", appid));
    std::fs::create_dir_all(&external_base)?;

    let tmp_name = format!(".{}.partial", game_name);
    let tmp_dir = external_base.join(&tmp_name);
    if tmp_dir.is_dir() {
        let _ = std::fs::remove_dir_all(&tmp_dir);
    }

    copy_dir_recursive(game_dir, &tmp_dir).map_err(|e| {
        let _ = std::fs::remove_dir_all(&tmp_dir);
        format!("gptk migrate: copy failed: {}", e)
    })?;

    if let Err(e) = verify_dir_copy(game_dir, &tmp_dir) {
        eprintln!("gptk migrate: verification failed, cleaning up: {}", e);
        let _ = std::fs::remove_dir_all(&tmp_dir);
        return Err(e.into());
    }

    if let Err(e) = std::fs::rename(&tmp_dir, &external_game_dir) {
        let _ = std::fs::remove_dir_all(&tmp_dir);
        return Err(format!("gptk migrate: rename failed: {}", e).into());
    }

    eprintln!("gptk migrate: game copied to external SSD at {}", external_game_dir.display());

    ensure_gptk_dosdevices(home)?;

    Ok(Some(external_game_dir))
}

pub fn ensure_gptk_prefix(home: &Path) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let gptk_prefix = gptk_prefix_path(home);

    if gptk_prefix_ready(home) {
        sync_gptk_prefix(home)?;
        return Ok(gptk_prefix);
    }

    if gptk_prefix_seeding(home) {
        return Err("GPTK prefix is still being prepared — try again in a moment".into());
    }

    let home = home.to_path_buf();
    let gptk_prefix = gptk_prefix_path(&home);
    let _ = std::fs::create_dir_all(&gptk_prefix);
    if let Err(e) = std::fs::write(gptk_prefix.join(".gptk-seeding"), "seeding") {
        return Err(format!("failed to write seeding marker: {}", e).into());
    }
    std::thread::spawn(move || {
        if let Err(e) = seed_gptk_prefix_sync(&home) {
            eprintln!("gptk prefix seed failed: {}", e);
            let marker = gptk_prefix_path(&home).join(".gptk-seeding");
            let _ = std::fs::remove_file(&marker);
        }
    });

    Err("GPTK prefix is being prepared — try again in a moment".into())
}

struct SeedingGuard {
    marker: PathBuf,
    armed: bool,
}

impl SeedingGuard {
    fn new(marker: PathBuf) -> Self {
        Self { marker, armed: true }
    }
    fn disarm(&mut self) {
        self.armed = false;
    }
}

impl Drop for SeedingGuard {
    fn drop(&mut self) {
        if self.armed && self.marker.exists() {
            eprintln!("gptk: cleaning up seeding marker after failure");
            let _ = std::fs::remove_file(&self.marker);
        }
    }
}

fn fast_copy_dir(src: &Path, dst: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let status = std::process::Command::new("ditto").arg(src).arg(dst).stderr(std::process::Stdio::piped()).status()?;
    if !status.success() {
        eprintln!("gptk: ditto failed ({:?}), falling back to recursive copy", status.code());
        let (copied, skipped) = copy_dir_tolerant(src, dst);
        if copied == 0 && skipped > 0 {
            return Err(format!(
                "gptk copy failed: 0 files copied, {} skipped from {} -> {}",
                skipped,
                src.display(),
                dst.display()
            )
            .into());
        }
    }
    Ok(())
}

fn copy_dir_tolerant(src: &Path, dst: &Path) -> (u64, u64) {
    let mut copied = 0u64;
    let mut skipped = 0u64;
    if let Err(e) = std::fs::create_dir_all(dst) {
        eprintln!("gptk copy: cannot create {}: {}", dst.display(), e);
        return (copied, skipped);
    }
    let entries = match std::fs::read_dir(src) {
        Ok(rd) => rd,
        Err(e) => {
            eprintln!("gptk copy: cannot read {}: {}", src.display(), e);
            return (copied, skipped);
        },
    };
    for entry in entries.flatten() {
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        let meta = match std::fs::symlink_metadata(&src_path) {
            Ok(m) => m,
            Err(e) => {
                eprintln!("gptk copy: skip {}: {}", src_path.display(), e);
                skipped += 1;
                continue;
            },
        };
        if meta.file_type().is_symlink() {
            match std::fs::read_link(&src_path) {
                Ok(target) => {
                    let _ = std::fs::remove_file(&dst_path);
                    match std::os::unix::fs::symlink(&target, &dst_path) {
                        Ok(_bytes) => copied += 1,
                        Err(e) => {
                            eprintln!(
                                "gptk copy: skip symlink {} -> {}: {}",
                                src_path.display(),
                                dst_path.display(),
                                e
                            );
                            skipped += 1;
                        },
                    }
                },
                Err(e) => {
                    eprintln!("gptk copy: skip readlink {}: {}", src_path.display(), e);
                    skipped += 1;
                },
            }
        } else if meta.is_dir() {
            let (c, s) = copy_dir_tolerant(&src_path, &dst_path);
            copied += c;
            skipped += s;
        } else {
            match std::fs::copy(&src_path, &dst_path) {
                Ok(_) => copied += 1,
                Err(e) => {
                    eprintln!("gptk copy: skip {} -> {}: {}", src_path.display(), dst_path.display(), e);
                    skipped += 1;
                },
            }
        }
    }
    (copied, skipped)
}

pub fn seed_gptk_prefix_sync(home: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let gptk_prefix = gptk_prefix_path(home);
    let steam_prefix = metalsharp_home_dir_for(home).join("prefix-steam");
    let gptk_wine64 = gptk_wine64_binary();
    let gptk_wineserver = gptk_wineserver_binary();
    if !gptk_wine64.is_file() {
        return Err("GPTK wine64 not found".into());
    }

    let seeding_marker = gptk_prefix.join(".gptk-seeding");
    let _ = std::fs::create_dir_all(&gptk_prefix);
    std::fs::write(&seeding_marker, "seeding")?;
    let mut guard = SeedingGuard::new(seeding_marker.clone());

    let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &gptk_prefix).arg("-k").status();
    eprintln!("gptk: wineserver killed (pre-seed)");

    if gptk_prefix.join("drive_c").is_dir() {
        let _ = std::fs::remove_dir_all(gptk_prefix.join("drive_c"));
    }
    std::fs::write(&seeding_marker, "seeding")?;

    let dyld = format!(
        "{}:{}:{}",
        gptk_wine_root().join("lib").display(),
        gptk_wine_root().join("lib").join("wine").join("x86_64-unix").display(),
        gptk_wine_root().join("lib").join("external").display(),
    );

    eprintln!("gptk: running wineboot --init ...");
    let status = std::process::Command::new(&gptk_wine64)
        .env("WINEPREFIX", &gptk_prefix)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg("wineboot")
        .arg("--init")
        .status()?;
    if !status.success() {
        return Err("GPTK wineboot --init failed".into());
    }
    eprintln!("gptk: wineboot --init done");

    // Validate that wineboot actually created the prefix skeleton.
    let drive_c = gptk_prefix.join("drive_c");
    let windows = drive_c.join("windows");
    let dosdevices = gptk_prefix.join("dosdevices");
    if !drive_c.is_dir() || !windows.is_dir() || !dosdevices.is_dir() {
        let missing: Vec<&str> =
            [("drive_c", drive_c.is_dir()), ("drive_c/windows", windows.is_dir()), ("dosdevices", dosdevices.is_dir())]
                .iter()
                .filter(|(_, exists)| !exists)
                .map(|(name, _)| *name)
                .collect();
        return Err(format!(
            "gptk: wineboot --init succeeded but prefix is incomplete (missing: {})",
            missing.join(", ")
        )
        .into());
    }

    if !steam_prefix.join("drive_c").join("Program Files (x86)").join("Steam").exists() {
        return Err("MetalSharp Wine Steam not installed — install Steam first".into());
    }

    eprintln!("gptk: copying Steam directory ...");
    let steam_src = steam_prefix.join("drive_c").join("Program Files (x86)").join("Steam");
    let steam_dst = drive_c.join("Program Files (x86)").join("Steam");
    std::fs::create_dir_all(steam_dst.parent().unwrap())?;
    fast_copy_dir(&steam_src, &steam_dst)?;
    eprintln!("gptk: Steam copy done");

    eprintln!("gptk: copying users ...");
    let users_src = steam_prefix.join("drive_c").join("users");
    let users_dst = drive_c.join("users");
    if users_src.is_dir() {
        if users_dst.is_dir() {
            let _ = std::fs::remove_dir_all(&users_dst);
        }
        fast_copy_dir(&users_src, &users_dst)?;
        eprintln!("gptk: users copy done");
    } else {
        eprintln!("gptk: no users dir to copy");
    }

    ensure_gptk_dosdevices(home)?;

    let steam_exe_check = steam_dst.join("Steam.exe");
    if !steam_exe_check.is_file() {
        return Err(format!("gptk: Steam.exe missing after copy — expected at {}", steam_exe_check.display()).into());
    }

    copy_registry_hive(&steam_prefix, &gptk_prefix, "system.reg");
    copy_registry_hive(&steam_prefix, &gptk_prefix, "user.reg");
    copy_registry_hive(&steam_prefix, &gptk_prefix, "userdef.reg");

    let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &gptk_prefix).arg("-k").status();

    eprintln!("gptk: installing components (vcrun, etc.) ...");
    install_gptk_prefix_components(home)?;
    eprintln!("gptk: components done");

    std::fs::write(gptk_prefix.join(".gptk-ready"), "ready")?;
    guard.disarm();
    let _ = std::fs::remove_file(&seeding_marker);
    eprintln!("gptk: prefix ready");

    Ok(())
}

pub fn sync_gptk_prefix(home: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let gptk_prefix = gptk_prefix_path(home);
    let steam_prefix = metalsharp_home_dir_for(home).join("prefix-steam");

    let steam_config_src = steam_prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("config");
    let steam_config_dst = gptk_prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("config");

    if steam_config_src.is_dir() {
        for file in &["loginusers.vdf", "config.vdf", "libraryfolders.vdf"] {
            let src = steam_config_src.join(file);
            let dst = steam_config_dst.join(file);
            if src.exists() {
                std::fs::copy(&src, &dst)
                    .map_err(|e| format!("gptk sync: copy {} -> {}: {}", src.display(), dst.display(), e))?;
            }
        }
    }

    copy_registry_hive(&steam_prefix, &gptk_prefix, "user.reg");

    ensure_gptk_dosdevices(home)?;

    Ok(())
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        let meta = std::fs::symlink_metadata(&src_path)?;
        if meta.file_type().is_symlink() {
            let target = std::fs::read_link(&src_path)?;
            let _ = std::fs::remove_file(&dst_path);
            std::os::unix::fs::symlink(&target, &dst_path)?;
        } else if meta.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            std::fs::copy(&src_path, &dst_path).map_err(|e| {
                std::io::Error::new(e.kind(), format!("copy {} -> {}: {}", src_path.display(), dst_path.display(), e))
            })?;
        }
    }
    Ok(())
}

fn verify_dir_copy(src: &Path, dst: &Path) -> Result<(), String> {
    let src_count = count_dir_entries(src).map_err(|e| format!("stat src {}: {}", src.display(), e))?;
    let dst_count = count_dir_entries(dst).map_err(|e| format!("stat dst {}: {}", dst.display(), e))?;
    if src_count != dst_count {
        return Err(format!(
            "copy verification failed: src {} has {} entries, dst {} has {} entries",
            src.display(),
            src_count,
            dst.display(),
            dst_count
        ));
    }
    Ok(())
}

fn count_dir_entries(path: &Path) -> std::io::Result<u64> {
    let mut count: u64 = 0;
    for entry in std::fs::read_dir(path)? {
        let _ = entry?;
        count += 1;
    }
    Ok(count)
}

pub fn gptk_vcrun_installed(home: &Path) -> bool {
    let gptk_prefix = gptk_prefix_path(home);
    let system32 = gptk_prefix.join("drive_c").join("windows").join("system32");
    let syswow64 = gptk_prefix.join("drive_c").join("windows").join("syswow64");
    let has = |dir: &std::path::Path, dll: &str| -> bool {
        let p = dir.join(dll);
        p.is_file() && p.metadata().map(|m| m.len()).unwrap_or(0) > 10_000
    };
    let x64_ok = has(&system32, "vcruntime140.dll") && has(&system32, "msvcp140.dll");
    let x86_ok = has(&syswow64, "vcruntime140.dll") && has(&syswow64, "msvcp140.dll");
    x64_ok && x86_ok
}

pub fn install_gptk_prefix_components(home: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let gptk_prefix = gptk_prefix_path(home);
    if !gptk_prefix.join("drive_c").is_dir() {
        return Err("GPTK prefix has no drive_c — run seed first".into());
    }

    if gptk_vcrun_installed(home) {
        eprintln!("gptk: vcrun already installed in prefix (x64 + x86)");
        return Ok(());
    }

    let gptk_wine64 = gptk_wine64_binary();
    let gptk_wineserver = gptk_wineserver_binary();
    let prefix_str = gptk_prefix.to_string_lossy().to_string();
    let dyld = format!(
        "{}:{}:{}",
        gptk_wine_root().join("lib").display(),
        gptk_wine_root().join("lib").join("wine").join("x86_64-unix").display(),
        gptk_wine_root().join("lib").join("external").display(),
    );

    let redist_dir = metalsharp_home_dir_for(home).join("runtime").join("redist").join("vcredist");
    let _ = std::fs::create_dir_all(&redist_dir);

    let x64 = resolve_or_download_vcrun(&redist_dir, "x64")?;
    let x86 = resolve_or_download_vcrun(&redist_dir, "x86")?;

    for (arch, path) in [("x64", &x64), ("x86", &x86)] {
        eprintln!("gptk: installing VC++ {} redist from {:?} ...", arch, path);
        let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-p").status();
        let status = std::process::Command::new(&gptk_wine64)
            .env("WINEPREFIX", &prefix_str)
            .env("WINEDEBUG", "-all")
            .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
            .arg(path)
            .args(["/install"])
            .status()?;
        if !status.success() {
            eprintln!("gptk: VC++ {} installer exited with {:?}", arch, status.code());
        }
        let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-w").status();
        let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-k").status();
    }

    eprintln!("gptk: VC++ redist install done, installed={}", gptk_vcrun_installed(home));
    Ok(())
}

fn resolve_or_download_vcrun(redist_dir: &Path, arch: &str) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let filename = format!("vc_redist.{}.exe", arch);
    let staged = redist_dir.join(&filename);
    if staged.is_file() && staged.metadata().map(|m| m.len()).unwrap_or(0) > 100_000 {
        eprintln!("gptk: using staged {} redist", arch);
        return Ok(staged);
    }

    let home = dirs::home_dir().unwrap_or_default();
    let steam_redist = home
        .join(".metalsharp")
        .join("prefix-steam")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps")
        .join("common")
        .join("Steamworks Shared")
        .join("_CommonRedist");

    for ver in &["2022", "2019", "2017", "2015"] {
        let candidate = steam_redist.join("vcredist").join(ver).join(&filename);
        if candidate.is_file() {
            eprintln!("gptk: found {} redist in Steam CommonRedist/{}", arch, ver);
            return Ok(candidate);
        }
    }

    eprintln!("gptk: downloading VC++ {} redist from Microsoft ...", arch);
    let url = format!("https://aka.ms/vs/17/release/{}", filename);
    let tmp = staged.with_extension("download");
    let config =
        ureq::config::Config::builder().user_agent(format!("MetalSharp/{}", env!("CARGO_PKG_VERSION"))).build();
    let agent = ureq::Agent::new_with_config(config);
    let resp = agent.get(&url).call().map_err(|e| format!("VC++ {} download failed: {}", arch, e))?;
    let mut input = resp.into_body().into_reader();
    let mut output = std::fs::File::create(&tmp)?;
    std::io::copy(&mut input, &mut output)?;
    std::fs::rename(&tmp, &staged)?;
    eprintln!("gptk: downloaded {} redist to {}", arch, staged.display());
    Ok(staged)
}

fn copy_registry_hive(src_prefix: &Path, dst_prefix: &Path, hive: &str) {
    let src = src_prefix.join(hive);
    let dst = dst_prefix.join(hive);
    if src.exists() {
        if let Err(e) = std::fs::copy(&src, &dst) {
            eprintln!("gptk: failed to copy {} -> {}: {}", src.display(), dst.display(), e);
        }
    }
}

pub fn gptk_wine64_binary() -> PathBuf {
    gptk_wine_root().join("bin").join("wine64")
}

pub fn gptk_wineserver_binary() -> PathBuf {
    gptk_wine_root().join("bin").join("wineserver")
}

pub fn gptk_is_installed() -> bool {
    gptk_wine64_binary().is_file() && gptk_wineserver_binary().is_file()
}

pub fn rosetta_is_installed() -> bool {
    std::path::Path::new("/Library/Apple/usr/lib/libRosettaAOT.dylib").exists()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    #[test]
    fn macos_resources_dir_accepts_backend_under_resources_runtime() {
        let exe = Path::new("/Applications/MetalSharp.app/Contents/Resources/runtime/metalsharp-backend");

        assert_eq!(
            macos_resources_dir_for_exe(exe),
            Some(PathBuf::from("/Applications/MetalSharp.app/Contents/Resources"))
        );
    }

    #[test]
    fn macos_resources_dir_accepts_app_binary_under_macos() {
        let exe = Path::new("/Applications/MetalSharp.app/Contents/MacOS/MetalSharp");

        assert_eq!(
            macos_resources_dir_for_exe(exe),
            Some(PathBuf::from("/Applications/MetalSharp.app/Contents/Resources"))
        );
    }
}
