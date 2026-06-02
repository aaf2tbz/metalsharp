use serde_json::{json, Map, Value};
use std::collections::HashSet;
use std::ffi::CString;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
#[cfg(unix)]
use std::os::fd::FromRawFd;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::UNIX_EPOCH;
use walkdir::WalkDir;

const ARTIFACT_TAIL_LINES: usize = 80;
const MAX_ARTIFACT_READ_BYTES: u64 = 1024 * 1024;
const WALK_MAX_DEPTH: usize = 10;
const MODULE_ASSET_MAX_DEPTH: usize = 8;

#[derive(Debug, Default)]
struct EacSummary {
    settings_path: Option<String>,
    process_title: Option<String>,
    executable_path: Option<String>,
    product_id: Option<String>,
    sandbox_id: Option<String>,
    deployment_id: Option<String>,
    module_url: Option<String>,
    module_target: Option<String>,
    connect_response_code: Option<i64>,
    wine_version: Option<String>,
    module_mapping_status: Option<String>,
    launcher_exit_code: Option<i64>,
    launcher_error: Option<String>,
    setup_exit_code: Option<i64>,
}

#[derive(Debug, Default)]
struct SteamSummary {
    protected_launcher_path: Option<String>,
    tracked_pid: Option<i64>,
    tracked_exit_code: Option<i64>,
    redist_exit_codes: Vec<Value>,
}

struct GameDirEvidence {
    path: PathBuf,
    source: &'static str,
    staged: bool,
}

#[derive(Debug, Default)]
struct EacIdentity {
    settings_path: Option<String>,
    process_title: Option<String>,
    executable_path: Option<String>,
    product_id: Option<String>,
    sandbox_id: Option<String>,
    deployment_id: Option<String>,
}

pub fn handle_steam_anticheat_evidence(body: &Map<String, Value>) -> Value {
    let appid = match body.get("appid").and_then(|v| v.as_u64()) {
        Some(id) if id > 0 && id <= u32::MAX as u64 => id as u32,
        _ => return json!({"ok": false, "error": "appid required"}),
    };

    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "appid": appid, "error": "no home dir"}),
    };

    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    let game_dir = resolve_anticheat_game_dir(appid);
    let eac_identity = game_dir.as_ref().and_then(|dir| read_eac_identity(&dir.path));
    let artifacts = collect_artifacts(&prefix, game_dir.as_ref().map(|dir| dir.path.as_path()));
    let eac = summarize_eac(appid, &artifacts, eac_identity.as_ref());
    let steam = summarize_steam(appid, &artifacts);
    let status = evidence_status(&eac, &steam, &artifacts);

    json!({
        "ok": true,
        "appid": appid,
        "status": status,
        "summary": summary_text(&status, &eac, &steam),
        "prefix": prefix.to_string_lossy(),
        "gameDir": game_dir.as_ref().map(|dir| dir.path.to_string_lossy().to_string()),
        "gameDirSource": game_dir.as_ref().map(|dir| dir.source),
        "gameDirStaged": game_dir.as_ref().map(|dir| dir.staged).unwrap_or(false),
        "easyAntiCheat": {
            "settingsPath": eac.settings_path,
            "processTitle": eac.process_title,
            "executablePath": eac.executable_path,
            "productId": eac.product_id,
            "sandboxId": eac.sandbox_id,
            "deploymentId": eac.deployment_id,
            "moduleUrl": eac.module_url,
            "moduleTarget": eac.module_target,
            "connectResponseCode": eac.connect_response_code,
            "wineVersion": eac.wine_version,
            "moduleMappingStatus": eac.module_mapping_status,
            "launcherExitCode": eac.launcher_exit_code,
            "launcherError": eac.launcher_error,
            "setupExitCode": eac.setup_exit_code,
        },
        "steam": {
            "protectedLauncherPath": steam.protected_launcher_path,
            "trackedPid": steam.tracked_pid,
            "trackedExitCode": steam.tracked_exit_code,
            "redistExitCodes": steam.redist_exit_codes,
        },
        "artifacts": artifacts,
        "nextActions": next_actions(&status),
    })
}

pub fn handle_steam_anticheat_probe(body: &Map<String, Value>) -> Value {
    let appid = match body.get("appid").and_then(|v| v.as_u64()) {
        Some(id) if id > 0 && id <= u32::MAX as u64 => id as u32,
        _ => return json!({"ok": false, "error": "appid required"}),
    };

    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "appid": appid, "error": "no home dir"}),
    };

    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    let game_dir = resolve_anticheat_game_dir(appid);
    let eac_identity = game_dir.as_ref().and_then(|dir| read_eac_identity(&dir.path));
    let artifacts = collect_artifacts(&prefix, game_dir.as_ref().map(|dir| dir.path.as_path()));
    let eac = summarize_eac(appid, &artifacts, eac_identity.as_ref());
    let steam = summarize_steam(appid, &artifacts);
    let module_assets = game_dir.as_ref().map(|dir| collect_module_assets(&dir.path)).unwrap_or_default();
    let runtime_checks = runtime_probe_checks(&home);
    let status = probe_status(&eac, &module_assets, game_dir.as_ref().map(|dir| dir.staged).unwrap_or(false));
    let host_os = std::env::consts::OS;
    let host_arch = std::env::consts::ARCH;

    json!({
        "ok": true,
        "appid": appid,
        "status": status,
        "summary": probe_summary(&status, &eac),
        "host": {
            "os": host_os,
            "arch": host_arch,
            "canDlopenLinuxElfDirectly": can_dlopen_linux_elf_directly(host_os),
        },
        "prefix": prefix.to_string_lossy(),
        "gameDir": game_dir.as_ref().map(|dir| dir.path.to_string_lossy().to_string()),
        "gameDirSource": game_dir.as_ref().map(|dir| dir.source),
        "gameDirStaged": game_dir.as_ref().map(|dir| dir.staged).unwrap_or(false),
        "evidenceStatus": evidence_status(&eac, &steam, &artifacts),
        "easyAntiCheat": {
            "moduleTarget": eac.module_target,
            "moduleUrl": eac.module_url,
            "wineVersion": eac.wine_version,
            "moduleMappingStatus": eac.module_mapping_status,
            "launcherExitCode": eac.launcher_exit_code,
            "launcherError": eac.launcher_error,
        },
        "runtimeChecks": runtime_checks,
        "moduleAssets": module_assets,
        "contractChecks": module_contract_checks(host_os, &eac, &module_assets),
        "nextActions": probe_next_actions(&status),
    })
}

