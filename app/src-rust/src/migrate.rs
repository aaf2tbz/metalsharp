use serde_json::json;
use std::collections::hash_map::DefaultHasher;
use std::fs;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

const MIGRATE_VERSION: &str = env!("CARGO_PKG_VERSION");
const MIGRATE_SCHEMA_VERSION: u64 = 3;
const MIGRATION_EXACT_KILL_PATTERNS: &[&str] =
    &["wineloader", "steam.exe", "steamwebhelper.exe", "steamwebhelper", "wineserver", "wine64", "wine"];
const MIGRATION_COMMAND_KILL_PATTERNS: &[&str] = &["Steam.exe", "steamwebhelper.exe", "wineserver", "wineloader"];

static MIGRATING: AtomicBool = AtomicBool::new(false);

fn migrate_progress_path() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("migrate_progress.json")
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
    let _ = fs::write(migrate_progress_path(), serde_json::to_string(&data).unwrap_or_default());
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
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return json!({"ok": false, "error": "no home dir"}),
    };

    let setup_path = home.join(".metalsharp").join("setup.json");
    let setup_dir_exists = home.join(".metalsharp").exists();

    if !setup_dir_exists || !setup_path.exists() {
        return json!({"ok": true, "needed": false, "reason": "fresh_install"});
    }

    let setup_data = match fs::read_to_string(&setup_path) {
        Ok(d) => d,
        Err(_) => return json!({"ok": true, "needed": false, "reason": "cannot_read_setup"}),
    };

    let setup: serde_json::Map<String, serde_json::Value> = match serde_json::from_str(&setup_data) {
        Ok(m) => m,
        Err(_) => return json!({"ok": true, "needed": false, "reason": "cannot_parse_setup"}),
    };

    let current_schema = setup.get("runtime_migration_schema").and_then(|v| v.as_u64());
    let legacy_migrated_version = setup.get("last_migrated_version").and_then(|v| v.as_str());
    let current_version = legacy_migrated_version.unwrap_or("0.0.0");
    let setup_completed = setup.get("completed").and_then(|v| v.as_bool()).unwrap_or(false);

    let needed = match current_schema {
        Some(schema) => schema < MIGRATE_SCHEMA_VERSION,
        None if legacy_migrated_version.is_some() => false,
        None => runtime_needs_repair(&home, setup_completed),
    };

    json!({
        "ok": true,
        "needed": needed,
        "current_version": current_version,
        "target_version": MIGRATE_VERSION,
        "current_schema": current_schema.unwrap_or(0),
        "target_schema": MIGRATE_SCHEMA_VERSION,
        "reason": if needed { "runtime_schema_or_repair_needed" } else { "up_to_date" },
    })
}

fn runtime_needs_repair(home: &Path, setup_completed: bool) -> bool {
    let ms_dir = home.join(".metalsharp");
    if runtime_core_ready(&ms_dir) {
        return false;
    }

    setup_completed || ms_dir.join("prefix-steam").exists()
}

