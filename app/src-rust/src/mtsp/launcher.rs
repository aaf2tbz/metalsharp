use super::engine::{get_pipeline, PipelineId, PipelineNode};
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

static BRIDGE_PORT: u16 = 18733;
const DEFAULT_M12_SUITE_MAX_PROBE_MS: u64 = 15_000;
const DEFAULT_M12_SUITE_MAX_TOTAL_MS: u64 = 60_000;

struct M12RuntimeArtifact {
    name: String,
    source_path: Option<PathBuf>,
    runtime_path: PathBuf,
    required: bool,
}

struct CachePaths {
    shader: String,
    pipeline: String,
}

pub fn bridge_is_running() -> bool {
    if let Ok(mut stream) =
        TcpStream::connect_timeout(&format!("127.0.0.1:{}", BRIDGE_PORT).parse().unwrap(), Duration::from_millis(500))
    {
        let ping: [u8; 4] = 0xFFu32.to_ne_bytes();
        let _ = stream.write_all(&ping);
        let _ = stream.write_all(&0u32.to_ne_bytes());
        let mut buf = [0u8; 8];
        if stream.read_exact(&mut buf).is_ok() {
            return true;
        }
    }
    false
}

pub fn ensure_bridge_running() -> Result<(), Box<dyn std::error::Error>> {
    if bridge_is_running() {
        return Ok(());
    }

    let ms_home = crate::platform::metalsharp_home();
    let bridge_exe = ms_home.join("runtime").join("steam-bridge").join("steambridge.exe");
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    let prefix = crate::platform::steam_prefix_dir();

    if !bridge_exe.exists() {
        return Err("steambridge.exe not found".into());
    }
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let bridge_dir = bridge_exe.parent().unwrap_or(std::path::Path::new(""));
    let dll_dest = bridge_dir.join("steam_api64.dll");
    if !dll_dest.exists() {
        let steam_dll = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("steam_api64.dll");
        if steam_dll.exists() {
            let _ = std::fs::copy(&steam_dll, &dll_dest);
        }
    }

    let mut cmd = Command::new(&wine);
    cmd.arg(&bridge_exe)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    let _child = cmd.spawn()?;

    for _ in 0..20 {
        std::thread::sleep(Duration::from_millis(250));
        if bridge_is_running() {
            return Ok(());
        }
    }

    Err("steam bridge failed to start within 5s".into())
}

fn deploy_steam_shim(game_dir: &PathBuf) {
    let shim_src = crate::platform::runtime_dir().join("steam-bridge").join("libsteam_api.dylib");
    if !shim_src.exists() {
        return;
    }
    let dest = game_dir.join("libsteam_api.dylib");
    if dest.exists() {
        return;
    }
    let _ = std::fs::copy(&shim_src, &dest);
}

pub fn launch_with_pipeline(
    appid: u32,
    pipeline_id: PipelineId,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(pipeline_id);

    match pipeline_id {
        PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 => launch_dxmt_metal(appid, node),
        PipelineId::M32 => launch_wine_bare(appid, node),
        PipelineId::FnaArm64 => launch_fna_arm64(appid),
        PipelineId::Steam => launch_steam(appid),
        PipelineId::MacSteam => launch_macos_steam(appid),
        PipelineId::WineBare => launch_wine_bare(appid, node),
    }
}

pub fn launch_auto(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    launch_with_pipeline(appid, pipeline_id)
}

pub fn prepare_pipeline(appid: u32) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    prepare_pipeline_with_pipeline(appid, pipeline_id)
}

pub fn prepare_pipeline_with_pipeline(
    appid: u32,
    pipeline_id: PipelineId,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let node = get_pipeline(pipeline_id);
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let deployed_sources: Vec<String> = recipe.dlls.iter().map(|dll| dll.source_subpath.clone()).collect();
    deploy_recipe_dlls(&recipe)?;
    let wine_appdefaults = apply_recipe_appdefault_dll_overrides(&recipe, node)?;

    Ok(serde_json::json!({
        "ok": true,
        "appid": appid,
        "pipeline": node.id,
        "pipeline_name": node.name,
        "recipe": recipe,
        "deployed_dlls": deployed_sources.len(),
        "deployed_sources": deployed_sources,
        "wine_appdefaults": wine_appdefaults,
    }))
}

fn apply_recipe_appdefault_dll_overrides(
    recipe: &super::recipe::LaunchRecipe,
    node: &PipelineNode,
) -> Result<Vec<serde_json::Value>, Box<dyn std::error::Error>> {
    let Some(exe_name) = recipe.exe_name.as_deref() else {
        return Ok(Vec::new());
    };
    let Some(overrides) = node.wine_overrides else {
        return Ok(Vec::new());
    };

    let mut applied = Vec::new();
    for (dll, mode) in appdefault_dll_overrides_for_pipeline(recipe.appid, node.id, overrides) {
        set_wine_appdefault_dll_override(exe_name, &dll, &mode)?;
        applied.push(serde_json::json!({
            "exe": exe_name,
            "dll": dll,
            "mode": mode,
        }));
    }

    Ok(applied)
}

fn appdefault_dll_overrides_for_pipeline(_appid: u32, _pipeline: PipelineId, overrides: &str) -> Vec<(String, String)> {
    parse_wine_dll_overrides(overrides)
}

fn parse_wine_dll_overrides(overrides: &str) -> Vec<(String, String)> {
    let mut parsed = Vec::new();
    for group in overrides.split(';') {
        let Some((dlls, mode)) = group.split_once('=') else {
            continue;
        };
        let mode = registry_override_mode(mode);
        for dll in dlls.split(',').map(str::trim).filter(|dll| !dll.is_empty()) {
            parsed.push((dll.to_string(), mode.clone()));
        }
    }

    parsed
}

fn registry_override_mode(mode: &str) -> String {
    mode.split(',')
        .map(str::trim)
        .filter(|part| !part.is_empty())
        .map(|part| match part {
            "n" | "native" => "native",
            "b" | "builtin" => "builtin",
            "d" | "disabled" => "disabled",
            other => other,
        })
        .collect::<Vec<_>>()
        .join(",")
}

fn set_wine_appdefault_dll_override(exe_name: &str, dll: &str, mode: &str) -> Result<(), Box<dyn std::error::Error>> {
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = crate::platform::steam_prefix_dir();
    let key = format!("HKCU\\Software\\Wine\\AppDefaults\\{}\\DllOverrides", exe_name);
    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .args(["reg", "add", &key, "/v", dll, "/t", "REG_SZ", "/d", mode, "/f"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    let status = cmd.status()?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("failed to set Wine AppDefaults override {}={} for {}", dll, mode, exe_name).into())
    }
}

pub fn verify_m12_runtime(
    probe_path: Option<&Path>,
    timeout_ms: u64,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let probe_source = match probe_path {
        Some(path) => path.to_path_buf(),
        None => default_m12_probe_path()?,
    };
    run_m12_probe(&probe_source, timeout_ms, 4_200_000_012)
}

pub fn verify_m12_runtime_suite(
    probe_paths: Vec<PathBuf>,
    timeout_ms: u64,
    max_probe_ms: Option<u64>,
    max_total_ms: Option<u64>,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let probes = if probe_paths.is_empty() { default_m12_probe_paths()? } else { probe_paths };
    let max_probe_ms = max_probe_ms.unwrap_or(DEFAULT_M12_SUITE_MAX_PROBE_MS);
    let max_total_ms = max_total_ms.unwrap_or(DEFAULT_M12_SUITE_MAX_TOTAL_MS);
    let started = Instant::now();
    let mut results = Vec::new();

    for (idx, probe) in probes.iter().enumerate() {
        let launch_id = 4_200_000_020u32 + idx as u32;
        let mut result = match run_m12_probe(probe, timeout_ms, launch_id) {
            Ok(result) => result,
            Err(error) => serde_json::json!({
                "ok": false,
                "status": "error",
                "probe_source": probe,
                "error": error.to_string(),
            }),
        };
        annotate_m12_probe_result(&mut result, probe);
        results.push(result);
    }

    let passed = results.iter().filter(|result| result.get("ok").and_then(|v| v.as_bool()) == Some(true)).count();
    let failed = results.len().saturating_sub(passed);
    let duration_ms = started.elapsed().as_millis() as u64;
    let slow_probes: Vec<serde_json::Value> = results
        .iter()
        .filter(|result| result.get("duration_ms").and_then(|v| v.as_u64()).unwrap_or(0) > max_probe_ms)
        .map(|result| {
            serde_json::json!({
                "probe": result.get("probe_name").cloned().unwrap_or_else(|| serde_json::json!("unknown")),
                "duration_ms": result.get("duration_ms").cloned().unwrap_or_else(|| serde_json::json!(0)),
                "limit_ms": max_probe_ms,
            })
        })
        .collect();
    let total_over_budget = duration_ms > max_total_ms;
    let performance_ok = slow_probes.is_empty() && !total_over_budget;
    let ok = failed == 0 && !results.is_empty() && performance_ok;
    let status = if failed > 0 {
        "failed"
    } else if !performance_ok {
        "performance_regression"
    } else {
        "passed"
    };

    let report = serde_json::json!({
        "ok": ok,
        "status": status,
        "pipeline": PipelineId::M12,
        "pipeline_name": get_pipeline(PipelineId::M12).name,
        "duration_ms": duration_ms,
        "probe_count": results.len(),
        "passed": passed,
        "failed": failed,
        "performance_gate": {
            "ok": performance_ok,
            "max_probe_ms": max_probe_ms,
            "max_total_ms": max_total_ms,
            "slow_probes": slow_probes,
            "total_over_budget": total_over_budget,
        },
        "coverage": m12_suite_coverage(),
        "results": results,
    });
    persist_m12_json_report(&crate::platform::metalsharp_home(), "latest-suite.json", "suite", &report)?;
    Ok(report)
}