pub fn handle_steam_anticheat_delta_audit(body: &Map<String, Value>) -> Value {
    let appid =
        body.get("appid").and_then(|v| v.as_u64()).filter(|id| *id > 0 && *id <= u32::MAX as u64).map(|id| id as u32);
    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "error": "no home dir"}),
    };

    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    let wine_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let game_dir = appid.and_then(resolve_anticheat_game_dir);
    let eac_identity = game_dir.as_ref().and_then(|dir| read_eac_identity(&dir.path));
    let artifacts = collect_artifacts(&prefix, game_dir.as_ref().map(|dir| dir.path.as_path()));
    let eac = summarize_eac(appid.unwrap_or_default(), &artifacts, eac_identity.as_ref());
    let module_assets = game_dir.as_ref().map(|dir| collect_module_assets(&dir.path)).unwrap_or_default();
    let host_os = std::env::consts::OS;

    let wineserver_socket_root = wineserver_socket_root();
    let wineserver_socket_dirs = wineserver_socket_dirs();
    let wineserver_running = process_list_contains(&["wineserver"]);
    let kernel32_win64 = wine_root.join("lib").join("wine").join("x86_64-windows").join("kernel32.dll");
    let user32_win64 = wine_root.join("lib").join("wine").join("x86_64-windows").join("user32.dll");
    let ntdll_win64 = wine_root.join("lib").join("wine").join("x86_64-windows").join("ntdll.dll");
    let ntdll_unix = wine_root.join("lib").join("wine").join("x86_64-unix").join("ntdll.so");

    let surfaces = vec![
        delta_group(
            "wine_loader",
            "Wine loader/syscall baseline",
            vec![
                delta_path("wineserver", "required", &wine_root.join("bin").join("wineserver"), None),
                delta_path("wine", "required", &wine_root.join("bin").join("wine"), None),
                delta_path("ntdll_unix", "required", &ntdll_unix, None),
                delta_path("ntdll_win64", "required", &ntdll_win64, None),
                delta_path("ntdll_win32", "wow64_required", &wine_root.join("lib").join("wine").join("i386-windows").join("ntdll.dll"), None),
                delta_path(
                    "wine_preloader",
                    "proton_comparison",
                    &wine_root.join("bin").join("wine-preloader"),
                    Some("Absent is common in packaged macOS Wine; record it because Proton/Linux loader behavior often assumes Linux mapping semantics."),
                ),
            ],
        ),
        delta_group(
            "wineserver_state",
            "Wineserver process, socket, and synchronization boundary",
            vec![
                delta_capability(
                    "wineserver_process_running",
                    "runtime_observation",
                    wineserver_running,
                    "Wine synchronizes Windows processes through wineserver; absence is expected when no Wine app is active, but protected launch evidence should show the correct shared server boundary.",
                ),
                delta_path(
                    "wineserver_socket_root",
                    "runtime_observation",
                    &wineserver_socket_root,
                    Some("Wine stores per-user server socket directories here on macOS."),
                ),
                delta_observation(
                    "active_wineserver_socket_dirs",
                    "runtime_observation",
                    !wineserver_socket_dirs.is_empty(),
                    Some(json!(wineserver_socket_dirs)),
                    "Active Wine server socket directories indicate currently initialized Wine server state.",
                ),
            ],
        ),
        delta_group(
            "win32_translation_contract",
            "Win32 DLL translation and syscall dispatch lanes",
            vec![
                delta_path("kernel32_win64", "required", &kernel32_win64, Some("Win32 process, file, and synchronization API calls enter Wine through this PE DLL lane.")),
                delta_path("user32_win64", "required", &user32_win64, Some("Window/message/input calls enter Wine through this PE DLL lane before reaching macOS/Cocoa surfaces.")),
                delta_path("ntdll_win64", "required", &ntdll_win64, Some("NT loader/syscall-facing calls enter Wine through this PE DLL lane.")),
                delta_path("ntdll_unix", "required", &ntdll_unix, Some("Unix-side ntdll implementation is the host boundary for Wine's syscall/loader behavior.")),
                delta_capability(
                    "wine_dll_translation_lanes_present",
                    "required",
                    kernel32_win64.exists() && user32_win64.exists() && ntdll_win64.exists() && ntdll_unix.exists(),
                    "Protected launch debugging should treat Win32 translation as present only when both PE DLL and Unix-side ntdll lanes exist.",
                ),
            ],
        ),
        delta_group(
            "steam_runtime_bridge",
            "Steam client bridge and protected launch surface",
            vec![
                delta_path(
                    "steamclient_dll",
                    "required",
                    &prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("steamclient.dll"),
                    None,
                ),
                delta_path(
                    "steamclient64_dll",
                    "required",
                    &prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("steamclient64.dll"),
                    None,
                ),
                delta_path(
                    "lsteamclient_bridge",
                    "proton_comparison",
                    &wine_root.join("lib").join("wine").join("x86_64-unix").join("lsteamclient.so"),
                    Some("Proton relies on a Linux Steam client bridge layer; MetalSharp needs an explicit equivalent story if protected launch depends on it."),
                ),
            ],
        ),
        delta_group(
            "container_linux_runtime",
            "Pressure-vessel, seccomp, and Linux namespace assumptions",
            vec![
                delta_capability("host_is_linux", "proton_comparison", host_os == "linux", "Proton anti-cheat support targets Linux user space; macOS cannot provide seccomp/namespaces directly."),
                delta_capability("pressure_vessel_available", "proton_comparison", false, "No pressure-vessel container is present in the MetalSharp macOS runtime."),
                delta_capability("seccomp_available", "proton_comparison", host_os == "linux", "Darwin has different syscall filtering and process policy APIs."),
            ],
        ),
        delta_group(
            "darwin_executable_module_boundary",
            "Darwin executable mapping and module format boundary",
            vec![
                delta_capability(
                    "host_uses_mach_o_loader",
                    "required",
                    host_os == "macos",
                    "On macOS, executable host modules must satisfy dyld/Mach-O expectations rather than Linux ELF loader semantics.",
                ),
                delta_capability(
                    "darwin_can_load_linux_elf_directly",
                    "blocking_when_false",
                    can_dlopen_linux_elf_directly(host_os),
                    "A Linux EAC/BattlEye ELF module cannot be directly hosted by macOS dyld.",
                ),
                delta_capability(
                    "vendor_mach_o_module_present",
                    "possible_direct_path",
                    module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("mach_o")),
                    "A vendor-supported Mach-O module is the direct host-compatible path if a game ships one.",
                ),
                delta_capability(
                    "linux_elf_module_present",
                    "blocking_when_macos",
                    module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("elf")),
                    "Linux ELF anti-cheat modules imply a Linux user-space substrate on macOS.",
                ),
            ],
        ),
        delta_group(
            "graphics_runtime",
            "Graphics translation assets adjacent to protected launch",
            vec![
                delta_path("dxmt_win64_d3d12", "route_asset", &wine_root.join("lib").join("dxmt").join("x86_64-windows").join("d3d12.dll"), None),
                delta_path("dxmt_winemetal_unix", "route_asset", &wine_root.join("lib").join("dxmt").join("x86_64-unix").join("winemetal.so"), None),
                delta_path("dxvk_win32_d3d9", "route_asset", &wine_root.join("lib").join("dxvk").join("i386-windows").join("d3d9.dll"), None),
                delta_path("moltenvk_unix", "route_asset", &wine_root.join("lib").join("wine").join("x86_64-unix").join("libMoltenVK.dylib"), None),
            ],
        ),
        delta_group(
            "anticheat_module_contract",
            "Protected module target and host substrate decision",
            vec![
                delta_capability(
                    "selected_linux_module",
                    "blocking_when_macos",
                    eac.module_target.as_deref().unwrap_or("").starts_with("linux"),
                    "EAC selected a Linux module target from the vendor CDN.",
                ),
                delta_capability(
                    "darwin_can_load_linux_elf_directly",
                    "blocking_when_false",
                    can_dlopen_linux_elf_directly(host_os),
                    "macOS dyld cannot directly load Linux ELF modules.",
                ),
                delta_capability(
                    "darwin_vendor_asset_found",
                    "possible_direct_path",
                    module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("mach_o")),
                    "A vendor-supported Mach-O/dylib anti-cheat module would be the direct macOS path.",
                ),
            ],
        ),
    ];

    json!({
        "ok": true,
        "appid": appid,
        "status": delta_audit_status(&surfaces),
        "summary": delta_audit_summary(&eac, host_os),
        "host": {
            "os": host_os,
            "arch": std::env::consts::ARCH,
        },
        "surfaces": surfaces,
        "moduleAssets": module_assets,
        "nextActions": vec![
            "Use this report as the Phase 3 checklist before changing Wine loader behavior.",
            "Compare blocking and proton_comparison rows against Proton's EAC-enabled Wine tree.",
            "Promote any required missing runtime bridge into a specific implementation task instead of a generic anti-cheat claim.",
        ],
    })
}

pub fn handle_steam_anticheat_contract_probe(body: &Map<String, Value>) -> Value {
    let appid =
        body.get("appid").and_then(|v| v.as_u64()).filter(|id| *id > 0 && *id <= u32::MAX as u64).map(|id| id as u32);
    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "error": "no home dir"}),
    };

    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    let game_dir = appid.and_then(resolve_anticheat_game_dir);
    let eac_identity = game_dir.as_ref().and_then(|dir| read_eac_identity(&dir.path));
    let artifacts = collect_artifacts(&prefix, game_dir.as_ref().map(|dir| dir.path.as_path()));
    let eac = summarize_eac(appid.unwrap_or_default(), &artifacts, eac_identity.as_ref());
    let steam = appid.map(|id| summarize_steam(id, &artifacts)).unwrap_or_default();
    let module_assets = game_dir.as_ref().map(|dir| collect_module_assets(&dir.path)).unwrap_or_default();
    let host_os = std::env::consts::OS;
    let runtime_checks = runtime_probe_checks(&home);
    let host_contract = host_contract_probe(host_os);
    let status = contract_probe_status(host_os, &eac, &module_assets, &host_contract);

    json!({
        "ok": true,
        "appid": appid,
        "status": status,
        "summary": contract_probe_summary(&status),
        "prefix": prefix.to_string_lossy(),
        "gameDir": game_dir.as_ref().map(|dir| dir.path.to_string_lossy().to_string()),
        "gameDirSource": game_dir.as_ref().map(|dir| dir.source),
        "gameDirStaged": game_dir.as_ref().map(|dir| dir.staged).unwrap_or(false),
        "evidenceStatus": evidence_status(&eac, &steam, &artifacts),
        "easyAntiCheat": {
            "settingsPath": eac.settings_path,
            "processTitle": eac.process_title,
            "executablePath": eac.executable_path,
            "productId": eac.product_id,
            "sandboxId": eac.sandbox_id,
            "deploymentId": eac.deployment_id,
            "moduleUrl": eac.module_url,
            "moduleTarget": eac.module_target,
            "wineVersion": eac.wine_version,
            "moduleMappingStatus": eac.module_mapping_status,
            "launcherExitCode": eac.launcher_exit_code,
            "launcherError": eac.launcher_error,
        },
        "runtimeChecks": runtime_checks,
        "hostContract": host_contract,
        "moduleAssets": module_assets,
        "contractChecks": module_contract_checks(host_os, &eac, &module_assets),
        "nextActions": contract_probe_next_actions(&status),
    })
}

