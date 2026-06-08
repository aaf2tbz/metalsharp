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
    eprintln!("gptk: Steam copy done");

    eprintln!("gptk: copying users ...");
    let users_src = steam_prefix.join("drive_c").join("users");
    let users_dst = drive_c.join("users");
    if users_src.is_dir() {
        let _ = std::fs::remove_dir_all(&users_dst);
        copy_dir_recursive(&users_src, &users_dst)?;
    }

    for dev in &["y:", "z:"] {
        let src_link = steam_prefix.join("dosdevices").join(dev);
        let dst_link = gptk_prefix.join("dosdevices").join(dev);
        if src_link.exists() && !dst_link.exists() {
            if let Ok(target) = std::fs::read_link(&src_link) {
                let _ = std::os::unix::fs::symlink(&target, &dst_link);
            }
        }
    }

    copy_registry_hive(&steam_prefix, &gptk_prefix, "system.reg");
    copy_registry_hive(&steam_prefix, &gptk_prefix, "user.reg");
    copy_registry_hive(&steam_prefix, &gptk_prefix, "userdef.reg");

    let _ = std::process::Command::new(&gptk_wineserver)
        .env("WINEPREFIX", &gptk_prefix)
        .arg("-k")
        .status();

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
                let _ = std::fs::copy(&src, &dst);
            }
        }
    }

    copy_registry_hive(&steam_prefix, &gptk_prefix, "user.reg");

    Ok(())
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> std::io::Result<()> {
    if !dst.exists() {
        std::fs::create_dir_all(dst)?;
    }
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        if src_path.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            let _ = std::fs::copy(&src_path, &dst_path);
        }
    }
    Ok(())
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
            if p.exists() { Some(p) } else { None }
        })
        .next();

    if let Some(installer) = installer {
        eprintln!("gptk: installing VC++ redist from {:?} ...", installer);
        let _ = std::process::Command::new(&gptk_wineserver)
            .env("WINEPREFIX", &prefix_str)
            .arg("-p")
            .status();

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

        let _ = std::process::Command::new(&gptk_wineserver)
            .env("WINEPREFIX", &prefix_str)
            .arg("-w")
            .status();
        let _ = std::process::Command::new(&gptk_wineserver)
            .env("WINEPREFIX", &prefix_str)
            .arg("-k")
            .status();

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
        let _ = std::fs::copy(&src, &dst);
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