pub fn verify_m12_runtime_parity(dxmt_root: Option<&Path>) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let dxmt_root = dxmt_root.map(Path::to_path_buf).unwrap_or_else(default_m12_dxmt_root);
    let build_root = select_m12_build_root(&dxmt_root);
    let ms_root = crate::platform::wine_runtime_root();
    let artifacts = m12_runtime_artifacts(&build_root, &ms_root);
    let agility_shim = inspect_m12_agility_shim(&dxmt_root, &build_root);

    let mut entries = Vec::new();
    for artifact in &artifacts {
        entries.push(compare_runtime_artifact(artifact)?);
    }

    let comparable_entries =
        entries.iter().filter(|entry| entry.get("comparable").and_then(|v| v.as_bool()) == Some(true));
    let comparable_count = comparable_entries.clone().count();
    let matching =
        comparable_entries.filter(|entry| entry.get("matching").and_then(|v| v.as_bool()) == Some(true)).count();
    let missing_required: Vec<serde_json::Value> = entries
        .iter()
        .filter(|entry| {
            entry.get("required").and_then(|v| v.as_bool()) == Some(true)
                && entry.pointer("/runtime/present").and_then(|v| v.as_bool()) != Some(true)
        })
        .map(|entry| entry.get("name").cloned().unwrap_or_else(|| serde_json::json!("unknown")))
        .collect();
    let mismatched: Vec<serde_json::Value> = entries
        .iter()
        .filter(|entry| entry.get("comparable").and_then(|v| v.as_bool()) == Some(true))
        .filter(|entry| entry.get("matching").and_then(|v| v.as_bool()) != Some(true))
        .map(|entry| entry.get("name").cloned().unwrap_or_else(|| serde_json::json!("unknown")))
        .collect();
    let unverified: Vec<serde_json::Value> = entries
        .iter()
        .filter(|entry| entry.get("comparable").and_then(|v| v.as_bool()) != Some(true))
        .map(|entry| entry.get("name").cloned().unwrap_or_else(|| serde_json::json!("unknown")))
        .collect();
    let ok = missing_required.is_empty() && mismatched.is_empty() && comparable_count > 0;

    Ok(serde_json::json!({
        "ok": ok,
        "status": if ok { "passed" } else { "failed" },
        "dxmt_root": dxmt_root,
        "build_root": build_root,
        "runtime_root": ms_root,
        "checked": entries.len(),
        "comparable": comparable_count,
        "matching": matching,
        "mismatched": mismatched,
        "missing_required": missing_required,
        "unverified": unverified,
        "agility_shim": agility_shim,
        "artifacts": entries,
    }))
}

pub fn verify_m12_title_readiness(
    appids: Vec<u32>,
    dxmt_root: Option<&Path>,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let started = Instant::now();
    let appids = if appids.is_empty() { default_m12_title_appids() } else { appids };
    let dxmt_root = dxmt_root.map(Path::to_path_buf).unwrap_or_else(default_m12_dxmt_root);
    let source_status = inspect_dxmt_source_tree(&dxmt_root);
    let parity = verify_m12_runtime_parity(Some(&dxmt_root))?;
    let mut results = Vec::new();

    for appid in appids {
        let (pipeline, node, resolved_pipeline, resolved_node) = m12_title_readiness_pipelines(appid);
        let doctor = super::recipe::diagnose_launch_request(appid, node);
        let mut blockers = Vec::new();

        if !doctor.ready {
            blockers.push(serde_json::json!({
                "id": "launch_prerequisites",
                "detail": doctor.summary,
                "blockers": doctor.blockers,
            }));
        }

        results.push(serde_json::json!({
            "ok": blockers.is_empty(),
            "appid": appid,
            "pipeline": pipeline,
            "pipeline_name": node.name,
            "resolved_pipeline": resolved_pipeline,
            "resolved_pipeline_name": resolved_node.name,
            "blockers": blockers,
            "doctor": doctor,
        }));
    }

    let title_count = results.len();
    let ready_titles = results.iter().filter(|result| result.get("ok").and_then(|v| v.as_bool()) == Some(true)).count();
    let mut blockers = Vec::new();

    if source_status.get("ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "dxmt_source_cleanliness",
            "detail": "DXMT source tree is not clean enough for AAA title readiness",
            "status": source_status.get("status").cloned().unwrap_or_else(|| serde_json::json!("unknown")),
            "dirty": source_status.get("dirty").cloned().unwrap_or(serde_json::Value::Null),
            "changes": source_status.get("changes").cloned().unwrap_or_else(|| serde_json::json!([])),
        }));
    }

    if parity.get("ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "runtime_parity",
            "detail": "Active M12 runtime does not match the current DXMT build artifacts",
            "mismatched": parity.get("mismatched").cloned().unwrap_or_else(|| serde_json::json!([])),
            "missing_required": parity.get("missing_required").cloned().unwrap_or_else(|| serde_json::json!([])),
        }));
    }

    if parity.pointer("/agility_shim/ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "d3d12_agility_shim",
            "detail": "DXMT D3D12 build does not expose the expected Agility SDK compatibility surface",
            "agility_shim": parity.get("agility_shim").cloned().unwrap_or(serde_json::Value::Null),
        }));
    }

    if ready_titles != title_count {
        blockers.push(serde_json::json!({
            "id": "title_launch_prerequisites",
            "detail": format!("{} of {} requested M12 title(s) passed launch-doctor checks", ready_titles, title_count),
        }));
    }

    let ok = blockers.is_empty();
    let report = serde_json::json!({
        "ok": ok,
        "status": if ok { "ready" } else { "blocked" },
        "duration_ms": started.elapsed().as_millis() as u64,
        "title_count": title_count,
        "ready_titles": ready_titles,
        "blockers": blockers,
        "source_status": source_status,
        "parity": parity,
        "titles": results,
    });
    persist_m12_json_report(
        &crate::platform::metalsharp_home(),
        "latest-title-readiness.json",
        "title-readiness",
        &report,
    )?;
    Ok(report)
}

fn m12_title_readiness_pipelines(appid: u32) -> (PipelineId, &'static PipelineNode, PipelineId, &'static PipelineNode) {
    let resolved_pipeline = super::rules::resolve_pipeline(appid);
    (PipelineId::M12, get_pipeline(PipelineId::M12), resolved_pipeline, get_pipeline(resolved_pipeline))
}

pub fn verify_m12_title_smoke(appid: u32, timeout_ms: u64) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::M12);
    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let doctor = super::recipe::diagnose_launch_request(appid, node);
    if !doctor.ready {
        return Ok(serde_json::json!({
            "ok": false,
            "status": "blocked",
            "appid": appid,
            "pipeline": node.id,
            "pipeline_name": node.name,
            "doctor": doctor,
            "error": "title launch prerequisites are not ready",
        }));
    }

    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix = crate::platform::steam_prefix_dir();
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().ok_or("game exe path does not have a filename")?.to_string_lossy().to_string();

    deploy_recipe_dlls(&recipe)?;

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");
    let cache_paths = build_cache_paths(&ms_home, node, appid);
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env(runtime_lib_key, &dyld_path)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);
    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }
    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);

    let timeout_ms = timeout_ms.clamp(5_000, 120_000);
    let started = Instant::now();
    let mut child = cmd.spawn()?;
    let pid = child.id();
    let deadline = started + Duration::from_millis(timeout_ms);

    loop {
        if let Some(status) = child.try_wait()? {
            let duration_ms = started.elapsed().as_millis() as u64;
            let exit_code = status.code();
            let passed = status.success();
            return Ok(serde_json::json!({
                "ok": passed,
                "status": if passed { "exited_success" } else { "exited_failure" },
                "appid": appid,
                "pipeline": node.id,
                "pipeline_name": node.name,
                "pid": pid,
                "exit_code": exit_code,
                "duration_ms": duration_ms,
                "timeout_ms": timeout_ms,
                "runtime_home": ms_home,
                "wine": wine,
                "prefix": prefix,
                "runtime_library_env": {
                    "key": runtime_lib_key,
                    "value": dyld_path,
                },
                "dxmt_config_file": dxmt_config_file,
                "doctor": doctor,
                "recipe": recipe,
            }));
        }

        if Instant::now() >= deadline {
            let _ = crate::launch::kill_process_tree(pid as i32);
            let _ = child.wait();
            return Ok(serde_json::json!({
                "ok": true,
                "status": "launched_timeout_killed",
                "appid": appid,
                "pipeline": node.id,
                "pipeline_name": node.name,
                "pid": pid,
                "duration_ms": started.elapsed().as_millis() as u64,
                "timeout_ms": timeout_ms,
                "runtime_home": ms_home,
                "wine": wine,
                "prefix": prefix,
                "runtime_library_env": {
                    "key": runtime_lib_key,
                    "value": dyld_path,
                },
                "dxmt_config_file": dxmt_config_file,
                "doctor": doctor,
                "recipe": recipe,
            }));
        }

        std::thread::sleep(Duration::from_millis(100));
    }
}