pub fn handle_steam_anticheat_substrate_decision(body: &Map<String, Value>) -> Value {
    let appid = match body.get("appid").and_then(|v| v.as_u64()) {
        Some(id) if id > 0 && id <= u32::MAX as u64 => id as u32,
        _ => return json!({"ok": false, "error": "appid required"}),
    };

    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "appid": appid, "error": "no home dir"}),
    };

    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    let game_dir = resolve_anticheat_game_dir(appid);
    let eac_identity = game_dir.as_ref().and_then(|dir| read_eac_identity(&dir.path));
    let artifacts = collect_artifacts(&prefix, game_dir.as_ref().map(|dir| dir.path.as_path()));
    let eac = summarize_eac(appid, &artifacts, eac_identity.as_ref());
    let steam = summarize_steam(appid, &artifacts);
    let module_assets = game_dir.as_ref().map(|dir| collect_module_assets(&dir.path)).unwrap_or_default();
    let host_os = std::env::consts::OS;
    let decision = substrate_decision(host_os, &eac, &module_assets);

    json!({
        "ok": true,
        "appid": appid,
        "decision": decision,
        "summary": substrate_decision_summary(&decision),
        "host": {
            "os": host_os,
            "arch": std::env::consts::ARCH,
        },
        "evidenceStatus": evidence_status(&eac, &steam, &artifacts),
        "gameDir": game_dir.as_ref().map(|dir| dir.path.to_string_lossy().to_string()),
        "gameDirSource": game_dir.as_ref().map(|dir| dir.source),
        "gameDirStaged": game_dir.as_ref().map(|dir| dir.staged).unwrap_or(false),
        "facts": {
            "moduleTarget": eac.module_target,
            "moduleMappingStatus": eac.module_mapping_status,
            "launcherExitCode": eac.launcher_exit_code,
            "hasLinuxElfAsset": module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("elf")),
            "hasDarwinDylibAsset": module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("mach_o")),
            "canDlopenLinuxElfDirectly": can_dlopen_linux_elf_directly(host_os),
        },
        "allowedPaths": allowed_substrate_paths(&decision),
        "rejectedPaths": vec![
            "spoof anti-cheat host identity",
            "hide MetalSharp or Wine from the protected launcher",
            "fake kernel driver support",
            "tamper with protected modules",
            "claim online anti-cheat support before the protected module maps and launches with vendor-supported assets",
        ],
        "nextActions": substrate_next_actions(&decision),
    })
}

fn resolve_anticheat_game_dir(appid: u32) -> Option<GameDirEvidence> {
    let completed = crate::setup::resolve_game_dir(appid).map(|path| GameDirEvidence {
        path,
        source: "resolved_install",
        staged: false,
    });
    let staged = resolve_staged_steam_download_dir(appid);

    match (completed, staged) {
        (Some(install), Some(download)) if !path_has_anticheat_probe_targets(&install.path) => Some(download),
        (Some(install), _) => Some(install),
        (None, staged) => staged,
    }
}

fn resolve_staged_steam_download_dir(appid: u32) -> Option<GameDirEvidence> {
    steamapps_roots()
        .into_iter()
        .map(|steamapps| steamapps.join("downloading").join(appid.to_string()))
        .find(|path| path.exists() && path_has_anticheat_probe_targets(path))
        .map(|path| GameDirEvidence { path, source: "steam_downloading", staged: true })
}

fn steamapps_roots() -> Vec<PathBuf> {
    let mut roots = Vec::new();
    roots.extend(crate::scan::macos_steam_library_paths());
    roots.extend(crate::scan::wine_steam_library_paths());

    if let Ok(configured) = std::env::var("METALSHARP_STEAM_LIBRARY_ROOTS") {
        for root in configured.split(':').map(str::trim).filter(|root| !root.is_empty()) {
            roots.push(PathBuf::from(root).join("steamapps"));
            roots.push(PathBuf::from(root));
        }
    }

    let mut seen = HashSet::new();
    roots.into_iter().filter(|path| path.exists()).filter(|path| seen.insert(path.clone())).collect()
}

fn path_has_anticheat_probe_targets(root: &Path) -> bool {
    if !root.exists() {
        return false;
    }
    WalkDir::new(root)
        .max_depth(MODULE_ASSET_MAX_DEPTH)
        .into_iter()
        .filter_map(Result::ok)
        .any(|entry| entry.file_type().is_file() && is_anticheat_probe_target(entry.path()))
}

fn collect_artifacts(prefix: &Path, game_dir: Option<&Path>) -> Vec<Value> {
    let mut candidates = Vec::new();
    let drive_c = prefix.join("drive_c");
    let steam_logs = drive_c.join("Program Files (x86)").join("Steam").join("logs");
    candidates.push(("steam_gameprocess", steam_logs.join("gameprocess_log.txt")));
    candidates.push(("steam_runprocess", steam_logs.join("runprocess_log.txt")));

    let users_dir = drive_c.join("users");
    if users_dir.exists() {
        for entry in WalkDir::new(&users_dir).max_depth(WALK_MAX_DEPTH).into_iter().filter_map(Result::ok) {
            if !entry.file_type().is_file() {
                continue;
            }
            let path = entry.path();
            let name = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
            let path_lc = path.to_string_lossy().to_ascii_lowercase();
            if path_lc.contains("easyanticheat") && name.ends_with(".log") {
                let id = if name == "service.log" { "eac_service" } else { "eac_launcher" };
                candidates.push((id, path.to_path_buf()));
            } else if path_lc.contains("battleye") && name.ends_with(".log") {
                candidates.push(("battleye_user_log", path.to_path_buf()));
            }
        }
    }

    for common in [
        drive_c.join("Program Files (x86)").join("Common Files").join("BattlEye"),
        drive_c.join("Program Files").join("Common Files").join("BattlEye"),
    ] {
        collect_named_logs(&common, "battleye_common_log", &mut candidates);
    }

    if let Some(dir) = game_dir {
        collect_named_logs(dir, "game_anticheat_log", &mut candidates);
    }

    let mut seen = std::collections::HashSet::new();
    candidates
        .into_iter()
        .filter(|(_, path)| seen.insert(path.clone()))
        .map(|(id, path)| artifact_json(id, &path))
        .collect()
}

fn collect_named_logs(root: &Path, id: &'static str, candidates: &mut Vec<(&'static str, PathBuf)>) {
    if !root.exists() {
        return;
    }
    for entry in WalkDir::new(root).max_depth(WALK_MAX_DEPTH).into_iter().filter_map(Result::ok) {
        if !entry.file_type().is_file() {
            continue;
        }
        let path = entry.path();
        let name = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
        let path_lc = path.to_string_lossy().to_ascii_lowercase();
        if name.ends_with(".log") && (path_lc.contains("easyanticheat") || path_lc.contains("battleye")) {
            candidates.push((id, path.to_path_buf()));
        }
    }
}

fn collect_module_assets(game_dir: &Path) -> Vec<Value> {
    if !game_dir.exists() {
        return Vec::new();
    }

    WalkDir::new(game_dir)
        .max_depth(MODULE_ASSET_MAX_DEPTH)
        .into_iter()
        .filter_map(Result::ok)
        .filter(|entry| entry.file_type().is_file())
        .filter_map(|entry| {
            let path = entry.path();
            if !is_anticheat_probe_target(path) {
                return None;
            }
            let name = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
            let extension = path.extension().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
            let interesting = matches!(extension.as_str(), "dll" | "exe" | "so" | "dylib" | "sys")
                || name.contains("beservice")
                || name.contains("beclient")
                || name.contains("easyanticheat");
            if !interesting {
                return None;
            }

            let metadata = fs::metadata(path).ok();
            Some(json!({
                "path": path.to_string_lossy(),
                "bytes": metadata.as_ref().map(|m| m.len()),
                "kind": classify_module_path(path),
                "format": read_binary_format(path),
            }))
        })
        .collect()
}

fn read_eac_identity(game_dir: &Path) -> Option<EacIdentity> {
    let settings = find_eac_settings_path(game_dir)?;
    let text = fs::read_to_string(&settings).ok()?;
    let mut identity =
        EacIdentity { settings_path: Some(settings.to_string_lossy().to_string()), ..EacIdentity::default() };

    for line in text.trim_start_matches('\u{feff}').lines() {
        let Some((key, value)) = parse_loose_json_string_pair(line) else {
            continue;
        };
        match key.as_str() {
            "title" => identity.process_title = Some(value),
            "executable" => identity.executable_path = Some(value),
            "productid" => identity.product_id = Some(value),
            "sandboxid" => identity.sandbox_id = Some(value),
            "deploymentid" => identity.deployment_id = Some(value),
            _ => {},
        }
    }

    Some(identity)
}

fn find_eac_settings_path(game_dir: &Path) -> Option<PathBuf> {
    WalkDir::new(game_dir)
        .max_depth(MODULE_ASSET_MAX_DEPTH)
        .into_iter()
        .filter_map(Result::ok)
        .find(|entry| {
            entry.file_type().is_file()
                && entry.file_name().to_string_lossy().eq_ignore_ascii_case("settings.json")
                && entry.path().to_string_lossy().to_ascii_lowercase().contains("easyanticheat")
        })
        .map(|entry| entry.path().to_path_buf())
}

fn parse_loose_json_string_pair(line: &str) -> Option<(String, String)> {
    let mut parts = line.trim().trim_end_matches(',').splitn(2, ':');
    let key = parts.next()?.trim().trim_matches('"').to_ascii_lowercase();
    let value = parts.next()?.trim().trim_matches('"').to_string();
    if key.is_empty() || value.is_empty() {
        return None;
    }
    Some((key, value))
}

fn is_anticheat_probe_target(path: &Path) -> bool {
    let path_lc = path.to_string_lossy().to_ascii_lowercase();
    let name = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
    path_lc.contains("easyanticheat")
        || path_lc.contains("battleye")
        || path_lc.contains("beclient")
        || name == "start_protected_game.exe"
}

