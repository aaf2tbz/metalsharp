use serde_json::{json, Map, Value};
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom};
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::time::UNIX_EPOCH;
use walkdir::WalkDir;

const ARTIFACT_TAIL_LINES: usize = 80;
const MAX_ARTIFACT_READ_BYTES: u64 = 1024 * 1024;
const MAX_BINARY_PROBE_BYTES: u64 = 8 * 1024 * 1024;
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
    direct_game_path: Option<String>,
    tracked_pid: Option<i64>,
    tracked_exit_code: Option<i64>,
    direct_game_exit_code: Option<i64>,
    redist_exit_codes: Vec<Value>,
    tracked_processes: HashMap<i64, String>,
}

#[derive(Debug, Default)]
struct EacIdentity {
    product_id: Option<String>,
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

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let game_dir = crate::setup::resolve_game_dir(appid);
    let artifacts = collect_artifacts(&prefix, game_dir.as_deref());
    let eac = summarize_eac(appid, &artifacts);
    let steam = summarize_steam(appid, &artifacts);
    let status = evidence_status(&eac, &steam, &artifacts);

    json!({
        "ok": true,
        "appid": appid,
        "status": status,
        "summary": summary_text(&status, &eac, &steam),
        "prefix": prefix.to_string_lossy(),
        "gameDir": game_dir.map(|p| p.to_string_lossy().to_string()),
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
            "directGamePath": steam.direct_game_path,
            "trackedPid": steam.tracked_pid,
            "trackedExitCode": steam.tracked_exit_code,
            "directGameExitCode": steam.direct_game_exit_code,
            "directGameCrash": steam.direct_game_exit_code.map(describe_exit_code),
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

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let game_dir = crate::setup::resolve_game_dir(appid);
    let artifacts = collect_artifacts(&prefix, game_dir.as_deref());
    let eac = summarize_eac(appid, &artifacts);
    let steam = summarize_steam(appid, &artifacts);
    let module_assets = game_dir.as_deref().map(collect_module_assets).unwrap_or_default();
    let runtime_checks = runtime_probe_checks(&home);
    let status = probe_status(&eac, &module_assets);
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
        "gameDir": game_dir.map(|p| p.to_string_lossy().to_string()),
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

pub fn handle_steam_wine_syscall_probe(body: &Map<String, Value>) -> Value {
    let appid =
        body.get("appid").and_then(|v| v.as_u64()).filter(|id| *id > 0 && *id <= u32::MAX as u64).map(|id| id as u32);
    let explicit_token = body.get("token").and_then(|v| v.as_str()).map(normalize_probe_token);
    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "error": "no home dir"}),
    };
    let game_dir = appid.and_then(crate::setup::resolve_game_dir);
    let mut tokens = explicit_token.into_iter().filter(|token| !token.is_empty()).collect::<Vec<_>>();
    if let Some(appid) = appid {
        tokens.push(appid.to_string());
    }
    if let Some(dir) = game_dir.as_deref() {
        tokens.extend(path_probe_tokens(dir));
    }
    tokens.sort();
    tokens.dedup();

    let diagnostics_root = home.join(".metalsharp").join("diagnostics");
    let dirs = collect_probe_diagnostic_dirs(&diagnostics_root, &tokens);
    let reports = dirs.iter().map(|dir| summarize_wine_syscall_dir(dir)).collect::<Vec<_>>();
    let aggregate = aggregate_wine_syscall_reports(&reports);
    let status = wine_syscall_probe_status(&aggregate);

    json!({
        "ok": true,
        "appid": appid,
        "status": status,
        "summary": wine_syscall_probe_summary(&status, &aggregate),
        "diagnosticsRoot": diagnostics_root.to_string_lossy(),
        "tokens": tokens,
        "reports": reports,
        "signals": aggregate,
        "nextActions": wine_syscall_probe_next_actions(&status),
    })
}

pub fn handle_steam_mscompatdb_probe(body: &Map<String, Value>) -> Value {
    let appid =
        body.get("appid").and_then(|v| v.as_u64()).filter(|id| *id > 0 && *id <= u32::MAX as u64).map(|id| id as u32);
    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "error": "no home dir"}),
    };
    let probe = mscompatdb_probe(&home);
    let status = probe.get("status").and_then(|v| v.as_str()).unwrap_or("unknown").to_string();

    json!({
        "ok": true,
        "appid": appid,
        "status": status,
        "summary": mscompatdb_probe_summary(&status),
        "probe": probe,
        "nextActions": mscompatdb_probe_next_actions(&status),
    })
}

pub fn handle_steam_mscompatdb_prepare_dylib(body: &Map<String, Value>) -> Value {
    let force = body.get("force").and_then(|v| v.as_bool()).unwrap_or(false);
    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "error": "no home dir"}),
    };
    prepare_mscompatdb_dylib(&home, force)
}

