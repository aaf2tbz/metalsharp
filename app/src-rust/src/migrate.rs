use serde_json::json;
use std::cmp::Ordering as CmpOrdering;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

fn mac_cmd(name: &str) -> Command {
    let path = match name {
        "pkill" => "/usr/bin/pkill",
        _ => name,
    };
    Command::new(path)
}

const MIGRATE_VERSION: &str = env!("CARGO_PKG_VERSION");
const MIGRATE_SCHEMA_VERSION: u64 = 3;
const MIGRATION_EXACT_KILL_PATTERNS: &[&str] =
    &["wineloader", "steam.exe", "steamwebhelper.exe", "steamwebhelper", "wineserver", "wine64", "wine"];
const MIGRATION_COMMAND_KILL_PATTERNS: &[&str] = &["Steam.exe", "steamwebhelper.exe", "wineserver", "wineloader"];
const MIGRATION_PAYLOAD_DENY_NAMES: &[&str] = &[
    "steamapps",
    "common",
    "downloading",
    "shadercache",
    "compatdata",
    "prefix",
    "prefix-steam",
    "drive_c",
    "dosdevices",
    "Program Files",
    "Program Files (x86)",
    "Steam",
    "runtime",
    "downloads",
    "updates",
    "updater-tools",
    "tmp",
    "Temp",
    "cache",
    "logs",
    "crashes",
];
const MIGRATION_SETTINGS_FILE_NAMES: &[&str] = &[
    "setup.json",
    "steam_config.json",
    "bottle.json",
    "metalsharp-compatdata.json",
    "library.json",
    "apps.json",
    "routes.json",
    "settings.json",
    "preferences.json",
    "user.reg",
    "userdef.reg",
    "system.reg",
    "libraryfolders.vdf",
    "config.vdf",
    "loginusers.vdf",
    "localconfig.vdf",
    "shortcuts.vdf",
    "steam_autocloud.vdf",
];
const MIGRATION_SETTINGS_EXTENSIONS: &[&str] = &["json", "toml", "plist", "vdf", "reg", "ini", "cfg", "conf"];
const MIGRATION_TOTAL_STEPS: usize = 8;
const MIGRATION_PREFIX_INIT_TIMEOUT_SECS: u64 = 10;
const MIGRATION_PREFIX_INIT_ARGS: &[&str] = &["cmd", "/c", "exit"];

static MIGRATING: AtomicBool = AtomicBool::new(false);

#[derive(Clone, Debug, Default)]
struct PostUpdateMigrationMarker {
    needed: bool,
    target_version: Option<String>,
}

fn migrate_progress_path() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("migrate_progress.json")
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

    let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
    let post_update_marker = read_post_update_marker(&ms_dir);
    let setup_path = ms_dir.join("setup.json");
    let setup_dir_exists = ms_dir.exists();

    if !setup_dir_exists || !setup_path.exists() {
        return json!({"ok": true, "needed": false, "reason": "fresh_install"});
    }

    let marker_requested = post_update_marker.as_ref().map(|marker| marker.needed).unwrap_or(false);
    let marker_target_version = post_update_marker.as_ref().and_then(|marker| marker.target_version.clone());
    let marker_target_mismatch =
        post_update_marker.as_ref().is_some_and(|marker| post_update_target_newer_than_running(marker));

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
    let repair_needed = runtime_needs_repair(&home, setup_completed);

    let schema_current = current_schema.is_some_and(|schema| schema >= MIGRATE_SCHEMA_VERSION);
    let needed = repair_needed || marker_requested;

    json!({
        "ok": true,
        "needed": needed,
        "current_version": current_version,
        "target_version": MIGRATE_VERSION,
        "current_schema": current_schema.unwrap_or(0),
        "target_schema": MIGRATE_SCHEMA_VERSION,
        "post_update_target_version": marker_target_version,
        "running_version": MIGRATE_VERSION,
        "update_target_satisfied": !marker_target_mismatch,
        "reason": if marker_target_mismatch {
            "post_update_target_version_mismatch"
        } else if marker_requested && repair_needed {
            "post_update_marker_and_runtime_repair"
        } else if marker_requested {
            "post_update_marker"
        } else if repair_needed {
            "runtime_bundle_update_required"
        } else if schema_current {
            "up_to_date"
        } else {
            "runtime_schema_already_satisfied"
        },
    })
}

fn runtime_needs_repair(home: &Path, setup_completed: bool) -> bool {
    let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
    if runtime_core_ready(&ms_dir) {
        return false;
    }

    setup_completed || ms_dir.join("prefix-steam").exists()
}

fn post_update_marker_path(ms_dir: &Path) -> PathBuf {
    ms_dir.join(".post-update-migration")
}

fn read_post_update_marker(ms_dir: &Path) -> Option<PostUpdateMigrationMarker> {
    let marker_path = post_update_marker_path(ms_dir);
    let data = fs::read_to_string(marker_path).ok()?;
    let value = serde_json::from_str::<serde_json::Value>(&data).ok()?;
    Some(PostUpdateMigrationMarker {
        needed: value.get("needed").and_then(|v| v.as_bool()).unwrap_or(false),
        target_version: value.get("target_version").and_then(|v| v.as_str()).map(str::to_string),
    })
}

fn post_update_target_newer_than_running(marker: &PostUpdateMigrationMarker) -> bool {
    marker.target_version.as_deref().map(|target| compare_versions(target, MIGRATE_VERSION).is_gt()).unwrap_or(false)
}

fn post_update_target_error(marker: Option<&PostUpdateMigrationMarker>) -> Option<String> {
    let marker = marker?;
    let target = marker.target_version.as_deref()?;
    if compare_versions(target, MIGRATE_VERSION).is_gt() {
        return Some(format!(
            "Update handoff targeted MetalSharp v{}, but the running app is v{}. Relaunch the installed update and retry migration.",
            target, MIGRATE_VERSION
        ));
    }
    None
}

fn compare_versions(left: &str, right: &str) -> CmpOrdering {
    let left = parse_version_parts(left);
    let right = parse_version_parts(right);
    let len = left.len().max(right.len());
    for index in 0..len {
        let left_part = left.get(index).copied().unwrap_or(0);
        let right_part = right.get(index).copied().unwrap_or(0);
        match left_part.cmp(&right_part) {
            CmpOrdering::Equal => {},
            ordering => return ordering,
        }
    }
    CmpOrdering::Equal
}