fn runtime_core_ready(ms_dir: &Path) -> bool {
    let runtime_wine = ms_dir.join("runtime").join("wine");
    let runtime_host = ms_dir.join("runtime").join("host");
    let wine = crate::platform::runtime_wine_binary(&runtime_wine);
    if !wine.exists() {
        return false;
    }

    if !host_runtime_ready(&runtime_host) {
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

fn host_runtime_ready(dir: &Path) -> bool {
    file_nonempty(&dir.join("manifest.json"))
        && file_nonempty(&dir.join("HostRuntimeABI.h"))
        && (file_nonempty(&dir.join("libmetalsharp_host_runtime.dylib"))
            || file_nonempty(&dir.join("libmetalsharp_host_runtime.so"))
            || file_nonempty(&dir.join("metalsharp_host_runtime.dll")))
}

fn file_nonempty(path: &Path) -> bool {
    path.metadata().map(|meta| meta.is_file() && meta.len() > 0).unwrap_or(false)
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
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => {
            write_migrate_progress("error", 0, 0, "no home directory", Some("no_home"));
            return;
        },
    };

    let ms_dir = home.join(".metalsharp");

    if !ms_dir.exists() {
        write_migrate_progress("error", 0, 0, "~/.metalsharp not found", Some("no_metalsharp_dir"));
        return;
    }

    let total_steps = 5usize;
    let mut step = 0usize;

    step += 1;
    write_migrate_progress("running", step, total_steps, "Stopping Steam and Wine processes...", None);
    kill_steam_wine();

    step += 1;
    write_migrate_progress("running", step, total_steps, "Preserving user data...", None);
    let preserved = preserve_user_data(&ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Removing old runtime...", None);
    remove_old_runtime(&ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Restoring user data...", None);
    restore_user_data(&ms_dir, &preserved);

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
        let setup_path = ms_dir.join("setup.json");
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
        let marker = ms_dir.join(".post-update-migration");
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

    log_to_file(&format!("Migration to v{} finished (install_ok={})", MIGRATE_VERSION, install_ok));
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
    for pat in MIGRATION_EXACT_KILL_PATTERNS {
        run_pkill(&["-x", pat]);
    }

    for pat in MIGRATION_COMMAND_KILL_PATTERNS {
        run_pkill(&["-f", pat]);
    }
    std::thread::sleep(std::time::Duration::from_millis(750));
}

fn run_pkill(args: &[&str]) {
    let Ok(mut child) = Command::new("pkill").args(args).spawn() else {
        return;
    };

    for _ in 0..20 {
        if child.try_wait().ok().flatten().is_some() {
            return;
        }
        std::thread::sleep(std::time::Duration::from_millis(25));
    }

    let _ = child.kill();
    let _ = child.wait();
}

struct PreservedData {
    setup_json: Option<Vec<u8>>,
    steam_config_json: Option<Vec<u8>>,
    gptk_steam_install_progress_json: Option<Vec<u8>>,
    prefix_steam_tmp: PathBuf,
    prefix_gptk_steam_tmp: PathBuf,
    games_tmp: PathBuf,
    sharp_library_tmp: PathBuf,
    bottles_tmp: PathBuf,
    compatdata_tmp: PathBuf,
}

fn preserve_user_data(ms_dir: &PathBuf) -> PreservedData {
    let tmp = migration_preserve_tmp_dir(ms_dir);
    let _ = fs::remove_dir_all(&tmp);
    let _ = fs::create_dir_all(&tmp);

    let setup_json = ms_dir.join("setup.json").exists().then(|| fs::read(ms_dir.join("setup.json")).ok()).flatten();

    let steam_config_path = ms_dir.join("cache").join("steam_config.json");
    let steam_config_json = steam_config_path.exists().then(|| fs::read(&steam_config_path).ok()).flatten();

    let gptk_steam_install_progress_path = ms_dir.join("gptk_steam_install_progress.json");
    let gptk_steam_install_progress_json =
        gptk_steam_install_progress_path.exists().then(|| fs::read(&gptk_steam_install_progress_path).ok()).flatten();

    write_migrate_progress("running", 2, 5, "Preserving user data (Steam prefix)...", None);
    let prefix_steam_tmp = tmp.join("prefix-steam");
    let prefix_steam = ms_dir.join("prefix-steam");
    if prefix_steam.exists() {
        let _ = fs::create_dir_all(&prefix_steam_tmp);
        let skip = ["dosdevices", "windows", "ProgramData"];
        preserve_selective(&prefix_steam, &prefix_steam_tmp, &skip);
    }

    write_migrate_progress("running", 2, 5, "Preserving user data (GPTK Steam prefix)...", None);
    let prefix_gptk_steam_tmp = tmp.join("prefix-gptk-steam");
    let prefix_gptk_steam = ms_dir.join("prefix-gptk-steam");
    if prefix_gptk_steam.exists() {
        let _ = fs::create_dir_all(&prefix_gptk_steam_tmp);
        let skip = ["dosdevices", "windows", "ProgramData"];
        preserve_selective(&prefix_gptk_steam, &prefix_gptk_steam_tmp, &skip);
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

    write_migrate_progress("running", 2, 5, "Preserving user data (bottles)...", None);
    let bottles_tmp = tmp.join("bottles");
    let bottles = ms_dir.join("bottles");
    if bottles.exists() {
        let _ = fs::create_dir_all(&bottles_tmp);
        copy_dir_recursive(&bottles, &bottles_tmp);
    }

    write_migrate_progress("running", 2, 5, "Preserving user data (compatdata)...", None);
    let compatdata_tmp = tmp.join("compatdata");
    let compatdata = ms_dir.join("compatdata");
    if compatdata.exists() {
        let _ = fs::create_dir_all(&compatdata_tmp);
        copy_dir_recursive(&compatdata, &compatdata_tmp);
    }

    PreservedData {
        setup_json,
        steam_config_json,
        gptk_steam_install_progress_json,
        prefix_steam_tmp,
        prefix_gptk_steam_tmp,
        games_tmp,
        sharp_library_tmp,
        bottles_tmp,
        compatdata_tmp,
    }
}

fn migration_preserve_tmp_dir(ms_dir: &Path) -> PathBuf {
    let mut hasher = DefaultHasher::new();
    ms_dir.hash(&mut hasher);
    std::env::temp_dir().join(format!("metalsharp-migration-preserve-{}-{:016x}", std::process::id(), hasher.finish()))
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

        ensure_prefix_dosdevices(&dst);
    }

    if preserved.prefix_gptk_steam_tmp.exists() {
        let dst = ms_dir.join("prefix-gptk-steam");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        copy_dir_recursive(&preserved.prefix_gptk_steam_tmp, &dst);

        ensure_prefix_dosdevices(&dst);
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

    if preserved.bottles_tmp.exists() {
        let dst = ms_dir.join("bottles");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        copy_dir_recursive(&preserved.bottles_tmp, &dst);
    }

    if preserved.compatdata_tmp.exists() {
        let dst = ms_dir.join("compatdata");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        copy_dir_recursive(&preserved.compatdata_tmp, &dst);
    }

    if let Some(ref data) = preserved.setup_json {
        let _ = fs::write(ms_dir.join("setup.json"), data);
    }

    if let Some(ref data) = preserved.steam_config_json {
        let cache_dir = ms_dir.join("cache");
        let _ = fs::create_dir_all(&cache_dir);
        let _ = fs::write(cache_dir.join("steam_config.json"), data);
    }

    if let Some(ref data) = preserved.gptk_steam_install_progress_json {
        let _ = fs::write(ms_dir.join("gptk_steam_install_progress.json"), data);
    }
}

fn ensure_prefix_dosdevices(prefix: &Path) {
    let dosdevices = prefix.join("dosdevices");
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
    let home = dirs::home_dir().unwrap_or_default();
    let log_dir = home.join(".metalsharp").join("logs");
    let _ = fs::create_dir_all(&log_dir);
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = now.as_secs();
    let h = (secs / 3600) % 24;
    let m = (secs / 60) % 60;
    let s = secs % 60;
    let line = format!("[{:02}:{:02}:{:02}] {}\n", h, m, s, msg);
    let (year, month, day) = unix_days_to_ymd(secs / 86400);
    let log_path = log_dir.join(format!("{:04}-{:02}-{:02}.log", year, month, day));
    let _ = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
        .and_then(|mut f| std::io::Write::write_all(&mut f, line.as_bytes()));
}

fn unix_days_to_ymd(days_since_epoch: u64) -> (i64, u32, u32) {
    let z = days_since_epoch as i64 + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365;
    let mut year = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = mp + if mp < 10 { 3 } else { -9 };
    if month <= 2 {
        year += 1;
    }
    (year, month as u32, day as u32)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn completed_setups_repair_missing_runtime_without_steam_prefix() {
        let home = test_dir("missing-runtime");
        fs::create_dir_all(home.join(".metalsharp")).expect("create ms dir");

        assert!(runtime_needs_repair(&home, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn incomplete_fresh_setups_do_not_enter_migration_without_prefix() {
        let home = test_dir("fresh-incomplete");
        fs::create_dir_all(home.join(".metalsharp")).expect("create ms dir");

        assert!(!runtime_needs_repair(&home, false));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn complete_runtime_does_not_request_repair() {
        let home = test_dir("complete-runtime");
        let ms_dir = home.join(".metalsharp");
        write_runtime_core(&ms_dir);

        assert!(runtime_core_ready(&ms_dir));
        assert!(!runtime_needs_repair(&home, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_preserves_bottles_across_runtime_cleanup() {
        let home = test_dir("preserve-bottles");
        let ms_dir = home.join(".metalsharp");
        let bottle_manifest = ms_dir.join("bottles").join("steam_620").join("bottle.json");
        fs::create_dir_all(bottle_manifest.parent().unwrap()).expect("create bottle dir");
        fs::write(&bottle_manifest, br#"{"id":"steam_620"}"#).expect("write bottle manifest");

        let preserved = preserve_user_data(&ms_dir);
        fs::remove_dir_all(ms_dir.join("bottles")).expect("remove live bottles");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert_eq!(
            fs::read_to_string(ms_dir.join("bottles").join("steam_620").join("bottle.json")).unwrap(),
            r#"{"id":"steam_620"}"#
        );
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_preserves_compatdata_across_runtime_cleanup() {
        let home = test_dir("preserve-compatdata");
        let ms_dir = home.join(".metalsharp");
        let compat_manifest = ms_dir.join("compatdata").join("620").join("metalsharp-compatdata.json");
        fs::create_dir_all(compat_manifest.parent().unwrap()).expect("create compatdata dir");
        fs::write(&compat_manifest, br#"{"appid":620}"#).expect("write compatdata manifest");

        let preserved = preserve_user_data(&ms_dir);
        fs::remove_dir_all(ms_dir.join("compatdata")).expect("remove live compatdata");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert_eq!(
            fs::read_to_string(ms_dir.join("compatdata").join("620").join("metalsharp-compatdata.json")).unwrap(),
            r#"{"appid":620}"#
        );
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_preserves_gptk_steam_prefix_and_install_progress() {
        let home = test_dir("preserve-gptk-steam");
        let ms_dir = home.join(".metalsharp");
        let gptk_login = ms_dir
            .join("prefix-gptk-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam")
            .join("config")
            .join("loginusers.vdf");
        fs::create_dir_all(gptk_login.parent().unwrap()).expect("create gptk steam config dir");
        fs::write(&gptk_login, b"gptk-user").expect("write gptk login");
        fs::write(ms_dir.join("gptk_steam_install_progress.json"), br#"{"phase":"ready"}"#)
            .expect("write gptk install progress");

        let preserved = preserve_user_data(&ms_dir);
        fs::remove_dir_all(ms_dir.join("prefix-gptk-steam")).expect("remove live gptk prefix");
        fs::remove_file(ms_dir.join("gptk_steam_install_progress.json")).expect("remove live gptk progress");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert_eq!(fs::read_to_string(&gptk_login).unwrap(), "gptk-user");
        assert_eq!(
            fs::read_to_string(ms_dir.join("gptk_steam_install_progress.json")).unwrap(),
            r#"{"phase":"ready"}"#
        );
        assert!(ms_dir.join("prefix-gptk-steam").join("dosdevices").join("c:").exists());
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_kill_patterns_avoid_broad_command_matches() {
        assert!(MIGRATION_EXACT_KILL_PATTERNS.contains(&"wineloader"));
        assert!(!MIGRATION_COMMAND_KILL_PATTERNS.contains(&"steam"));
        assert!(!MIGRATION_COMMAND_KILL_PATTERNS.contains(&"wine"));
    }

    #[test]
    fn unix_days_to_ymd_handles_current_dates_without_underflow() {
        assert_eq!(unix_days_to_ymd(20_592), (2026, 5, 19));
    }

    fn write_runtime_core(ms_dir: &Path) {
        let runtime_wine = ms_dir.join("runtime").join("wine");
        let wine = runtime_wine.join("bin").join("metalsharp-wine");
        fs::create_dir_all(wine.parent().unwrap()).expect("create wine bin");
        fs::write(&wine, b"#!/bin/sh\n").expect("write wine");
        write_host_runtime(ms_dir);

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

    fn write_host_runtime(ms_dir: &Path) {
        let host = ms_dir.join("runtime").join("host");
        fs::create_dir_all(&host).expect("create host runtime");
        fs::write(host.join("manifest.json"), br#"{"abi":"metalsharp-host-runtime"}"#).expect("write manifest");
        fs::write(host.join("HostRuntimeABI.h"), b"header").expect("write header");
        fs::write(host.join("libmetalsharp_host_runtime.dylib"), b"dylib").expect("write dylib");
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