pub fn handle_steam_anticheat_delta_audit(body: &Map<String, Value>) -> Value {
    let appid =
        body.get("appid").and_then(|v| v.as_u64()).filter(|id| *id > 0 && *id <= u32::MAX as u64).map(|id| id as u32);
    let home = match dirs::home_dir() {
        Some(home) => home,
        None => return json!({"ok": false, "error": "no home dir"}),
    };

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let wine_root = home.join(".metalsharp").join("runtime").join("wine");
    let game_dir = appid.and_then(crate::setup::resolve_game_dir);
    let artifacts = collect_artifacts(&prefix, game_dir.as_deref());
    let eac = summarize_eac(appid.unwrap_or_default(), &artifacts);
    let module_assets = game_dir.as_deref().map(collect_module_assets).unwrap_or_default();
    let mscompatdb = mscompatdb_probe(&home);
    let host_os = std::env::consts::OS;

    let surfaces = vec![
        delta_group(
            "wine_loader",
            "Wine loader/syscall baseline",
            vec![
                delta_path("wineserver", "required", &wine_root.join("bin").join("wineserver"), None),
                delta_path("wine", "required", &wine_root.join("bin").join("wine"), None),
                delta_path("ntdll_unix", "required", &wine_root.join("lib").join("wine").join("x86_64-unix").join("ntdll.so"), None),
                delta_path("ntdll_win64", "required", &wine_root.join("lib").join("wine").join("x86_64-windows").join("ntdll.dll"), None),
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
            "mscompatdb_ntdll_bridge",
            "MetalSharp mscompatdb and Wine ntdll hook surface",
            vec![
                delta_path(
                    "mscompatdb_unix",
                    "required",
                    &wine_root.join("lib").join("wine").join("x86_64-unix").join("mscompatdb.so"),
                    Some("MetalSharp's current Crossover-style bridge shim."),
                ),
                delta_path(
                    "mscompatdb_rules",
                    "required",
                    &wine_root.join("etc").join("mscompatdb_rules.toml"),
                    Some("Rule file consumed by mscompatdb before protected launch."),
                ),
                delta_capability(
                    "mscompatdb_hooked",
                    "blocking_when_false",
                    mscompatdb.get("hooked").and_then(|v| v.as_bool()).unwrap_or(false),
                    "The shim must resolve a Wine syscall hook point; present-but-unhooked cannot affect protected launch.",
                ),
                delta_capability(
                    "ntdll_ke_table_exported",
                    "blocking_when_false",
                    mscompatdb
                        .pointer("/ntdllSymbols/exportsKeServiceDescriptorTable")
                        .and_then(|v| v.as_bool())
                        .unwrap_or(false),
                    "The installed mscompatdb binary looks for KeServiceDescriptorTable, but Wine exposes it as a local/private symbol in current builds.",
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
        "mscompatdb": mscompatdb,
        "moduleAssets": module_assets,
        "nextActions": vec![
            "Use this report as the Phase 3 checklist before changing Wine loader behavior.",
            "Compare blocking and proton_comparison rows against Proton's EAC-enabled Wine tree.",
            "Promote any required missing runtime bridge into a specific implementation task instead of a generic anti-cheat claim.",
        ],
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

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let game_dir = crate::setup::resolve_game_dir(appid);
    let artifacts = collect_artifacts(&prefix, game_dir.as_deref());
    let eac = summarize_eac(appid, &artifacts);
    let steam = summarize_steam(appid, &artifacts);
    let module_assets = game_dir.as_deref().map(collect_module_assets).unwrap_or_default();
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

fn collect_artifacts(prefix: &Path, game_dir: Option<&Path>) -> Vec<Value> {
    let mut candidates = Vec::new();
    let drive_c = prefix.join("drive_c");
    let steam_logs = drive_c.join("Program Files (x86)").join("Steam").join("logs");
    candidates.push(("steam_gameprocess", steam_logs.join("gameprocess_log.txt")));
    candidates.push(("steam_runprocess", steam_logs.join("runprocess_log.txt")));

    let eac_identity = game_dir.and_then(eac_identity_for_game);
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
                if id == "eac_launcher" && !eac_log_matches_identity(path, eac_identity.as_ref()) {
                    continue;
                }
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

fn eac_identity_for_game(game_dir: &Path) -> Option<EacIdentity> {
    for entry in WalkDir::new(game_dir).max_depth(MODULE_ASSET_MAX_DEPTH).into_iter().filter_map(Result::ok) {
        if !entry.file_type().is_file() {
            continue;
        }
        let path = entry.path();
        let path_lc = path.to_string_lossy().to_ascii_lowercase();
        let name_lc = path.file_name().and_then(|v| v.to_str()).unwrap_or("").to_ascii_lowercase();
        if !path_lc.contains("easyanticheat") || name_lc != "settings.json" {
            continue;
        }
        let Some(text) = read_recent_text_limited(path) else {
            continue;
        };
        let identity = EacIdentity {
            product_id: extract_loose_json_string(&text, "productid"),
            deployment_id: extract_loose_json_string(&text, "deploymentid"),
        };
        if identity.product_id.is_some() || identity.deployment_id.is_some() {
            return Some(identity);
        }
    }
    None
}

fn eac_log_matches_identity(path: &Path, identity: Option<&EacIdentity>) -> bool {
    let Some(identity) = identity else {
        return true;
    };
    let path_lc = path.to_string_lossy().to_ascii_lowercase();
    let product_ok = identity.product_id.as_ref().map(|id| path_lc.contains(&id.to_ascii_lowercase())).unwrap_or(true);
    let deployment_ok =
        identity.deployment_id.as_ref().map(|id| path_lc.contains(&id.to_ascii_lowercase())).unwrap_or(true);
    product_ok && deployment_ok
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
            let path_lc = path.to_string_lossy().to_ascii_lowercase();
            if !path_lc.contains("easyanticheat") && !path_lc.contains("battleye") && !path_lc.contains("beclient") {
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

fn runtime_probe_checks(home: &Path) -> Value {
    let wine_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine_bin = wine_root.join("bin").join("wine");
    let wine64_bin = wine_root.join("bin").join("wine64");
    let wine_unix = wine_root.join("lib").join("wine").join("x86_64-unix");
    let dxmt_unix = wine_root.join("lib").join("dxmt").join("x86_64-unix");

    json!({
        "wineRoot": path_check(&wine_root),
        "wineBinary": path_check(&wine_bin),
        "wine64Binary": path_check(&wine64_bin),
        "wineUnixLibDir": path_check(&wine_unix),
        "dxmtUnixLibDir": path_check(&dxmt_unix),
        "mscompatdb": mscompatdb_probe(home),
        "expectedDyldBoundary": "macos_dylib",
    })
}

fn mscompatdb_probe(home: &Path) -> Value {
    let wine_root = home.join(".metalsharp").join("runtime").join("wine");
    let mscompatdb_path = wine_root.join("lib").join("wine").join("x86_64-unix").join("mscompatdb.so");
    let mscompatdb_dylib_path = wine_root.join("lib").join("wine").join("x86_64-unix").join("mscompatdb.dylib");
    let ntdll_path = wine_root.join("lib").join("wine").join("x86_64-unix").join("ntdll.so");
    let rules_path = wine_root.join("etc").join("mscompatdb_rules.toml");
    let nested_rules_path = wine_root.join("etc").join("etc").join("mscompatdb_rules.toml");
    let mscompatdb_bytes = read_probe_bytes(&mscompatdb_path).unwrap_or_default();
    let ntdll_symbols = inspect_ntdll_symbols(&ntdll_path);
    let trace = mscompatdb_trace_signals(home);
    let dyld_alias = dyld_alias_probe(&mscompatdb_path, &mscompatdb_dylib_path);
    let rules_present = rules_path.exists() || nested_rules_path.exists();
    let expects_ke_table = ascii_bytes_contains(&mscompatdb_bytes, b"KeServiceDescriptorTable");
    let expects_legacy_rule_api = ascii_bytes_contains(&mscompatdb_bytes, b"prepend")
        || ascii_bytes_contains(&mscompatdb_bytes, b"load_rules OK");
    let exports_ke_table =
        ntdll_symbols.get("exportsKeServiceDescriptorTable").and_then(|v| v.as_bool()).unwrap_or(false);
    let has_unresolved_ke_error = trace.get("hasKeTableResolutionFailure").and_then(|v| v.as_bool()).unwrap_or(false);
    let has_null_rule_api = trace.get("hasNullRuleApi").and_then(|v| v.as_bool()).unwrap_or(false);
    let hooked = mscompatdb_path.exists()
        && rules_present
        && (!expects_ke_table || exports_ke_table)
        && !has_unresolved_ke_error
        && !has_null_rule_api;
    let status = mscompatdb_status(
        mscompatdb_path.exists(),
        rules_present,
        expects_ke_table,
        exports_ke_table,
        has_unresolved_ke_error,
        has_null_rule_api,
    );

    json!({
        "status": status,
        "hooked": hooked,
        "mscompatdb": {
            "path": mscompatdb_path.to_string_lossy(),
            "present": mscompatdb_path.exists(),
            "format": read_binary_format(&mscompatdb_path),
            "expectsKeServiceDescriptorTable": expects_ke_table,
            "expectsLegacyRuleApi": expects_legacy_rule_api,
            "hasTraceStrings": ascii_bytes_contains(&mscompatdb_bytes, b"mscompatdb:trace"),
        },
        "dyldAlias": dyld_alias,
        "ntdll": {
            "path": ntdll_path.to_string_lossy(),
            "present": ntdll_path.exists(),
            "format": read_binary_format(&ntdll_path),
        },
        "ntdllSymbols": ntdll_symbols,
        "rules": {
            "primary": path_check(&rules_path),
            "nestedFallback": path_check(&nested_rules_path),
            "present": rules_present,
        },
        "trace": trace,
    })
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

fn summarize_eac(appid: u32, artifacts: &[Value]) -> EacSummary {
    let mut summary = EacSummary::default();
    for artifact in artifacts {
        let id = artifact.get("id").and_then(|v| v.as_str()).unwrap_or("");
        if !id.starts_with("eac_") && id != "steam_runprocess" && id != "game_anticheat_log" {
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
    let marker = format!("[AppID {}]", appid);
    if !line.contains(&marker) {
        return;
    }
    let lower = line.to_ascii_lowercase();
    if !lower.contains("easyanticheat") || !lower.contains("setup") {
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
        let pid = line.split("adding PID ").nth(1).and_then(first_i64);
        summary.tracked_pid = pid;
        if let Some(path) = line.split("tracked process ").nth(1).map(normalize_steam_command) {
            if let Some(pid) = pid {
                summary.tracked_processes.insert(pid, path.clone());
            }
            if path.to_ascii_lowercase().contains("start_protected_game") {
                summary.protected_launcher_path = Some(path);
            } else if path.to_ascii_lowercase().ends_with(".exe") || path.to_ascii_lowercase().contains(".exe\"") {
                summary.direct_game_path = Some(path);
            }
        }
    } else if line.contains("no longer tracking PID") {
        let pid = line.split("no longer tracking PID ").nth(1).and_then(first_i64);
        let exit_code = line.split("exit code ").nth(1).and_then(first_i64);
        summary.tracked_exit_code = exit_code;
        let path = pid.and_then(|pid| summary.tracked_processes.get(&pid));
        if path.map(|path| !path.to_ascii_lowercase().contains("start_protected_game")).unwrap_or(false) {
            summary.direct_game_exit_code = exit_code;
        }
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
    if steam.direct_game_exit_code == Some(-1073741819) {
        return "direct_game_access_violation".to_string();
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

fn probe_status(eac: &EacSummary, module_assets: &[Value]) -> String {
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
        "direct_game_access_violation" => format!(
            "The direct game executable crashed with {}.",
            steam.direct_game_exit_code.map(describe_exit_code).unwrap_or_else(|| "an unknown exit code".to_string())
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
        "direct_game_access_violation" => vec![
            "Capture a focused M12 D3D12/DXMT crash log for the direct game executable.",
            "Compare loaded d3d12/dxgi exports and MoltenVK initialization against the deployed game DLLs.",
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

fn describe_exit_code(code: i64) -> String {
    match code {
        -1073741819 => "0xC0000005 access violation".to_string(),
        -2147483392 => "0x80000100 Wine unimplemented stub".to_string(),
        206 => "206 protected launcher failure".to_string(),
        other if other < 0 => format!("0x{:08X}", other as i32 as u32),
        other => other.to_string(),
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

fn path_check(path: &Path) -> Value {
    let metadata = fs::metadata(path).ok();
    json!({
        "path": path.to_string_lossy(),
        "exists": metadata.is_some(),
        "isDir": metadata.as_ref().map(|m| m.is_dir()).unwrap_or(false),
    })
}

fn mscompatdb_status(
    mscompatdb_present: bool,
    rules_present: bool,
    expects_ke_table: bool,
    exports_ke_table: bool,
    has_unresolved_ke_error: bool,
    has_null_rule_api: bool,
) -> &'static str {
    if !mscompatdb_present {
        return "missing_mscompatdb";
    }
    if !rules_present {
        return "missing_rules";
    }
    if has_unresolved_ke_error || (expects_ke_table && !exports_ke_table) {
        return "present_but_ke_table_unresolved";
    }
    if has_null_rule_api {
        return "present_but_rule_api_unresolved";
    }
    "hook_surface_ready"
}

fn dyld_alias_probe(so_path: &Path, dylib_path: &Path) -> Value {
    let so_meta = fs::metadata(so_path).ok();
    let dylib_meta = fs::metadata(dylib_path).ok();
    let source_is_macho = read_binary_format(so_path) == "mach_o";
    let dylib_is_macho = read_binary_format(dylib_path) == "mach_o";
    let alias_current = so_meta.as_ref().map(|meta| meta.len()) == dylib_meta.as_ref().map(|meta| meta.len())
        && so_meta
            .as_ref()
            .and_then(|meta| meta.modified().ok())
            .zip(dylib_meta.as_ref().and_then(|meta| meta.modified().ok()))
            .map(|(source, alias)| alias >= source)
            .unwrap_or(false);
    let signature = code_signature_probe(dylib_path);
    let signed = signature.get("valid").and_then(|v| v.as_bool()).unwrap_or(false);

    json!({
        "status": dylib_alias_status(so_path.exists(), source_is_macho, dylib_path.exists(), dylib_is_macho, signed),
        "source": path_check(so_path),
        "dylib": path_check(dylib_path),
        "sourceFormat": read_binary_format(so_path),
        "dylibFormat": read_binary_format(dylib_path),
        "aliasCurrent": alias_current,
        "signature": signature,
    })
}

fn dylib_alias_status(
    source_present: bool,
    source_is_macho: bool,
    dylib_present: bool,
    dylib_is_macho: bool,
    signed: bool,
) -> &'static str {
    if !source_present {
        return "missing_source_so";
    }
    if !source_is_macho {
        return "source_not_macho";
    }
    if !dylib_present {
        return "missing_dylib_alias";
    }
    if !dylib_is_macho {
        return "dylib_alias_not_macho";
    }
    if !signed {
        return "dylib_alias_unsigned";
    }
    "dylib_alias_ready"
}

fn prepare_mscompatdb_dylib(home: &Path, force: bool) -> Value {
    let wine_root = home.join(".metalsharp").join("runtime").join("wine");
    let unix_dir = wine_root.join("lib").join("wine").join("x86_64-unix");
    let so_path = unix_dir.join("mscompatdb.so");
    let dylib_path = unix_dir.join("mscompatdb.dylib");
    let mut steps = Vec::new();

    if !so_path.exists() {
        return json!({
            "ok": false,
            "status": "missing_source_so",
            "source": so_path.to_string_lossy(),
            "dylib": dylib_path.to_string_lossy(),
            "steps": steps,
        });
    }
    if read_binary_format(&so_path) != "mach_o" {
        return json!({
            "ok": false,
            "status": "source_not_macho",
            "source": so_path.to_string_lossy(),
            "sourceFormat": read_binary_format(&so_path),
            "steps": steps,
        });
    }

    let should_copy = force || dylib_needs_refresh(&so_path, &dylib_path);
    if should_copy {
        match fs::copy(&so_path, &dylib_path) {
            Ok(bytes) => steps.push(json!({"step": "copy_dylib_alias", "ok": true, "bytes": bytes})),
            Err(err) => {
                steps.push(json!({"step": "copy_dylib_alias", "ok": false, "error": err.to_string()}));
                return json!({
                    "ok": false,
                    "status": "copy_failed",
                    "source": so_path.to_string_lossy(),
                    "dylib": dylib_path.to_string_lossy(),
                    "steps": steps,
                });
            },
        }
    } else {
        steps.push(json!({"step": "copy_dylib_alias", "ok": true, "skipped": true, "reason": "alias_current"}));
    }

    steps.push(command_result_json(
        "clear_quarantine",
        Command::new("xattr").arg("-d").arg("com.apple.quarantine").arg(&dylib_path),
        true,
    ));
    steps.push(command_result_json(
        "set_install_name",
        Command::new("install_name_tool").arg("-id").arg("@rpath/mscompatdb.dylib").arg(&dylib_path),
        true,
    ));
    steps.push(command_result_json(
        "ad_hoc_codesign",
        Command::new("codesign").arg("--force").arg("--sign").arg("-").arg(&dylib_path),
        false,
    ));
    let verify = code_signature_probe(&dylib_path);
    let signed = verify.get("valid").and_then(|v| v.as_bool()).unwrap_or(false);
    let status =
        dylib_alias_status(true, true, dylib_path.exists(), read_binary_format(&dylib_path) == "mach_o", signed);

    json!({
        "ok": signed,
        "status": status,
        "source": so_path.to_string_lossy(),
        "dylib": dylib_path.to_string_lossy(),
        "steps": steps,
        "dyldAlias": dyld_alias_probe(&so_path, &dylib_path),
    })
}

fn dylib_needs_refresh(source: &Path, dylib: &Path) -> bool {
    let Some(source_meta) = fs::metadata(source).ok() else {
        return false;
    };
    let Some(dylib_meta) = fs::metadata(dylib).ok() else {
        return true;
    };
    if source_meta.len() != dylib_meta.len() {
        return true;
    }
    let source_modified = source_meta.modified().ok();
    let dylib_modified = dylib_meta.modified().ok();
    source_modified.zip(dylib_modified).map(|(source, dylib)| source > dylib).unwrap_or(false)
}

fn code_signature_probe(path: &Path) -> Value {
    if !path.exists() {
        return json!({"valid": false, "present": false});
    }
    let verify =
        Command::new("codesign").arg("--verify").arg("--deep").arg("--strict").arg("--verbose=2").arg(path).output();
    let details = Command::new("codesign").arg("-dv").arg("--verbose=4").arg(path).output();
    let valid = verify.as_ref().map(|output| output.status.success()).unwrap_or(false);
    json!({
        "present": true,
        "valid": valid,
        "verify": command_output_json(verify),
        "details": command_output_json(details),
    })
}

fn command_result_json(step: &str, command: &mut Command, allow_failure: bool) -> Value {
    let output = command.output();
    let ok = output.as_ref().map(|output| output.status.success()).unwrap_or(false);
    json!({
        "step": step,
        "ok": ok || allow_failure,
        "allowedFailure": allow_failure,
        "commandOk": ok,
        "output": command_output_json(output),
    })
}

fn command_output_json(output: std::io::Result<std::process::Output>) -> Value {
    match output {
        Ok(output) => json!({
            "status": output.status.code(),
            "success": output.status.success(),
            "stdout": String::from_utf8_lossy(&output.stdout).trim(),
            "stderr": String::from_utf8_lossy(&output.stderr).trim(),
        }),
        Err(err) => json!({
            "success": false,
            "error": err.to_string(),
        }),
    }
}

fn mscompatdb_probe_summary(status: &str) -> &'static str {
    match status {
        "missing_mscompatdb" => "mscompatdb.so is missing from the Wine runtime; protected launch cannot use the bridge shim.",
        "missing_rules" => "mscompatdb is installed, but its rule file is missing.",
        "present_but_ke_table_unresolved" => {
            "mscompatdb is installed, but the current Wine ntdll exposes KeServiceDescriptorTable as a private/local symbol, so the shim cannot hook the syscall table."
        },
        "present_but_rule_api_unresolved" => {
            "mscompatdb is installed, but trace logs show its legacy rule API pointers resolved to null."
        },
        "hook_surface_ready" => "mscompatdb, rules, and the Wine ntdll hook surface look ready for a protected-launch test.",
        _ => "mscompatdb probe returned an unknown state.",
    }
}

fn mscompatdb_probe_next_actions(status: &str) -> Vec<&'static str> {
    match status {
        "present_but_ke_table_unresolved" => vec![
            "Patch the Wine ntdll build to expose an intentional MetalSharp hook/accessor instead of relying on private Mach-O symbols.",
            "Rebuild mscompatdb from source against that explicit hook point.",
            "Re-run protected Steam launch and confirm mscompatdb logs report a patched syscall hook before interpreting EAC/BattlEye errors.",
        ],
        "present_but_rule_api_unresolved" => vec![
            "Recover or recreate the mscompatdb source so rule loading targets the current Wine runtime API.",
            "Add an init-time failure code when rules cannot attach instead of silently continuing.",
        ],
        "missing_mscompatdb" | "missing_rules" => vec![
            "Restore the missing runtime artifact from the MetalSharp bundle before testing protected launch.",
        ],
        _ => vec![
            "Run a protected Steam launch, then refresh /steam/mscompatdb-probe and /steam/anticheat-evidence.",
        ],
    }
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

fn read_probe_bytes(path: &Path) -> Option<Vec<u8>> {
    let file = File::open(path).ok()?;
    let mut bytes = Vec::new();
    let mut limited = file.take(MAX_BINARY_PROBE_BYTES);
    limited.read_to_end(&mut bytes).ok()?;
    Some(bytes)
}

fn ascii_bytes_contains(haystack: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    haystack.windows(needle.len()).any(|window| window == needle)
}

fn inspect_ntdll_symbols(path: &Path) -> Value {
    let output = Command::new("nm").arg("-a").arg(path).output().ok();
    let text = output
        .as_ref()
        .filter(|output| output.status.success())
        .map(|output| String::from_utf8_lossy(&output.stdout).into_owned())
        .unwrap_or_default();
    let parsed = parse_ntdll_nm_symbols(&text);
    json!({
        "nmAvailable": output.is_some(),
        "hasKeServiceDescriptorTable": parsed.has_ke_service_descriptor_table,
        "exportsKeServiceDescriptorTable": parsed.exports_ke_service_descriptor_table,
        "keServiceDescriptorTableIsLocal": parsed.ke_service_descriptor_table_is_local,
        "hasWineSyscallDispatcher": parsed.has_wine_syscall_dispatcher,
        "hasKeAddSystemServiceTable": parsed.has_ke_add_system_service_table,
        "hasMetalSharpHookContract": parsed.has_metalsharp_hook_contract,
        "hasMetalSharpHookContractVersion": parsed.has_metalsharp_hook_contract_version,
        "hookContractReady": parsed.has_metalsharp_hook_contract && parsed.has_metalsharp_hook_contract_version,
    })
}

#[derive(Debug, Default)]
struct NtdllSymbolProbe {
    has_ke_service_descriptor_table: bool,
    exports_ke_service_descriptor_table: bool,
    ke_service_descriptor_table_is_local: bool,
    has_wine_syscall_dispatcher: bool,
    has_ke_add_system_service_table: bool,
    has_metalsharp_hook_contract: bool,
    has_metalsharp_hook_contract_version: bool,
}

fn parse_ntdll_nm_symbols(text: &str) -> NtdllSymbolProbe {
    let mut probe = NtdllSymbolProbe::default();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let raw_symbol = line.split_whitespace().last().unwrap_or_default();
        let symbol = raw_symbol.trim_start_matches('_');
        let kind = nm_symbol_kind(line);
        if symbol == "KeServiceDescriptorTable" {
            probe.has_ke_service_descriptor_table = true;
            if kind.map(|kind| kind.is_ascii_uppercase() && kind != 'U').unwrap_or(false) {
                probe.exports_ke_service_descriptor_table = true;
            }
            if kind.map(|kind| kind.is_ascii_lowercase()).unwrap_or(false) {
                probe.ke_service_descriptor_table_is_local = true;
            }
        } else if raw_symbol == "___wine_syscall_dispatcher" || symbol == "wine_syscall_dispatcher" {
            probe.has_wine_syscall_dispatcher = true;
        } else if symbol == "KeAddSystemServiceTable" {
            probe.has_ke_add_system_service_table = true;
        } else if symbol == "MetalSharpGetMscompatdbHookContract" {
            probe.has_metalsharp_hook_contract = true;
        } else if symbol == "MetalSharpGetMscompatdbHookContractVersion" {
            probe.has_metalsharp_hook_contract_version = true;
        }
    }
    probe
}

fn nm_symbol_kind(line: &str) -> Option<char> {
    line.split_whitespace().find_map(|part| {
        let mut chars = part.chars();
        let ch = chars.next()?;
        if chars.next().is_none() && ch.is_ascii_alphabetic() {
            Some(ch)
        } else {
            None
        }
    })
}

fn mscompatdb_trace_signals(home: &Path) -> Value {
    let paths = vec![home.join(".metalsharp").join("mscompatdb_trace.log"), PathBuf::from("/tmp/mscompatdb_debug.log")];
    let mut logs = Vec::new();
    let mut combined = String::new();
    for path in paths {
        let Some(text) = read_recent_text_limited(&path) else {
            logs.push(json!({
                "path": path.to_string_lossy(),
                "exists": path.exists(),
            }));
            continue;
        };
        combined.push_str(&text);
        combined.push('\n');
        logs.push(json!({
            "path": path.to_string_lossy(),
            "exists": true,
            "tail": tail_lines(&text, 24),
        }));
    }
    let lower = combined.to_ascii_lowercase();
    json!({
        "logs": logs,
        "hasKeTableResolutionFailure": lower.contains("couldn't find keservicedescriptortable")
            || lower.contains("could not find keservicedescriptortable"),
        "hasNullRuleApi": lower.contains("p_add=0x0") || lower.contains("p_prepend=0x0"),
        "hasPatchedNtCreateUserProcess": lower.contains("patched ntcreateuserprocess"),
        "hasHookInvocation": lower.contains("hook_ntcreateuserprocess called"),
    })
}

fn tail_lines(text: &str, max_lines: usize) -> Vec<String> {
    let lines: Vec<&str> = text.lines().collect();
    let start = lines.len().saturating_sub(max_lines);
    lines[start..].iter().map(|line| line.trim_end_matches('\r').to_string()).collect()
}

fn collect_probe_diagnostic_dirs(root: &Path, tokens: &[String]) -> Vec<PathBuf> {
    let mut dirs = fs::read_dir(root)
        .ok()
        .into_iter()
        .flat_map(|entries| entries.flatten())
        .map(|entry| entry.path())
        .filter(|path| path.is_dir())
        .filter(|path| {
            if tokens.is_empty() {
                return true;
            }
            let name = path.file_name().map(|name| name.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
            tokens.iter().any(|token| name.contains(token))
        })
        .collect::<Vec<_>>();
    dirs.sort_by_key(|path| fs::metadata(path).and_then(|meta| meta.modified()).unwrap_or(UNIX_EPOCH));
    dirs.reverse();
    dirs.truncate(6);
    dirs
}

fn summarize_wine_syscall_dir(dir: &Path) -> Value {
    let mut files = Vec::new();
    let mut combined = String::new();
    for path in probe_files(dir) {
        let Some(text) = read_recent_text_limited(&path) else {
            continue;
        };
        let name = path.file_name().map(|name| name.to_string_lossy().to_string()).unwrap_or_default();
        combined.push_str(&text);
        combined.push('\n');
        files.push(json!({
            "name": name,
            "path": path.to_string_lossy(),
            "bytesRead": text.len(),
            "tail": tail_lines(&text, 24),
        }));
    }

    let modified = fs::metadata(dir)
        .and_then(|meta| meta.modified())
        .ok()
        .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
        .map(|duration| duration.as_secs())
        .unwrap_or_default();
    let lower = combined.to_ascii_lowercase();
    let create_swapchain = count_any(&lower, &["createswapchain", "create swapchain"]);
    let present = count_any(&lower, &[" present(", "present1(", "::present"]);
    let d3d12_device = count_any(&lower, &["d3d12createdevice success", "d3d12 device created"]);

    json!({
        "path": dir.to_string_lossy(),
        "modifiedUnix": modified,
        "files": files,
        "signals": {
            "coreLoggingThread": lower.contains("core.logging.backgroundstrategy"),
            "cheatDetectionThread": lower.contains("cscheatdetectiontitlemodule"),
            "wineSyscallDispatch": lower.contains("__wine_syscall_dispatcher"),
            "ntWaitStacks": count_any(&lower, &["waitforsingleobject", "waitformultipleobjects"]),
            "fileLoggingStack": count_any(&lower, &["_fsopen", "_wfsopen", "getfiletype"]),
            "d3d12DeviceCreated": d3d12_device > 0,
            "d3d12WaitStacks": lower.contains(" in d3d12 ") || lower.contains(" d3d12 (+"),
            "createSwapchainCount": create_swapchain,
            "presentCount": present,
            "preSwapchain": d3d12_device > 0 && create_swapchain == 0 && present == 0,
            "protectedLauncherTracked": lower.contains("start_protected_game.exe"),
            "moduleMappingStarted": lower.contains("starting wine module mapping"),
            "moduleMappingFailed": lower.contains("failed to map the anti-cheat module"),
            "protectedLauncher206": lower.contains("launcher finished with: 206") || lower.contains("exit code 206"),
        }
    })
}

fn probe_files(dir: &Path) -> Vec<PathBuf> {
    let wanted = ["winedbg.txt", "process.txt", "run.log", "sample.txt", "dxmt_d3d12_trace.log", "dxmt_dxgi_trace.log"];
    let mut files = Vec::new();
    for name in wanted {
        let path = dir.join(name);
        if path.exists() {
            files.push(path);
        }
    }
    if let Ok(entries) = fs::read_dir(dir) {
        for path in entries.flatten().map(|entry| entry.path()) {
            let Some(name) = path.file_name().map(|name| name.to_string_lossy().to_ascii_lowercase()) else {
                continue;
            };
            let looks_relevant = name.contains("winedbg")
                || name.contains("sample")
                || name.contains("dxmt")
                || name.contains("eac")
                || name.contains("anticheat");
            if looks_relevant && !files.iter().any(|existing| existing == &path) {
                files.push(path);
            }
        }
    }
    files.truncate(10);
    files
}

fn aggregate_wine_syscall_reports(reports: &[Value]) -> Value {
    let mut reports_with_core_logging = 0u64;
    let mut reports_with_wine_syscall = 0u64;
    let mut reports_with_file_logging = 0u64;
    let mut reports_with_cheat_detection = 0u64;
    let mut reports_with_preswapchain = 0u64;
    let mut reports_with_module_mapping_failed = 0u64;
    let mut create_swapchain_count = 0u64;
    let mut present_count = 0u64;
    for report in reports {
        let signals = report.get("signals").and_then(|v| v.as_object());
        if signal_bool(signals, "coreLoggingThread") {
            reports_with_core_logging += 1;
        }
        if signal_bool(signals, "wineSyscallDispatch") || signal_u64(signals, "ntWaitStacks") > 0 {
            reports_with_wine_syscall += 1;
        }
        if signal_u64(signals, "fileLoggingStack") > 0 {
            reports_with_file_logging += 1;
        }
        if signal_bool(signals, "cheatDetectionThread") {
            reports_with_cheat_detection += 1;
        }
        if signal_bool(signals, "preSwapchain") {
            reports_with_preswapchain += 1;
        }
        if signal_bool(signals, "moduleMappingFailed") || signal_bool(signals, "protectedLauncher206") {
            reports_with_module_mapping_failed += 1;
        }
        create_swapchain_count += signal_u64(signals, "createSwapchainCount");
        present_count += signal_u64(signals, "presentCount");
    }

    json!({
        "reportsScanned": reports.len(),
        "reportsWithCoreLogging": reports_with_core_logging,
        "reportsWithWineSyscallOrWait": reports_with_wine_syscall,
        "reportsWithFileLoggingStack": reports_with_file_logging,
        "reportsWithCheatDetectionThread": reports_with_cheat_detection,
        "reportsWithPreSwapchainD3d12": reports_with_preswapchain,
        "reportsWithModuleMappingFailed": reports_with_module_mapping_failed,
        "createSwapchainCount": create_swapchain_count,
        "presentCount": present_count,
    })
}

fn wine_syscall_probe_status(aggregate: &Value) -> String {
    if aggregate.get("reportsScanned").and_then(|v| v.as_u64()).unwrap_or_default() == 0 {
        return "no_diagnostics_found".to_string();
    }
    if aggregate.get("reportsWithModuleMappingFailed").and_then(|v| v.as_u64()).unwrap_or_default() > 0 {
        return "protected_module_mapping_failed".to_string();
    }
    if aggregate.get("reportsWithCoreLogging").and_then(|v| v.as_u64()).unwrap_or_default() > 0
        && aggregate.get("reportsWithWineSyscallOrWait").and_then(|v| v.as_u64()).unwrap_or_default() > 0
    {
        return "wine_syscall_logging_stall".to_string();
    }
    if aggregate.get("reportsWithPreSwapchainD3d12").and_then(|v| v.as_u64()).unwrap_or_default() > 0 {
        return "pre_swapchain_d3d12_stall".to_string();
    }
    "diagnostics_collected".to_string()
}

fn wine_syscall_probe_summary(status: &str, aggregate: &Value) -> String {
    match status {
        "protected_module_mapping_failed" => {
            "Protected launch reached anti-cheat module mapping and failed; focus on the EAC/BattlEye loader boundary before direct-game rendering.".to_string()
        },
        "wine_syscall_logging_stall" => {
            "Diagnostics show a game logging thread parked in Wine syscall/wait behavior before the game reaches a frame.".to_string()
        },
        "pre_swapchain_d3d12_stall" => {
            "Diagnostics show D3D12 device creation without swapchain or Present calls, so the white screen is still before normal frame presentation.".to_string()
        },
        "no_diagnostics_found" => {
            "No matching diagnostic folders were found. Launch the game once, then run this probe again.".to_string()
        },
        _ => format!(
            "Scanned {} diagnostic folder(s); no single blocking signature dominated.",
            aggregate.get("reportsScanned").and_then(|v| v.as_u64()).unwrap_or_default()
        ),
    }
}

fn wine_syscall_probe_next_actions(status: &str) -> Vec<&'static str> {
    match status {
        "protected_module_mapping_failed" => vec![
            "Use the protected Steam handoff path so start_protected_game.exe is the authoritative launch evidence.",
            "Compare the failed module target against Proton's EAC-enabled Wine loader path.",
        ],
        "wine_syscall_logging_stall" => vec![
            "Capture a gated winedbg stack after DXGI tracing is disabled.",
            "Inspect Wine file/logging syscalls around the hot logging thread before changing D3D12 again.",
        ],
        "pre_swapchain_d3d12_stall" => vec![
            "Keep D3D12 tracing gated and inspect the last wait stack before swapchain creation.",
            "Verify whether the same app advances when launched through protected Steam instead of direct exe.",
        ],
        _ => vec!["Run a protected Steam launch and refresh /steam/anticheat-evidence plus this syscall probe."],
    }
}

fn signal_bool(signals: Option<&Map<String, Value>>, key: &str) -> bool {
    signals.and_then(|signals| signals.get(key)).and_then(|v| v.as_bool()).unwrap_or(false)
}

fn signal_u64(signals: Option<&Map<String, Value>>, key: &str) -> u64 {
    signals.and_then(|signals| signals.get(key)).and_then(|v| v.as_u64()).unwrap_or_default()
}

fn count_any(text: &str, needles: &[&str]) -> u64 {
    needles.iter().map(|needle| text.matches(needle).count() as u64).sum()
}

fn normalize_probe_token(token: &str) -> String {
    token.chars().filter(|ch| ch.is_ascii_alphanumeric()).collect::<String>().to_ascii_lowercase()
}

fn path_probe_tokens(path: &Path) -> Vec<String> {
    path.components()
        .filter_map(|component| match component {
            Component::Normal(value) => Some(value.to_string_lossy()),
            _ => None,
        })
        .flat_map(|value| value.split(|ch: char| !ch.is_ascii_alphanumeric()).map(str::to_string).collect::<Vec<_>>())
        .map(|token| normalize_probe_token(&token))
        .filter(|token| token.len() >= 4)
        .collect()
}

fn extract_loose_json_string(text: &str, key: &str) -> Option<String> {
    let quoted_key = format!("\"{}\"", key);
    let rest = text.split(&quoted_key).nth(1)?;
    let rest = rest.split(':').nth(1)?.trim_start();
    let value = rest.strip_prefix('"')?.split('"').next()?;
    Some(value.trim().to_string())
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
    fn wine_syscall_probe_promotes_logging_stall_signature() {
        let reports = vec![json!({
            "signals": {
                "coreLoggingThread": true,
                "wineSyscallDispatch": true,
                "ntWaitStacks": 1,
                "fileLoggingStack": 1,
                "cheatDetectionThread": true,
                "preSwapchain": true,
                "createSwapchainCount": 0,
                "presentCount": 0,
                "moduleMappingFailed": false,
                "protectedLauncher206": false
            }
        })];

        let aggregate = aggregate_wine_syscall_reports(&reports);

        assert_eq!(aggregate.get("reportsWithCoreLogging").and_then(|v| v.as_u64()), Some(1));
        assert_eq!(aggregate.get("reportsWithWineSyscallOrWait").and_then(|v| v.as_u64()), Some(1));
        assert_eq!(wine_syscall_probe_status(&aggregate), "wine_syscall_logging_stall");
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
    fn parses_direct_game_access_violation_separately_from_protected_launcher() {
        let mut summary = SteamSummary::default();
        parse_gameprocess_line(
            1245620,
            "[2026-05-20 11:23:00] AppID 1245620 adding PID 1572 as a tracked process \"\"Z:\\Volumes\\AverySSD\\SteamLibrary\\steamapps\\common\\ELDEN RING\\Game\\eldenring.exe\"\"",
            &mut summary,
        );
        parse_gameprocess_line(
            1245620,
            "[2026-05-20 11:23:16] AppID 1245620 no longer tracking PID 1572, exit code -1073741819",
            &mut summary,
        );
        parse_gameprocess_line(
            1245620,
            "[2026-05-20 11:24:03] AppID 1245620 adding PID 1856 as a tracked process \"\"Z:\\Volumes\\AverySSD\\SteamLibrary\\steamapps\\common\\ELDEN RING\\Game\\start_protected_game.exe\"\"",
            &mut summary,
        );
        parse_gameprocess_line(
            1245620,
            "[2026-05-20 11:24:08] AppID 1245620 no longer tracking PID 1856, exit code 206",
            &mut summary,
        );

        assert!(summary.direct_game_path.as_deref().unwrap_or_default().contains("eldenring.exe"));
        assert_eq!(summary.direct_game_exit_code, Some(-1073741819));
        assert!(summary.protected_launcher_path.as_deref().unwrap_or_default().contains("start_protected_game.exe"));
        assert_eq!(summary.tracked_exit_code, Some(206));
    }

    #[test]
    fn eac_roaming_logs_are_filtered_by_game_identity() {
        let identity = EacIdentity {
            product_id: Some("773d3a68f76f4b2ebebc5b4127bbad3e".to_string()),
            deployment_id: Some("d2842e93d53b4c0c98a8f963ebb4c222".to_string()),
        };
        let elden = Path::new(
            "/prefix/drive_c/users/u/AppData/Roaming/EasyAntiCheat/773d3a68f76f4b2ebebc5b4127bbad3e/d2842e93d53b4c0c98a8f963ebb4c222/anticheatlauncher.log",
        );
        let rubicon = Path::new(
            "/prefix/drive_c/users/u/AppData/Roaming/EasyAntiCheat/789399aada914e66bb3c3facebc5d709/b978a2afd2254108bbb39201d0a24a98/anticheatlauncher.log",
        );

        assert!(eac_log_matches_identity(elden, Some(&identity)));
        assert!(!eac_log_matches_identity(rubicon, Some(&identity)));
    }

    #[test]
    fn parses_eac_settings_with_trailing_comma() {
        let text = r#"{
            "productid" : "773d3a68f76f4b2ebebc5b4127bbad3e",
            "deploymentid" : "d2842e93d53b4c0c98a8f963ebb4c222",
        }"#;

        assert_eq!(extract_loose_json_string(text, "productid").as_deref(), Some("773d3a68f76f4b2ebebc5b4127bbad3e"));
        assert_eq!(
            extract_loose_json_string(text, "deploymentid").as_deref(),
            Some("d2842e93d53b4c0c98a8f963ebb4c222")
        );
    }

    #[test]
    fn eac_setup_summary_is_filtered_by_appid() {
        let artifacts = vec![json!({
            "id": "steam_runprocess",
            "tail": [
                "05/20/26 01:03:10 [AppID 1888160] Exit Code (7) : \"Z:\\\\Rubicon\\\\EasyAntiCheat_EOS_Setup.exe\" install rubicon GLE 0",
                "05/20/26 10:56:58 [AppID 1245620] Exit Code (0) : \"Z:\\\\Elden\\\\EasyAntiCheat_EOS_Setup.exe\" install elden GLE 0"
            ]
        })];

        assert_eq!(summarize_eac(1245620, &artifacts).setup_exit_code, Some(0));
        assert_eq!(summarize_eac(1888160, &artifacts).setup_exit_code, Some(7));
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
        assert_eq!(probe_status(&eac, &[]), "linux_module_on_darwin_boundary");
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
    fn parses_ntdll_local_ke_table_from_nm_output() {
        let text = "\
0000000000097710 d _KeServiceDescriptorTable
0000000000040ac0 t ___wine_syscall_dispatcher
000000000005d110 T _KeAddSystemServiceTable
";

        let probe = parse_ntdll_nm_symbols(text);

        assert!(probe.has_ke_service_descriptor_table);
        assert!(probe.ke_service_descriptor_table_is_local);
        assert!(!probe.exports_ke_service_descriptor_table);
        assert!(probe.has_wine_syscall_dispatcher);
        assert!(probe.has_ke_add_system_service_table);
        assert!(!probe.has_metalsharp_hook_contract);
    }

    #[test]
    fn parses_ntdll_exported_ke_table_from_nm_output() {
        let text = "0000000000097710 D _KeServiceDescriptorTable";

        let probe = parse_ntdll_nm_symbols(text);

        assert!(probe.has_ke_service_descriptor_table);
        assert!(probe.exports_ke_service_descriptor_table);
        assert!(!probe.ke_service_descriptor_table_is_local);
    }

    #[test]
    fn parses_explicit_metalsharp_mscompatdb_hook_contract() {
        let text = "\
0000000000061000 T _MetalSharpGetMscompatdbHookContract
0000000000061010 T _MetalSharpGetMscompatdbHookContractVersion
";

        let probe = parse_ntdll_nm_symbols(text);

        assert!(probe.has_metalsharp_hook_contract);
        assert!(probe.has_metalsharp_hook_contract_version);
    }

    #[test]
    fn mscompatdb_status_flags_present_but_private_ke_table() {
        assert_eq!(mscompatdb_status(true, true, true, false, false, false), "present_but_ke_table_unresolved");
        assert_eq!(mscompatdb_status(true, true, false, false, false, true), "present_but_rule_api_unresolved");
        assert_eq!(mscompatdb_status(true, true, false, false, false, false), "hook_surface_ready");
    }

    #[test]
    fn dylib_alias_status_requires_macho_signed_alias() {
        assert_eq!(dylib_alias_status(false, false, false, false, false), "missing_source_so");
        assert_eq!(dylib_alias_status(true, false, false, false, false), "source_not_macho");
        assert_eq!(dylib_alias_status(true, true, false, false, false), "missing_dylib_alias");
        assert_eq!(dylib_alias_status(true, true, true, false, false), "dylib_alias_not_macho");
        assert_eq!(dylib_alias_status(true, true, true, true, false), "dylib_alias_unsigned");
        assert_eq!(dylib_alias_status(true, true, true, true, true), "dylib_alias_ready");
    }

    #[test]
    fn substrate_decision_requires_linux_substrate_for_linux_module_on_macos() {
        let eac = EacSummary { module_target: Some("linux64".to_string()), ..Default::default() };
        assert_eq!(substrate_decision("macos", &eac, &[]), "requires_linux_user_space_substrate_or_vendor_macos_asset");
    }
}