fn parse_version_parts(value: &str) -> Vec<u32> {
    value
        .trim()
        .trim_start_matches('v')
        .split(['-', '+'])
        .next()
        .unwrap_or("")
        .split('.')
        .map(|part| part.chars().take_while(|ch| ch.is_ascii_digit()).collect::<String>())
        .map(|part| part.parse::<u32>().unwrap_or(0))
        .collect()
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

    if !crate::installer::dxmt_runtime_current_for_ms_dir(ms_dir) {
        return false;
    }

    if !crate::installer::metalsharp_runtime_lib_ready(&runtime_wine) {
        return false;
    }

    [
        runtime_wine.join("lib").join("wine").join("x86_64-unix"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d9.dll"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10.dll"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10_1.dll"),
        runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("d3d10.dll"),
        runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("d3d11.dll"),
        runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("d3d12.dll"),
        runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("dxgi.dll"),
        runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("nvapi64.dll"),
        runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("nvngx-on-metalfx.dll"),
        runtime_wine
            .join("lib")
            .join("external")
            .join("D3DMetal.framework")
            .join("Versions")
            .join("A")
            .join("D3DMetal"),
        runtime_wine
            .join("lib")
            .join("external")
            .join("D3DMetal.framework")
            .join("Versions")
            .join("A")
            .join("Resources"),
        ms_dir.join("runtime").join("goldberg").join("x86").join("steam_api.dll"),
        ms_dir.join("runtime").join("goldberg").join("x64").join("steam_api64.dll"),
        ms_dir.join("runtime").join("eac-toggle").join("x86_64-windows").join("_winhttp.dll"),
        ms_dir.join("configs").join("mtsp-rules.toml"),
        runtime_wine.join("etc").join("dxmt.conf"),
    ]
    .iter()
    .all(|path| path.exists())
        && framework_has_resource_dylib(&runtime_wine.join("lib").join("external").join("D3DMetal.framework"))
}

fn framework_has_resource_dylib(framework: &Path) -> bool {
    for resources_dir in [framework.join("Resources"), framework.join("Versions").join("A").join("Resources")] {
        if let Ok(entries) = fs::read_dir(resources_dir) {
            if entries.flatten().any(|entry| {
                entry.path().extension().and_then(|ext| ext.to_str()) == Some("dylib") && file_nonempty(&entry.path())
            }) {
                return true;
            }
        }
    }
    false
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

    write_migrate_progress("running", 0, MIGRATION_TOTAL_STEPS, "Starting MetalSharp migration...", None);

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

    let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
    let post_update_marker = read_post_update_marker(&ms_dir);

    if !ms_dir.exists() {
        write_migrate_progress("error", 0, 0, "~/.metalsharp not found", Some("no_metalsharp_dir"));
        return;
    }

    if let Some(error) = post_update_target_error(post_update_marker.as_ref()) {
        write_migrate_progress("error", 0, MIGRATION_TOTAL_STEPS, &error, Some("post_update_target_version_mismatch"));
        log_to_file(&format!("Migration blocked before runtime work: {}", error));
        return;
    }

    let marker_requested = post_update_marker.as_ref().map(|marker| marker.needed).unwrap_or(false);
    if runtime_core_ready(&ms_dir) && !marker_requested {
        update_migration_metadata(&ms_dir);
        let marker = post_update_marker_path(&ms_dir);
        let _ = fs::remove_file(&marker);
        write_migrate_progress("complete", 1, 1, "Runtime already ready; app update complete.", None);
        log_to_file(&format!("Migration to v{} skipped; runtime already ready", MIGRATE_VERSION));
        return;
    }

    let total_steps = MIGRATION_TOTAL_STEPS;
    let mut step = 0usize;

    step += 1;
    write_migrate_progress("running", step, total_steps, "Ensuring extract tools (zstd) are available...", None);
    if let Err(e) = crate::installer::ensure_zstd() {
        write_migrate_progress("error", step, total_steps, &format!("Failed to install zstd: {}", e), Some(&e));
        log_to_file(&format!("Migration blocked: zstd not available: {}", e));
        return;
    }

    step += 1;
    write_migrate_progress(
        "running",
        step,
        total_steps,
        "Preserving user preferences, Steam API key, and bottle settings...",
        None,
    );
    let preserved = preserve_user_data(&ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Cleaning stale runtime state...", None);
    remove_old_runtime(&ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Installing update...", None);
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

    step += 1;
    write_migrate_progress("running", step, total_steps, "Restoring preserved user data...", None);
    restore_user_data(&ms_dir, &preserved);

    if !install_ok {
        write_migrate_progress(
            "error",
            total_steps,
            total_steps,
            "Runtime install incomplete — re-run setup wizard after restart",
            Some("runtime_install_incomplete"),
        );
        log_to_file(&format!("Migration to v{} finished (install_ok=false)", MIGRATE_VERSION));
        return;
    }

    step += 1;
    write_migrate_progress(
        "running",
        step,
        total_steps,
        "Initializing Wine prefixes with refreshed runtime DLLs...",
        None,
    );
    match update_existing_wine_prefixes(&ms_dir, step) {
        Ok(updated) => log_to_file(&format!("Migration: prefix runtime init completed for {} prefix(es)", updated)),
        Err(e) => log_to_file(&format!("Migration: prefix runtime init failed (non-fatal): {}", e)),
    }
    register_external_steam_libraries(&ms_dir);
    clear_steam_crash_marker(&ms_dir);

    step += 1;
    write_migrate_progress("running", step, total_steps, "Verifying MetalSharp update...", None);
    if let Err(e) = verify_migration_ready(&ms_dir, post_update_marker.as_ref()) {
        write_migrate_progress("error", step, total_steps, &format!("Update verification failed: {}", e), Some(&e));
        log_to_file(&format!("Migration to v{} failed verification: {}", MIGRATE_VERSION, e));
        return;
    }

    update_migration_metadata(&ms_dir);
    if !migration_metadata_current(&ms_dir) {
        write_migrate_progress(
            "error",
            step,
            total_steps,
            "Update verification failed: migration metadata was not saved",
            Some("migration_metadata_not_saved"),
        );
        log_to_file(&format!("Migration to v{} failed because setup metadata was not saved", MIGRATE_VERSION));
        return;
    }

    let marker = post_update_marker_path(&ms_dir);
    let _ = fs::remove_file(&marker);
    let _ = fs::remove_file(migration_steam_config_backup_path(&ms_dir));

    kill_steam_wine();
    log_to_file("Migration: killed Wine/Steam processes after prefix init guard");

    write_migrate_progress("complete", total_steps, total_steps, "MetalSharp is updated and ready.", None);
    log_to_file(&format!("Migration to v{} finished (install_ok=true)", MIGRATE_VERSION));
}

fn update_migration_metadata(ms_dir: &Path) {
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
}

fn verify_migration_ready(ms_dir: &Path, marker: Option<&PostUpdateMigrationMarker>) -> Result<(), String> {
    if let Some(error) = post_update_target_error(marker) {
        return Err(error);
    }

    if !runtime_core_ready(ms_dir) {
        return Err("runtime bundle is still incomplete after install".into());
    }

    Ok(())
}

fn migration_metadata_current(ms_dir: &Path) -> bool {
    let setup_path = ms_dir.join("setup.json");
    let Ok(contents) = fs::read_to_string(setup_path) else {
        return false;
    };
    let Ok(cfg) = serde_json::from_str::<serde_json::Map<String, serde_json::Value>>(&contents) else {
        return false;
    };
    cfg.get("last_migrated_version").and_then(|v| v.as_str()) == Some(MIGRATE_VERSION)
        && cfg.get("runtime_migration_schema").and_then(|v| v.as_u64()).unwrap_or(0) >= MIGRATE_SCHEMA_VERSION
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

fn steam_update_process_alive(prefix_str: &str, process_output: &str) -> bool {
    let prefix = prefix_str.to_ascii_lowercase();
    process_output.lines().any(|line| {
        let lower = line.to_ascii_lowercase();
        lower.contains(&prefix)
            && (lower.contains("steam.exe")
                || lower.contains("steamupdate.exe")
                || lower.contains("steamwebhelper.exe"))
    })
}

fn force_kill_steam_update_processes_for_prefix(prefix_str: &str) {
    if prefix_str.trim().is_empty() {
        return;
    }

    for signal in ["-TERM", "-KILL"] {
        let output = match Command::new("ps").args(["axo", "pid=,command="]).output() {
            Ok(o) if o.status.success() => String::from_utf8_lossy(&o.stdout).to_string(),
            _ => return,
        };
        for pid in steam_process_pids_for_prefix(prefix_str, &output) {
            let _ = Command::new("kill").args([signal, &pid.to_string()]).status();
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
}

fn steam_process_pids_for_prefix(prefix_str: &str, process_output: &str) -> Vec<u32> {
    let prefix = prefix_str.to_ascii_lowercase();
    process_output
        .lines()
        .filter_map(|line| {
            let trimmed = line.trim_start();
            let mut parts = trimmed.splitn(2, char::is_whitespace);
            let pid_text = parts.next()?;
            let command = parts.next().unwrap_or("");
            let pid = pid_text.parse::<u32>().ok()?;
            let lower = command.to_ascii_lowercase();
            let is_steam = lower.contains("steam.exe")
                || lower.contains("steamupdate.exe")
                || lower.contains("steamwebhelper.exe");
            (lower.contains(&prefix) && is_steam).then_some(pid)
        })
        .collect()
}

fn run_pkill(args: &[&str]) {
    let Ok(mut child) = mac_cmd("pkill").args(args).spawn() else {
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
    prefix_steam_tmp: PathBuf,
    prefix_gptk_tmp: PathBuf,
    cache_tmp: PathBuf,
    games_tmp: PathBuf,
    sharp_library_tmp: PathBuf,
    bottles_tmp: PathBuf,
    compatdata_tmp: PathBuf,
    prefix_steam_dosdevice_links: Vec<(String, PathBuf)>,
    prefix_gptk_dosdevice_links: Vec<(String, PathBuf)>,
}

fn preserve_user_data(ms_dir: &PathBuf) -> PreservedData {
    let tmp = std::env::temp_dir().join(format!(
        "metalsharp-migration-preserve-{}-{}-{:x}",
        std::process::id(),
        temp_suffix(),
        std::hash::Hasher::finish(&std::collections::hash_map::DefaultHasher::new())
    ));
    let _ = fs::remove_dir_all(&tmp);
    let _ = fs::create_dir_all(&tmp);

    let setup_json = ms_dir.join("setup.json").exists().then(|| fs::read(ms_dir.join("setup.json")).ok()).flatten();

    let steam_config_json = read_preserved_steam_config(ms_dir);

    write_migrate_progress("running", 2, MIGRATION_TOTAL_STEPS, "Preserving user data (cache metadata)...", None);
    let cache_tmp = tmp.join("cache");
    let cache = ms_dir.join("cache");
    if cache.exists() {
        let _ = fs::create_dir_all(&cache_tmp);
        preserve_selective(&cache, &cache_tmp, &["downloads", "updates", "updater-tools", "tmp"]);
    }

    write_migrate_progress(
        "running",
        2,
        MIGRATION_TOTAL_STEPS,
        "Preserving user settings (Steam prefix metadata)...",
        None,
    );
    let prefix_steam_tmp = tmp.join("prefix-steam");
    let prefix_steam = ms_dir.join("prefix-steam");
    if prefix_steam.exists() {
        let _ = fs::create_dir_all(&prefix_steam_tmp);
        preserve_settings_only(&prefix_steam, &prefix_steam_tmp);
    }

    let prefix_gptk_tmp = tmp.join("prefix-gptk");
    let prefix_gptk = ms_dir.join("prefix-gptk");
    if prefix_gptk.exists() {
        let _ = fs::create_dir_all(&prefix_gptk_tmp);
        preserve_settings_only(&prefix_gptk, &prefix_gptk_tmp);
    }

    write_migrate_progress("running", 2, MIGRATION_TOTAL_STEPS, "Preserving user settings (game metadata)...", None);
    let games_tmp = tmp.join("games");
    let games = ms_dir.join("games");
    if games.exists() {
        let _ = fs::create_dir_all(&games_tmp);
        preserve_settings_only(&games, &games_tmp);
    }

    write_migrate_progress("running", 2, MIGRATION_TOTAL_STEPS, "Preserving user settings (library metadata)...", None);
    let sharp_library_tmp = tmp.join("sharp-library");
    let sharp_library = ms_dir.join("sharp-library");
    if sharp_library.exists() {
        let _ = fs::create_dir_all(&sharp_library_tmp);
        preserve_settings_only(&sharp_library, &sharp_library_tmp);
    }

    write_migrate_progress("running", 2, MIGRATION_TOTAL_STEPS, "Preserving user settings (bottle metadata)...", None);
    let bottles_tmp = tmp.join("bottles");
    let bottles = ms_dir.join("bottles");
    if bottles.exists() {
        let _ = fs::create_dir_all(&bottles_tmp);
        preserve_settings_only(&bottles, &bottles_tmp);
    }

    write_migrate_progress(
        "running",
        2,
        MIGRATION_TOTAL_STEPS,
        "Preserving user settings (compatdata metadata)...",
        None,
    );
    let compatdata_tmp = tmp.join("compatdata");
    let compatdata = ms_dir.join("compatdata");
    if compatdata.exists() {
        let _ = fs::create_dir_all(&compatdata_tmp);
        preserve_settings_only(&compatdata, &compatdata_tmp);
    }

    let prefix_steam_dosdevice_links = collect_prefix_dosdevice_links(&ms_dir.join("prefix-steam"));
    let prefix_gptk_dosdevice_links = if ms_dir.join("prefix-gptk").exists() {
        collect_prefix_dosdevice_links(&ms_dir.join("prefix-gptk"))
    } else {
        Vec::new()
    };

    PreservedData {
        setup_json,
        steam_config_json,
        prefix_steam_tmp,
        prefix_gptk_tmp,
        cache_tmp,
        games_tmp,
        sharp_library_tmp,
        bottles_tmp,
        compatdata_tmp,
        prefix_steam_dosdevice_links,
        prefix_gptk_dosdevice_links,
    }
}

fn update_existing_wine_prefixes(ms_dir: &Path, step: usize) -> Result<usize, String> {
    let runtime_wine = ms_dir.join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&runtime_wine);
    if !wine.exists() {
        return Err(format!("MetalSharp Wine not found at {}", wine.display()));
    }

    let mut updated = 0usize;
    for prefix in collect_existing_wine_prefixes(ms_dir) {
        let steam_library_drive_links = collect_steam_library_drive_links(&prefix);
        // Snapshot all dosdevice symlinks before the bounded prefix init can
        // rewrite them.
        // This covers custom drive mappings (external drives, Z:, etc.) that
        // Wine initialization may destroy.
        let all_dosdevice_links = collect_prefix_dosdevice_links(&prefix);

        write_migrate_progress(
            "running",
            step,
            MIGRATION_TOTAL_STEPS,
            &format!("Initializing Wine prefix {}...", prefix.display()),
            None,
        );

        // Pre-check: dosdevices must exist and be a directory.
        let dosdevices = prefix.join("dosdevices");
        if dosdevices.exists() && !dosdevices.is_dir() {
            log_to_file(&format!(
                "Migration: dosdevices exists but is not a directory for {} — skipping prefix init",
                prefix.display()
            ));
            continue;
        }

        let staged = stage_updated_prefix_runtime_surface(&runtime_wine, &prefix)?;
        log_to_file(&format!("Migration: staged {} refreshed runtime file(s) into {}", staged, prefix.display()));
        run_prefix_runtime_init(&wine, &runtime_wine, &prefix)?;
        guard_against_steam_opening_during_prefix_init(&prefix);

        restore_steam_library_drive_links(&prefix, &steam_library_drive_links);
        restore_prefix_dosdevice_links(&prefix, &all_dosdevice_links);

        // Post-check: verify critical dosdevice links survived prefix init.
        verify_prefix_dosdevices_integrity(&prefix);

        updated += 1;
    }

    Ok(updated)
}

fn collect_existing_wine_prefixes(ms_dir: &Path) -> Vec<PathBuf> {
    let mut prefixes = Vec::new();
    push_existing_prefix(&mut prefixes, ms_dir.join("prefix-steam"));
    prefixes
}

fn push_existing_prefix(prefixes: &mut Vec<PathBuf>, prefix: PathBuf) {
    if !prefix.exists() {
        return;
    }
    if prefixes.iter().any(|existing| existing == &prefix) {
        return;
    }
    prefixes.push(prefix);
}

fn stage_updated_prefix_runtime_surface(runtime_wine: &Path, prefix: &Path) -> Result<usize, String> {
    let system32 = prefix.join("drive_c").join("windows").join("system32");
    let unix_surface = prefix.join(".metalsharp").join("unix");
    fs::create_dir_all(&system32).map_err(|e| format!("create prefix system32 for {}: {}", prefix.display(), e))?;
    fs::create_dir_all(&unix_surface)
        .map_err(|e| format!("create prefix unix surface for {}: {}", prefix.display(), e))?;

    let mut copied = 0usize;
    for (subdir, filename) in prefix_runtime_dll_sources() {
        let source = runtime_wine.join(subdir).join(filename);
        if !source.is_file() {
            return Err(format!("required runtime DLL missing for prefix init: {}", source.display()));
        }
        let dest = system32.join(filename);
        copy_runtime_file_if_changed(&source, &dest)
            .map_err(|e| format!("stage {} to {}: {}", source.display(), dest.display(), e))?;
        copied += 1;
    }

    for filename in prefix_runtime_unix_sidecars() {
        let source = prefix_runtime_unix_source(runtime_wine, filename)
            .ok_or_else(|| format!("required Unix sidecar missing for prefix init: {}", filename))?;
        let dest = unix_surface.join(filename);
        copy_runtime_file_if_changed(&source, &dest)
            .map_err(|e| format!("stage {} to {}: {}", source.display(), dest.display(), e))?;
        copied += 1;
    }

    Ok(copied)
}

fn prefix_runtime_dll_sources() -> &'static [(&'static str, &'static str)] {
    &[
        ("lib/wine/x86_64-windows", "d3d9.dll"),
        ("lib/wine/x86_64-windows", "d3d10.dll"),
        ("lib/wine/x86_64-windows", "d3d10_1.dll"),
        ("lib/dxmt/x86_64-windows", "d3d10core.dll"),
        ("lib/dxmt/x86_64-windows", "d3d11.dll"),
        ("lib/dxmt/x86_64-windows", "d3d12.dll"),
        ("lib/dxmt/x86_64-windows", "dxgi.dll"),
        ("lib/dxmt/x86_64-windows", "dxgi_dxmt.dll"),
        ("lib/dxmt/x86_64-windows", "winemetal.dll"),
        ("lib/dxmt/x86_64-windows", "nvapi64.dll"),
        ("lib/dxmt/x86_64-windows", "nvngx.dll"),
        ("lib/metalsharp/x86_64-windows", "metalsharp_ntdll_hook.dll"),
    ]
}

fn prefix_runtime_unix_sidecars() -> &'static [&'static str] {
    &["winemetal.so", "winemac.so", "ntdll.so", "libc++.1.dylib", "libc++abi.1.dylib", "libunwind.1.dylib"]
}

fn prefix_runtime_unix_source(runtime_wine: &Path, filename: &str) -> Option<PathBuf> {
    [
        runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join(filename),
        runtime_wine.join("lib").join("wine").join("x86_64-unix").join(filename),
    ]
    .into_iter()
    .find(|path| path.is_file())
}

fn copy_runtime_file_if_changed(source: &Path, dest: &Path) -> std::io::Result<()> {
    if dest.is_file() && files_match(source, dest) {
        return Ok(());
    }
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::copy(source, dest)?;
    Ok(())
}

fn files_match(left: &Path, right: &Path) -> bool {
    match (fs::read(left), fs::read(right)) {
        (Ok(left), Ok(right)) => left == right,
        _ => false,
    }
}

fn run_prefix_runtime_init(wine: &Path, runtime_wine: &Path, prefix: &Path) -> Result<(), String> {
    let mut cmd = Command::new(wine);
    cmd.env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "/usr/bin/true")
        .env("WINEDLLOVERRIDES", "winedbg=d;steam,steamwebhelper,steamservice=d")
        .env("WINEDLLPATH", prefix_runtime_winedllpath(runtime_wine))
        .args(MIGRATION_PREFIX_INIT_ARGS)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, runtime_wine);

    log_to_file(&format!("Starting bounded prefix runtime init for prefix: {}", prefix.display()));

    let mut child = cmd.spawn().map_err(|e| {
        log_to_file(&format!("Failed to spawn prefix runtime init for {}: {}", prefix.display(), e));
        format!("spawn prefix runtime init for {}: {}", prefix.display(), e)
    })?;

    for _ in 0..(MIGRATION_PREFIX_INIT_TIMEOUT_SECS * 2) {
        if let Some(status) = child.try_wait().map_err(|e| {
            log_to_file(&format!("Failed to wait for prefix runtime init for {}: {}", prefix.display(), e));
            format!("wait prefix runtime init for {}: {}", prefix.display(), e)
        })? {
            if status.success() {
                log_to_file(&format!("Prefix runtime init completed successfully for prefix: {}", prefix.display()));
                return Ok(());
            }
            let error_msg =
                format!("prefix runtime init failed for {} with exit code: {:?}", prefix.display(), status.code());
            log_to_file(&error_msg);
            return Err(error_msg);
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }

    let error_msg = format!(
        "prefix runtime init exceeded {} seconds for {}; continuing after killing init process",
        MIGRATION_PREFIX_INIT_TIMEOUT_SECS,
        prefix.display()
    );
    log_to_file(&error_msg);
    let _ = child.kill();
    let _ = child.wait();
    Ok(())
}

fn prefix_runtime_winedllpath(runtime_wine: &Path) -> String {
    [
        runtime_wine.join("lib").join("dxmt").join("x86_64-windows"),
        runtime_wine.join("lib").join("wine").join("x86_64-windows"),
        runtime_wine.join("lib").join("metalsharp").join("x86_64-windows"),
    ]
    .iter()
    .map(|path| path.to_string_lossy().to_string())
    .collect::<Vec<_>>()
    .join(":")
}

fn guard_against_steam_opening_during_prefix_init(prefix: &Path) {
    let prefix_str = prefix.to_string_lossy().to_string();
    for _ in 0..(MIGRATION_PREFIX_INIT_TIMEOUT_SECS * 2) {
        let output = match Command::new("ps").args(["axo", "pid=,command="]).output() {
            Ok(o) if o.status.success() => String::from_utf8_lossy(&o.stdout).to_string(),
            _ => return,
        };
        if steam_update_process_alive(&prefix_str, &output) {
            log_to_file("Migration: Steam attempted to open during prefix init; killing prefix-scoped Steam processes");
            force_kill_steam_update_processes_for_prefix(&prefix_str);
            return;
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct SteamLibraryDriveLink {
    drive: char,
    target: PathBuf,
    library_path: String,
}

fn collect_steam_library_drive_links(prefix: &Path) -> Vec<SteamLibraryDriveLink> {
    let mut links = Vec::new();
    for libraryfolders in steam_libraryfolders_files(prefix) {
        let Ok(contents) = fs::read_to_string(&libraryfolders) else {
            continue;
        };
        for line in contents.lines() {
            let Some(path) = parse_steam_vdf_path(line) else {
                continue;
            };
            let Some((drive, rest)) = split_steam_library_wine_path(&path) else {
                continue;
            };
            let Some(target) = resolve_steam_library_drive_target(prefix, drive, &rest) else {
                continue;
            };
            if links.iter().any(|link: &SteamLibraryDriveLink| link.drive.eq_ignore_ascii_case(&drive)) {
                continue;
            }
            links.push(SteamLibraryDriveLink { drive: drive.to_ascii_lowercase(), target, library_path: path });
        }
    }
    links
}

fn steam_libraryfolders_files(prefix: &Path) -> Vec<PathBuf> {
    let steam = prefix.join("drive_c").join("Program Files (x86)").join("Steam");
    vec![steam.join("config").join("libraryfolders.vdf"), steam.join("steamapps").join("libraryfolders.vdf")]
}

fn parse_steam_vdf_path(line: &str) -> Option<String> {
    let trimmed = line.trim();
    if !trimmed.starts_with("\"path\"") {
        return None;
    }
    let mut quoted = Vec::new();
    let mut chars = trimmed.char_indices().peekable();
    while let Some((start, ch)) = chars.next() {
        if ch != '"' {
            continue;
        }
        let value_start = start + ch.len_utf8();
        let mut escaped = false;
        for (end, value_ch) in chars.by_ref() {
            if escaped {
                escaped = false;
                continue;
            }
            if value_ch == '\\' {
                escaped = true;
                continue;
            }
            if value_ch == '"' {
                quoted.push(&trimmed[value_start..end]);
                break;
            }
        }
    }
    quoted.get(1).map(|value| value.replace("\\\\", "\\"))
}

fn split_steam_library_wine_path(path: &str) -> Option<(char, String)> {
    let mut chars = path.chars();
    let drive = chars.next()?;
    if chars.next()? != ':' || !drive.is_ascii_alphabetic() || drive.eq_ignore_ascii_case(&'c') {
        return None;
    }
    let rest = path.get(2..)?.replace('\\', "/");
    if !rest.starts_with('/') {
        return None;
    }
    Some((drive.to_ascii_lowercase(), rest))
}

fn resolve_steam_library_drive_target(prefix: &Path, drive: char, rest: &str) -> Option<PathBuf> {
    let dosdevice = prefix.join("dosdevices").join(format!("{}:", drive.to_ascii_lowercase()));
    if let Ok(target) = fs::read_link(&dosdevice) {
        if steam_library_target_has_steamapps(&target, rest) {
            return Some(target);
        }
    }

    if steam_library_path_prefers_root_mapping(rest) {
        return Some(PathBuf::from("/"));
    }

    let root = Path::new("/");
    if steam_library_target_has_steamapps(root, rest) {
        return Some(root.to_path_buf());
    }

    None
}

fn steam_library_path_prefers_root_mapping(rest: &str) -> bool {
    rest.starts_with("/Volumes/")
        || rest.starts_with("/Users/")
        || rest.starts_with("/private/")
        || rest.starts_with("/Applications/")
}

fn steam_library_target_has_steamapps(target: &Path, rest: &str) -> bool {
    target.join(rest.trim_start_matches('/')).join("steamapps").exists()
}

fn restore_steam_library_drive_links(prefix: &Path, links: &[SteamLibraryDriveLink]) {
    if links.is_empty() {
        return;
    }

    let dosdevices = prefix.join("dosdevices");
    if let Err(e) = fs::create_dir_all(&dosdevices) {
        log_to_file(&format!("Migration prefix update: failed to create dosdevices for {}: {}", prefix.display(), e));
        return;
    }

    for link in links {
        let dosdevice = dosdevices.join(format!("{}:", link.drive.to_ascii_lowercase()));
        if let Ok(current) = fs::read_link(&dosdevice) {
            if current == link.target {
                continue;
            }
        }

        match fs::symlink_metadata(&dosdevice) {
            Ok(meta) if meta.file_type().is_symlink() || meta.is_file() => {
                if let Err(e) = fs::remove_file(&dosdevice) {
                    log_to_file(&format!(
                        "Migration prefix update: failed to remove stale {} for Steam library {}: {}",
                        dosdevice.display(),
                        link.library_path,
                        e
                    ));
                    continue;
                }
            },
            Ok(meta) if meta.is_dir() => {
                log_to_file(&format!(
                    "Migration prefix update: skipped Steam library drive {} because {} is a directory",
                    link.library_path,
                    dosdevice.display()
                ));
                continue;
            },
            Ok(_) => {
                if let Err(e) = fs::remove_file(&dosdevice) {
                    log_to_file(&format!(
                        "Migration prefix update: failed to remove stale {} for Steam library {}: {}",
                        dosdevice.display(),
                        link.library_path,
                        e
                    ));
                    continue;
                }
            },
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {},
            Err(e) => {
                log_to_file(&format!(
                    "Migration prefix update: failed to inspect {} for Steam library {}: {}",
                    dosdevice.display(),
                    link.library_path,
                    e
                ));
                continue;
            },
        }

        match std::os::unix::fs::symlink(&link.target, &dosdevice) {
            Ok(()) => log_to_file(&format!(
                "Migration prefix update: restored Steam library drive {} -> {} for {}",
                dosdevice.display(),
                link.target.display(),
                link.library_path
            )),
            Err(e) => log_to_file(&format!(
                "Migration prefix update: failed to restore Steam library drive {} -> {} for {}: {}",
                dosdevice.display(),
                link.target.display(),
                link.library_path,
                e
            )),
        }
    }
}

fn verify_prefix_dosdevices_integrity(prefix: &Path) {
    let dosdevices = prefix.join("dosdevices");
    if !dosdevices.is_dir() {
        log_to_file(&format!("Migration: dosdevices directory missing after prefix init for {}", prefix.display()));
        let _ = fs::create_dir_all(&dosdevices);
    }

    // Verify c: -> drive_c exists — this is the critical Wine system drive.
    let c_drive = dosdevices.join("c:");
    let expected_c = prefix.join("drive_c");
    let c_ok = match fs::read_link(&c_drive) {
        Ok(target) => {
            // Wine may use relative (../drive_c) or absolute path.
            let resolved = if target.is_relative() { dosdevices.join(&target) } else { target };
            match fs::canonicalize(&resolved) {
                Ok(resolved) => match fs::canonicalize(&expected_c) {
                    Ok(expected) => resolved == expected,
                    Err(_) => true, // Can't verify — assume OK
                },
                Err(_) => true, // Can't resolve — assume OK
            }
        },
        Err(_) => false,
    };

    if !c_ok {
        if c_drive.exists() {
            let _ = fs::remove_file(&c_drive);
        }
        match std::os::unix::fs::symlink("../drive_c", &c_drive) {
            Ok(()) => log_to_file(&format!("Migration: recreated c: -> drive_c for {}", prefix.display())),
            Err(e) => {
                log_to_file(&format!("Migration: failed to recreate c: dosdevice for {}: {}", prefix.display(), e))
            },
        }
    }
}

fn temp_suffix() -> u128 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos()
}

fn is_dmg_mount_target(target: &Path) -> bool {
    let Some(name) = target.file_name().and_then(|n| n.to_str()) else {
        return false;
    };
    let lower = name.to_ascii_lowercase();
    if !lower.contains("metalsharp") {
        return false;
    }
    lower.contains("-arm64") || lower.contains("-x86_64") || lower.contains(".dmg") || lower.contains("-intel")
}

fn collect_prefix_dosdevice_links(prefix: &Path) -> Vec<(String, PathBuf)> {
    let dosdevices = prefix.join("dosdevices");
    if !dosdevices.is_dir() {
        return Vec::new();
    }
    let mut links = Vec::new();
    let entries = match fs::read_dir(&dosdevices) {
        Ok(e) => e,
        Err(_) => return links,
    };
    for entry in entries.flatten() {
        let name = match entry.file_name().to_str() {
            Some(n) => n.to_string(),
            None => continue,
        };
        if name == "c:" {
            continue;
        }
        if let Ok(target) = fs::read_link(entry.path()) {
            if target == Path::new("/") {
                continue;
            }
            if is_dmg_mount_target(&target) {
                continue;
            }
            links.push((name, target));
        }
    }
    links
}

fn restore_prefix_dosdevice_links(prefix: &Path, links: &[(String, PathBuf)]) {
    if links.is_empty() {
        return;
    }
    let dosdevices = prefix.join("dosdevices");
    if !dosdevices.exists() {
        let _ = fs::create_dir_all(&dosdevices);
    }
    for (name, target) in links {
        if is_dmg_mount_target(target) {
            log_to_file(&format!(
                "Migration restore dosdevices: skipping DMG mount target {} -> {}",
                name,
                target.display()
            ));
            continue;
        }
        let link_path = dosdevices.join(name);
        if let Ok(current) = fs::read_link(&link_path) {
            if current == *target {
                continue;
            }
        }
        match fs::symlink_metadata(&link_path) {
            Ok(meta) if meta.file_type().is_symlink() || meta.is_file() => {
                if let Err(e) = fs::remove_file(&link_path) {
                    log_to_file(&format!(
                        "Migration restore dosdevices: failed to remove stale {}: {}",
                        link_path.display(),
                        e
                    ));
                    continue;
                }
            },
            Ok(meta) if meta.is_dir() => {
                log_to_file(&format!(
                    "Migration restore dosdevices: skipped {} because it is a directory",
                    link_path.display()
                ));
                continue;
            },
            Ok(_) => {
                if let Err(e) = fs::remove_file(&link_path) {
                    log_to_file(&format!(
                        "Migration restore dosdevices: failed to remove stale {}: {}",
                        link_path.display(),
                        e
                    ));
                    continue;
                }
            },
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {},
            Err(e) => {
                log_to_file(&format!("Migration restore dosdevices: failed to inspect {}: {}", link_path.display(), e));
                continue;
            },
        }
        match std::os::unix::fs::symlink(target, &link_path) {
            Ok(()) => log_to_file(&format!(
                "Migration restore dosdevices: restored {} -> {}",
                link_path.display(),
                target.display()
            )),
            Err(e) => log_to_file(&format!(
                "Migration restore dosdevices: failed to restore {} -> {}: {}",
                link_path.display(),
                target.display(),
                e
            )),
        }
    }
}

fn preserve_selective(src: &PathBuf, dst: &PathBuf, skip_names: &[&str]) {
    let entries = match fs::read_dir(src) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name_str = name.to_string_lossy().to_string();
        let src_path = entry.path();
        let dst_path = dst.join(&name);

        if skip_names.contains(&name_str.as_str()) {
            continue;
        }

        let meta = match fs::symlink_metadata(&src_path) {
            Ok(m) => m,
            Err(_) => continue,
        };

        if meta.file_type().is_symlink() {
            continue;
        } else if meta.is_dir() {
            let _ = fs::create_dir_all(&dst_path);
            preserve_selective(&src_path, &dst_path, skip_names);
        } else {
            let _ = fs::copy(&src_path, &dst_path);
        }
    }
}

fn preserve_settings_only(src: &PathBuf, dst: &PathBuf) {
    let entries = match fs::read_dir(src) {
        Ok(entries) => entries,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name_str = name.to_string_lossy().to_string();
        let src_path = entry.path();
        let dst_path = dst.join(&name);

        if migration_preserve_denies_name(&name_str) {
            continue;
        }

        let meta = match fs::symlink_metadata(&src_path) {
            Ok(m) => m,
            Err(_) => continue,
        };

        if meta.file_type().is_symlink() {
            continue;
        } else if meta.is_dir() {
            let before = count_settings_files(&dst_path);
            preserve_settings_only(&src_path, &dst_path);
            if count_settings_files(&dst_path) == before
                && fs::read_dir(&dst_path).map(|mut e| e.next().is_none()).unwrap_or(false)
            {
                let _ = fs::remove_dir(&dst_path);
            }
        } else if migration_preserve_allows_file(&src_path) {
            if let Some(parent) = dst_path.parent() {
                let _ = fs::create_dir_all(parent);
            }
            if let Err(e) = fs::copy(&src_path, &dst_path) {
                log_to_file(&format!("Migration preserve: failed to copy {}: {}", src_path.display(), e));
            }
        }
    }
}

fn count_settings_files(path: &Path) -> usize {
    let mut count = 0usize;
    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            let p = entry.path();
            let meta = match fs::symlink_metadata(&p) {
                Ok(meta) => meta,
                Err(_) => continue,
            };
            if meta.file_type().is_symlink() {
                continue;
            } else if meta.is_dir() {
                count += count_settings_files(&p);
            } else if meta.is_file() {
                count += 1;
            }
        }
    }
    count
}

fn migration_preserve_denies_name(name: &str) -> bool {
    MIGRATION_PAYLOAD_DENY_NAMES.iter().any(|denied| name.eq_ignore_ascii_case(denied))
}

fn migration_preserve_allows_file(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
        return false;
    };

    if migration_preserve_denies_name(name) {
        return false;
    }

    if MIGRATION_SETTINGS_FILE_NAMES.iter().any(|allowed| name.eq_ignore_ascii_case(allowed)) {
        return true;
    }

    path.extension()
        .and_then(|ext| ext.to_str())
        .map(|ext| MIGRATION_SETTINGS_EXTENSIONS.iter().any(|allowed| ext.eq_ignore_ascii_case(allowed)))
        .unwrap_or(false)
}

fn clear_steam_crash_marker(ms_dir: &Path) {
    let crash_file =
        ms_dir.join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam").join(".crash");
    if crash_file.exists() {
        match fs::remove_file(&crash_file) {
            Ok(()) => log_to_file("Migration: cleared Steam .crash marker"),
            Err(e) => log_to_file(&format!("Migration: failed to clear Steam .crash marker: {}", e)),
        }
    }
}

fn register_external_steam_libraries(ms_dir: &Path) {
    let prefix = ms_dir.join("prefix-steam");
    if !prefix.exists() {
        return;
    }

    let steamapps_dir = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");
    if !steamapps_dir.exists() {
        return;
    }

    let external_libs = discover_external_steam_libraries();
    if external_libs.is_empty() {
        log_to_file("Migration: no external Steam libraries found on mounted volumes");
        return;
    }

    log_to_file(&format!(
        "Migration: found {} external Steam librar(y/ies), registering Wine drive mappings",
        external_libs.len()
    ));

    let dosdevices = prefix.join("dosdevices");
    let mut next_drive = find_next_available_drive_letter(&dosdevices);

    for lib in &external_libs {
        if next_drive.is_none() {
            log_to_file("Migration: no more drive letters available for external libraries");
            break;
        }
        let drive = next_drive.unwrap();
        let drive_link = dosdevices.join(format!("{}:", drive));

        if let Err(e) = std::os::unix::fs::symlink(&lib.unix_path, &drive_link) {
            log_to_file(&format!(
                "Migration: failed to create dosdevice {} -> {}: {}",
                drive,
                lib.unix_path.display(),
                e
            ));
            continue;
        }

        log_to_file(&format!(
            "Migration: mapped Wine drive {} -> {} for Steam library '{}'",
            drive,
            lib.unix_path.display(),
            lib.unix_path.display()
        ));

        next_drive = next_drive_after(drive).and_then(|d| find_next_available_drive_letter_from(&dosdevices, d));
    }

    if let Err(e) = update_libraryfolders_vdf(&steamapps_dir, &external_libs) {
        log_to_file(&format!("Migration: failed to update libraryfolders.vdf: {}", e));
    }
}

struct ExternalSteamLibrary {
    unix_path: PathBuf,
}

fn discover_external_steam_libraries() -> Vec<ExternalSteamLibrary> {
    discover_external_steam_libraries_from(Path::new("/Volumes"))
}

fn discover_external_steam_libraries_from(volumes_dir: &Path) -> Vec<ExternalSteamLibrary> {
    let mut libs = Vec::new();
    let volumes = match fs::read_dir(volumes_dir) {
        Ok(entries) => entries,
        Err(_) => return libs,
    };

    for entry in volumes.flatten() {
        let mount_point = entry.path();
        if !mount_point.is_dir() {
            continue;
        }

        let is_system = mount_point
            .file_name()
            .and_then(|n| n.to_str())
            .map(|name| {
                let lower = name.to_ascii_lowercase();
                lower.starts_with("macintosh") || lower.contains("metalsharp") || lower.starts_with(".")
            })
            .unwrap_or(true);
        if is_system {
            continue;
        }

        // Exhaustive candidate list for Steam libraries on external drives.
        // Covers common layouts: dedicated SteamLibrary, nested, and volume-root.
        let candidates = [
            // Standard Steam library layout on external drives
            mount_point.join("SteamLibrary").join("SteamLibrary"),
            mount_point.join("SteamLibrary"),
            // Some Steam installs nest under a steamapps parent
            mount_point.join("steamapps").join(".."),
            // Volume root itself may be the library (e.g. AverySSD with SteamLibrary at root)
            mount_point.clone(),
        ];

        for candidate in &candidates {
            let steamapps = candidate.join("steamapps");
            if !steamapps.is_dir() {
                continue;
            }
            // Accept a library if it has steamapps with manifests, OR if
            // steamapps is a valid directory (some installs are in-progress
            // or have manifest files in non-standard locations).
            let has_manifests = fs::read_dir(&steamapps)
                .map(|entries| entries.flatten().any(|e| e.file_name().to_string_lossy().starts_with("appmanifest_")))
                .unwrap_or(false);
            if has_manifests {
                // Deduplicate — skip if we already found this path
                if !libs.iter().any(|lib| lib.unix_path == *candidate) {
                    libs.push(ExternalSteamLibrary { unix_path: candidate.clone() });
                }
                break;
            }
        }
    }

    libs
}

fn find_next_available_drive_letter(dosdevices: &Path) -> Option<char> {
    find_next_available_drive_letter_from(dosdevices, 'd')
}

fn find_next_available_drive_letter_from(dosdevices: &Path, start: char) -> Option<char> {
    let mut c = start;
    loop {
        let link_path = dosdevices.join(format!("{}:", c));
        if !link_path.exists() {
            return Some(c);
        }
        c = (c as u8 + 1) as char;
        if c > 'z' {
            return None;
        }
    }
}

fn next_drive_after(drive: char) -> Option<char> {
    let next = (drive as u8 + 1) as char;
    if next <= 'z' {
        Some(next)
    } else {
        None
    }
}

fn update_libraryfolders_vdf(steamapps_dir: &Path, external_libs: &[ExternalSteamLibrary]) -> Result<(), String> {
    let lf_path = steamapps_dir.join("libraryfolders.vdf");
    let existing = fs::read_to_string(&lf_path).unwrap_or_default();

    let mut max_index: u32 = 0;
    let mut existing_paths: Vec<String> = Vec::new();

    for line in existing.lines() {
        let trimmed = line.trim();
        if let Some(idx_str) = trimmed.strip_prefix('"') {
            if let Some(end) = idx_str.find('"') {
                if let Ok(idx) = idx_str[..end].parse::<u32>() {
                    if idx > max_index {
                        max_index = idx;
                    }
                }
            }
        }
        if trimmed.starts_with("\"path\"") {
            let mut quoted = Vec::new();
            let mut chars = trimmed.char_indices().peekable();
            while let Some((start, ch)) = chars.next() {
                if ch != '"' {
                    continue;
                }
                let value_start = start + ch.len_utf8();
                let mut escaped = false;
                for (end, value_ch) in chars.by_ref() {
                    if escaped {
                        escaped = false;
                        continue;
                    }
                    if value_ch == '\\' {
                        escaped = true;
                        continue;
                    }
                    if value_ch == '"' {
                        quoted.push(&trimmed[value_start..end]);
                        break;
                    }
                }
            }
            if let Some(path_val) = quoted.get(1) {
                existing_paths.push(path_val.to_string());
            }
        }
    }

    let dosdevices = steamapps_dir.join("..").join("..").join("..").join("dosdevices");

    let mut additions = String::new();
    for lib in external_libs {
        let windows_path = match resolve_unix_path_to_windows(&lib.unix_path, &dosdevices) {
            Some(p) => p,
            None => {
                log_to_file(&format!("Migration: could not resolve Windows path for {}", lib.unix_path.display()));
                continue;
            },
        };

        let already_registered = existing_paths.iter().any(|p| {
            let normalized = p.replace('\\', "/");
            let lib_normalized = windows_path.replace('\\', "/");
            normalized.eq_ignore_ascii_case(&lib_normalized)
        });
        if already_registered {
            log_to_file(&format!(
                "Migration: external library {} already registered in libraryfolders.vdf",
                lib.unix_path.display()
            ));
            continue;
        }

        max_index += 1;
        additions.push_str(&format!("\t\"{}\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n", max_index, windows_path));
        log_to_file(&format!("Migration: added external library entry {} -> {}", max_index, windows_path));
    }

    if additions.is_empty() {
        return Ok(());
    }

    let new_content = if let Some(pos) = existing.rfind('}') {
        let mut result = existing[..=pos].to_string();
        result.push_str(&additions);
        result.push('}');
        result
    } else {
        return Err("libraryfolders.vdf has unexpected format".into());
    };

    fs::write(&lf_path, &new_content).map_err(|e| format!("failed to write libraryfolders.vdf: {}", e))
}

fn resolve_unix_path_to_windows(unix_path: &Path, dosdevices: &Path) -> Option<String> {
    // First pass: check existing dosdevice mappings (dedicated drive letters).
    let entries = fs::read_dir(dosdevices).ok()?;
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        if !name_str.ends_with(':') || name_str == "c:" {
            continue;
        }
        let target = fs::read_link(entry.path()).ok()?;
        if unix_path.starts_with(&target) {
            let drive = &name_str[..name_str.len() - 1];
            let rest = match unix_path.strip_prefix(&target) {
                Ok(r) => r,
                Err(_) => continue,
            };
            let rest_str = rest.to_string_lossy();
            let windows_rest: String = rest_str.replace('/', "\\\\");
            if windows_rest.is_empty() {
                return Some(format!("{}:\\\\", drive.to_uppercase()));
            } else {
                return Some(format!("{}:\\\\{}", drive.to_uppercase(), windows_rest.trim_start_matches('\\')));
            }
        }
    }

    // Fallback: use Z: drive (Wine's standard mapping of Z: -> /).
    // External volumes like /Volumes/AverySSD/SteamLibrary become
    // Z:\\Volumes\\AverySSD\\SteamLibrary in Wine.
    ensure_z_drive(dosdevices);
    let unix_str = unix_path.to_string_lossy();
    if unix_str.starts_with('/') {
        let windows_path = format!("Z:{}", unix_str.replace('/', "\\\\"));
        log_to_file(&format!("Migration: resolved external library to Z: path: {}", windows_path));
        return Some(windows_path);
    }
    None
}