pub fn deploy_m12_runtime(
    dxmt_root: Option<&Path>,
    apply: bool,
    allow_dirty: bool,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let dxmt_root = dxmt_root.map(Path::to_path_buf).unwrap_or_else(default_m12_dxmt_root);
    let build_root = select_m12_build_root(&dxmt_root);
    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let source_status = inspect_dxmt_source_tree(&dxmt_root);
    let before = verify_m12_runtime_parity(Some(&dxmt_root))?;
    let artifacts = m12_runtime_artifacts(&build_root, &ms_root);
    let mut blockers = Vec::new();
    let mut actions = Vec::new();

    if source_status.get("ok").and_then(|v| v.as_bool()) != Some(true) && !allow_dirty {
        blockers.push(serde_json::json!({
            "id": "dxmt_source_cleanliness",
            "detail": "DXMT source tree is dirty; pass allowDirty only after explicitly accepting those changes",
            "source_status": source_status.clone(),
        }));
    }

    for artifact in &artifacts {
        let deployable = artifact.source_path.is_some();
        let source_present = artifact.source_path.as_ref().map(|path| path.is_file()).unwrap_or(false);
        if artifact.required && deployable && !source_present {
            blockers.push(serde_json::json!({
                "id": "missing_source_artifact",
                "artifact": artifact.name,
                "source_path": artifact.source_path.as_ref(),
            }));
        }
        actions.push(serde_json::json!({
            "artifact": artifact.name,
            "deployable": deployable,
            "source_path": artifact.source_path.as_ref(),
            "runtime_path": &artifact.runtime_path,
            "source_present": source_present,
            "runtime_present": artifact.runtime_path.is_file(),
        }));
    }

    if !blockers.is_empty() || !apply {
        let report = serde_json::json!({
            "ok": blockers.is_empty(),
            "status": if blockers.is_empty() { "dry_run" } else { "blocked" },
            "apply": apply,
            "allow_dirty": allow_dirty,
            "dxmt_root": dxmt_root,
            "build_root": build_root,
            "runtime_root": ms_root,
            "source_status": source_status,
            "blockers": blockers,
            "actions": actions,
            "before": before,
        });
        persist_m12_json_report(&ms_home, "latest-deploy.json", "deploy", &report)?;
        return Ok(report);
    }

    let backup_root = ms_home.join("backups").join("m12-runtime").join(report_timestamp());
    let mut copied = Vec::new();
    for artifact in &artifacts {
        let Some(source_path) = artifact.source_path.as_ref() else {
            continue;
        };
        if let Some(parent) = artifact.runtime_path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let backup_path = backup_root.join(&artifact.name);
        if artifact.runtime_path.exists() {
            if let Some(parent) = backup_path.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::copy(&artifact.runtime_path, &backup_path)?;
        }
        std::fs::copy(source_path, &artifact.runtime_path)?;
        copied.push(serde_json::json!({
            "artifact": artifact.name,
            "source_path": source_path,
            "runtime_path": &artifact.runtime_path,
            "backup_path": if backup_path.exists() { Some(backup_path) } else { None },
        }));
    }

    let after = verify_m12_runtime_parity(Some(&dxmt_root))?;
    let ok = after.get("ok").and_then(|v| v.as_bool()) == Some(true);
    let report = serde_json::json!({
        "ok": ok,
        "status": if ok { "deployed" } else { "deployed_with_parity_gap" },
        "apply": apply,
        "allow_dirty": allow_dirty,
        "dxmt_root": dxmt_root,
        "build_root": build_root,
        "runtime_root": ms_root,
        "backup_root": backup_root,
        "source_status": source_status,
        "copied": copied,
        "before": before,
        "after": after,
    });
    persist_m12_json_report(&ms_home, "latest-deploy.json", "deploy", &report)?;
    Ok(report)
}

pub fn verify_m12_readiness(
    probe_paths: Vec<PathBuf>,
    timeout_ms: u64,
    max_probe_ms: Option<u64>,
    max_total_ms: Option<u64>,
    dxmt_root: Option<&Path>,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let started = Instant::now();
    let dxmt_root = dxmt_root.map(Path::to_path_buf).unwrap_or_else(default_m12_dxmt_root);
    let source_status = inspect_dxmt_source_tree(&dxmt_root);
    let suite = verify_m12_runtime_suite(probe_paths, timeout_ms, max_probe_ms, max_total_ms)?;
    let parity = verify_m12_runtime_parity(Some(&dxmt_root))?;
    let mut blockers = Vec::new();

    if source_status.get("ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "dxmt_source_cleanliness",
            "detail": "DXMT source tree is not clean enough for a safe runtime redeploy",
            "status": source_status.get("status").cloned().unwrap_or_else(|| serde_json::json!("unknown")),
            "dirty": source_status.get("dirty").cloned().unwrap_or(serde_json::Value::Null),
            "changes": source_status.get("changes").cloned().unwrap_or_else(|| serde_json::json!([])),
        }));
    }

    if suite.get("ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "m12_conformance",
            "detail": format!(
                "M12 conformance suite is {}",
                suite.get("status").and_then(|v| v.as_str()).unwrap_or("unknown")
            ),
        }));
    }

    if parity.get("ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "runtime_parity",
            "detail": "Active M12 runtime does not match the current DXMT build artifacts",
            "mismatched": parity.get("mismatched").cloned().unwrap_or_else(|| serde_json::json!([])),
            "missing_required": parity.get("missing_required").cloned().unwrap_or_else(|| serde_json::json!([])),
        }));
    }

    if parity.pointer("/agility_shim/ok").and_then(|v| v.as_bool()) != Some(true) {
        blockers.push(serde_json::json!({
            "id": "d3d12_agility_shim",
            "detail": "DXMT D3D12 build does not expose the expected Agility SDK compatibility surface",
            "agility_shim": parity.get("agility_shim").cloned().unwrap_or(serde_json::Value::Null),
        }));
    }

    let ok = blockers.is_empty();
    let report = serde_json::json!({
        "ok": ok,
        "status": if ok { "ready" } else { "blocked" },
        "pipeline": PipelineId::M12,
        "pipeline_name": get_pipeline(PipelineId::M12).name,
        "duration_ms": started.elapsed().as_millis() as u64,
        "blockers": blockers,
        "source_status": source_status,
        "suite": suite,
        "parity": parity,
    });
    persist_m12_json_report(&crate::platform::metalsharp_home(), "latest-readiness.json", "readiness", &report)?;
    Ok(report)
}

fn default_m12_dxmt_root() -> PathBuf {
    PathBuf::from("/Volumes/AverySSD/metalsharp/dxmt-src")
}

fn default_m12_title_appids() -> Vec<u32> {
    vec![
        2050650, // Resident Evil 4
        1091500, // Cyberpunk 2077
        1551360, // Forza Horizon 5
        1716740, // Starfield
        1282100, // REMNANT II
        1196590, // Resident Evil Village
        1888160, // ARMORED CORE VI FIRES OF RUBICON
        990080,  // Hogwarts Legacy
        1623730, // Palworld
        553850,  // HELLDIVERS 2
    ]
}

fn m12_runtime_artifacts(build_root: &Path, ms_root: &Path) -> Vec<M12RuntimeArtifact> {
    let dxmt_windows = ms_root.join("lib").join("dxmt").join("x86_64-windows");
    let mut artifacts = Vec::new();

    for (dll, source_dir) in [
        ("d3d12.dll", "d3d12"),
        ("d3d11.dll", "d3d11"),
        ("dxgi.dll", "dxgi"),
        ("winemetal.dll", "winemetal"),
        ("d3d10core.dll", "d3d10"),
    ] {
        let source_name = if dll == "d3d10core.dll" { "d3d10core.dll" } else { dll };
        let source_path = build_root.join("src").join(source_dir).join(source_name);
        artifacts.push(M12RuntimeArtifact {
            name: format!("dxmt-runtime/{}", dll),
            source_path: Some(source_path.clone()),
            runtime_path: dxmt_windows.join(dll),
            required: true,
        });
    }

    artifacts.push(M12RuntimeArtifact {
        name: "dxmt-runtime/winemetal.so".into(),
        source_path: Some(build_root.join("src").join("winemetal").join("unix").join("winemetal.so")),
        runtime_path: ms_root.join("lib").join("dxmt").join("x86_64-unix").join("winemetal.so"),
        required: true,
    });

    for (dll, source_dir) in [("d3d12.dll", "d3d12"), ("dxgi.dll", "dxgi"), ("d3d10core.dll", "d3d10")] {
        let source_path = build_root.join("src").join(source_dir).join(dll);
        artifacts.push(M12RuntimeArtifact {
            name: format!("wine-builtin/{}", dll),
            source_path: Some(source_path),
            runtime_path: ms_root.join("lib").join("wine").join("x86_64-windows").join(dll),
            required: true,
        });
    }

    artifacts.push(M12RuntimeArtifact {
        name: "wine-builtin/winemetal.dll".into(),
        source_path: Some(build_root.join("src").join("winemetal").join("winemetal.dll")),
        runtime_path: ms_root.join("lib").join("wine").join("x86_64-windows").join("winemetal.dll"),
        required: true,
    });

    artifacts.push(M12RuntimeArtifact {
        name: "wine-builtin/winemetal.so".into(),
        source_path: Some(build_root.join("src").join("winemetal").join("unix").join("winemetal.so")),
        runtime_path: ms_root.join("lib").join("wine").join("x86_64-unix").join("winemetal.so"),
        required: true,
    });

    artifacts
}