fn runtime_probe_checks(home: &Path) -> Value {
    let wine_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine_bin = wine_root.join("bin").join("wine");
    let wine64_bin = wine_root.join("bin").join("wine64");
    let wineserver_bin = wine_root.join("bin").join("wineserver");
    let wine_unix = wine_root.join("lib").join("wine").join("x86_64-unix");
    let dxmt_unix = wine_root.join("lib").join("dxmt").join("x86_64-unix");
    let ntdll = wine_unix.join("ntdll.so");
    let kernel32 = wine_root.join("lib").join("wine").join("x86_64-windows").join("kernel32.dll");
    let user32 = wine_root.join("lib").join("wine").join("x86_64-windows").join("user32.dll");

    json!({
        "wineRoot": path_check(&wine_root),
        "wineBinary": path_check(&wine_bin),
        "wineVersion": wine_version(&wine_bin),
        "wine64Binary": path_check(&wine64_bin),
        "wineserverBinary": path_check(&wineserver_bin),
        "wineserverProcessRunning": process_list_contains(&["wineserver"]),
        "wineserverSocketRoot": path_check(&wineserver_socket_root()),
        "activeWineserverSocketDirs": wineserver_socket_dirs(),
        "wineUnixLibDir": path_check(&wine_unix),
        "dxmtUnixLibDir": path_check(&dxmt_unix),
        "ntdllUnix": path_check(&ntdll),
        "kernel32Win64": path_check(&kernel32),
        "user32Win64": path_check(&user32),
        "expectedDyldBoundary": "macos_dylib",
        "linuxElfDirectHostLoadPossible": can_dlopen_linux_elf_directly(std::env::consts::OS),
    })
}

fn host_contract_probe(host_os: &str) -> Value {
    json!({
        "hostOs": host_os,
        "hostArch": std::env::consts::ARCH,
        "anonymousExecutableMapping": anonymous_executable_mapping_probe(),
        "syntheticElfDirectLoad": synthetic_elf_direct_load_probe(),
        "canDlopenLinuxElfDirectly": can_dlopen_linux_elf_directly(host_os),
        "notes": [
            "This probe uses synthetic temporary data only.",
            "It does not load, patch, inject, or inspect protected anti-cheat modules.",
            "It records whether the host looks like a Linux ELF module host or a macOS Mach-O/dyld host."
        ],
    })
}

#[cfg(unix)]
fn anonymous_executable_mapping_probe() -> Value {
    unsafe {
        let len = 4096;
        let ptr = libc::mmap(
            std::ptr::null_mut(),
            len,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_PRIVATE | libc::MAP_ANON,
            -1,
            0,
        );
        if ptr == libc::MAP_FAILED {
            return json!({
                "ok": false,
                "stage": "mmap_rw",
                "errno": last_errno(),
                "summary": "Anonymous read/write mmap failed on the host.",
            });
        }

        std::ptr::write_bytes(ptr, 0x90, len);
        let protect_result = libc::mprotect(ptr, len, libc::PROT_READ | libc::PROT_EXEC);
        let errno = if protect_result == 0 { None } else { Some(last_errno()) };
        let _ = libc::munmap(ptr, len);

        json!({
            "ok": protect_result == 0,
            "stage": "mprotect_rx",
            "errno": errno,
            "summary": if protect_result == 0 {
                "Anonymous memory can transition from writable to executable in this process."
            } else {
                "Anonymous memory could not transition from writable to executable in this process."
            },
        })
    }
}

#[cfg(not(unix))]
fn anonymous_executable_mapping_probe() -> Value {
    json!({
        "ok": false,
        "stage": "unsupported_host",
        "summary": "Anonymous executable mapping probe is only implemented for Unix hosts.",
    })
}

#[cfg(unix)]
fn synthetic_elf_direct_load_probe() -> Value {
    let path = match write_secure_synthetic_elf() {
        Ok(path) => path,
        Err(err) => {
            return json!({
                "ok": false,
                "stage": "write_synthetic_elf",
                "error": err,
            });
        },
    };

    let c_path = match CString::new(path.to_string_lossy().as_bytes()) {
        Ok(path) => path,
        Err(err) => {
            let _ = fs::remove_file(&path);
            return json!({
                "ok": false,
                "path": path.to_string_lossy(),
                "stage": "prepare_dlopen_path",
                "error": err.to_string(),
            });
        },
    };

    unsafe {
        libc::dlerror();
        let handle = libc::dlopen(c_path.as_ptr(), libc::RTLD_NOW | libc::RTLD_LOCAL);
        let error = if handle.is_null() {
            dlerror_string()
        } else {
            let _ = libc::dlclose(handle);
            None
        };
        let _ = fs::remove_file(&path);
        json!({
            "ok": !handle.is_null(),
            "path": path.to_string_lossy(),
            "format": "elf",
            "stage": "dlopen_synthetic_elf",
            "error": error,
            "summary": if handle.is_null() {
                "Host dynamic loader did not accept a synthetic Linux ELF shared object."
            } else {
                "Host dynamic loader accepted a synthetic Linux ELF shared object."
            },
        })
    }
}

#[cfg(unix)]
fn write_secure_synthetic_elf() -> Result<PathBuf, String> {
    let template = std::env::temp_dir().join("metalsharp-synthetic-eac-module-XXXXXX");
    let mut bytes = template.to_string_lossy().into_owned().into_bytes();
    bytes.push(0);

    let fd = unsafe { libc::mkstemp(bytes.as_mut_ptr().cast()) };
    if fd < 0 {
        return Err(format!("mkstemp failed with errno {}", last_errno()));
    }

    let path_bytes = bytes.split(|byte| *byte == 0).next().unwrap_or_default();
    let path = PathBuf::from(String::from_utf8_lossy(path_bytes).into_owned());
    let mut file = unsafe { File::from_raw_fd(fd) };
    if let Err(err) = file.write_all(synthetic_elf_bytes()) {
        let _ = fs::remove_file(&path);
        return Err(err.to_string());
    }
    if let Err(err) = file.flush() {
        let _ = fs::remove_file(&path);
        return Err(err.to_string());
    }
    Ok(path)
}

#[cfg(not(unix))]
fn synthetic_elf_direct_load_probe() -> Value {
    json!({
        "ok": false,
        "format": "elf",
        "stage": "unsupported_host",
        "summary": "Synthetic ELF direct-load probe is only implemented for Unix hosts.",
    })
}

#[cfg(unix)]
fn dlerror_string() -> Option<String> {
    unsafe {
        let err = libc::dlerror();
        if err.is_null() {
            None
        } else {
            Some(std::ffi::CStr::from_ptr(err).to_string_lossy().to_string())
        }
    }
}

#[cfg(unix)]
fn last_errno() -> i32 {
    std::io::Error::last_os_error().raw_os_error().unwrap_or_default()
}

fn synthetic_elf_bytes() -> &'static [u8] {
    b"\x7fELF\x02\x01\x01\0\0\0\0\0\0\0\0\0\x03\0>\0\x01\0\0\0\0\0\0\0\0\0\0\0"
}

fn artifact_json(id: &str, path: &Path) -> Value {
    let metadata = fs::metadata(path).ok();
    let modified_at = metadata
        .as_ref()
        .and_then(|m| m.modified().ok())
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs());
    let tail = read_recent_text_limited(path).map(|text| tail_lines(&text, ARTIFACT_TAIL_LINES)).unwrap_or_default();

    json!({
        "id": id,
        "path": path.to_string_lossy(),
        "exists": metadata.is_some(),
        "bytes": metadata.map(|m| m.len()),
        "modifiedAtEpoch": modified_at,
        "tail": tail,
    })
}

fn summarize_eac(appid: u32, artifacts: &[Value], identity: Option<&EacIdentity>) -> EacSummary {
    let mut summary = identity.map(eac_summary_from_identity).unwrap_or_default();
    for artifact in artifacts {
        let id = artifact.get("id").and_then(|v| v.as_str()).unwrap_or("");
        if !id.starts_with("eac_") && id != "steam_runprocess" && id != "game_anticheat_log" {
            continue;
        }
        if id.starts_with("eac_") && !eac_artifact_matches_identity(artifact, identity) {
            continue;
        }
        for line in artifact_lines(artifact) {
            parse_eac_line(&line, &mut summary);
            if id == "steam_runprocess" {
                parse_eac_setup_line(appid, &line, &mut summary);
            }
        }
    }
    summary
}

fn eac_summary_from_identity(identity: &EacIdentity) -> EacSummary {
    EacSummary {
        settings_path: identity.settings_path.clone(),
        process_title: identity.process_title.clone(),
        executable_path: identity.executable_path.clone(),
        product_id: identity.product_id.clone(),
        sandbox_id: identity.sandbox_id.clone(),
        deployment_id: identity.deployment_id.clone(),
        ..EacSummary::default()
    }
}

fn eac_artifact_matches_identity(artifact: &Value, identity: Option<&EacIdentity>) -> bool {
    let Some(identity) = identity else {
        return true;
    };
    let path = artifact.get("path").and_then(|v| v.as_str()).unwrap_or("").to_ascii_lowercase();
    match (&identity.product_id, &identity.deployment_id) {
        (Some(product), Some(deployment)) => {
            path.contains(&product.to_ascii_lowercase()) && path.contains(&deployment.to_ascii_lowercase())
        },
        (Some(product), None) => path.contains(&product.to_ascii_lowercase()),
        _ => true,
    }
}

