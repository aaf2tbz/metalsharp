use serde_json::json;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

const MIGRATE_VERSION: &str = env!("CARGO_PKG_VERSION");
const MIGRATE_SCHEMA_VERSION: u64 = 1;

static MIGRATING: AtomicBool = AtomicBool::new(false);

fn migrate_progress_path() -> PathBuf {
    crate::platform::metalsharp_home().join("migrate_progress.json")
}

fn write_migrate_progress(status: &str, step: usize, total: usize, message: &str, error: Option<&str>) {
    let data = json!({
        "status": status,
        "step": step,
        "total": total,
        "message": message,
        "error": error,
        "version": MIGRATE_VERSION,
    });
    let path = migrate_progress_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let _ = fs::write(path, serde_json::to_string(&data).unwrap_or_default());
}

pub fn is_migrating() -> bool {
    MIGRATING.load(Ordering::SeqCst)
}

pub fn read_migrate_progress() -> serde_json::Value {
    let path = migrate_progress_path();
    if path.exists() {
        if let Ok(contents) = fs::read_to_string(&path) {
            if let Ok(v) = serde_json::from_str::<serde_json::Value>(&contents) {
                return v;
            }
        }
    }
    json!({
        "status": "idle",
        "step": 0,
        "total": 0,
        "message": "",
        "error": null,
        "version": MIGRATE_VERSION,
    })
}

pub fn needs_migration() -> serde_json::Value {
    let active_ms_dir = crate::platform::metalsharp_home();
    let legacy_ms_dir = match legacy_metalsharp_home() {
        Some(path) => path,
        None => return json!({"ok": false, "error": "no home dir"}),
    };

    if let Some(status) = setup_migration_status(&active_ms_dir) {
        return status;
    }

    if active_ms_dir != legacy_ms_dir && has_local_metalsharp_data(&active_ms_dir) {
        return json!({
            "ok": true,
            "needed": true,
            "current_version": "0.0.0",
            "target_version": MIGRATE_VERSION,
            "current_schema": 0,
            "target_schema": MIGRATE_SCHEMA_VERSION,
            "reason": "active_home_setup_metadata_missing",
            "target_home": active_ms_dir,
        });
    }

    if active_ms_dir != legacy_ms_dir && legacy_ms_dir.join("setup.json").exists() {
        return json!({
            "ok": true,
            "needed": true,
            "current_version": "0.0.0",
            "target_version": MIGRATE_VERSION,
            "current_schema": 0,
            "target_schema": MIGRATE_SCHEMA_VERSION,
            "reason": "legacy_home_to_active_home",
            "source_home": legacy_ms_dir,
            "target_home": active_ms_dir,
        });
    }

    json!({
        "ok": true,
        "needed": false,
        "reason": "fresh_install",
        "target_home": active_ms_dir,
    })
}

fn legacy_metalsharp_home() -> Option<PathBuf> {
    dirs::home_dir().map(|home| home.join(".metalsharp"))
}

fn has_local_metalsharp_data(ms_dir: &Path) -> bool {
    [
        ms_dir.join("runtime"),
        ms_dir.join("prefix-steam"),
        ms_dir.join("games"),
        ms_dir.join("sharp-library"),
        ms_dir.join("cache").join("steam_config.json"),
    ]
    .iter()
    .any(|path| path.exists())
}

fn setup_migration_status(ms_dir: &Path) -> Option<serde_json::Value> {
    let setup_path = ms_dir.join("setup.json");
    if !ms_dir.exists() || !setup_path.exists() {
        return None;
    }

    let setup_data = match fs::read_to_string(&setup_path) {
        Ok(d) => d,
        Err(_) => {
            return Some(json!({
                "ok": true,
                "needed": false,
                "reason": "cannot_read_setup",
                "target_home": ms_dir,
            }));
        },
    };

    let setup: serde_json::Map<String, serde_json::Value> = match serde_json::from_str(&setup_data) {
        Ok(m) => m,
        Err(_) => {
            return Some(json!({
                "ok": true,
                "needed": false,
                "reason": "cannot_parse_setup",
                "target_home": ms_dir,
            }));
        },
    };

    let current_schema = setup.get("runtime_migration_schema").and_then(|v| v.as_u64());
    let legacy_migrated_version = setup.get("last_migrated_version").and_then(|v| v.as_str());
    let current_version = legacy_migrated_version.unwrap_or("0.0.0");
    let setup_completed = setup.get("completed").and_then(|v| v.as_bool()).unwrap_or(false);

    let needed = match current_schema {
        Some(schema) => schema < MIGRATE_SCHEMA_VERSION,
        None if legacy_migrated_version.is_some() && runtime_core_ready(ms_dir) => false,
        None => runtime_needs_repair(ms_dir, setup_completed),
    };

    Some(json!({
        "ok": true,
        "needed": needed,
        "current_version": current_version,
        "target_version": MIGRATE_VERSION,
        "current_schema": current_schema.unwrap_or(0),
        "target_schema": MIGRATE_SCHEMA_VERSION,
        "reason": if needed { "runtime_schema_or_repair_needed" } else { "up_to_date" },
        "target_home": ms_dir,
    }))
}