fn select_m12_build_root(dxmt_root: &Path) -> PathBuf {
    if let Ok(value) = std::env::var("METALSHARP_DXMT_BUILD_ROOT") {
        let trimmed = value.trim();
        if !trimmed.is_empty() {
            return PathBuf::from(trimmed);
        }
    }

    for name in ["build64", "build"] {
        let candidate = dxmt_root.join(name);
        if candidate.join("src").join("d3d12").join("d3d12.dll").is_file() {
            return candidate;
        }
    }

    for name in ["build64", "build"] {
        let candidate = dxmt_root.join(name);
        if candidate.is_dir() {
            return candidate;
        }
    }

    dxmt_root.join("build64")
}

fn inspect_m12_agility_shim(dxmt_root: &Path, build_root: &Path) -> serde_json::Value {
    let source_path = dxmt_root.join("src").join("d3d12").join("d3d12.cpp");
    let source_text = std::fs::read_to_string(&source_path).unwrap_or_default();
    let source_present = source_path.is_file();
    let source_sdk_620 = source_text.contains("kD3D12AgilitySDKVersion = 620");
    let source_sdk_configuration = source_text.contains("kCLSID_D3D12SDKConfiguration")
        && source_text.contains("D3D12GetInterface SDKConfiguration");
    let source_sdk_version_trace = source_text.contains("D3D12SDKVersion() -> %u");
    let dll_path = build_root.join("src").join("d3d12").join("d3d12.dll");
    let export_status = inspect_d3d12_exports(&dll_path);
    let exports_ok = export_status.get("ok").and_then(|v| v.as_bool()) == Some(true);
    let source_ok = source_present && source_sdk_620 && source_sdk_configuration && source_sdk_version_trace;
    let ok = source_ok && (!dll_path.is_file() || exports_ok);

    serde_json::json!({
        "ok": ok,
        "status": if ok { "ready" } else { "incomplete" },
        "source": {
            "path": source_path,
            "present": source_present,
            "sdk_version_620": source_sdk_620,
            "sdk_configuration_interface": source_sdk_configuration,
            "sdk_version_trace": source_sdk_version_trace,
        },
        "built_dll": export_status,
    })
}

fn inspect_d3d12_exports(dll_path: &Path) -> serde_json::Value {
    if !dll_path.is_file() {
        return serde_json::json!({
            "ok": false,
            "status": "missing",
            "path": dll_path,
        });
    }

    let output = match Command::new("objdump").arg("-p").arg(dll_path).output() {
        Ok(output) => output,
        Err(error) => {
            return serde_json::json!({
                "ok": false,
                "status": "objdump_failed",
                "path": dll_path,
                "error": error.to_string(),
            });
        },
    };
    if !output.status.success() {
        return serde_json::json!({
            "ok": false,
            "status": "objdump_failed",
            "path": dll_path,
            "stderr": String::from_utf8_lossy(&output.stderr).trim(),
        });
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    let has_get_interface = stdout.contains("D3D12GetInterface");
    let has_sdk_version = stdout.contains("D3D12SDKVersion");
    serde_json::json!({
        "ok": has_get_interface && has_sdk_version,
        "status": if has_get_interface && has_sdk_version { "ready" } else { "missing_exports" },
        "path": dll_path,
        "exports": {
            "D3D12GetInterface": has_get_interface,
            "D3D12SDKVersion": has_sdk_version,
        },
    })
}

fn inspect_dxmt_source_tree(dxmt_root: &Path) -> serde_json::Value {
    if !dxmt_root.exists() {
        return serde_json::json!({
            "ok": false,
            "status": "missing",
            "path": dxmt_root,
            "dirty": serde_json::Value::Null,
            "changes": [],
            "error": "DXMT source root does not exist",
        });
    }

    let status_output =
        match Command::new("git").arg("-C").arg(dxmt_root).args(["status", "--porcelain=v1", "--branch"]).output() {
            Ok(output) => output,
            Err(error) => {
                return serde_json::json!({
                    "ok": false,
                    "status": "error",
                    "path": dxmt_root,
                    "dirty": serde_json::Value::Null,
                    "changes": [],
                    "error": error.to_string(),
                });
            },
        };

    if !status_output.status.success() {
        return serde_json::json!({
            "ok": false,
            "status": "error",
            "path": dxmt_root,
            "dirty": serde_json::Value::Null,
            "changes": [],
            "stderr": String::from_utf8_lossy(&status_output.stderr).trim().to_string(),
        });
    }

    let stdout = String::from_utf8_lossy(&status_output.stdout);
    let (branch_status, changes) = parse_git_status_porcelain(&stdout);
    let dirty = !changes.is_empty();
    let nested_changes = inspect_nested_git_changes(dxmt_root, &changes);
    let changes_json: Vec<serde_json::Value> = changes
        .iter()
        .map(|(code, path)| {
            serde_json::json!({
                "code": code,
                "path": path,
            })
        })
        .collect();

    serde_json::json!({
        "ok": !dirty,
        "status": if dirty { "dirty" } else { "clean" },
        "path": dxmt_root,
        "branch": git_stdout_line(dxmt_root, &["rev-parse", "--abbrev-ref", "HEAD"]),
        "head": git_stdout_line(dxmt_root, &["rev-parse", "--short", "HEAD"]),
        "branch_status": branch_status,
        "dirty": dirty,
        "changes": changes_json,
        "nested_changes": nested_changes,
    })
}

fn inspect_nested_git_changes(root: &Path, changes: &[(String, String)]) -> Vec<serde_json::Value> {
    changes.iter().filter_map(|(_, path)| inspect_nested_git_change(root, path)).collect()
}

fn inspect_nested_git_change(root: &Path, path: &str) -> Option<serde_json::Value> {
    if !is_tracked_gitlink(root, path) {
        return None;
    }

    let nested_root = root.join(path);
    if !nested_root.is_dir() {
        return None;
    }

    let inside =
        Command::new("git").arg("-C").arg(&nested_root).args(["rev-parse", "--is-inside-work-tree"]).output().ok()?;
    if !inside.status.success() || String::from_utf8_lossy(&inside.stdout).trim() != "true" {
        return None;
    }

    let status =
        Command::new("git").arg("-C").arg(&nested_root).args(["status", "--porcelain=v1", "--branch"]).output().ok()?;
    if !status.status.success() {
        return None;
    }

    let stdout = String::from_utf8_lossy(&status.stdout);
    let (branch_status, changes) = parse_git_status_porcelain(&stdout);
    if changes.is_empty() {
        return None;
    }

    let changes_json: Vec<serde_json::Value> = changes
        .iter()
        .map(|(code, path)| {
            serde_json::json!({
                "code": code,
                "path": path,
            })
        })
        .collect();

    Some(serde_json::json!({
        "path": path,
        "branch": git_stdout_line(&nested_root, &["rev-parse", "--abbrev-ref", "HEAD"]),
        "head": git_stdout_line(&nested_root, &["rev-parse", "--short", "HEAD"]),
        "branch_status": branch_status,
        "changes": changes_json,
    }))
}

fn is_tracked_gitlink(root: &Path, path: &str) -> bool {
    let output = match Command::new("git").arg("-C").arg(root).args(["ls-files", "--stage", "--"]).arg(path).output() {
        Ok(output) => output,
        Err(_) => return false,
    };
    if !output.status.success() {
        return false;
    }

    String::from_utf8_lossy(&output.stdout).lines().any(|line| line.starts_with("160000 "))
}

fn git_stdout_line(root: &Path, args: &[&str]) -> Option<String> {
    let output = Command::new("git").arg("-C").arg(root).args(args).output().ok()?;
    if !output.status.success() {
        return None;
    }

    let value = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if value.is_empty() {
        None
    } else {
        Some(value)
    }
}

fn parse_git_status_porcelain(output: &str) -> (Option<String>, Vec<(String, String)>) {
    let mut branch = None;
    let mut changes = Vec::new();

    for line in output.lines() {
        if let Some(rest) = line.strip_prefix("## ") {
            branch = Some(rest.to_string());
            continue;
        }

        if line.len() < 3 {
            continue;
        }

        changes.push((line[..2].to_string(), line[3..].to_string()));
    }

    (branch, changes)
}

fn run_m12_probe(
    probe_source: &Path,
    timeout_ms: u64,
    launch_id: u32,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::M12);
    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    if !probe_source.is_file() {
        return Err(format!("M12 probe executable not found: {}", probe_source.display()).into());
    }

    let run_dir = create_probe_run_dir(&ms_home)?;
    let probe_name = probe_source.file_name().ok_or("M12 probe path does not have a filename")?;
    let staged_probe = run_dir.join(probe_name);
    std::fs::copy(probe_source, &staged_probe)?;

    let recipe = super::recipe::build_custom_launch_recipe(launch_id, node, &run_dir, Some(&staged_probe))?;
    deploy_recipe_dlls(&recipe)?;

    let prefix = crate::platform::steam_prefix_dir();
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = staged_probe.file_name().ok_or("staged M12 probe path does not have a filename")?.to_string_lossy();
    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");
    let cache_paths = build_cache_paths(&ms_home, node, launch_id);
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&run_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env(runtime_lib_key, &dyld_path)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);
    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }
    cmd.arg(exe_name.to_string());
    cmd.args(&node.launch_args);

    let timeout_ms = timeout_ms.clamp(1_000, 120_000);
    let started = Instant::now();
    let mut child = cmd.spawn()?;
    let pid = child.id();
    let deadline = started + Duration::from_millis(timeout_ms);

    loop {
        if let Some(status) = child.try_wait()? {
            let duration_ms = started.elapsed().as_millis() as u64;
            let exit_code = status.code();
            let passed = status.success();
            return Ok(serde_json::json!({
                "ok": passed,
                "status": if passed { "passed" } else { "failed" },
                "pipeline": node.id,
                "pipeline_name": node.name,
                "pid": pid,
                "exit_code": exit_code,
                "duration_ms": duration_ms,
                "timeout_ms": timeout_ms,
                "probe_source": probe_source,
                "run_dir": run_dir,
                "runtime_home": ms_home,
                "wine": wine,
                "prefix": prefix,
                "runtime_library_env": {
                    "key": runtime_lib_key,
                    "value": dyld_path,
                },
                "dxmt_config_file": dxmt_config_file,
                "recipe": recipe,
            }));
        }

        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            return Ok(serde_json::json!({
                "ok": false,
                "status": "timeout",
                "pipeline": node.id,
                "pipeline_name": node.name,
                "pid": pid,
                "duration_ms": started.elapsed().as_millis() as u64,
                "timeout_ms": timeout_ms,
                "probe_source": probe_source,
                "run_dir": run_dir,
                "runtime_home": ms_home,
                "wine": wine,
                "prefix": prefix,
                "runtime_library_env": {
                    "key": runtime_lib_key,
                    "value": dyld_path,
                },
                "dxmt_config_file": dxmt_config_file,
                "recipe": recipe,
            }));
        }

        std::thread::sleep(Duration::from_millis(100));
    }
}