fn summarize_steam(appid: u32, artifacts: &[Value]) -> SteamSummary {
    let mut summary = SteamSummary::default();
    for artifact in artifacts {
        let id = artifact.get("id").and_then(|v| v.as_str()).unwrap_or("");
        for line in artifact_lines(artifact) {
            if id == "steam_gameprocess" {
                parse_gameprocess_line(appid, &line, &mut summary);
            } else if id == "steam_runprocess" {
                parse_runprocess_line(appid, &line, &mut summary);
            }
        }
    }
    summary
}

fn parse_eac_line(line: &str, summary: &mut EacSummary) {
    if let Some(value) = extract_between(line, "Loaded the following settings .json file: '", "'") {
        summary.settings_path = Some(value);
    }
    for (prefix, slot) in [
        (" - ProcessTitle: ", &mut summary.process_title),
        (" - ExecutablePath: ", &mut summary.executable_path),
        (" - ProductId: ", &mut summary.product_id),
        (" - SandboxId: ", &mut summary.sandbox_id),
        (" - DeploymentId: ", &mut summary.deployment_id),
    ] {
        if let Some(value) = line.split(prefix).nth(1) {
            *slot = Some(value.trim().trim_end_matches('.').to_string());
        }
    }
    if let Some(url) = line.split("Connecting to URL: ").nth(1) {
        let url = url.trim().to_string();
        summary.module_target = url.rsplit('/').next().map(|v| v.to_string());
        summary.module_url = Some(url);
    }
    if let Some(code) = line.split("Response Code: ").nth(1).and_then(first_i64) {
        summary.connect_response_code = Some(code);
    }
    if let Some(version) = line.split("Starting Wine module mapping, Wine version: ").nth(1) {
        summary.wine_version = Some(version.trim().trim_end_matches('.').to_string());
        summary.module_mapping_status.get_or_insert_with(|| "started".to_string());
    }
    if line.contains("Failed to map the anti-cheat module") {
        summary.module_mapping_status = Some("failed".to_string());
    }
    if let Some(rest) = line.split("Launcher finished with: ").nth(1) {
        summary.launcher_exit_code = first_i64(rest);
        summary.launcher_error = extract_between(rest, "'", "'");
    }
}

fn parse_eac_setup_line(appid: u32, line: &str, summary: &mut EacSummary) {
    let lower = line.to_ascii_lowercase();
    if !lower.contains("easyanticheat") || !lower.contains("setup") {
        return;
    }
    let marker = format!("[appid {}]", appid);
    if !lower.contains(&marker) {
        return;
    }
    if let Some(code) = line.split("Exit Code (").nth(1).and_then(first_i64) {
        summary.setup_exit_code = Some(code);
    }
}

fn parse_gameprocess_line(appid: u32, line: &str, summary: &mut SteamSummary) {
    let marker = format!("AppID {}", appid);
    if !line.contains(&marker) {
        return;
    }
    if line.contains("adding PID") {
        summary.tracked_pid = line.split("adding PID ").nth(1).and_then(first_i64);
        if let Some(path) = line.split("tracked process ").nth(1).map(normalize_steam_command) {
            if path.to_ascii_lowercase().contains("start_protected_game") {
                summary.protected_launcher_path = Some(path);
            }
        }
    } else if line.contains("no longer tracking PID") {
        summary.tracked_exit_code = line.split("exit code ").nth(1).and_then(first_i64);
    }
}

fn parse_runprocess_line(appid: u32, line: &str, summary: &mut SteamSummary) {
    let marker = format!("[AppID {}]", appid);
    if !line.contains(&marker) || !line.contains("Exit Code (") {
        return;
    }
    let code = line.split("Exit Code (").nth(1).and_then(first_i64);
    let command = extract_between(line, ") :  ", " GLE").unwrap_or_else(|| line.to_string());
    summary.redist_exit_codes.push(json!({"exitCode": code, "command": command}));
}

fn evidence_status(eac: &EacSummary, steam: &SteamSummary, artifacts: &[Value]) -> String {
    if eac.module_mapping_status.as_deref() == Some("failed") {
        return "module_mapping_failed".to_string();
    }
    if steam.tracked_exit_code == Some(206) || eac.launcher_exit_code == Some(206) {
        return "protected_launcher_failed".to_string();
    }
    if eac.setup_exit_code == Some(0) && eac.module_target.is_some() {
        return "protected_module_downloaded".to_string();
    }
    if eac.setup_exit_code == Some(0) {
        return "setup_installed".to_string();
    }
    if artifacts.iter().any(|a| a.get("id").and_then(|v| v.as_str()).unwrap_or("").contains("battleye")) {
        return "battleye_evidence_found".to_string();
    }
    "unknown".to_string()
}

fn probe_status(eac: &EacSummary, module_assets: &[Value], game_dir_staged: bool) -> String {
    if game_dir_staged
        && module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("unknown"))
    {
        return "staged_download_incomplete".to_string();
    }
    if eac.module_mapping_status.as_deref() == Some("failed")
        && eac.module_target.as_deref().unwrap_or("").starts_with("linux")
    {
        return "linux_module_on_darwin_boundary".to_string();
    }
    if eac.module_mapping_status.as_deref() == Some("failed") {
        return "module_mapping_failed".to_string();
    }
    if module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("elf")) {
        return "linux_module_assets_present".to_string();
    }
    if module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("mach_o")) {
        return "darwin_module_assets_present".to_string();
    }
    "no_module_probe_target".to_string()
}

fn summary_text(status: &str, eac: &EacSummary, steam: &SteamSummary) -> String {
    match status {
        "module_mapping_failed" => format!(
            "Protected launcher reached Wine module mapping under Wine {} and failed to map the anti-cheat module{}.",
            eac.wine_version.as_deref().unwrap_or("unknown"),
            eac.module_target.as_ref().map(|t| format!(" after downloading {}", t)).unwrap_or_default()
        ),
        "protected_launcher_failed" => format!(
            "Protected launcher exited with code {}.",
            eac.launcher_exit_code.or(steam.tracked_exit_code).unwrap_or_default()
        ),
        "protected_module_downloaded" => {
            format!(
                "EAC setup installed and downloaded the {} module.",
                eac.module_target.as_deref().unwrap_or("unknown")
            )
        },
        "setup_installed" => {
            "Anti-cheat setup completed, but no protected-launch module download was found yet.".to_string()
        },
        "battleye_evidence_found" => {
            "BattlEye evidence was found; inspect the attached artifacts for the launch failure.".to_string()
        },
        _ => "No conclusive anti-cheat launch evidence was found for this appid.".to_string(),
    }
}

fn next_actions(status: &str) -> Vec<&'static str> {
    match status {
        "module_mapping_failed" => vec![
            "Run the Wine module-mapping probe against this prefix and appid.",
            "Compare MetalSharp Wine loader/syscall behavior with Proton for EAC EOS module mapping.",
            "Check whether the downloaded module target is a Linux ELF module that macOS cannot host without a compatibility substrate.",
        ],
        "protected_launcher_failed" => vec![
            "Inspect Steam gameprocess and EAC launcher tails for the last protected-launch transition.",
            "Verify the protected launcher is running inside the correct Steam game bottle prefix.",
        ],
        "setup_installed" | "protected_module_downloaded" => vec![
            "Launch through the protected Steam route and refresh this evidence report.",
            "Verify Steam kept the route-specific bottle environment for the protected launcher.",
        ],
        _ => vec![
            "Launch the game once through the protected Steam route, then refresh this report.",
            "If the game uses BattlEye, check the game directory and Common Files BattlEye logs.",
        ],
    }
}

fn probe_summary(status: &str, eac: &EacSummary) -> String {
    match status {
        "linux_module_on_darwin_boundary" => format!(
            "EAC selected a {} module and Wine reached module mapping on macOS; Darwin cannot directly load that Linux module as a dylib.",
            eac.module_target.as_deref().unwrap_or("linux")
        ),
        "module_mapping_failed" => {
            "The protected launcher reached module mapping, but the module target could not be classified from the logs.".to_string()
        },
        "linux_module_assets_present" => {
            "The game folder contains Linux anti-cheat module assets; MetalSharp needs a truthful host substrate before those can run on macOS.".to_string()
        },
        "darwin_module_assets_present" => {
            "The game folder contains Darwin module assets. This is the only direct dylib path MetalSharp could investigate without a Linux substrate.".to_string()
        },
        "staged_download_incomplete" => {
            "Anti-cheat launch targets were found in Steam's staged download area, but at least one target does not have a valid binary header yet. Treat this install as evidence-only, not launchable.".to_string()
        },
        _ => "No anti-cheat module asset or module-mapping target was found yet.".to_string(),
    }
}

fn probe_next_actions(status: &str) -> Vec<&'static str> {
    match status {
        "linux_module_on_darwin_boundary" | "linux_module_assets_present" => vec![
            "Audit Proton's EAC loader path around Linux module mapping and Wine syscall dispatch.",
            "Prototype a read-only host contract probe for mmap, executable protections, and loader callbacks before changing Wine.",
            "Decide whether MetalSharp can ship a signed Linux user-space substrate or must require publisher/vendor macOS assets.",
        ],
        "darwin_module_assets_present" => vec![
            "Inspect the dylib signature and expected host API before attempting any load.",
            "Confirm publisher/vendor support before treating the Darwin asset as launchable.",
        ],
        "module_mapping_failed" => vec![
            "Capture the full EAC launcher log and locate the downloaded module target.",
            "Compare the failing Wine version against Proton's EAC-enabled Wine tree.",
        ],
        "staged_download_incomplete" => vec![
            "Let Steam finish or repair the download before attempting protected launch.",
            "Re-run the probe after executable headers classify as PE, ELF, or Mach-O instead of unknown.",
        ],
        _ => vec![
            "Launch once through the protected route, then run /steam/anticheat-evidence and /steam/anticheat-probe again.",
        ],
    }
}