fn runtime_needs_repair(ms_dir: &Path, setup_completed: bool) -> bool {
    if runtime_core_ready(&ms_dir) {
        return false;
    }

    setup_completed || ms_dir.join("prefix-steam").exists()
}

fn runtime_core_ready(ms_dir: &Path) -> bool {
    let runtime_wine = ms_dir.join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&runtime_wine);
    if !wine.exists() {
        return false;
    }

    if crate::platform::current() == crate::platform::HostPlatform::Linux {
        return true;
    }

    [
        runtime_wine.join("lib").join("wine").join("x86_64-unix"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d9.dll"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10.dll"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10_1.dll"),
        runtime_wine.join("lib").join("dxmt").join("x86_64-unix"),
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d10core.dll"),
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d11.dll"),
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d12.dll"),
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("dxgi.dll"),
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("winemetal.dll"),
        runtime_wine.join("etc").join("dxmt.conf"),
    ]
    .iter()
    .all(|path| path.exists())
}

pub fn start_migration() -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    if MIGRATING.load(Ordering::SeqCst) {
        return Ok(json!({"ok": false, "error": "migration already in progress"}));
    }

    if MIGRATING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok(json!({"ok": false, "error": "migration already in progress"}));
    }

    std::thread::spawn(|| {
        run_migration();
        MIGRATING.store(false, Ordering::SeqCst);
    });

    Ok(json!({"ok": true}))
}