fn compare_runtime_artifact(artifact: &M12RuntimeArtifact) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let source = artifact.source_path.as_deref().map(file_fingerprint).transpose()?;
    let runtime = file_fingerprint(&artifact.runtime_path)?;
    let comparable = source.as_ref().and_then(|s| s.get("present")).and_then(|v| v.as_bool()) == Some(true)
        && runtime.get("present").and_then(|v| v.as_bool()) == Some(true);
    let matching = comparable
        && source.as_ref().and_then(|s| s.get("hash")).and_then(|v| v.as_str())
            == runtime.get("hash").and_then(|v| v.as_str());

    Ok(serde_json::json!({
        "name": artifact.name,
        "required": artifact.required,
        "comparable": comparable,
        "matching": if comparable { serde_json::json!(matching) } else { serde_json::json!(null) },
        "source": source,
        "runtime": runtime,
    }))
}

fn file_fingerprint(path: &Path) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    if !path.exists() {
        return Ok(serde_json::json!({
            "path": path,
            "present": false,
        }));
    }

    let metadata = std::fs::metadata(path)?;
    let modified_unix = metadata
        .modified()
        .ok()
        .and_then(|time| time.duration_since(UNIX_EPOCH).ok().map(|duration| duration.as_secs()));
    let hash = fnv1a64_file(path)?;

    Ok(serde_json::json!({
        "path": path,
        "present": true,
        "size": metadata.len(),
        "modified_unix": modified_unix,
        "hash": format!("{:016x}", hash),
    }))
}

fn fnv1a64_file(path: &Path) -> Result<u64, Box<dyn std::error::Error>> {
    let mut file = std::fs::File::open(path)?;
    let mut hash = 0xcbf29ce484222325u64;
    let mut buf = [0u8; 64 * 1024];

    loop {
        let read = file.read(&mut buf)?;
        if read == 0 {
            break;
        }
        for byte in &buf[..read] {
            hash ^= *byte as u64;
            hash = hash.wrapping_mul(0x100000001b3);
        }
    }

    Ok(hash)
}

fn report_timestamp() -> String {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs().to_string()
}

pub fn deploy_recipe_dlls(recipe: &super::recipe::LaunchRecipe) -> Result<(), Box<dyn std::error::Error>> {
    validate_recipe_runtime(recipe)?;

    if recipe.dlls.is_empty() {
        return Ok(());
    }

    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let injection_dir = game_dir.join(".metalsharp");
    let originals_dir = injection_dir.join("originals");
    std::fs::create_dir_all(&originals_dir)?;

    let mut manifest_dlls = Vec::new();
    for deploy in &recipe.dlls {
        if !deploy.source_present {
            return Err(format!(
                "required runtime DLL {} missing at {}",
                deploy.filename,
                deploy.source_path.display()
            )
            .into());
        }
        if let Some(parent) = deploy.dest_path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }

        let backup_path = originals_dir.join(backup_name_for(game_dir, &deploy.dest_path));
        if deploy.dest_path.exists() && !files_match(&deploy.source_path, &deploy.dest_path) && !backup_path.exists() {
            std::fs::copy(&deploy.dest_path, &backup_path)?;
        }

        std::fs::copy(&deploy.source_path, &deploy.dest_path)?;
        manifest_dlls.push(serde_json::json!({
            "filename": deploy.filename,
            "source_path": deploy.source_path,
            "dest_path": deploy.dest_path,
            "backup_path": if backup_path.exists() { Some(backup_path) } else { None },
        }));
    }

    let manifest = serde_json::json!({
        "appid": recipe.appid,
        "pipeline": recipe.pipeline,
        "pipeline_name": recipe.pipeline_name,
        "backend": recipe.backend,
        "exe_path": recipe.exe_path,
        "updated_at_unix": std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs(),
        "dlls": manifest_dlls,
    });
    std::fs::write(injection_dir.join("injections.json"), serde_json::to_string_pretty(&manifest)?)?;
    Ok(())
}

fn default_m12_probe_path() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let mut candidates = Vec::new();
    if let Ok(path) = std::env::var("METALSHARP_M12_PROBE") {
        candidates.push(PathBuf::from(path));
    }

    let ms_home = crate::platform::metalsharp_home();
    candidates.push(ms_home.join("probes").join("m12").join("probe6_texture.exe"));
    candidates.extend(default_m12_suite_candidates());

    for candidate in &candidates {
        if candidate.is_file() {
            return Ok(candidate.clone());
        }
    }

    Err(format!(
        "No M12 probe executable found. Set METALSHARP_M12_PROBE or install one at {}. Checked: {}",
        ms_home.join("probes").join("m12").join("probe6_texture.exe").display(),
        candidates.iter().map(|p| p.display().to_string()).collect::<Vec<_>>().join(", ")
    )
    .into())
}

fn default_m12_probe_paths() -> Result<Vec<PathBuf>, Box<dyn std::error::Error>> {
    let mut probes = Vec::new();
    for candidate in default_m12_suite_candidates() {
        if candidate.is_file() {
            probes.push(candidate);
        }
    }

    if probes.is_empty() {
        Err("No M12 conformance probe executables found. Pass probePaths or install probes under /Volumes/AverySSD/metalsharp/dxmt-src/tests.".into())
    } else {
        Ok(probes)
    }
}

fn default_m12_suite_candidates() -> Vec<PathBuf> {
    let avery_tests = PathBuf::from("/Volumes/AverySSD/metalsharp/dxmt-src/tests");
    [
        ("probe2", "probe2_compute.exe"),
        ("probe3", "probe3_triangle.exe"),
        ("probe4", "probe4_indexed.exe"),
        ("probe5", "probe5_depth.exe"),
        ("probe6", "probe6_texture.exe"),
    ]
    .iter()
    .map(|(dir, exe)| avery_tests.join(dir).join(exe))
    .collect()
}

fn annotate_m12_probe_result(result: &mut serde_json::Value, probe: &Path) {
    let Some(obj) = result.as_object_mut() else {
        return;
    };
    let name = probe.file_stem().and_then(|s| s.to_str()).unwrap_or("unknown");
    let coverage = m12_probe_coverage(name);
    obj.insert("probe_name".into(), serde_json::json!(name));
    obj.insert("coverage".into(), serde_json::json!(coverage));
}

fn m12_probe_coverage(probe_name: &str) -> Vec<&'static str> {
    if probe_name.contains("probe2") {
        vec!["compute_pso", "root_signature", "committed_buffer", "fence"]
    } else if probe_name.contains("probe3") {
        vec!["triangle_render", "sm50_shader_compile", "sv_vertex_id", "swapchain_present", "pixel_readback"]
    } else if probe_name.contains("probe4") {
        vec!["indexed_draw", "vertex_buffer", "index_buffer", "input_layout", "pixel_readback"]
    } else if probe_name.contains("probe5") {
        vec!["depth_stencil_state", "depth_clear", "depth_compare", "render_encoder_state"]
    } else if probe_name.contains("probe6") {
        vec!["texture_upload", "texture_sampling", "sampler_state", "argument_buffer_residency"]
    } else {
        vec!["custom_m12_probe"]
    }
}

fn m12_suite_coverage() -> Vec<&'static str> {
    vec![
        "compute_pso",
        "triangle_render",
        "indexed_draw",
        "depth_stencil",
        "texture_sampling",
        "sm50_shader_compile",
        "dxmt_dll_deployment",
        "external_drive_runtime",
    ]
}