fn module_contract_checks(host_os: &str, eac: &EacSummary, module_assets: &[Value]) -> Value {
    let module_target = eac.module_target.as_deref().unwrap_or("");
    let selected_linux_module = module_target.starts_with("linux");
    let has_elf_asset = module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("elf"));
    let has_macho_asset =
        module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("mach_o"));
    json!({
        "selectedLinuxModule": selected_linux_module,
        "hasLinuxElfAsset": has_elf_asset,
        "hasDarwinDylibAsset": has_macho_asset,
        "directHostLoadPossible": !selected_linux_module && (!has_elf_asset || can_dlopen_linux_elf_directly(host_os)),
        "needsLinuxUserSpaceSubstrate": (selected_linux_module || has_elf_asset) && !can_dlopen_linux_elf_directly(host_os),
        "needsVendorMacOSAsset": host_os == "macos" && selected_linux_module && !has_macho_asset,
    })
}

fn contract_probe_status(host_os: &str, eac: &EacSummary, module_assets: &[Value], host_contract: &Value) -> String {
    let selected_linux_module = eac.module_target.as_deref().unwrap_or("").starts_with("linux");
    let has_elf_asset = module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("elf"));
    let direct_elf_load = host_contract
        .get("syntheticElfDirectLoad")
        .and_then(|v| v.get("ok"))
        .and_then(|v| v.as_bool())
        .unwrap_or(false);
    let executable_mapping = host_contract
        .get("anonymousExecutableMapping")
        .and_then(|v| v.get("ok"))
        .and_then(|v| v.as_bool())
        .unwrap_or(false);

    if host_os == "macos" && (selected_linux_module || has_elf_asset) && !direct_elf_load {
        "linux_elf_host_gap_confirmed".to_string()
    } else if (selected_linux_module || has_elf_asset) && direct_elf_load && executable_mapping {
        "linux_elf_host_contract_present".to_string()
    } else if eac.module_mapping_status.as_deref() == Some("failed") {
        "loader_contract_needs_delta_audit".to_string()
    } else {
        "host_contract_recorded".to_string()
    }
}

fn contract_probe_summary(status: &str) -> &'static str {
    match status {
        "linux_elf_host_gap_confirmed" => {
            "Protected launch selected Linux module semantics, but the current host contract is macOS/Mach-O rather than a Linux ELF module host."
        },
        "linux_elf_host_contract_present" => {
            "The host contract appears able to load Linux ELF-style modules; compare remaining Wine loader behavior before claiming anti-cheat support."
        },
        "loader_contract_needs_delta_audit" => {
            "Module mapping failed without a confirmed Linux ELF host gap; inspect Wine loader, memory protection, and wineserver state next."
        },
        _ => "Host contract probe recorded; collect protected-launch evidence to connect it to a game-specific anti-cheat failure.",
    }
}

fn contract_probe_next_actions(status: &str) -> Vec<&'static str> {
    match status {
        "linux_elf_host_gap_confirmed" => vec![
            "Do not change graphics routing; the failing layer is the protected module host contract.",
            "Compare Proton's Linux EAC module host expectations against MetalSharp's macOS Wine boundary.",
            "Choose between a transparent Linux user-space substrate prototype or vendor-supported macOS module assets.",
        ],
        "linux_elf_host_contract_present" => vec![
            "Add narrower Wine loader probes for ntdll loader callbacks and executable section mapping.",
            "Compare protected-launch logs before and after any Wine loader changes.",
        ],
        "loader_contract_needs_delta_audit" => vec![
            "Run the delta audit and inspect loader/syscall rows before touching protected launch.",
        ],
        _ => vec!["Launch a protected offline-compatible title once, then re-run the evidence and contract probes."],
    }
}

fn path_check(path: &Path) -> Value {
    let metadata = fs::metadata(path).ok();
    json!({
        "path": path.to_string_lossy(),
        "exists": metadata.is_some(),
        "isDir": metadata.as_ref().map(|m| m.is_dir()).unwrap_or(false),
    })
}

fn wine_version(wine_bin: &Path) -> Option<String> {
    command_stdout(wine_bin, &["--version"])
        .map(|out| out.lines().next().unwrap_or("").trim().to_string())
        .filter(|v| !v.is_empty())
}

fn wineserver_socket_root() -> PathBuf {
    current_uid_string()
        .map(|uid| PathBuf::from("/tmp").join(format!(".wine-{}", uid)))
        .unwrap_or_else(|| PathBuf::from("/tmp/.wine-unknown"))
}

fn wineserver_socket_dirs() -> Vec<String> {
    let root = wineserver_socket_root();
    let mut dirs = fs::read_dir(&root)
        .ok()
        .into_iter()
        .flat_map(|entries| entries.filter_map(Result::ok))
        .map(|entry| entry.path())
        .filter(|path| {
            path.file_name().and_then(|v| v.to_str()).map(|name| name.starts_with("server-")).unwrap_or(false)
        })
        .map(|path| path.to_string_lossy().to_string())
        .collect::<Vec<_>>();
    dirs.sort();
    dirs
}

fn current_uid_string() -> Option<String> {
    command_stdout(Path::new("id"), &["-u"]).map(|out| out.trim().to_string()).filter(|uid| !uid.is_empty())
}

fn process_list_contains(patterns: &[&str]) -> bool {
    let Some(output) = command_stdout(Path::new("ps"), &["-axo", "comm,args"]) else {
        return false;
    };
    process_text_contains(&output, patterns)
}

fn process_text_contains(output: &str, patterns: &[&str]) -> bool {
    output.lines().any(|line| patterns.iter().any(|pattern| line.contains(pattern)))
}

fn command_stdout(program: &Path, args: &[&str]) -> Option<String> {
    let output = Command::new(program).args(args).output().ok()?;
    if !output.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&output.stdout).into_owned())
}

fn delta_group(id: &str, label: &str, checks: Vec<Value>) -> Value {
    json!({
        "id": id,
        "label": label,
        "status": delta_group_status(&checks),
        "checks": checks,
    })
}

fn delta_path(id: &str, importance: &str, path: &Path, note: Option<&str>) -> Value {
    let metadata = fs::metadata(path).ok();
    json!({
        "id": id,
        "importance": importance,
        "present": metadata.is_some(),
        "path": path.to_string_lossy(),
        "note": note,
    })
}

fn delta_capability(id: &str, importance: &str, present: bool, note: &str) -> Value {
    json!({
        "id": id,
        "importance": importance,
        "present": present,
        "note": note,
    })
}

fn delta_observation(id: &str, importance: &str, present: bool, value: Option<Value>, note: &str) -> Value {
    json!({
        "id": id,
        "importance": importance,
        "present": present,
        "value": value,
        "note": note,
    })
}

fn delta_group_status(checks: &[Value]) -> &'static str {
    if checks.iter().any(|check| {
        let importance = check.get("importance").and_then(|v| v.as_str()).unwrap_or("");
        let present = check.get("present").and_then(|v| v.as_bool()).unwrap_or(false);
        matches!(importance, "required" | "blocking_when_false") && !present
            || matches!(importance, "blocking_when_macos") && present && std::env::consts::OS == "macos"
    }) {
        "blocking"
    } else if checks.iter().any(|check| check.get("present").and_then(|v| v.as_bool()) == Some(false)) {
        "informational_gap"
    } else {
        "ready"
    }
}

fn delta_audit_status(surfaces: &[Value]) -> &'static str {
    if surfaces.iter().any(|surface| surface.get("status").and_then(|v| v.as_str()) == Some("blocking")) {
        "blocking_delta_found"
    } else if surfaces.iter().any(|surface| surface.get("status").and_then(|v| v.as_str()) == Some("informational_gap"))
    {
        "comparison_gaps_found"
    } else {
        "no_blocking_delta_found"
    }
}

fn delta_audit_summary(eac: &EacSummary, host_os: &str) -> String {
    if host_os == "macos" && eac.module_target.as_deref().unwrap_or("").starts_with("linux") {
        return format!(
            "MetalSharp has Wine/DXMT runtime pieces, but protected launch selected {} and needs a Linux-user-space or vendor macOS module answer.",
            eac.module_target.as_deref().unwrap_or("linux")
        );
    }
    "Delta audit completed; inspect blocking and proton_comparison rows for the next implementation target.".to_string()
}

fn substrate_decision(host_os: &str, eac: &EacSummary, module_assets: &[Value]) -> String {
    let selected_linux = eac.module_target.as_deref().unwrap_or("").starts_with("linux");
    let has_elf_asset = module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("elf"));
    let has_macho_asset =
        module_assets.iter().any(|asset| asset.get("format").and_then(|v| v.as_str()) == Some("mach_o"));
    if host_os == "macos" && has_macho_asset {
        "investigate_vendor_macos_module".to_string()
    } else if host_os == "macos" && (selected_linux || has_elf_asset) {
        "requires_linux_user_space_substrate_or_vendor_macos_asset".to_string()
    } else if eac.module_mapping_status.as_deref() == Some("failed") {
        "requires_loader_delta_audit".to_string()
    } else {
        "collect_protected_launch_evidence".to_string()
    }
}