fn ensure_z_drive(dosdevices: &Path) {
    let z_drive = dosdevices.join("z:");
    if z_drive.exists() {
        return;
    }
    match std::os::unix::fs::symlink("/", &z_drive) {
        Ok(()) => log_to_file("Migration: created Z: drive mapping to /"),
        Err(e) => log_to_file(&format!("Migration: failed to create Z: drive: {}", e)),
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
    let steam_config_json = preserved.steam_config_json.as_ref().map(|data| normalize_steam_config_json(data));
    let steam_api_key_restored = steam_config_json.as_deref().is_some_and(steam_config_has_api_key);

    if preserved.prefix_steam_tmp.exists() {
        let dst = ms_dir.join("prefix-steam");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        remove_root_dosdevice_mapping(&dst);
        preserve_settings_only(&preserved.prefix_steam_tmp, &dst);

        let dosdevices = dst.join("dosdevices");
        if !dosdevices.exists() {
            let _ = fs::create_dir_all(&dosdevices);
        }
        let c_link = dosdevices.join("c:");
        if !c_link.exists() {
            let _ = std::os::unix::fs::symlink("../drive_c", &c_link);
        }
        remove_root_dosdevice_mapping(&dst);
    }

    restore_prefix_dosdevice_links(&ms_dir.join("prefix-steam"), &preserved.prefix_steam_dosdevice_links);

    if preserved.prefix_gptk_tmp.exists() {
        let dst = ms_dir.join("prefix-gptk");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        preserve_settings_only(&preserved.prefix_gptk_tmp, &dst);

        let dosdevices = dst.join("dosdevices");
        if !dosdevices.exists() {
            let _ = fs::create_dir_all(&dosdevices);
        }
    }
    restore_prefix_dosdevice_links(&ms_dir.join("prefix-gptk"), &preserved.prefix_gptk_dosdevice_links);

    if preserved.games_tmp.exists() {
        let dst = ms_dir.join("games");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        preserve_settings_only(&preserved.games_tmp, &dst);
    }

    if preserved.sharp_library_tmp.exists() {
        let dst = ms_dir.join("sharp-library");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        preserve_settings_only(&preserved.sharp_library_tmp, &dst);
    }

    if preserved.bottles_tmp.exists() {
        let dst = ms_dir.join("bottles");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        preserve_settings_only(&preserved.bottles_tmp, &dst);
    }

    if preserved.compatdata_tmp.exists() {
        let dst = ms_dir.join("compatdata");
        if !dst.exists() {
            let _ = fs::create_dir_all(&dst);
        }
        preserve_settings_only(&preserved.compatdata_tmp, &dst);
    }

    if let Some(ref data) = preserved.setup_json {
        restore_setup_json(ms_dir, data, steam_api_key_restored);
    }

    if let Some(ref data) = steam_config_json {
        restore_steam_config(ms_dir, data);
    }
}

fn steam_config_path(ms_dir: &Path) -> PathBuf {
    ms_dir.join("cache").join("steam_config.json")
}

fn migration_steam_config_backup_path(ms_dir: &Path) -> PathBuf {
    ms_dir.join(".migration-steam_config.json")
}

fn read_preserved_steam_config(ms_dir: &Path) -> Option<Vec<u8>> {
    for path in [steam_config_path(ms_dir), migration_steam_config_backup_path(ms_dir)] {
        let Ok(data) = fs::read(&path) else {
            continue;
        };
        let normalized = normalize_steam_config_json(&data);
        if steam_config_has_api_key(&normalized) {
            let _ = fs::write(migration_steam_config_backup_path(ms_dir), &normalized);
        }
        return Some(normalized);
    }
    None
}

fn normalize_steam_config_json(data: &[u8]) -> Vec<u8> {
    let Ok(mut cfg) = serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(data) else {
        return data.to_vec();
    };

    let mut changed = false;
    let has_runtime_key = cfg.get("steam_api_key").and_then(|v| v.as_str()).is_some_and(|key| !key.is_empty());
    if !has_runtime_key {
        if let Some(legacy_key) =
            cfg.get("api_key").and_then(|v| v.as_str()).filter(|key| !key.is_empty()).map(str::to_string)
        {
            cfg.insert("steam_api_key".into(), json!(legacy_key));
            changed = true;
        }
    }

    if !changed {
        return data.to_vec();
    }

    serde_json::to_vec_pretty(&cfg).unwrap_or_else(|_| data.to_vec())
}

fn steam_config_has_api_key(data: &[u8]) -> bool {
    serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(data)
        .ok()
        .and_then(|cfg| cfg.get("steam_api_key").and_then(|v| v.as_str()).map(str::to_string))
        .is_some_and(|key| !key.is_empty())
}

fn restore_setup_json(ms_dir: &Path, data: &[u8], steam_api_key_restored: bool) {
    if steam_api_key_restored {
        if let Ok(mut cfg) = serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(data) {
            cfg.insert("steamApiKeySet".into(), json!(true));
            if let Ok(serialized) = serde_json::to_vec_pretty(&cfg) {
                let _ = fs::write(ms_dir.join("setup.json"), serialized);
                return;
            }
        }
    }
    let _ = fs::write(ms_dir.join("setup.json"), data);
}

fn restore_steam_config(ms_dir: &Path, data: &[u8]) {
    let normalized = normalize_steam_config_json(data);
    let cache_dir = ms_dir.join("cache");
    let _ = fs::create_dir_all(&cache_dir);
    let _ = fs::write(cache_dir.join("steam_config.json"), &normalized);
    if steam_config_has_api_key(&normalized) {
        let _ = fs::write(migration_steam_config_backup_path(ms_dir), &normalized);
    }
}

fn remove_root_dosdevice_mapping(prefix: &Path) {
    let z_link = prefix.join("dosdevices").join("z:");
    let Ok(meta) = fs::symlink_metadata(&z_link) else {
        return;
    };
    if meta.file_type().is_symlink() && fs::read_link(&z_link).map(|target| target == Path::new("/")).unwrap_or(false) {
        let _ = fs::remove_file(z_link);
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
    let log_dir = crate::platform::metalsharp_home_dir_for(&home).join("logs");
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
        fs::create_dir_all(crate::platform::metalsharp_home_dir_for(&home)).expect("create ms dir");

        assert!(runtime_needs_repair(&home, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn incomplete_fresh_setups_do_not_enter_migration_without_prefix() {
        let home = test_dir("fresh-incomplete");
        fs::create_dir_all(crate::platform::metalsharp_home_dir_for(&home)).expect("create ms dir");

        assert!(!runtime_needs_repair(&home, false));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn complete_runtime_does_not_request_repair() {
        let home = test_dir("complete-runtime");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        write_runtime_core(&ms_dir);

        assert!(runtime_core_ready(&ms_dir));
        assert!(!runtime_needs_repair(&home, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn missing_beta7_foundation_requests_repair() {
        let home = test_dir("missing-beta7-foundation");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        write_runtime_core(&ms_dir);

        fs::remove_file(
            ms_dir
                .join("runtime")
                .join("wine")
                .join("lib")
                .join("gptk")
                .join("x86_64-windows")
                .join("nvngx-on-metalfx.dll"),
        )
        .expect("remove beta7 foundation file");

        assert!(!runtime_core_ready(&ms_dir));
        assert!(runtime_needs_repair(&home, true));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn missing_dxmt_manifest_requests_repair() {
        let home = test_dir("missing-dxmt-manifest");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        write_runtime_core(&ms_dir);

        fs::remove_file(
            ms_dir.join("runtime").join("wine").join("lib").join("dxmt").join("metalsharp-dxmt-runtime.json"),
        )
        .expect("remove DXMT manifest");

        assert!(!runtime_core_ready(&ms_dir));
        assert!(runtime_needs_repair(&home, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn missing_metalsharp_hook_requests_runtime_repair() {
        let home = test_dir("missing-metalsharp-hook");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        write_runtime_core(&ms_dir);

        fs::remove_file(
            ms_dir
                .join("runtime")
                .join("wine")
                .join("lib")
                .join("metalsharp")
                .join("x86_64-windows")
                .join("metalsharp_ntdll_hook.dll"),
        )
        .expect("remove MetalSharp ntdll hook");

        assert!(!runtime_core_ready(&ms_dir));
        assert!(runtime_needs_repair(&home, true));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_collects_existing_wine_prefixes_for_update() {
        let home = test_dir("prefix-update");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let steam_prefix = ms_dir.join("prefix-steam");
        let bottle_dir = ms_dir.join("bottles").join("installer_demo");
        let bottle_prefix = bottle_dir.join("prefix");
        fs::create_dir_all(&steam_prefix).expect("create Steam prefix");
        fs::create_dir_all(&bottle_prefix).expect("create bottle prefix");
        fs::write(
            bottle_dir.join("bottle.json"),
            serde_json::to_vec(&json!({
                "id": "installer_demo",
                "prefix_path": bottle_prefix.to_string_lossy().to_string(),
            }))
            .expect("serialize bottle manifest"),
        )
        .expect("write bottle manifest");

        let prefixes = collect_existing_wine_prefixes(&ms_dir);

        assert!(prefixes.contains(&steam_prefix));
        assert!(!prefixes.contains(&bottle_prefix));
        assert_eq!(prefixes.len(), 1);
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_preserves_bottles_across_runtime_cleanup() {
        let home = test_dir("preserve-bottles");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
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
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
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
    fn migration_does_not_preserve_steam_game_install_payloads() {
        let home = test_dir("skip-steam-payloads");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let steamapps =
            ms_dir.join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");
        fs::create_dir_all(steamapps.join("common").join("Portal 2")).expect("create steam game payload");
        fs::create_dir_all(steamapps.join("downloading").join("620")).expect("create downloading payload");
        fs::create_dir_all(steamapps.join("shadercache").join("620")).expect("create shadercache payload");
        fs::write(steamapps.join("common").join("Portal 2").join("portal2.exe"), b"game").expect("write game payload");
        fs::write(steamapps.join("appmanifest_620.acf"), b"manifest").expect("write app manifest");
        fs::write(ms_dir.join("prefix-steam").join("user.reg"), b"settings").expect("write prefix settings");

        let preserved = preserve_user_data(&ms_dir);
        assert!(preserved.prefix_steam_tmp.join("user.reg").exists());
        assert!(!preserved.prefix_steam_tmp.join("drive_c").exists());
        assert!(!find_descendant_named(&preserved.prefix_steam_tmp, "steamapps"));
        assert!(!find_descendant_named(&preserved.prefix_steam_tmp, "portal2.exe"));

        let injected_steamapps =
            preserved.prefix_steam_tmp.join("drive_c").join("Program Files (x86)").join("Steam").join("steamapps");
        fs::create_dir_all(injected_steamapps.join("common").join("Portal 2")).expect("inject preserved game payload");
        fs::write(injected_steamapps.join("common").join("Portal 2").join("portal2.exe"), b"game")
            .expect("write injected game payload");

        fs::remove_dir_all(ms_dir.join("prefix-steam")).expect("remove live prefix");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert!(ms_dir.join("prefix-steam").join("user.reg").exists());
        assert!(!ms_dir.join("prefix-steam").join("drive_c").exists());
        assert!(!find_descendant_named(&ms_dir.join("prefix-steam"), "portal2.exe"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_restore_removes_root_z_drive_and_does_not_count_symlink_targets() {
        let home = test_dir("skip-root-z-drive");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let prefix = ms_dir.join("prefix-steam");
        let dosdevices = prefix.join("dosdevices");
        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        std::os::unix::fs::symlink("/", dosdevices.join("z:")).expect("create z drive symlink");
        fs::write(prefix.join("user.reg"), b"settings").expect("write prefix settings");

        assert_eq!(count_settings_files(&prefix), 1);

        let preserved = preserve_user_data(&ms_dir);
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert!(ms_dir.join("prefix-steam").join("user.reg").exists());
        assert!(!ms_dir.join("prefix-steam").join("dosdevices").join("z:").exists());
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_prefix_update_restores_root_steam_library_drive() {
        let home = test_dir("restore-root-steam-library-drive");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let prefix = ms_dir.join("prefix-steam");
        let dosdevices = prefix.join("dosdevices");
        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        write_steam_libraryfolders(&prefix, r#""path" "Z:\\Volumes\\AverySSD\\SteamLibrary""#);

        let links = collect_steam_library_drive_links(&prefix);
        assert_eq!(
            links,
            vec![SteamLibraryDriveLink {
                drive: 'z',
                target: PathBuf::from("/"),
                library_path: String::from("Z:\\Volumes\\AverySSD\\SteamLibrary"),
            }]
        );

        restore_steam_library_drive_links(&prefix, &links);

        assert_eq!(fs::read_link(dosdevices.join("z:")).expect("read restored z drive"), PathBuf::from("/"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_prefix_update_restores_existing_custom_steam_library_drive() {
        let home = test_dir("restore-custom-steam-library-drive");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let prefix = ms_dir.join("prefix-steam");
        let external = home.join("ExternalSteamDisk");
        let dosdevices = prefix.join("dosdevices");
        fs::create_dir_all(external.join("SteamLibrary").join("steamapps")).expect("create external steamapps");
        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        std::os::unix::fs::symlink(&external, dosdevices.join("y:")).expect("create y drive symlink");
        write_steam_libraryfolders(&prefix, r#""path" "Y:\\SteamLibrary""#);

        let links = collect_steam_library_drive_links(&prefix);
        assert_eq!(
            links,
            vec![SteamLibraryDriveLink {
                drive: 'y',
                target: external.clone(),
                library_path: String::from("Y:\\SteamLibrary"),
            }]
        );

        fs::remove_file(dosdevices.join("y:")).expect("remove y drive symlink");
        std::os::unix::fs::symlink(&home, dosdevices.join("y:")).expect("simulate prefix init y rewrite");
        restore_steam_library_drive_links(&prefix, &links);

        assert_eq!(fs::read_link(dosdevices.join("y:")).expect("read restored y drive"), external);
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_preserves_bottle_settings_without_prefix_payloads() {
        let home = test_dir("skip-bottle-prefix-payloads");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let bottle = ms_dir.join("bottles").join("steam_620");
        fs::create_dir_all(
            bottle
                .join("prefix")
                .join("drive_c")
                .join("Program Files (x86)")
                .join("Steam")
                .join("steamapps")
                .join("common")
                .join("Portal 2"),
        )
        .expect("create bottle prefix payload");
        fs::write(bottle.join("bottle.json"), br#"{"id":"steam_620","profile":"m9"}"#).expect("write bottle settings");
        fs::write(
            bottle
                .join("prefix")
                .join("drive_c")
                .join("Program Files (x86)")
                .join("Steam")
                .join("steamapps")
                .join("common")
                .join("Portal 2")
                .join("portal2.exe"),
            b"game",
        )
        .expect("write bottle game payload");

        let preserved = preserve_user_data(&ms_dir);
        assert!(preserved.bottles_tmp.join("steam_620").join("bottle.json").exists());
        assert!(!preserved.bottles_tmp.join("steam_620").join("prefix").exists());
        assert!(!find_descendant_named(&preserved.bottles_tmp, "portal2.exe"));

        let injected_bottle_payload = preserved
            .bottles_tmp
            .join("steam_620")
            .join("prefix")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam")
            .join("steamapps")
            .join("common")
            .join("Portal 2");
        fs::create_dir_all(&injected_bottle_payload).expect("inject preserved bottle payload");
        fs::write(injected_bottle_payload.join("portal2.exe"), b"game").expect("write injected bottle payload");

        fs::remove_dir_all(ms_dir.join("bottles")).expect("remove live bottles");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert_eq!(
            fs::read_to_string(ms_dir.join("bottles").join("steam_620").join("bottle.json")).unwrap(),
            r#"{"id":"steam_620","profile":"m9"}"#
        );
        assert!(!ms_dir.join("bottles").join("steam_620").join("prefix").exists());
        assert!(!find_descendant_named(&ms_dir.join("bottles"), "portal2.exe"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_preserves_cache_metadata_without_download_payloads() {
        let home = test_dir("preserve-cache");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(ms_dir.join("cache").join("covers")).expect("create cache metadata dir");
        fs::create_dir_all(ms_dir.join("cache").join("updates")).expect("create update cache dir");
        fs::write(ms_dir.join("cache").join("steam_config.json"), br#"{"api_key_set":true}"#)
            .expect("write steam config");
        fs::write(ms_dir.join("cache").join("covers").join("620.png"), b"cover").expect("write cover");
        fs::write(ms_dir.join("cache").join("updates").join("MetalSharp.dmg"), b"dmg").expect("write cached dmg");

        let preserved = preserve_user_data(&ms_dir);
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert_eq!(
            fs::read_to_string(ms_dir.join("cache").join("steam_config.json")).unwrap(),
            r#"{"api_key_set":true}"#
        );
        assert!(!ms_dir.join("cache").join("covers").join("620.png").exists());
        assert!(!ms_dir.join("cache").join("updates").join("MetalSharp.dmg").exists());
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_restores_setup_preferences_and_steam_api_key() {
        let home = test_dir("restore-user-settings");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(ms_dir.join("cache")).expect("create cache dir");
        fs::write(ms_dir.join("setup.json"), br#"{"completed":true,"deviceName":"Avery","steamApiKeySet":true}"#)
            .expect("write setup preferences");
        fs::write(
            ms_dir.join("cache").join("steam_config.json"),
            br#"{"steam_api_key":"STEAM_WEB_API_KEY","steam_id":"76561198000000000"}"#,
        )
        .expect("write Steam API key");

        let preserved = preserve_user_data(&ms_dir);
        remove_old_runtime(&ms_dir);
        let _ = fs::remove_file(ms_dir.join("setup.json"));
        restore_user_data(&ms_dir, &preserved);

        let setup = fs::read_to_string(ms_dir.join("setup.json")).expect("read restored setup");
        let setup_json: serde_json::Value = serde_json::from_str(&setup).expect("parse restored setup");
        let steam_config =
            fs::read_to_string(ms_dir.join("cache").join("steam_config.json")).expect("read restored Steam config");

        assert_eq!(setup_json.get("deviceName").and_then(|v| v.as_str()), Some("Avery"));
        assert_eq!(setup_json.get("steamApiKeySet").and_then(|v| v.as_bool()), Some(true));
        assert!(steam_config.contains("steam_api_key"));
        assert!(steam_config.contains("STEAM_WEB_API_KEY"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_normalizes_legacy_steam_api_key_alias() {
        let home = test_dir("restore-legacy-steam-api-key");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(ms_dir.join("cache")).expect("create cache dir");
        fs::write(ms_dir.join("setup.json"), br#"{"completed":true,"steamApiKeySet":false}"#)
            .expect("write setup preferences");
        fs::write(
            ms_dir.join("cache").join("steam_config.json"),
            br#"{"api_key":"STEAM_WEB_API_KEY","steam_id":"76561198000000000"}"#,
        )
        .expect("write legacy Steam API key");

        let preserved = preserve_user_data(&ms_dir);
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        let setup = fs::read_to_string(ms_dir.join("setup.json")).expect("read restored setup");
        let setup_json: serde_json::Value = serde_json::from_str(&setup).expect("parse restored setup");
        let steam_config =
            fs::read_to_string(ms_dir.join("cache").join("steam_config.json")).expect("read restored Steam config");

        assert_eq!(setup_json.get("steamApiKeySet").and_then(|v| v.as_bool()), Some(true));
        assert!(steam_config.contains("\"steam_api_key\""));
        assert!(steam_config.contains("STEAM_WEB_API_KEY"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_recovers_steam_api_key_from_durable_backup() {
        let home = test_dir("restore-steam-api-key-backup");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(&ms_dir).expect("create ms dir");
        fs::write(ms_dir.join("setup.json"), br#"{"completed":true,"steamApiKeySet":true}"#)
            .expect("write setup preferences");
        fs::write(
            migration_steam_config_backup_path(&ms_dir),
            br#"{"steam_api_key":"STEAM_WEB_API_KEY","steam_id":"76561198000000000"}"#,
        )
        .expect("write durable Steam config backup");

        let preserved = preserve_user_data(&ms_dir);
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        let steam_config =
            fs::read_to_string(ms_dir.join("cache").join("steam_config.json")).expect("read restored Steam config");

        assert!(steam_config.contains("\"steam_api_key\""));
        assert!(steam_config.contains("STEAM_WEB_API_KEY"));
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_kill_patterns_avoid_broad_command_matches() {
        assert!(MIGRATION_EXACT_KILL_PATTERNS.contains(&"wineloader"));
        assert!(!MIGRATION_COMMAND_KILL_PATTERNS.contains(&"steam"));
        assert!(!MIGRATION_COMMAND_KILL_PATTERNS.contains(&"wine"));
    }

    #[test]
    fn migration_steam_update_process_detection_is_prefix_scoped() {
        let prefix = "/Users/alex/.metalsharp/prefix-steam";
        let ps = "\
101 /Users/alex/.metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/steam.exe
102 /tmp/other-prefix/drive_c/Program Files (x86)/Steam/steam.exe
103 /Users/alex/.metalsharp/prefix-steam/drive_c/Steam/steamwebhelper.exe
";

        assert!(steam_update_process_alive(prefix, ps));
        assert!(!steam_update_process_alive("/tmp/missing-prefix", ps));
        assert_eq!(steam_process_pids_for_prefix(prefix, ps), vec![101, 103]);
    }

    #[test]
    fn migration_prefix_init_uses_cmd_probe_not_wineboot() {
        assert_eq!(MIGRATION_PREFIX_INIT_ARGS, &["cmd", "/c", "exit"]);
        assert!(!MIGRATION_PREFIX_INIT_ARGS.contains(&"wineboot"));
        assert_eq!(MIGRATION_PREFIX_INIT_TIMEOUT_SECS, 10);
    }

    #[test]
    fn migration_prefix_runtime_surface_stages_updated_dlls_and_sidecars() {
        let home = test_dir("prefix-runtime-surface");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        write_runtime_core(&ms_dir);
        let runtime_wine = ms_dir.join("runtime").join("wine");
        let prefix = ms_dir.join("prefix-steam");

        let copied = stage_updated_prefix_runtime_surface(&runtime_wine, &prefix).expect("stage runtime surface");

        assert_eq!(copied, prefix_runtime_dll_sources().len() + prefix_runtime_unix_sidecars().len());
        for (_, filename) in prefix_runtime_dll_sources() {
            assert!(
                prefix.join("drive_c").join("windows").join("system32").join(filename).is_file(),
                "missing staged DLL {}",
                filename
            );
        }
        for filename in prefix_runtime_unix_sidecars() {
            assert!(
                prefix.join(".metalsharp").join("unix").join(filename).is_file(),
                "missing staged Unix sidecar {}",
                filename
            );
        }

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn post_update_marker_overrides_ready_runtime() {
        let home = test_dir("marker-override");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        write_runtime_core(&ms_dir);
        fs::write(
            ms_dir.join("setup.json"),
            serde_json::to_vec(&json!({"completed": true, "runtime_migration_schema": 3})).unwrap(),
        )
        .expect("write setup.json");

        assert!(runtime_core_ready(&ms_dir));
        assert!(!runtime_needs_repair(&home, true));

        let marker = ms_dir.join(".post-update-migration");
        assert!(!marker.exists());

        fs::write(
            &marker,
            serde_json::to_vec(&json!({"needed": true, "target_version": "0.36.0", "timestamp": 1234567890})).unwrap(),
        )
        .expect("write marker");
        assert!(marker.exists());

        let marker_data = fs::read_to_string(&marker).expect("read marker");
        let marker_json: serde_json::Value = serde_json::from_str(&marker_data).expect("parse marker");
        assert!(marker_json.get("needed").and_then(|v| v.as_bool()).unwrap_or(false));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn post_update_marker_blocks_running_app_older_than_target() {
        let marker = PostUpdateMigrationMarker { needed: true, target_version: Some("999.0.0".into()) };

        let error = post_update_target_error(Some(&marker)).expect("newer target blocked");

        assert!(error.contains("999.0.0"));
        assert!(post_update_target_newer_than_running(&marker));
    }

    #[test]
    fn post_update_marker_accepts_running_app_at_target() {
        let marker = PostUpdateMigrationMarker { needed: true, target_version: Some(MIGRATE_VERSION.into()) };

        assert!(post_update_target_error(Some(&marker)).is_none());
        assert!(!post_update_target_newer_than_running(&marker));
    }

    #[test]
    fn migration_ready_requires_runtime_bundle() {
        let home = test_dir("verify-ready-runtime");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        fs::create_dir_all(&ms_dir).expect("create ms dir");

        assert_eq!(
            verify_migration_ready(&ms_dir, None).unwrap_err(),
            "runtime bundle is still incomplete after install"
        );

        write_runtime_core(&ms_dir);
        assert!(verify_migration_ready(&ms_dir, None).is_ok());
        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn dosdevices_dir_is_denied_in_preserve() {
        assert!(migration_preserve_denies_name("dosdevices"));
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

        for path in [
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join(".keep"),
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join("winemetal.so"),
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join("winemac.so"),
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join("ntdll.so"),
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join("libc++.1.dylib"),
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join("libc++abi.1.dylib"),
            runtime_wine.join("lib").join("wine").join("x86_64-unix").join("libunwind.1.dylib"),
            runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d9.dll"),
            runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10.dll"),
            runtime_wine.join("lib").join("wine").join("x86_64-windows").join("d3d10_1.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join(".keep"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d10core.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d11.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("d3d12.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("dxgi.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("dxgi_dxmt.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("winemetal.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("nvapi64.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-windows").join("nvngx.dll"),
            runtime_wine.join("lib").join("metalsharp").join("x86_64-windows").join("metalsharp_ntdll_hook.dll"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join("winemetal.so"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join("libc++.1.dylib"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join("libc++abi.1.dylib"),
            runtime_wine.join("lib").join("dxmt").join("x86_64-unix").join("libunwind.1.dylib"),
            runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("d3d10.dll"),
            runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("d3d11.dll"),
            runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("d3d12.dll"),
            runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("dxgi.dll"),
            runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("nvapi64.dll"),
            runtime_wine.join("lib").join("gptk").join("x86_64-windows").join("nvngx-on-metalfx.dll"),
            runtime_wine
                .join("lib")
                .join("external")
                .join("D3DMetal.framework")
                .join("Versions")
                .join("A")
                .join("D3DMetal"),
            runtime_wine
                .join("lib")
                .join("external")
                .join("D3DMetal.framework")
                .join("Versions")
                .join("A")
                .join("Resources")
                .join("libD3DMetalHelper.dylib"),
            ms_dir.join("runtime").join("goldberg").join("x86").join("steam_api.dll"),
            ms_dir.join("runtime").join("goldberg").join("x64").join("steam_api64.dll"),
            ms_dir.join("runtime").join("eac-toggle").join("x86_64-windows").join("_winhttp.dll"),
            ms_dir.join("configs").join("mtsp-rules.toml"),
            runtime_wine.join("etc").join("dxmt.conf"),
        ] {
            fs::create_dir_all(path.parent().unwrap()).expect("create runtime parent");
            fs::write(path, b"test").expect("write runtime file");
        }

        fs::write(
            runtime_wine.join("lib").join("dxmt").join("metalsharp-dxmt-runtime.json"),
            serde_json::to_string_pretty(&json!({
                "schema": "metalsharp.dxmt-runtime.v1",
                "version": crate::installer::DXMT_BUNDLED_RUNTIME_VERSION,
            }))
            .expect("serialize DXMT manifest"),
        )
        .expect("write DXMT manifest");
    }

    fn write_host_runtime(ms_dir: &Path) {
        let host = ms_dir.join("runtime").join("host");
        fs::create_dir_all(&host).expect("create host runtime");
        fs::write(host.join("manifest.json"), br#"{"abi":"metalsharp-host-runtime"}"#).expect("write manifest");
        fs::write(host.join("HostRuntimeABI.h"), b"header").expect("write header");
        fs::write(host.join("libmetalsharp_host_runtime.dylib"), b"dylib").expect("write dylib");
    }

    fn write_steam_libraryfolders(prefix: &Path, path_line: &str) {
        let config = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("config");
        fs::create_dir_all(&config).expect("create Steam config dir");
        fs::write(
            config.join("libraryfolders.vdf"),
            format!(
                r#""libraryfolders"
{{
    "0"
    {{
        {}
    }}
}}
"#,
                path_line
            ),
        )
        .expect("write libraryfolders.vdf");
    }

    #[test]
    fn migration_preserves_and_restores_external_drive_dosdevice_links() {
        let home = test_dir("preserve-dosdevice-links");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let prefix = ms_dir.join("prefix-steam");
        let gptk_prefix = ms_dir.join("prefix-gptk");
        let dosdevices = prefix.join("dosdevices");
        let gptk_dosdevices = gptk_prefix.join("dosdevices");
        let external_steam = home.join("ExternalSteam");

        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        fs::create_dir_all(external_steam.join("steamapps")).expect("create external steamapps");
        std::os::unix::fs::symlink("../drive_c", dosdevices.join("c:")).expect("create c drive");
        std::os::unix::fs::symlink(&external_steam, dosdevices.join("s:")).expect("create s drive");
        std::os::unix::fs::symlink("/", dosdevices.join("z:")).expect("create z drive");
        fs::write(prefix.join("user.reg"), b"settings").expect("write settings");

        fs::create_dir_all(&gptk_dosdevices).expect("create gptk dosdevices");
        std::os::unix::fs::symlink("../drive_c", gptk_dosdevices.join("c:")).expect("create gptk c drive");
        std::os::unix::fs::symlink(&home, gptk_dosdevices.join("l:")).expect("create gptk l drive");
        fs::write(gptk_prefix.join("user.reg"), b"gptk-settings").expect("write gptk settings");

        let preserved = preserve_user_data(&ms_dir);

        assert_eq!(preserved.prefix_steam_dosdevice_links, vec![(String::from("s:"), external_steam.clone())]);
        assert_eq!(preserved.prefix_gptk_dosdevice_links, vec![(String::from("l:"), home.clone())]);

        fs::remove_dir_all(ms_dir.join("prefix-steam")).expect("remove prefix-steam");
        fs::remove_dir_all(ms_dir.join("prefix-gptk")).expect("remove prefix-gptk");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert!(ms_dir.join("prefix-steam").join("user.reg").exists());
        assert_eq!(
            fs::read_link(ms_dir.join("prefix-steam").join("dosdevices").join("s:")).expect("read restored s drive"),
            external_steam
        );
        assert!(!ms_dir.join("prefix-steam").join("dosdevices").join("z:").exists());

        assert!(ms_dir.join("prefix-gptk").join("user.reg").exists());
        assert_eq!(
            fs::read_link(ms_dir.join("prefix-gptk").join("dosdevices").join("l:"))
                .expect("read restored gptk l drive"),
            home
        );

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_filters_dmg_mount_dosdevice_links() {
        let home = test_dir("filter-dmg-mounts");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let prefix = ms_dir.join("prefix-steam");
        let dosdevices = prefix.join("dosdevices");
        let external_steam = home.join("ExternalSteam");

        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        fs::create_dir_all(external_steam.join("steamapps")).expect("create external steamapps");
        std::os::unix::fs::symlink("../drive_c", dosdevices.join("c:")).expect("create c drive");
        std::os::unix::fs::symlink(&external_steam, dosdevices.join("d:")).expect("create d drive");
        std::os::unix::fs::symlink("/Volumes/MetalSharp 0.44.1-arm64", dosdevices.join("e:"))
            .expect("create dmg mount");
        std::os::unix::fs::symlink("/Volumes/MetalSharp-0.45.0-arm64", dosdevices.join("f:"))
            .expect("create dmg mount 2");
        fs::write(prefix.join("user.reg"), b"settings").expect("write settings");

        let preserved = preserve_user_data(&ms_dir);

        assert_eq!(preserved.prefix_steam_dosdevice_links.len(), 1);
        assert_eq!(preserved.prefix_steam_dosdevice_links[0], (String::from("d:"), external_steam.clone()));

        fs::remove_dir_all(ms_dir.join("prefix-steam")).expect("remove prefix");
        remove_old_runtime(&ms_dir);
        restore_user_data(&ms_dir, &preserved);

        assert_eq!(
            fs::read_link(ms_dir.join("prefix-steam").join("dosdevices").join("d:")).expect("read d drive"),
            external_steam
        );
        assert!(!ms_dir.join("prefix-steam").join("dosdevices").join("e:").exists());
        assert!(!ms_dir.join("prefix-steam").join("dosdevices").join("f:").exists());

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn migration_skips_gptk_dosdevice_links_when_prefix_absent() {
        let home = test_dir("skip-gptk-no-prefix");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let prefix = ms_dir.join("prefix-steam");
        let dosdevices = prefix.join("dosdevices");

        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        std::os::unix::fs::symlink("../drive_c", dosdevices.join("c:")).expect("create c drive");
        fs::write(prefix.join("user.reg"), b"settings").expect("write settings");

        assert!(!ms_dir.join("prefix-gptk").exists());

        let preserved = preserve_user_data(&ms_dir);

        assert!(preserved.prefix_steam_dosdevice_links.is_empty());
        assert!(preserved.prefix_gptk_dosdevice_links.is_empty());

        let _ = fs::remove_dir_all(home);
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-migrate-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }

    #[test]
    fn clear_steam_crash_marker_removes_existing_crash_file() {
        let home = test_dir("clear-crash-marker");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let crash_file =
            ms_dir.join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam").join(".crash");
        fs::create_dir_all(crash_file.parent().unwrap()).expect("create Steam dir");
        fs::write(&crash_file, b"").expect("write crash marker");

        assert!(crash_file.exists());
        clear_steam_crash_marker(&ms_dir);
        assert!(!crash_file.exists());

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn clear_steam_crash_marker_is_noop_when_no_crash_file() {
        let home = test_dir("no-crash-marker");
        let ms_dir = crate::platform::metalsharp_home_dir_for(&home);
        let steam_dir = ms_dir.join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam");
        fs::create_dir_all(&steam_dir).expect("create Steam dir");

        clear_steam_crash_marker(&ms_dir);
        assert!(!steam_dir.join(".crash").exists());

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn discover_external_steam_libraries_finds_volume_with_steamapps() {
        let home = test_dir("discover-external");
        let fake_volumes = home.join("fake_volumes");
        let steam_lib = fake_volumes.join("ExternalDisk").join("SteamLibrary").join("SteamLibrary");
        let steamapps = steam_lib.join("steamapps");
        fs::create_dir_all(&steamapps).expect("create steamapps");
        fs::write(steamapps.join("appmanifest_620.acf"), b"test").expect("write manifest");

        let libs = discover_external_steam_libraries_from(&fake_volumes);
        assert_eq!(libs.len(), 1);
        assert_eq!(libs[0].unix_path, steam_lib);

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn discover_external_skips_system_and_metalsharp_volumes() {
        let home = test_dir("discover-skip-system");
        let fake_volumes = home.join("fake_volumes");
        let mac_hd = fake_volumes.join("Macintosh HD");
        let ms_mount = fake_volumes.join("MetalSharp 0.45.1-arm64");
        let dot = fake_volumes.join(".hidden_volume");
        for v in [&mac_hd, &ms_mount, &dot] {
            let steamapps = v.join("SteamLibrary").join("steamapps");
            fs::create_dir_all(&steamapps).expect("create steamapps");
            fs::write(steamapps.join("appmanifest_620.acf"), b"test").expect("write manifest");
        }

        let libs = discover_external_steam_libraries_from(&fake_volumes);
        assert!(libs.is_empty());

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn discover_external_finds_nesting_level_one_steamlibrary() {
        let home = test_dir("discover-level-one");
        let fake_volumes = home.join("fake_volumes");
        let steam_lib = fake_volumes.join("ExternalDisk").join("SteamLibrary");
        let steamapps = steam_lib.join("steamapps");
        fs::create_dir_all(&steamapps).expect("create steamapps");
        fs::write(steamapps.join("appmanifest_620.acf"), b"test").expect("write manifest");

        let libs = discover_external_steam_libraries_from(&fake_volumes);
        assert_eq!(libs.len(), 1);
        assert_eq!(libs[0].unix_path, steam_lib);

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn next_drive_letter_picks_first_free() {
        let home = test_dir("drive-letter-basic");
        let dosdevices = home.join("dosdevices");
        fs::create_dir_all(&dosdevices).expect("create dosdevices");

        assert_eq!(find_next_available_drive_letter_from(&dosdevices, 'd'), Some('d'));

        std::os::unix::fs::symlink(&home, dosdevices.join("d:")).expect("create d drive");
        assert_eq!(find_next_available_drive_letter_from(&dosdevices, 'd'), Some('e'));

        let _ = fs::remove_dir_all(home);
    }

    #[test]
    fn resolve_unix_path_to_windows_maps_through_dosdevices() {
        let home = test_dir("resolve-wine-path");
        let dosdevices = home.join("dosdevices");
        let external = home.join("ExternalSteam");
        fs::create_dir_all(&dosdevices).expect("create dosdevices");
        fs::create_dir_all(&external).expect("create external");
        std::os::unix::fs::symlink(&external, dosdevices.join("e:")).expect("create e drive");

        let result = resolve_unix_path_to_windows(&external, &dosdevices);
        assert_eq!(result, Some("E:\\\\".to_string()));

        let sub = external.join("SteamLibrary");
        fs::create_dir_all(&sub).expect("create sub");
        let result2 = resolve_unix_path_to_windows(&sub, &dosdevices);
        assert_eq!(result2, Some("E:\\\\SteamLibrary".to_string()));

        let _ = fs::remove_dir_all(home);
    }

    fn find_descendant_named(root: &Path, name: &str) -> bool {
        if !root.exists() {
            return false;
        }
        let Ok(entries) = fs::read_dir(root) else {
            return false;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.file_name().and_then(|n| n.to_str()).is_some_and(|n| n == name) {
                return true;
            }
            if fs::symlink_metadata(&path).map(|m| m.is_dir()).unwrap_or(false) && find_descendant_named(&path, name) {
                return true;
            }
        }
        false
    }
}