fn persist_m12_json_report(
    ms_home: &Path,
    latest_name: &str,
    archive_prefix: &str,
    report: &serde_json::Value,
) -> Result<(), Box<dyn std::error::Error>> {
    let dir = ms_home.join("probes").join("m12-runs");
    std::fs::create_dir_all(&dir)?;
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis();
    let data = serde_json::to_string_pretty(report)?;
    std::fs::write(dir.join(latest_name), &data)?;
    std::fs::write(dir.join(format!("{}-{}.json", archive_prefix, stamp)), data)?;
    Ok(())
}

fn create_probe_run_dir(ms_home: &Path) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis();
    let dir = ms_home.join("probes").join("m12-runs").join(format!("{}-{}", stamp, std::process::id()));
    std::fs::create_dir_all(&dir)?;
    Ok(dir)
}

pub fn launch_custom_with_pipeline(
    launch_id: u32,
    game_dir: &std::path::Path,
    exe_path: &std::path::Path,
    pipeline_id: PipelineId,
) -> Result<(u32, &'static str, super::recipe::LaunchRecipe), Box<dyn std::error::Error>> {
    let node = get_pipeline(pipeline_id);
    match pipeline_id {
        PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M32
        | PipelineId::WineBare => {},
        PipelineId::FnaArm64 | PipelineId::Steam | PipelineId::MacSteam => {
            return Err("Sharp Library apps must use Auto, Wine, M9, M10, M11, M12, or M32".into());
        },
    }

    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let recipe = super::recipe::build_custom_launch_recipe(launch_id, node, game_dir, Some(exe_path))?;
    if node.deploy_dlls.is_empty() {
        validate_recipe_runtime(&recipe)?;
    } else {
        deploy_recipe_dlls(&recipe)?;
    }

    let prefix = crate::platform::steam_prefix_dir();
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let exe_name = exe_path.file_name().ok_or("game exe not found")?.to_string_lossy().to_string();
    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let cache_paths = build_cache_paths(&ms_home, node, launch_id);
    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir).env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all").env(runtime_lib_key, &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    if node.backend == "dxmt" {
        cmd.env("DXMT_CONFIG_FILE", ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string());
    }
    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method(), recipe))
}

fn backup_name_for(game_dir: &std::path::Path, dest_path: &std::path::Path) -> String {
    dest_path
        .strip_prefix(game_dir)
        .unwrap_or(dest_path)
        .components()
        .map(|c| c.as_os_str().to_string_lossy())
        .collect::<Vec<_>>()
        .join("__")
}

fn files_match(left: &std::path::Path, right: &std::path::Path) -> bool {
    match (std::fs::read(left), std::fs::read(right)) {
        (Ok(a), Ok(b)) => a == b,
        _ => false,
    }
}

fn launch_working_dir<'a>(game_dir: &'a std::path::Path, exe_path: &'a std::path::Path) -> &'a std::path::Path {
    exe_path.parent().unwrap_or(game_dir)
}

fn validate_recipe_runtime(recipe: &super::recipe::LaunchRecipe) -> Result<(), Box<dyn std::error::Error>> {
    let missing: Vec<String> = recipe
        .runtime_assets
        .iter()
        .filter(|asset| asset.required && !asset.present)
        .map(|asset| format!("{} ({})", asset.name, asset.path.display()))
        .collect();

    if missing.is_empty() {
        Ok(())
    } else {
        Err(format!("MetalSharp runtime is incomplete: {}", missing.join(", ")).into())
    }
}

fn launch_dxmt_metal(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix = crate::platform::steam_prefix_dir();
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    deploy_recipe_dlls(&recipe)?;

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let cache_paths = build_cache_paths(&ms_home, node, appid);
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir).env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all").env(runtime_lib_key, &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_wine_bare(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let wine = crate::platform::runtime_wine_binary(&ms_root);

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix = crate::platform::steam_prefix_dir();
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    validate_recipe_runtime(&recipe)?;

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir).env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all").env(runtime_lib_key, &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    let cache_paths = build_cache_paths(&ms_home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_steam(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pid = crate::launch::launch_via_steam(appid)?;
    Ok((pid, "steam"))
}

fn launch_macos_steam(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    if crate::steam::is_wine_steam_running() {
        return Err("Wine Steam is running. Stop Wine Steam before launching through MacOS Steam.".into());
    }

    let result = crate::steam::launch_macos_steam_game(appid)?;
    let pid = result.get("pid").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
    Ok((pid, "macos_steam"))
}

fn launch_fna_arm64(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::FnaArm64);
    let game_dir = resolve_fna_game_dir(appid)?;
    let ms_home = crate::platform::metalsharp_home();
    let local_dir = ms_home.join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };

    ensure_launcher_exe(appid, dir);
    deploy_fna_assemblies(dir);
    deploy_steam_shim(dir);

    let _ = ensure_bridge_running();

    let exe = match appid {
        105600 => find_preferred_exe(dir, &["TerrariaLauncher.exe", "Terraria.exe"])?,
        _ => resolve_game_exe(dir).into(),
    };

    let mono_bin = find_mono_binary()?;
    let mono_config = find_config("terraria-mono.config");
    let shims_dir = find_shims_dir();
    let mono_lib = mono_bin.parent().unwrap_or(std::path::Path::new("")).join("..").join("lib");
    let mut library_paths = vec![dir.to_string_lossy().to_string(), shims_dir, mono_lib.to_string_lossy().to_string()];
    if crate::platform::current() == crate::platform::HostPlatform::Macos {
        library_paths.push("/opt/homebrew/lib".into());
    } else {
        library_paths.push("/usr/lib".into());
        library_paths.push("/usr/local/lib".into());
    }
    let runtime_lib_path = library_paths.join(":");
    let runtime_lib_key = if crate::platform::current() == crate::platform::HostPlatform::Macos {
        "DYLD_LIBRARY_PATH"
    } else {
        "LD_LIBRARY_PATH"
    };

    let mut cmd = Command::new(&mono_bin);
    cmd.current_dir(dir)
        .env(runtime_lib_key, &runtime_lib_path)
        .env("MONO_CONFIG", mono_config)
        .env("MONO_ENV_OPTIONS", "--runtime=v4.0")
        .env("MONO_PATH", dir.to_string_lossy().to_string());

    let cache_paths = build_cache_paths(&ms_home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &crate::platform::wine_runtime_root());

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe);

    let child = cmd.spawn()?;
    Ok((child.id(), "xna_fna_arm64"))
}

fn find_mono_binary() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let ms_home = crate::platform::metalsharp_home();
    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/mono"),
        PathBuf::from("/usr/local/bin/mono"),
        PathBuf::from("/usr/bin/mono"),
        ms_home.join("runtime").join("mono-arm64").join("bin").join("mono"),
    ];
    for c in candidates {
        if c.exists() {
            return Ok(c);
        }
    }
    Err("Mono not found — install Mono or use setup to install runtime support".into())
}

fn build_dyld(ms_root: &PathBuf, paths: &[&str]) -> String {
    paths.iter().map(|p| ms_root.join(p).to_string_lossy().to_string()).collect::<Vec<_>>().join(":")
}

fn build_cache_paths(ms_home: &PathBuf, node: &PipelineNode, appid: u32) -> Option<CachePaths> {
    let subdir = node.shader_cache_subdir?;
    let shader_base = ms_home.join("shader-cache").join(subdir).join(appid.to_string());
    let pipeline_base = ms_home.join("pipeline-cache").join(subdir).join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_base);
    let _ = std::fs::create_dir_all(&pipeline_base);
    super::shader_cache::deploy_preset_cache(ms_home, subdir, appid);
    Some(CachePaths {
        shader: shader_base.to_string_lossy().to_string(),
        pipeline: pipeline_base.to_string_lossy().to_string(),
    })
}

fn apply_cache_env(cmd: &mut Command, node: &PipelineNode, cache_paths: Option<&CachePaths>, ms_root: &PathBuf) {
    for (key, val) in cache_env_pairs(node, cache_paths, ms_root) {
        cmd.env(key, val);
    }
}

fn cache_env_pairs(node: &PipelineNode, cache_paths: Option<&CachePaths>, ms_root: &PathBuf) -> Vec<(String, String)> {
    let Some(cache) = cache_paths else {
        return Vec::new();
    };

    let shader_dir = format!("{}/", cache.shader);
    let pipeline_dir = format!("{}/", cache.pipeline);
    let mut env = vec![
        ("METALSHARP_SHADER_CACHE_PATH".to_string(), shader_dir.clone()),
        ("METALSHARP_PIPELINE_CACHE_PATH".to_string(), pipeline_dir.clone()),
        ("MTL_SHADER_CACHE_DIR".to_string(), shader_dir.clone()),
    ];

    match node.backend {
        "dxmt" => {
            env.push(("DXMT_SHADER_CACHE_PATH".to_string(), shader_dir));
            env.push(("DXMT_PIPELINE_CACHE_PATH".to_string(), pipeline_dir));
        },
        "dxvk" => {
            env.push(("DXVK_STATE_CACHE_PATH".to_string(), shader_dir));
            env.push(("DXVK_LOG_PATH".to_string(), cache.pipeline.clone()));
            let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
            if moltenvk_icd.exists() {
                env.push(("VK_ICD_FILENAMES".to_string(), moltenvk_icd.to_string_lossy().to_string()));
            }
        },
        "wine32" | "wine" | "wine-steam" => {
            env.push(("DXMT_SHADER_CACHE_PATH".to_string(), shader_dir.clone()));
            env.push(("DXVK_STATE_CACHE_PATH".to_string(), shader_dir));
            env.push(("DXMT_PIPELINE_CACHE_PATH".to_string(), pipeline_dir));
        },
        "mono" | "macos-steam" => {
            env.push(("FNA3D_SHADER_CACHE_PATH".to_string(), shader_dir));
        },
        _ => {},
    }

    env
}

