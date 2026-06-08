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
    let steam_exe = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe");
    let has_ready = prefix.join(".gptk-ready").exists();
    let has_seeding = prefix.join(".gptk-seeding").exists();

    if !has_ready && !has_seeding && steam_exe.exists() {
        let _ = std::fs::write(prefix.join(".gptk-ready"), "ready");
        return true;
    }

    if has_ready && steam_exe.exists() {
        if has_seeding {
            let _ = std::fs::remove_file(prefix.join(".gptk-seeding"));
        }
        return true;
    }

    false
}

pub fn gptk_prefix_seeding(home: &Path) -> bool {
    gptk_prefix_path(home).join(".gptk-seeding").exists()
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
        for entry in std::fs::read_dir(dosdevices).unwrap_or_else(|_| {
            std::fs::create_dir_all(dosdevices).unwrap_or(());
            std::fs::read_dir(dosdevices).unwrap_or_else(|_| panic!("cannot read dosdevices"))
        }) {
            let Ok(entry) = entry else { continue };
            let name = entry.file_name();
            let name_str = name.to_string_lossy();
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
    copy_dir_recursive(game_dir, &external_game_dir).map_err(|e| format!("gptk migrate: copy failed: {}", e))?;

    if let Err(e) = verify_dir_copy(game_dir, &external_game_dir) {
        eprintln!("gptk migrate: verification failed, cleaning up: {}", e);
        let _ = std::fs::remove_dir_all(&external_base);
        return Err(e.into());
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
    std::thread::spawn(move || {
        if let Err(e) = seed_gptk_prefix_sync(&home) {
            eprintln!("gptk prefix seed failed: {}", e);
            let marker = gptk_prefix_path(&home).join(".gptk-seeding");
            let _ = std::fs::remove_file(&marker);
        }
    });

    Err("GPTK prefix is being prepared — try again in a moment".into())
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

    if gptk_prefix.exists() && gptk_prefix.join("drive_c").is_dir() {
        let _ = std::fs::remove_dir_all(&gptk_prefix);
    }
    let _ = std::fs::create_dir_all(&gptk_prefix);
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
        let _ = std::fs::remove_file(&seeding_marker);
        return Err("GPTK wineboot --init failed".into());
    }
    eprintln!("gptk: wineboot --init done");

    if !steam_prefix.join("drive_c").join("Program Files (x86)").join("Steam").exists() {
        let _ = std::fs::remove_file(&seeding_marker);
        return Err("MetalSharp Wine Steam not installed — install Steam first".into());
    }

    eprintln!("gptk: copying Steam directory ...");
    let drive_c = gptk_prefix.join("drive_c");
    let steam_src = steam_prefix.join("drive_c").join("Program Files (x86)").join("Steam");
    let steam_dst = drive_c.join("Program Files (x86)").join("Steam");
    std::fs::create_dir_all(steam_dst.parent().unwrap())?;
    copy_dir_recursive(&steam_src, &steam_dst)?;
    verify_dir_copy(&steam_src, &steam_dst).map_err(|e| format!("gptk: Steam copy {}", e))?;
    eprintln!("gptk: Steam copy done (verified)");

    eprintln!("gptk: copying users ...");
    let users_src = steam_prefix.join("drive_c").join("users");
    let users_dst = drive_c.join("users");
    if users_src.is_dir() {
        if users_dst.is_dir() {
            let _ = std::fs::remove_dir_all(&users_dst);
        }
        copy_dir_recursive(&users_src, &users_dst)?;
        verify_dir_copy(&users_src, &users_dst).map_err(|e| format!("gptk: users copy {}", e))?;
        eprintln!("gptk: users copy done (verified)");
    } else {
        eprintln!("gptk: no users dir to copy");
    }

    ensure_gptk_dosdevices(home)?;

    let steam_exe_check = steam_dst.join("Steam.exe");
    if !steam_exe_check.is_file() {
        let _ = std::fs::remove_file(&seeding_marker);
        return Err(format!("gptk: Steam.exe missing after copy — expected at {}", steam_exe_check.display()).into());
    }

    copy_registry_hive(&steam_prefix, &gptk_prefix, "system.reg");
    copy_registry_hive(&steam_prefix, &gptk_prefix, "user.reg");
    copy_registry_hive(&steam_prefix, &gptk_prefix, "userdef.reg");

    let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &gptk_prefix).arg("-k").status();

    eprintln!("gptk: installing components (vcrun, etc.) ...");
    install_gptk_prefix_components(home)?;
    eprintln!("gptk: components done");

    let _ = std::fs::remove_file(&seeding_marker);
    std::fs::write(gptk_prefix.join(".gptk-ready"), "ready")?;
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
        if src_path.is_dir() {
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
    let vcruntime140 = system32.join("vcruntime140.dll");
    let msvcp140 = system32.join("msvcp140.dll");
    if !vcruntime140.exists() || !msvcp140.exists() {
        return false;
    }
    let vcrun_size = vcruntime140.metadata().map(|m| m.len()).unwrap_or(0);
    let msvc_size = msvcp140.metadata().map(|m| m.len()).unwrap_or(0);
    vcrun_size > 100_000 && msvc_size > 100_000
}

pub fn install_gptk_prefix_components(home: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let gptk_prefix = gptk_prefix_path(home);
    let steam_prefix = metalsharp_home_dir_for(home).join("prefix-steam");

    if !gptk_prefix.join("drive_c").is_dir() {
        return Ok(());
    }

    if gptk_vcrun_installed(home) {
        eprintln!("gptk: vcrun already installed in prefix");
        return Ok(());
    }

    let gptk_wine64 = gptk_wine64_binary();
    let gptk_wineserver = gptk_wineserver_binary();

    let dyld = format!(
        "{}:{}:{}",
        gptk_wine_root().join("lib").display(),
        gptk_wine_root().join("lib").join("wine").join("x86_64-unix").display(),
        gptk_wine_root().join("lib").join("external").display(),
    );

    let prefix_str = gptk_prefix.to_string_lossy().to_string();

    let steam_redist = steam_prefix
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps")
        .join("common")
        .join("Steamworks Shared")
        .join("_CommonRedist");

    let installer = ["2022", "2019", "2017", "2015"]
        .iter()
        .filter_map(|ver| {
            let p = steam_redist.join("vcredist").join(ver).join("VC_redist.x64.exe");
            if p.exists() {
                Some(p)
            } else {
                None
            }
        })
        .next();

    if let Some(installer) = installer {
        eprintln!("gptk: installing VC++ redist from {:?} ...", installer);
        let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-p").status();

        let status = std::process::Command::new(&gptk_wine64)
            .env("WINEPREFIX", &prefix_str)
            .env("WINEDEBUG", "-all")
            .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
            .arg(&installer)
            .args(["/install", "/quiet"])
            .status()?;
        if !status.success() {
            eprintln!("gptk: VC++ redist installer exited with {:?}", status.code());
        }

        let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-w").status();
        let _ = std::process::Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-k").status();

        eprintln!("gptk: VC++ redist install done, installed={}", gptk_vcrun_installed(home));
    } else {
        eprintln!("gptk: no vcredist installer found in Steam CommonRedist");
    }

    Ok(())
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