fn substrate_decision_summary(decision: &str) -> &'static str {
    match decision {
        "investigate_vendor_macos_module" => {
            "A Darwin module asset appears present; verify vendor support, signature, and expected host API before attempting any load."
        },
        "requires_linux_user_space_substrate_or_vendor_macos_asset" => {
            "The protected launch path selected Linux anti-cheat assets on macOS; MetalSharp needs a legitimate Linux user-space substrate or vendor-supported macOS assets."
        },
        "requires_loader_delta_audit" => {
            "Module mapping failed, but the selected module target is unclear; complete the Proton/Wine loader delta audit first."
        },
        _ => "No protected-launch module decision can be made yet; collect EAC/BattlEye launch evidence first.",
    }
}

fn allowed_substrate_paths(decision: &str) -> Vec<&'static str> {
    match decision {
        "investigate_vendor_macos_module" => vec![
            "validate vendor-supported macOS module assets",
            "document expected host API and signing requirements",
            "build only transparent compatibility glue approved by the publisher or anti-cheat vendor",
        ],
        "requires_linux_user_space_substrate_or_vendor_macos_asset" => vec![
            "build a signed Linux user-space compatibility substrate for ELF module hosting",
            "obtain or document vendor-supported macOS anti-cheat module assets",
            "work with publisher/vendor enablement instead of spoofing trust",
        ],
        "requires_loader_delta_audit" => vec![
            "complete Proton/Wine loader and syscall delta audit",
            "add precise probes for mmap, executable protections, and loader callbacks",
        ],
        _ => vec!["collect protected-launch logs and module target evidence"],
    }
}

fn substrate_next_actions(decision: &str) -> Vec<&'static str> {
    match decision {
        "requires_linux_user_space_substrate_or_vendor_macos_asset" => vec![
            "Prototype a harmless ELF loader capability probe outside the protected module path.",
            "Map the minimum Linux user-space APIs a vendor EAC/BattlEye module expects under Proton.",
            "Prepare a vendor-facing proof bundle showing the exact module target, host OS boundary, and non-evasion policy.",
        ],
        "investigate_vendor_macos_module" => vec![
            "Verify the Mach-O asset is actually vendor anti-cheat code, not an unrelated helper.",
            "Check code signature and load requirements without injecting it into a protected process.",
        ],
        "requires_loader_delta_audit" => vec![
            "Run /steam/anticheat-delta-audit and compare the blocking rows with Proton behavior.",
        ],
        _ => vec![
            "Run the protected Steam launch once and then refresh /steam/anticheat-evidence.",
        ],
    }
}

fn classify_module_path(path: &Path) -> &'static str {
    let path_lc = path.to_string_lossy().to_ascii_lowercase();
    if path_lc.contains("battleye") || path_lc.contains("beclient") || path_lc.contains("beservice") {
        "battleye"
    } else if path_lc.contains("easyanticheat") {
        "easyanticheat"
    } else {
        "unknown"
    }
}

fn read_binary_format(path: &Path) -> &'static str {
    let mut file = match File::open(path) {
        Ok(file) => file,
        Err(_) => return "unknown",
    };
    let mut bytes = [0u8; 4];
    let len = match file.read(&mut bytes) {
        Ok(len) => len,
        Err(_) => return "unknown",
    };
    binary_format(&bytes[..len])
}

fn binary_format(bytes: &[u8]) -> &'static str {
    if bytes.len() >= 4 && &bytes[0..4] == b"\x7fELF" {
        return "elf";
    }
    if bytes.len() >= 2 && &bytes[0..2] == b"MZ" {
        return "pe";
    }
    if bytes.len() >= 4 {
        let magic = u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
        if matches!(magic, 0xfeedface | 0xfeedfacf | 0xcafebabe | 0xcffaedfe | 0xcefaedfe | 0xbebafeca) {
            return "mach_o";
        }
    }
    "unknown"
}

fn can_dlopen_linux_elf_directly(host_os: &str) -> bool {
    host_os == "linux"
}

fn artifact_tail(artifact: &Value) -> Vec<&str> {
    artifact
        .get("tail")
        .and_then(|v| v.as_array())
        .map(|lines| lines.iter().filter_map(|v| v.as_str()).collect())
        .unwrap_or_default()
}

fn artifact_lines(artifact: &Value) -> Vec<String> {
    artifact
        .get("path")
        .and_then(|v| v.as_str())
        .and_then(|path| read_recent_text_limited(Path::new(path)))
        .map(|text| text.lines().map(|line| line.trim_end_matches('\r').to_string()).collect())
        .unwrap_or_else(|| artifact_tail(artifact).into_iter().map(|line| line.to_string()).collect())
}

fn read_recent_text_limited(path: &Path) -> Option<String> {
    let mut file = File::open(path).ok()?;
    let len = file.metadata().ok()?.len();
    if len > MAX_ARTIFACT_READ_BYTES {
        file.seek(SeekFrom::Start(len - MAX_ARTIFACT_READ_BYTES)).ok()?;
    }
    let mut bytes = Vec::new();
    file.take(MAX_ARTIFACT_READ_BYTES).read_to_end(&mut bytes).ok()?;
    Some(String::from_utf8_lossy(&bytes).into_owned())
}

fn tail_lines(text: &str, max_lines: usize) -> Vec<String> {
    let lines: Vec<&str> = text.lines().collect();
    let start = lines.len().saturating_sub(max_lines);
    lines[start..].iter().map(|line| line.trim_end_matches('\r').to_string()).collect()
}

fn extract_between(text: &str, start: &str, end: &str) -> Option<String> {
    let rest = text.split(start).nth(1)?;
    let value = rest.split(end).next()?;
    Some(value.trim().to_string())
}

fn normalize_steam_command(command: &str) -> String {
    command.trim().trim_matches('"').to_string()
}