fn resolve_game_exe(game_dir: &PathBuf) -> String {
    super::recipe::resolve_game_exe(0, game_dir).unwrap_or_else(|_| game_dir.clone()).to_string_lossy().to_string()
}

fn find_config(name: &str) -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let ms_home = crate::platform::metalsharp_home();
    let candidates = vec![
        ms_home.join("configs").join(name),
        home.join("metalsharp").join("configs").join(name),
        home.join(".metalsharp").join("configs").join(name),
    ];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    ms_home.join("configs").join(name).to_string_lossy().to_string()
}

fn resolve_fna_game_dir(appid: u32) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let local_dir = crate::platform::metalsharp_home().join("games").join(appid.to_string());
    if local_dir.join(".metalsharp_prepared").exists() {
        return Ok(local_dir);
    }

    let dual = crate::scan::resolve_dual_game_dir(appid);

    if let Some(ref wine_dir) = dual.wine_dir {
        if wine_dir.exists() && has_exe_files(wine_dir) {
            return Ok(wine_dir.clone());
        }
    }

    if let Some(ref macos_dir) = dual.macos_dir {
        if macos_dir.exists() {
            return Ok(macos_dir.clone());
        }
    }

    if local_dir.exists() {
        return Ok(local_dir);
    }
    Err(format!("no game dir found for appid {}", appid).into())
}

fn has_exe_files(dir: &PathBuf) -> bool {
    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_lowercase();
            if name.ends_with(".exe") {
                return true;
            }
        }
    }
    false
}

fn find_preferred_exe(dir: &PathBuf, candidates: &[&str]) -> Result<PathBuf, Box<dyn std::error::Error>> {
    for name in candidates {
        let p = dir.join(name);
        if p.exists() {
            return Ok(p);
        }
    }
    Err(format!("game exe not found: tried {} in {}", candidates.join(", "), dir.display()).into())
}

fn ensure_launcher_exe(appid: u32, game_dir: &PathBuf) {
    let (launcher_name, source_file) = match appid {
        105600 => ("TerrariaLauncher.exe", "TerrariaLauncher.cs"),
        _ => return,
    };

    let launcher = game_dir.join(launcher_name);
    if launcher.exists() {
        return;
    }

    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return,
    };

    let mut candidates = vec![];
    let repo_src = home.join("repos").join("metalsharp").join("src").join("fna").join("terraria").join(source_file);
    if repo_src.exists() {
        candidates.push(repo_src);
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(mut dir) = exe.parent() {
            for _ in 0..8 {
                let p = dir.join("src").join("fna").join("terraria").join(source_file);
                if p.exists() {
                    candidates.push(p);
                    break;
                }
                match dir.parent() {
                    Some(d) => dir = d,
                    None => break,
                }
            }
        }
    }

    let source = match candidates.into_iter().next() {
        Some(s) => s,
        None => return,
    };

    let ms_home = crate::platform::metalsharp_home();
    let mono_bin = ms_home.join("runtime").join("mono-arm64").join("bin").join("mono");
    let mcs_exe = ms_home.join("runtime").join("mono-arm64").join("lib").join("mono").join("4.5").join("mcs.exe");
    if !mono_bin.exists() || !mcs_exe.exists() {
        return;
    }

    let _ = Command::new(&mono_bin)
        .arg(&mcs_exe)
        .args(["-out"])
        .arg(&launcher)
        .args(["-target:winexe"])
        .arg(&source)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
}

fn find_shims_dir() -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let ms_home = crate::platform::metalsharp_home();
    let candidates = vec![
        ms_home.join("runtime").join("shims"),
        ms_home.join("shims"),
        home.join(".metalsharp").join("runtime").join("shims"),
        home.join(".metalsharp").join("shims"),
    ];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    ms_home.join("runtime").join("shims").to_string_lossy().to_string()
}