fn run_migration() {
    let target_ms_dir = crate::platform::metalsharp_home();
    let legacy_ms_dir = match legacy_metalsharp_home() {
        Some(path) => path,
        None => {
            write_migrate_progress("error", 0, 0, "no home directory", Some("no_home"));
            return;
        },
    };
    let target_has_setup = target_ms_dir.join("setup.json").exists();
    let legacy_has_setup = legacy_ms_dir.join("setup.json").exists();
    let target_has_data = has_local_metalsharp_data(&target_ms_dir);
    let source_ms_dir = if target_has_setup || target_has_data || target_ms_dir == legacy_ms_dir {
        target_ms_dir.clone()
    } else if legacy_has_setup {
        legacy_ms_dir.clone()
    } else if target_ms_dir.exists() {
        target_ms_dir.clone()
    } else if legacy_ms_dir.exists() {
        legacy_ms_dir.clone()
    } else {
        target_ms_dir.clone()
    };

    if !source_ms_dir.exists() {
        write_migrate_progress("error", 0, 0, "MetalSharp data directory not found", Some("no_metalsharp_dir"));
        return;
    }

    let total_steps = 5usize;
    let mut step = 0usize;

    step += 1;
    write_migrate_progress("running", step, total_steps, "Stopping Steam and Wine processes...", None);
    kill_steam_wine();

    step += 1;
    write_migrate_progress("running", step, total_steps, "Preserving user data...", None);
    let preserved = preserve_user_data(&source_ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Removing old runtime...", None);
    remove_old_runtime(&target_ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Restoring user data...", None);
    restore_user_data(&target_ms_dir, &preserved);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Running full runtime install...", None);
    let install_ok = match crate::installer::start_install_all() {
        Ok(v) if v.get("ok").and_then(|ok| ok.as_bool()).unwrap_or(false) => match wait_for_install_complete() {
            Ok(()) => true,
            Err(e) => {
                write_migrate_progress("error", step, total_steps, &format!("Runtime install failed: {}", e), Some(&e));
                false
            },
        },
        Ok(v) => {
            let error = v.get("error").and_then(|e| e.as_str()).unwrap_or("runtime install did not start");
            write_migrate_progress(
                "error",
                step,
                total_steps,
                &format!("Runtime install failed: {}", error),
                Some(error),
            );
            false
        },
        Err(e) => {
            write_migrate_progress(
                "error",
                step,
                total_steps,
                &format!("Runtime install failed: {}", e),
                Some(&e.to_string()),
            );
            false
        },
    };

    if install_ok {
        let setup_path = target_ms_dir.join("setup.json");
        if setup_path.exists() {
            if let Ok(contents) = fs::read_to_string(&setup_path) {
                if let Ok(mut cfg) = serde_json::from_str::<serde_json::Map<String, serde_json::Value>>(&contents) {
                    cfg.insert("last_migrated_version".into(), json!(MIGRATE_VERSION));
                    cfg.insert("runtime_migration_schema".into(), json!(MIGRATE_SCHEMA_VERSION));
                    let _ = fs::write(&setup_path, serde_json::to_string_pretty(&cfg).unwrap_or_default());
                }
            }
        } else {
            let cfg = json!({
                "completed": true,
                "last_migrated_version": MIGRATE_VERSION,
                "runtime_migration_schema": MIGRATE_SCHEMA_VERSION,
            });
            let _ = fs::write(&setup_path, serde_json::to_string_pretty(&cfg).unwrap_or_default());
        }
        let marker = target_ms_dir.join(".post-update-migration");
        let _ = fs::remove_file(&marker);
        write_migrate_progress("complete", total_steps, total_steps, "Migration complete!", None);
    } else {
        write_migrate_progress(
            "error",
            total_steps,
            total_steps,
            "Runtime install incomplete — re-run setup wizard after restart",
            Some("runtime_install_incomplete"),
        );
    }

    log_to_file(&format!(
        "Migration to v{} finished (install_ok={}, source={}, target={})",
        MIGRATE_VERSION,
        install_ok,
        source_ms_dir.display(),
        target_ms_dir.display()
    ));
}

fn wait_for_install_complete() -> Result<(), String> {
    for _ in 0..600 {
        std::thread::sleep(std::time::Duration::from_millis(500));
        if !crate::installer::is_installing() {
            let progress = crate::installer::read_progress();
            return match progress.get("status").and_then(|v| v.as_str()) {
                Some("complete") => Ok(()),
                Some(status) => {
                    let detail = progress
                        .get("error")
                        .and_then(|v| v.as_str())
                        .or_else(|| progress.get("log").and_then(|v| v.as_str()))
                        .unwrap_or(status);
                    Err(detail.to_string())
                },
                None => Err("installer stopped without a final status".into()),
            };
        }
    }

    Err("runtime install timed out".into())
}

fn kill_steam_wine() {
    let patterns = ["steam", "steam.exe", "steamwebhelper", "steamwebhelper.exe", "wine", "wine64", "wineserver"];

    for pat in &patterns {
        let _ = Command::new("pkill").arg("-x").arg(pat).output();
        let _ = Command::new("pkill").arg("-f").arg(pat).output();
    }
    std::thread::sleep(std::time::Duration::from_secs(2));
}

struct PreservedData {
    setup_json: Option<Vec<u8>>,
    steam_config_json: Option<Vec<u8>>,
    prefix_steam_tmp: PathBuf,
    games_tmp: PathBuf,
    sharp_library_tmp: PathBuf,
}

fn preserve_user_data(ms_dir: &PathBuf) -> PreservedData {
    let tmp = std::env::temp_dir().join("metalsharp-migration-preserve");
    let _ = fs::remove_dir_all(&tmp);
    let _ = fs::create_dir_all(&tmp);

    let setup_json = ms_dir.join("setup.json").exists().then(|| fs::read(ms_dir.join("setup.json")).ok()).flatten();

    let steam_config_path = ms_dir.join("cache").join("steam_config.json");
    let steam_config_json = steam_config_path.exists().then(|| fs::read(&steam_config_path).ok()).flatten();

    write_migrate_progress("running", 2, 5, "Preserving user data (Steam prefix)...", None);
    let prefix_steam_tmp = tmp.join("prefix-steam");
    let prefix_steam = ms_dir.join("prefix-steam");
    if prefix_steam.exists() {
        let _ = fs::create_dir_all(&prefix_steam_tmp);
        let skip = ["dosdevices", "windows", "ProgramData"];
        preserve_selective(&prefix_steam, &prefix_steam_tmp, &skip);
    }

    write_migrate_progress("running", 2, 5, "Preserving user data (games)...", None);
    let games_tmp = tmp.join("games");
    let games = ms_dir.join("games");
    if games.exists() {
        let _ = fs::create_dir_all(&games_tmp);
        copy_dir_recursive(&games, &games_tmp);
    }

    write_migrate_progress("running", 2, 5, "Preserving user data (library)...", None);
    let sharp_library_tmp = tmp.join("sharp-library");
    let sharp_library = ms_dir.join("sharp-library");
    if sharp_library.exists() {
        let _ = fs::create_dir_all(&sharp_library_tmp);
        copy_dir_recursive(&sharp_library, &sharp_library_tmp);
    }

    PreservedData { setup_json, steam_config_json, prefix_steam_tmp, games_tmp, sharp_library_tmp }
}

fn preserve_selective(src: &PathBuf, dst: &PathBuf, skip_names: &[&str]) {
    if let Ok(entries) = fs::read_dir(src) {
        for entry in entries.flatten() {
            let name = entry.file_name();
            let name_str = name.to_string_lossy().to_string();
            let src_path = entry.path();
            let dst_path = dst.join(&name);

            if skip_names.contains(&name_str.as_str()) {
                continue;
            }

            match fs::symlink_metadata(&src_path) {
                Ok(meta) => {
                    if meta.file_type().is_symlink() {
                        if let Ok(target) = fs::read_link(&src_path) {
                            let _ = std::os::unix::fs::symlink(&target, &dst_path);
                        }
                    } else if meta.is_dir() {
                        let _ = fs::create_dir_all(&dst_path);
                        preserve_selective(&src_path, &dst_path, skip_names);
                    } else {
                        let _ = fs::copy(&src_path, &dst_path);
                    }
                },
                Err(_) => {},
            }
        }
    }
}

fn remove_old_runtime(ms_dir: &PathBuf) {
    let dirs_to_remove = [
        "runtime",
        "configs",
        "cache",
        "logs",
        "shader-cache",
        "crashes",
        "SteamSetup.exe",
        "install_progress.json",
        "update_progress.json",
        "migrate_progress.json",
    ];

    for name in &dirs_to_remove {
        let p = ms_dir.join(name);
        let is_dir = fs::symlink_metadata(&p).map(|m| m.is_dir()).unwrap_or(false);
        let is_file = fs::symlink_metadata(&p).map(|m| m.is_file()).unwrap_or(false);
        if is_dir {
            let _ = fs::remove_dir_all(&p);
        } else if is_file {
            let _ = fs::remove_file(&p);
        }
    }

    let _ = fs::create_dir_all(ms_dir.join("runtime"));
    let _ = fs::create_dir_all(ms_dir.join("configs"));
    let _ = fs::create_dir_all(ms_dir.join("cache"));
    let _ = fs::create_dir_all(ms_dir.join("logs"));
    let _ = fs::create_dir_all(ms_dir.join("shader-cache"));
}

fn restore_user_data(ms_dir: &PathBuf, preserved: &PreservedData) {
    if preserved.prefix_steam_tmp.exists() {
        let dst = ms_dir.join("prefix-steam");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        copy_dir_recursive(&preserved.prefix_steam_tmp, &dst);

        let dosdevices = dst.join("dosdevices");
        if !dosdevices.exists() {
            let _ = fs::create_dir_all(&dosdevices);
        }
        let c_link = dosdevices.join("c:");
        if !c_link.exists() {
            let _ = std::os::unix::fs::symlink("../drive_c", &c_link);
        }
        let z_link = dosdevices.join("z:");
        if !z_link.exists() {
            let _ = std::os::unix::fs::symlink("/", &z_link);
        }
    }

    if preserved.games_tmp.exists() {
        let dst = ms_dir.join("games");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        copy_dir_recursive(&preserved.games_tmp, &dst);
    }

    if preserved.sharp_library_tmp.exists() {
        let dst = ms_dir.join("sharp-library");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        copy_dir_recursive(&preserved.sharp_library_tmp, &dst);
    }

    if let Some(ref data) = preserved.setup_json {
        let _ = fs::write(ms_dir.join("setup.json"), data);
    }

    if let Some(ref data) = preserved.steam_config_json {
        let cache_dir = ms_dir.join("cache");
        let _ = fs::create_dir_all(&cache_dir);
        let _ = fs::write(cache_dir.join("steam_config.json"), data);
    }
}

fn copy_dir_recursive(src: &PathBuf, dst: &PathBuf) {
    if let Ok(entries) = fs::read_dir(src) {
        for entry in entries.flatten() {
            let src_path = entry.path();
            let dst_path = dst.join(entry.file_name());
            match fs::symlink_metadata(&src_path) {
                Ok(meta) => {
                    if meta.file_type().is_symlink() {
                        if let Ok(target) = fs::read_link(&src_path) {
                            let _ = std::os::unix::fs::symlink(&target, &dst_path);
                        }
                    } else if meta.is_dir() {
                        let _ = fs::create_dir_all(&dst_path);
                        copy_dir_recursive(&src_path, &dst_path);
                    } else {
                        let _ = fs::copy(&src_path, &dst_path);
                    }
                },
                Err(_) => {},
            }
        }
    }
}

fn log_to_file(msg: &str) {
    let log_dir = crate::platform::metalsharp_home().join("logs");
    let _ = fs::create_dir_all(&log_dir);
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = now.as_secs();
    let h = (secs / 3600) % 24;
    let m = (secs / 60) % 60;
    let s = secs % 60;
    let line = format!("[{:02}:{:02}:{:02}] {}\n", h, m, s, msg);
    let days = secs / 86400;
    let y = 1970 + (days * 400).div_ceil(146097);
    let mut remaining = days - (((y - 1) * 365) + ((y - 1) / 4) - ((y - 1) / 100) + ((y - 1) / 400));
    let ml = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut mo = 1;
    for (i, &md) in ml.iter().enumerate() {
        if remaining < md {
            mo = i + 1;
            break;
        }
        remaining -= md;
    }
    let log_path = log_dir.join(format!("{:04}-{:02}-{:02}.log", y, mo, remaining + 1));
    let _ = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
        .and_then(|mut f| std::io::Write::write_all(&mut f, line.as_bytes()));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn completed_setups_repair_missing_runtime_without_steam_prefix() {
        let home = test_dir("missing-runtime");
        let ms_dir = home.join(".metalsharp");
        fs::create_dir_all(&ms_dir).expect("create ms dir");

        assert!(runtime_needs_repair(&ms_dir, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn incomplete_fresh_setups_do_not_enter_migration_without_prefix() {
        let home = test_dir("fresh-incomplete");
        let ms_dir = home.join(".metalsharp");
        fs::create_dir_all(&ms_dir).expect("create ms dir");

        assert!(!runtime_needs_repair(&ms_dir, false));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn complete_runtime_does_not_request_repair() {
        let home = test_dir("complete-runtime");
        let ms_dir = home.join(".metalsharp");
        write_runtime_core(&ms_dir);

        assert!(runtime_core_ready(&ms_dir));
        assert!(!runtime_needs_repair(&ms_dir, true));
        let _ = fs::remove_dir_all(home);
    }

    fn write_runtime_core(ms_dir: &Path) {
        let runtime_wine = ms_dir.join("runtime").join("wine");
        let wine = runtime_wine.join("bin").join("metalsharp-wine");
        fs::create_dir_all(wine.parent().unwrap()).expect("create wine bin");
        fs::write(&wine, b"#!/bin/sh\n").expect("write wine");

        if crate::platform::current() == crate::platform::HostPlatform::Linux {
            return;
        }

        for path in [
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join(".keep"),
            runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d9.dll"),
            runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10.dll"),
            runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10_1.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join(".keep"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d10core.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d11.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d12.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("dxgi.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("winemetal.dll"),
            runtime_wine.join("etc").join("dxmt.conf"),
        ] {
            fs::create_dir_all(path.parent().unwrap()).expect("create runtime parent");
            fs::write(path, b"test").expect("write runtime file");
        }
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-migrate-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