fn first_i64(text: &str) -> Option<i64> {
    let mut chars = text.trim_start().chars().peekable();
    let mut buf = String::new();
    if chars.peek() == Some(&'-') {
        buf.push('-');
        chars.next();
    }
    while let Some(ch) = chars.peek() {
        if ch.is_ascii_digit() {
            buf.push(*ch);
            chars.next();
        } else {
            break;
        }
    }
    if buf.is_empty() || buf == "-" {
        None
    } else {
        buf.parse().ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_eac_module_mapping_failure() {
        let mut summary = EacSummary::default();
        parse_eac_line(
            "[07:14:24:940] [Windows] [EAC Launcher] [Info]  - ProductId: 789399aada914e66bb3c3facebc5d709.",
            &mut summary,
        );
        parse_eac_line("[07:14:26:048] [Windows] [EAC Launcher] [Info] [Connection] Connecting to URL: https://modules-cdn.eac-prod.on.epicgames.com/modules/product/deploy/linux64", &mut summary);
        parse_eac_line(
            "[07:14:26:516] [Windows] [EAC Launcher] [Info] Starting Wine module mapping, Wine version: 11.5.",
            &mut summary,
        );
        parse_eac_line(
            "[07:14:26:517] [Windows] [EAC Launcher] [Err!] Failed to map the anti-cheat module.",
            &mut summary,
        );
        parse_eac_line("[07:14:27:415] [Windows] [EAC Launcher] [Info] Launcher finished with: 206, 'Failed to load the anti-cheat module.'.", &mut summary);

        assert_eq!(summary.product_id.as_deref(), Some("789399aada914e66bb3c3facebc5d709"));
        assert_eq!(summary.module_target.as_deref(), Some("linux64"));
        assert_eq!(summary.wine_version.as_deref(), Some("11.5"));
        assert_eq!(summary.module_mapping_status.as_deref(), Some("failed"));
        assert_eq!(summary.launcher_exit_code, Some(206));
        assert_eq!(summary.launcher_error.as_deref(), Some("Failed to load the anti-cheat module."));
    }

    #[test]
    fn parses_steam_protected_launch_exit() {
        let mut summary = SteamSummary::default();
        parse_gameprocess_line(1888160, "[2026-05-20 01:14:24] AppID 1888160 adding PID 1316 as a tracked process \"\"Z:\\SteamLibrary\\steamapps\\common\\Game\\start_protected_game.exe\"\"", &mut summary);
        parse_gameprocess_line(
            1888160,
            "[2026-05-20 01:14:38] AppID 1888160 no longer tracking PID 1316, exit code 206",
            &mut summary,
        );

        assert_eq!(summary.tracked_pid, Some(1316));
        assert_eq!(summary.tracked_exit_code, Some(206));
        assert!(summary.protected_launcher_path.as_deref().unwrap_or_default().contains("start_protected_game.exe"));
    }

    #[test]
    fn binary_format_classifies_common_module_headers() {
        assert_eq!(binary_format(b"\x7fELF\x02\x01"), "elf");
        assert_eq!(binary_format(b"MZ\x90\x00"), "pe");
        assert_eq!(binary_format(&[0xfe, 0xed, 0xfa, 0xcf]), "mach_o");
        assert_eq!(binary_format(b"not a module"), "unknown");
    }

    #[test]
    fn probe_status_flags_linux_module_mapping_on_darwin_boundary() {
        let eac = EacSummary {
            module_target: Some("linux64".to_string()),
            module_mapping_status: Some("failed".to_string()),
            ..Default::default()
        };
        assert_eq!(probe_status(&eac, &[], false), "linux_module_on_darwin_boundary");
        let checks = module_contract_checks("macos", &eac, &[]);
        assert_eq!(checks.get("needsLinuxUserSpaceSubstrate").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(checks.get("needsVendorMacOSAsset").and_then(|v| v.as_bool()), Some(true));
    }

    #[test]
    fn delta_group_status_marks_missing_required_paths_blocking() {
        let checks = vec![
            json!({"id": "present_required", "importance": "required", "present": true}),
            json!({"id": "missing_required", "importance": "required", "present": false}),
        ];
        assert_eq!(delta_group_status(&checks), "blocking");
    }

    #[test]
    fn delta_audit_status_promotes_blocking_surface() {
        let surfaces = vec![json!({"id": "anticheat_module_contract", "status": "blocking"})];
        assert_eq!(delta_audit_status(&surfaces), "blocking_delta_found");
    }

    #[test]
    fn process_text_detects_wineserver_rows() {
        let ps = "/bin/zsh something else\n/Users/alex/.metalsharp/runtime/wine/bin/wineserver wineserver\n";

        assert!(process_text_contains(ps, &["wineserver"]));
        assert!(!process_text_contains(ps, &["steam.exe"]));
    }

    #[test]
    fn substrate_decision_requires_linux_substrate_for_linux_module_on_macos() {
        let eac = EacSummary { module_target: Some("linux64".to_string()), ..Default::default() };
        assert_eq!(substrate_decision("macos", &eac, &[]), "requires_linux_user_space_substrate_or_vendor_macos_asset");
    }

    #[test]
    fn contract_probe_confirms_macos_linux_elf_host_gap() {
        let eac = EacSummary {
            module_target: Some("linux64".to_string()),
            module_mapping_status: Some("failed".to_string()),
            ..Default::default()
        };
        let host_contract = json!({
            "syntheticElfDirectLoad": {"ok": false},
            "anonymousExecutableMapping": {"ok": true},
        });

        assert_eq!(contract_probe_status("macos", &eac, &[], &host_contract), "linux_elf_host_gap_confirmed");
    }

    #[test]
    fn contract_probe_distinguishes_loader_gap_without_linux_target() {
        let eac = EacSummary { module_mapping_status: Some("failed".to_string()), ..Default::default() };
        let host_contract = json!({
            "syntheticElfDirectLoad": {"ok": false},
            "anonymousExecutableMapping": {"ok": true},
        });

        assert_eq!(contract_probe_status("macos", &eac, &[], &host_contract), "loader_contract_needs_delta_audit");
    }

    #[cfg(unix)]
    #[test]
    fn synthetic_elf_probe_file_is_exclusive_and_removed_by_caller() {
        let path = write_secure_synthetic_elf().expect("secure synthetic elf");
        assert!(path.exists());
        assert!(path.file_name().unwrap_or_default().to_string_lossy().starts_with("metalsharp-synthetic-eac-module-"));
        assert_ne!(
            path.file_name().unwrap_or_default().to_string_lossy(),
            format!("metalsharp-synthetic-eac-module-{}.so", std::process::id())
        );
        assert_eq!(read_binary_format(&path), "elf");
        std::fs::remove_file(path).expect("remove synthetic elf");
    }

    #[test]
    fn module_assets_include_protected_launcher_and_unknown_staged_payloads() {
        let game_dir = test_dir("anticheat-staged-payload");
        let launcher = game_dir.join("Game").join("start_protected_game.exe");
        std::fs::create_dir_all(launcher.parent().expect("launcher parent")).expect("create launcher parent");
        std::fs::write(&launcher, [0_u8; 16]).expect("write staged launcher");

        let assets = collect_module_assets(&game_dir);
        assert!(assets.iter().any(|asset| {
            asset.get("path").and_then(|v| v.as_str()).unwrap_or_default().ends_with("start_protected_game.exe")
                && asset.get("format").and_then(|v| v.as_str()) == Some("unknown")
        }));
        assert_eq!(probe_status(&EacSummary::default(), &assets, true), "staged_download_incomplete");

        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn module_assets_classify_linux_eac_modules() {
        let game_dir = test_dir("anticheat-linux-module");
        let module = game_dir.join("EasyAntiCheat").join("easyanticheat_x64.so");
        std::fs::create_dir_all(module.parent().expect("module parent")).expect("create module parent");
        std::fs::write(&module, b"\x7fELF\x02\x01").expect("write elf module");

        let assets = collect_module_assets(&game_dir);
        assert!(assets.iter().any(|asset| {
            asset.get("path").and_then(|v| v.as_str()).unwrap_or_default().ends_with("easyanticheat_x64.so")
                && asset.get("format").and_then(|v| v.as_str()) == Some("elf")
        }));
        assert_eq!(probe_status(&EacSummary::default(), &assets, false), "linux_module_assets_present");

        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn eac_identity_reads_loose_settings_json() {
        let game_dir = test_dir("anticheat-settings");
        let settings = game_dir.join("Game").join("EasyAntiCheat").join("Settings.json");
        std::fs::create_dir_all(settings.parent().expect("settings parent")).expect("create settings parent");
        std::fs::write(
            &settings,
            "\u{feff}{\n\
\t\"title\" : \"ELDEN RING™\",\n\
\t\"executable\" : \"eldenring.exe\",\n\
\t\"productid\" : \"elden-product\",\n\
\t\"sandboxid\" : \"elden-sandbox\",\n\
\t\"deploymentid\" : \"elden-deploy\",\n\
}\n",
        )
        .expect("write settings");

        let identity = read_eac_identity(&game_dir).expect("read identity");
        assert_eq!(identity.process_title.as_deref(), Some("ELDEN RING™"));
        assert_eq!(identity.executable_path.as_deref(), Some("eldenring.exe"));
        assert_eq!(identity.product_id.as_deref(), Some("elden-product"));
        assert_eq!(identity.sandbox_id.as_deref(), Some("elden-sandbox"));
        assert_eq!(identity.deployment_id.as_deref(), Some("elden-deploy"));

        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn eac_summary_ignores_other_games_shared_prefix_logs() {
        let identity = EacIdentity {
            settings_path: Some("Z:\\ELDEN RING\\Game\\EasyAntiCheat\\Settings.json".to_string()),
            process_title: Some("ELDEN RING™".to_string()),
            executable_path: Some("eldenring.exe".to_string()),
            product_id: Some("elden-product".to_string()),
            sandbox_id: Some("elden-sandbox".to_string()),
            deployment_id: Some("elden-deploy".to_string()),
        };
        let artifacts = vec![
            json!({
                "id": "eac_launcher",
                "path": "C:\\users\\alex\\AppData\\Roaming\\EasyAntiCheat\\other-product\\other-deploy\\anticheatlauncher.log",
                "tail": [
                    "[Windows] [EAC Launcher] [Info]  - ProductId: other-product.",
                    "[Windows] [EAC Launcher] [Info] [Connection] Connecting to URL: https://modules/other-product/other-deploy/linux64",
                    "[Windows] [EAC Launcher] [Info] Starting Wine module mapping, Wine version: 11.5.",
                    "[Windows] [EAC Launcher] [Err!] Failed to map the anti-cheat module.",
                    "[Windows] [EAC Launcher] [Info] Launcher finished with: 206, 'Failed to load the anti-cheat module.'."
                ]
            }),
            json!({
                "id": "eac_launcher",
                "path": "C:\\users\\alex\\AppData\\Roaming\\EasyAntiCheat\\elden-product\\elden-deploy\\anticheatlauncher.log",
                "tail": [
                    "[Windows] [EAC Launcher] [Info]  - ProductId: elden-product.",
                    "[Windows] [EAC Launcher] [Info] [Connection] Connecting to URL: https://modules/elden-product/elden-deploy/linux64",
                    "[Windows] [EAC Launcher] [Info] Starting Wine module mapping, Wine version: 11.5.",
                    "[Windows] [EAC Launcher] [Err!] Failed to map the anti-cheat module.",
                    "[Windows] [EAC Launcher] [Info] Launcher finished with: 206, 'Failed to load the anti-cheat module.'."
                ]
            }),
            json!({
                "id": "steam_runprocess",
                "path": "C:\\Steam\\logs\\runprocess_log.txt",
                "tail": [
                    "05/21/26 21:52:50 [AppID 1888160] Exit Code (0) :  \"Z:\\ACVI\\EasyAntiCheat\\easyanticheat_eos_setup.exe\" install other-product GLE 0",
                    "05/21/26 22:07:57 [AppID 1245620] Exit Code (0) :  \"Z:\\ELDEN RING\\EasyAntiCheat\\easyanticheat_eos_setup.exe\" install elden-product GLE 0"
                ]
            }),
        ];

        let summary = summarize_eac(1245620, &artifacts, Some(&identity));
        assert_eq!(summary.product_id.as_deref(), Some("elden-product"));
        assert_eq!(summary.deployment_id.as_deref(), Some("elden-deploy"));
        assert_eq!(summary.setup_exit_code, Some(0));
        assert_eq!(summary.launcher_exit_code, Some(206));
        assert_eq!(summary.module_url.as_deref(), Some("https://modules/elden-product/elden-deploy/linux64"));
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-{}-{}", name, test_suffix()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("create test dir");
        dir
    }

    fn test_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