fn deploy_fna_assemblies(game_dir: &PathBuf) {
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return,
    };
    let fna_dll = crate::platform::metalsharp_home().join("runtime").join("fna").join("FNA.dll");
    if !fna_dll.exists() {
        return;
    }

    let shims_dir = PathBuf::from(find_shims_dir());

    let mac_terrarria_libs = home
        .join("Library")
        .join("Application Support")
        .join("Steam")
        .join("steamapps")
        .join("common")
        .join("Terraria")
        .join("Terraria.app")
        .join("Contents")
        .join("MacOS")
        .join("osx");

    let native_libs = [
        ("libFNA3D.0.dylib", Some("libFNA3D.dylib")),
        ("libSDL3.0.dylib", Some("libSDL3.dylib")),
        ("libFAudio.0.dylib", Some("libFAudio.dylib")),
        ("libsteam_api.dylib", None),
        ("libnfd.dylib", None),
    ];

    for (lib, symlink) in &native_libs {
        let src = mac_terrarria_libs.join(lib).to_string_lossy().to_string();
        let shims_src = shims_dir.join(lib);

        if game_dir.join(lib).exists() {
            continue;
        }

        if std::path::Path::new(&src).exists() {
            let _ = std::fs::copy(std::path::Path::new(&src), game_dir.join(lib));
            if let Some(sym) = symlink {
                let _ = std::os::unix::fs::symlink(lib, game_dir.join(sym));
            }
        } else if shims_src.exists() {
            let _ = std::fs::copy(&shims_src, game_dir.join(lib));
            if let Some(sym) = symlink {
                let _ = std::os::unix::fs::symlink(lib, game_dir.join(sym));
            }
        }
    }

    let gdiplus_src =
        home.join("repos").join("metalsharp").join("src").join("fna").join("terraria").join("gdiplus_stub.c");
    if !game_dir.join("libgdiplus.dylib").exists() && gdiplus_src.exists() {
        let _ = Command::new("clang")
            .args(["-shared", "-arch", "arm64", "-o"])
            .arg(game_dir.join("libgdiplus.dylib"))
            .arg(&gdiplus_src)
            .args(["-install_name", "@loader_path/libgdiplus.dylib"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    let xna_assemblies = [
        "Microsoft.Xna.Framework.dll",
        "Microsoft.Xna.Framework.Game.dll",
        "Microsoft.Xna.Framework.Graphics.dll",
        "Microsoft.Xna.Framework.Audio.dll",
        "Microsoft.Xna.Framework.Input.dll",
        "Microsoft.Xna.Framework.Media.dll",
        "Microsoft.Xna.Framework.Storage.dll",
    ];

    if !game_dir.join("FNA.dll").exists() {
        let _ = std::fs::copy(&fna_dll, game_dir.join("FNA.dll"));
    }
    for name in &xna_assemblies {
        let dst = game_dir.join(name);
        if !dst.exists() {
            let _ = std::fs::copy(&fna_dll, dst);
        }
    }
}

fn deploy_goldberg(home: &PathBuf, game_dir: &PathBuf, appid: u32) {
    deploy_goldberg_internal(home, game_dir, appid);
}

pub fn deploy_goldberg_internal(home: &PathBuf, game_dir: &PathBuf, appid: u32) {
    let goldberg_dir = crate::platform::metalsharp_home().join("runtime").join("goldberg");
    if !goldberg_dir.exists() {
        return;
    }

    let targets: Vec<PathBuf> = vec![
        game_dir.clone(),
        game_dir.join("Game"),
        game_dir.join("bin"),
        game_dir.join("Binaries").join("Win32"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("win64"),
    ];

    for target in &targets {
        if !target.exists() {
            continue;
        }

        let x86_src = goldberg_dir.join("x86").join("steam_api.dll");
        let x86_dst = target.join("steam_api.dll");
        if x86_src.exists() && !x86_dst.with_extension("orig").exists() {
            if x86_dst.exists() {
                let _ = std::fs::rename(&x86_dst, target.join("steam_api.dll.orig"));
            }
            let _ = std::fs::copy(&x86_src, &x86_dst);
        }

        let x64_src = goldberg_dir.join("x64").join("steam_api64.dll");
        let x64_dst = target.join("steam_api64.dll");
        if x64_src.exists() && !x64_dst.with_extension("orig").exists() {
            if x64_dst.exists() {
                let _ = std::fs::rename(&x64_dst, target.join("steam_api64.dll.orig"));
            }
            let _ = std::fs::copy(&x64_src, &x64_dst);
        }
    }

    let steam_settings = game_dir.join("steam_settings");
    if !steam_settings.exists() {
        let _ = std::fs::create_dir_all(&steam_settings);
    }
    let _ = std::fs::write(steam_settings.join("force_steam_appid.txt"), appid.to_string());
}

pub fn cleanup_goldberg(game_dir: &PathBuf) {
    let targets: Vec<PathBuf> = vec![
        game_dir.clone(),
        game_dir.join("Game"),
        game_dir.join("bin"),
        game_dir.join("Binaries").join("Win32"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("win64"),
    ];

    for target in &targets {
        if !target.exists() {
            continue;
        }

        let x86_orig = target.join("steam_api.dll.orig");
        let x86_goldberg = target.join("steam_api.dll");
        if x86_orig.exists() && x86_goldberg.exists() {
            let _ = std::fs::rename(&x86_orig, &x86_goldberg);
        }

        let x64_orig = target.join("steam_api64.dll.orig");
        let x64_goldberg = target.join("steam_api64.dll");
        if x64_orig.exists() && x64_goldberg.exists() {
            let _ = std::fs::rename(&x64_orig, &x64_goldberg);
        }
    }

    let steam_settings = game_dir.join("steam_settings");
    if steam_settings.exists() {
        let _ = std::fs::remove_file(steam_settings.join("force_steam_appid.txt"));
        if std::fs::read_dir(&steam_settings).map(|d| d.count()).unwrap_or(1) == 0 {
            let _ = std::fs::remove_dir(&steam_settings);
        }
    }
}

pub fn goldberg_status(game_dir: &PathBuf) -> bool {
    let targets: Vec<PathBuf> = vec![
        game_dir.clone(),
        game_dir.join("Game"),
        game_dir.join("bin"),
        game_dir.join("Binaries").join("Win32"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("win64"),
    ];

    for target in &targets {
        if !target.exists() {
            continue;
        }
        if target.join("steam_api.dll.orig").exists() || target.join("steam_api64.dll.orig").exists() {
            return true;
        }
    }
    false
}

pub fn deploy_eac_toggle(game_dir: &PathBuf) {
    let eac_dir = crate::platform::metalsharp_home().join("runtime").join("eac-toggle").join("x86_64-windows");
    if !eac_dir.exists() {
        return;
    }

    let targets: Vec<PathBuf> = vec![
        game_dir.join("Game"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("bin"),
        game_dir.join("win64"),
        game_dir.clone(),
    ];

    let dll = eac_dir.join("_winhttp.dll");
    let config = eac_dir.join("anti_cheat_toggler_config.ini");
    let mod_list = eac_dir.join("anti_cheat_toggler_mod_list.txt");
    if !dll.exists() {
        return;
    }

    for target in &targets {
        if !target.exists() {
            continue;
        }
        if !target.join("_winhttp.dll").exists() {
            let _ = std::fs::copy(&dll, target.join("_winhttp.dll"));
        }
        if !target.join("anti_cheat_toggler_config.ini").exists() {
            let _ = std::fs::copy(&config, target.join("anti_cheat_toggler_config.ini"));
        }
        if !target.join("anti_cheat_toggler_mod_list.txt").exists() {
            let _ = std::fs::copy(&mod_list, target.join("anti_cheat_toggler_mod_list.txt"));
        }
        break;
    }
}

pub fn cleanup_eac_toggle(game_dir: &PathBuf) {
    let targets: Vec<PathBuf> = vec![
        game_dir.clone(),
        game_dir.join("Game"),
        game_dir.join("bin"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("win64"),
    ];

    for target in &targets {
        if !target.exists() {
            continue;
        }
        let _ = std::fs::remove_file(target.join("_winhttp.dll"));
        let _ = std::fs::remove_file(target.join("anti_cheat_toggler_config.ini"));
        let _ = std::fs::remove_file(target.join("anti_cheat_toggler_mod_list.txt"));
    }
}

pub fn eac_toggle_status(game_dir: &PathBuf) -> bool {
    let targets: Vec<PathBuf> = vec![
        game_dir.clone(),
        game_dir.join("Game"),
        game_dir.join("bin"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("win64"),
    ];

    for target in &targets {
        if target.exists() && target.join("_winhttp.dll").exists() {
            return true;
        }
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn m9_cache_env_uses_dxmt_family_not_dxvk() {
        let node = get_pipeline(PipelineId::M9);
        let cache = CachePaths { shader: "/tmp/m9-shaders".into(), pipeline: "/tmp/m9-pipelines".into() };

        let env = cache_env_pairs(node, Some(&cache), &PathBuf::from("/tmp/metalsharp-runtime"));
        let keys: std::collections::HashSet<_> = env.iter().map(|(key, _)| key.as_str()).collect();

        assert!(keys.contains("DXMT_SHADER_CACHE_PATH"));
        assert!(keys.contains("DXMT_PIPELINE_CACHE_PATH"));
        assert!(!keys.contains("DXVK_STATE_CACHE_PATH"));
        assert!(!keys.contains("DXVK_LOG_PATH"));
        assert!(!keys.contains("VK_ICD_FILENAMES"));
    }

    #[test]
    fn no_dll_recipes_do_not_require_game_dir_for_deploy() {
        let recipe = super::super::recipe::LaunchRecipe {
            appid: 1,
            pipeline: PipelineId::Steam,
            pipeline_name: "Steam".into(),
            backend: "wine-steam".into(),
            game_dir: None,
            exe_path: None,
            exe_name: None,
            launch_args: vec![],
            env: vec![],
            dlls: vec![],
            runtime_assets: vec![],
            anti_cheat: vec![],
            warnings: vec![],
        };

        deploy_recipe_dlls(&recipe).expect("no-op deploy should succeed");
    }

    #[test]
    fn m12_title_readiness_forces_m12_even_when_auto_rules_pick_m11() {
        let (pipeline, node, resolved_pipeline, resolved_node) = m12_title_readiness_pipelines(848450);

        assert_eq!(resolved_pipeline, PipelineId::M11);
        assert_eq!(resolved_node.name, "M11");
        assert_eq!(pipeline, PipelineId::M12);
        assert_eq!(node.name, "M12");
        assert!(node.launch_args.contains(&"-dx12"));
    }

    #[test]
    fn m12_build_root_prefers_x64_build_artifacts() {
        let root = std::env::temp_dir().join(format!("metalsharp-m12-build-root-{}", std::process::id()));
        let d3d12_dir = root.join("build64").join("src").join("d3d12");
        std::fs::create_dir_all(&d3d12_dir).unwrap();
        std::fs::write(d3d12_dir.join("d3d12.dll"), b"not-a-real-dll").unwrap();

        assert_eq!(select_m12_build_root(&root), root.join("build64"));
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn m12_appdefaults_keep_full_native_dx_surface_for_game_exes() {
        let entries = appdefault_dll_overrides_for_pipeline(
            1669000,
            PipelineId::M12,
            "d3d12,dxgi,d3d11,d3d10core,winemetal=n,b;gameoverlayrenderer=d",
        );

        assert!(entries.contains(&("d3d12".into(), "native,builtin".into())));
        assert!(entries.contains(&("dxgi".into(), "native,builtin".into())));
        assert!(entries.contains(&("winemetal".into(), "native,builtin".into())));
        assert!(entries.contains(&("d3d11".into(), "native,builtin".into())));
        assert!(entries.contains(&("d3d10core".into(), "native,builtin".into())));
    }

    #[test]
    fn sons_m12_appdefaults_keep_d3d11_and_d3d12_native() {
        let entries = appdefault_dll_overrides_for_pipeline(
            1326470,
            PipelineId::M12,
            "d3d12,dxgi,d3d11,d3d10core,winemetal=n,b;gameoverlayrenderer=d",
        );

        assert!(entries.contains(&("d3d12".into(), "native,builtin".into())));
        assert!(entries.contains(&("dxgi".into(), "native,builtin".into())));
        assert!(entries.contains(&("d3d11".into(), "native,builtin".into())));
        assert!(entries.contains(&("d3d10core".into(), "native,builtin".into())));
        assert!(entries.contains(&("winemetal".into(), "native,builtin".into())));
    }

    #[test]
    fn subnautica_m12_appdefaults_keep_dx12_and_dx11_native() {
        let entries = appdefault_dll_overrides_for_pipeline(
            848450,
            PipelineId::M12,
            "d3d12,dxgi,d3d11,d3d10core,winemetal=n,b;gameoverlayrenderer=d",
        );

        assert!(entries.contains(&("d3d12".into(), "native,builtin".into())));
        assert!(entries.contains(&("dxgi".into(), "native,builtin".into())));
        assert!(entries.contains(&("d3d11".into(), "native,builtin".into())));
    }

    #[test]
    fn nested_executables_launch_from_their_parent_directory() {
        let game_dir = PathBuf::from("/tmp/Game");
        let exe_path = game_dir.join("Engine").join("Binaries").join("Win64").join("Game-Win64-Shipping.exe");

        assert_eq!(launch_working_dir(&game_dir, &exe_path), exe_path.parent().unwrap());
    }

    #[test]
    fn git_status_parser_keeps_dirty_submodule_and_untracked_entries() {
        let (branch, changes) = parse_git_status_porcelain(
            "## feat/d3d12-conformance-probes...fork/feat/d3d12-conformance-probes\n M include/native/directx\n M src/d3d12/d3d12_pipeline_state.cpp\n?? .graphiq/\n",
        );

        assert_eq!(branch.as_deref(), Some("feat/d3d12-conformance-probes...fork/feat/d3d12-conformance-probes"));
        assert_eq!(
            changes,
            vec![
                (" M".to_string(), "include/native/directx".to_string()),
                (" M".to_string(), "src/d3d12/d3d12_pipeline_state.cpp".to_string()),
                ("??".to_string(), ".graphiq/".to_string()),
            ]
        );
    }
}
