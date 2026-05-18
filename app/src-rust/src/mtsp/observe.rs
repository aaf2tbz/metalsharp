use super::engine::{get_pipeline, PipelineId};
use serde::Serialize;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::UNIX_EPOCH;
use walkdir::WalkDir;

const MAX_LOG_SCAN_BYTES: u64 = 512 * 1024;

#[derive(Debug, Default, Clone, Serialize)]
struct RuntimeSignals {
    dx12_requested: bool,
    d3d12_dll_loaded: bool,
    d3d12_device_created: bool,
    d3d11_loaded: bool,
    d3d11on12_created: bool,
    d3d11_fallback: bool,
    dxgi_factory_or_swapchain: bool,
    d3d12_sdk_configuration: bool,
    d3d12_sdk_version: bool,
    shader_translation_incomplete: bool,
    shader_translation_failure: bool,
    state_object_notimpl: bool,
    pso_failure: bool,
    pso_bind_failure: bool,
    draw_or_dispatch_skipped: bool,
}

#[derive(Debug, Clone, Serialize)]
struct SignalEvidence {
    signal: &'static str,
    path: PathBuf,
    line: usize,
    text: String,
}

#[derive(Debug, Clone, Serialize)]
struct ObservedLog {
    kind: &'static str,
    path: PathBuf,
    present: bool,
    modified_unix: Option<u64>,
}

#[derive(Debug, Clone, Serialize)]
struct ObservedProcess {
    pid: u32,
    command: String,
    modules: Vec<String>,
    d3d12_dll_loaded: bool,
    d3d11_dll_loaded: bool,
    dxgi_dll_loaded: bool,
    winemetal_loaded: bool,
}

#[derive(Debug, Default, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ObservationScope {
    pub steam_log_offset: Option<u64>,
}

pub fn observe_m12_title(
    appid: u32,
    extra_logs: Vec<PathBuf>,
    scope: ObservationScope,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::M12);
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let ms_home = crate::platform::metalsharp_home();
    let prefix = crate::platform::steam_prefix_dir();
    let mut logs = default_observation_logs(recipe.exe_name.as_deref(), &ms_home, &prefix);

    for path in extra_logs {
        logs.push(ObservedLog { kind: "extra", modified_unix: modified_unix(&path), present: path.is_file(), path });
    }

    dedupe_logs(&mut logs);

    let mut signals = RuntimeSignals::default();
    let mut evidence = Vec::new();
    for log in &logs {
        if !log.present {
            continue;
        }
        let start_offset = if log.kind == "steam_gameprocess" { scope.steam_log_offset } else { None };
        scan_log_for_signals(&log.path, start_offset, &mut signals, &mut evidence);
    }
    let processes = observe_live_processes(recipe.exe_name.as_deref(), &mut signals, &mut evidence);

    let status = classify_runtime(&signals);
    let ok = matches!(status, "d3d12_device_created" | "d3d11on12_over_d3d12");
    let warnings = warnings_for_status(status, &signals);
    let report = serde_json::json!({
        "ok": ok,
        "status": status,
        "appid": appid,
        "pipeline": PipelineId::M12,
        "pipeline_name": node.name,
        "summary": summary_for_status(status),
        "signals": signals,
        "evidence": evidence,
        "logs": logs,
        "processes": processes,
        "scope": scope,
        "recipe": recipe,
        "warnings": warnings,
    });

    persist_observation(&ms_home, appid, &report)?;
    Ok(report)
}

fn default_observation_logs(exe_name: Option<&str>, ms_home: &Path, prefix: &Path) -> Vec<ObservedLog> {
    let mut logs = Vec::new();
    let steam_logs = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("logs");
    push_log(&mut logs, "steam_console", steam_logs.join("console_log.txt"));
    push_log(&mut logs, "steam_gameprocess", steam_logs.join("gameprocess_log.txt"));
    push_log(&mut logs, "steam_shader", steam_logs.join("shader_log.txt"));
    push_recent_metalsharp_logs(&mut logs, ms_home);
    push_unity_player_logs(&mut logs, prefix, exe_name);

    for (kind, path) in [
        ("dxmt_trace", "/private/tmp/dxmt_dxgi_trace.log"),
        ("dxmt_dxil_trace", "/private/tmp/dxmt_dxil_trace.log"),
        ("dxmt_ps_args_trace", "/private/tmp/dxmt_ps_args_debug.log"),
    ] {
        let path = PathBuf::from(path);
        if path.exists() {
            push_log(&mut logs, kind, path);
        }
    }

    logs
}

fn push_log(logs: &mut Vec<ObservedLog>, kind: &'static str, path: PathBuf) {
    logs.push(ObservedLog { kind, modified_unix: modified_unix(&path), present: path.is_file(), path });
}

fn push_recent_metalsharp_logs(logs: &mut Vec<ObservedLog>, ms_home: &Path) {
    let log_dir = ms_home.join("logs");
    let mut paths: Vec<PathBuf> = match std::fs::read_dir(&log_dir) {
        Ok(entries) => entries
            .flatten()
            .map(|entry| entry.path())
            .filter(|path| path.extension().and_then(|e| e.to_str()) == Some("log"))
            .collect(),
        Err(_) => Vec::new(),
    };
    paths.sort_by_key(|path| std::cmp::Reverse(modified_unix(path).unwrap_or(0)));
    for path in paths.into_iter().take(3) {
        push_log(logs, "metalsharp_app", path);
    }
}

fn push_unity_player_logs(logs: &mut Vec<ObservedLog>, prefix: &Path, exe_name: Option<&str>) {
    let users = prefix.join("drive_c").join("users");
    let exe_stem = exe_name
        .and_then(|name| Path::new(name).file_stem())
        .and_then(|name| name.to_str())
        .map(|name| name.to_ascii_lowercase());
    for entry in WalkDir::new(users).max_depth(8).into_iter().flatten() {
        let path = entry.path();
        if !entry.file_type().is_file() || path.file_name().and_then(|n| n.to_str()) != Some("Player.log") {
            continue;
        }
        if let Some(stem) = &exe_stem {
            let path_lower = path.to_string_lossy().to_ascii_lowercase();
            if !path_lower.contains(stem) && !player_log_mentions(path, stem) {
                continue;
            }
        }
        push_log(logs, "unity_player", path.to_path_buf());
    }
}

fn player_log_mentions(path: &Path, needle: &str) -> bool {
    read_tail_lossy(path, MAX_LOG_SCAN_BYTES)
        .map(|content| content.to_ascii_lowercase().contains(needle))
        .unwrap_or(false)
}

fn scan_log_for_signals(
    path: &Path,
    start_offset: Option<u64>,
    signals: &mut RuntimeSignals,
    evidence: &mut Vec<SignalEvidence>,
) {
    let content = match start_offset {
        Some(offset) => read_from_offset_lossy(path, offset, MAX_LOG_SCAN_BYTES),
        None => read_tail_lossy(path, MAX_LOG_SCAN_BYTES),
    };
    let Ok(content) = content else {
        return;
    };

    for (idx, line) in content.lines().enumerate() {
        for signal in detect_signals(line) {
            apply_signal(signals, signal);
            evidence.push(SignalEvidence {
                signal,
                path: path.to_path_buf(),
                line: idx + 1,
                text: truncate_line(line),
            });
        }
    }
}

fn observe_live_processes(
    exe_name: Option<&str>,
    signals: &mut RuntimeSignals,
    evidence: &mut Vec<SignalEvidence>,
) -> Vec<ObservedProcess> {
    let Some(exe_name) = exe_name else {
        return Vec::new();
    };
    let exe_lower = exe_name.to_ascii_lowercase();
    let mut processes = Vec::new();
    for line in process_lines() {
        let Some((pid, command)) = parse_process_line(&line) else {
            continue;
        };
        if is_process_probe_command(command) || !command.to_ascii_lowercase().contains(&exe_lower) {
            continue;
        }

        let modules = process_modules(pid);
        let process = ObservedProcess {
            pid,
            command: command.to_string(),
            d3d12_dll_loaded: modules.iter().any(|path| module_matches(path, "d3d12.dll")),
            d3d11_dll_loaded: modules.iter().any(|path| module_matches(path, "d3d11.dll")),
            dxgi_dll_loaded: modules.iter().any(|path| module_matches(path, "dxgi.dll")),
            winemetal_loaded: modules.iter().any(|path| module_matches(path, "winemetal")),
            modules,
        };
        apply_process_signals(&process, signals, evidence);
        processes.push(process);
    }
    processes
}

fn apply_process_signals(process: &ObservedProcess, signals: &mut RuntimeSignals, evidence: &mut Vec<SignalEvidence>) {
    let process_path = PathBuf::from(format!("process:{}", process.pid));
    if process.d3d12_dll_loaded {
        signals.d3d12_dll_loaded = true;
        evidence.push(SignalEvidence {
            signal: "d3d12_dll_loaded",
            path: process_path.clone(),
            line: 0,
            text: format!("{} loaded d3d12.dll", process.command),
        });
    }
    if process.d3d11_dll_loaded {
        signals.d3d11_loaded = true;
        evidence.push(SignalEvidence {
            signal: "d3d11_loaded",
            path: process_path,
            line: 0,
            text: format!("{} loaded d3d11.dll", process.command),
        });
    }
}

fn process_lines() -> Vec<String> {
    Command::new("ps")
        .args(["axo", "pid=,command="])
        .output()
        .ok()
        .map(|output| String::from_utf8_lossy(&output.stdout).lines().map(|line| line.to_string()).collect())
        .unwrap_or_default()
}

fn parse_process_line(line: &str) -> Option<(u32, &str)> {
    let trimmed = line.trim_start();
    let split = trimmed.find(char::is_whitespace)?;
    let pid = trimmed[..split].parse::<u32>().ok()?;
    Some((pid, trimmed[split..].trim_start()))
}

fn is_process_probe_command(command: &str) -> bool {
    command.contains(" rg ") || command.contains("rg -i") || command.contains("ps axo") || command.contains("lsof -p")
}

fn process_modules(pid: u32) -> Vec<String> {
    let output = Command::new("lsof").args(["-p", &pid.to_string()]).output();
    let Ok(output) = output else {
        return Vec::new();
    };
    String::from_utf8_lossy(&output.stdout).lines().skip(1).filter_map(lsof_path).collect()
}

fn lsof_path(line: &str) -> Option<String> {
    let path_start = line.find('/')?;
    Some(line[path_start..].trim().to_string())
}

fn module_matches(path: &str, needle: &str) -> bool {
    path.to_ascii_lowercase().contains(needle)
}

fn read_tail_lossy(path: &Path, max_bytes: u64) -> std::io::Result<String> {
    let mut file = std::fs::File::open(path)?;
    let len = file.metadata()?.len();
    if len > max_bytes {
        file.seek(SeekFrom::Start(len - max_bytes))?;
    }
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

fn read_from_offset_lossy(path: &Path, start_offset: u64, max_bytes: u64) -> std::io::Result<String> {
    let mut file = std::fs::File::open(path)?;
    let len = file.metadata()?.len();
    if start_offset >= len {
        return Ok(String::new());
    }
    let bytes_available = len - start_offset;
    let offset = if bytes_available > max_bytes { len - max_bytes } else { start_offset };
    file.seek(SeekFrom::Start(offset))?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

fn detect_signals(line: &str) -> Vec<&'static str> {
    let lower = line.to_ascii_lowercase();
    let mut signals = Vec::new();

    if lower.contains("-dx12") || lower.contains("-force-d3d12") {
        signals.push("dx12_requested");
    }
    if lower.contains("loaddll") && lower.contains("d3d12.dll") && lower.contains("native") {
        signals.push("d3d12_dll_loaded");
    }
    if lower.contains("d3d12 device created")
        || lower.contains("d3d12createdevice: created device")
        || lower.contains("d3d12createdevice success")
    {
        signals.push("d3d12_device_created");
    }
    if lower.contains("loaddll") && lower.contains("d3d11.dll") && lower.contains("native") {
        signals.push("d3d11_loaded");
    }
    if lower.contains("d3d11on12createdevice") {
        signals.push("d3d11on12_created");
    }
    if lower.contains("graphicsdevicetype = direct3d11") || lower.contains("version:  direct3d 11") {
        signals.push("d3d11_fallback");
    }
    if lower.contains("dxgi::createswapchain")
        || lower.contains("createdxgifactory")
        || lower.contains("createswapchainforhwnd")
    {
        signals.push("dxgi_factory_or_swapchain");
    }
    if lower.contains("d3d12getinterface") && lower.contains("sdkconfiguration") {
        signals.push("d3d12_sdk_configuration");
    }
    if lower.contains("d3d12sdkversion()") {
        signals.push("d3d12_sdk_version");
    }
    if lower.contains("dxilcontainer::parse failed")
        || lower.contains("bitcodereader::parse failed")
        || lower.contains("dxiltomsl::convert failed")
        || lower.contains("newlibrarywithsource failed")
        || lower.contains("newfunction returned null")
        || lower.contains("dxil msl compilation failed")
        || lower.contains("failed to get function from compiled library")
        || (lower.contains("pso compile failure") && lower.contains("shader/"))
    {
        signals.push("shader_translation_failure");
    }
    if lower.contains("unsupported_intrinsics=")
        || lower.contains("unsupported_opcodes=")
        || lower.contains("dxil unknown intrinsic")
        || lower.contains("dxil unhandled opcode")
        || lower.contains("dxil intrinsic id is not a literal")
    {
        signals.push("shader_translation_incomplete");
    }
    if lower.contains("createstateobject") && lower.contains("e_notimpl") {
        signals.push("state_object_notimpl");
    }
    if lower.contains("failed to create pso")
        || lower.contains("failed to create render pso")
        || lower.contains("failed to create compute pso")
        || lower.contains("pso compile failure")
    {
        signals.push("pso_failure");
    }
    if lower.contains("render_pso_not_bound") {
        signals.push("pso_bind_failure");
    }
    if lower.contains("drawinstanced skipped")
        || lower.contains("drawindexedinstanced skipped")
        || lower.contains("dispatch skipped")
    {
        signals.push("draw_or_dispatch_skipped");
    }

    signals
}

fn apply_signal(signals: &mut RuntimeSignals, signal: &str) {
    match signal {
        "dx12_requested" => signals.dx12_requested = true,
        "d3d12_dll_loaded" => signals.d3d12_dll_loaded = true,
        "d3d12_device_created" => signals.d3d12_device_created = true,
        "d3d11_loaded" => signals.d3d11_loaded = true,
        "d3d11on12_created" => signals.d3d11on12_created = true,
        "d3d11_fallback" => signals.d3d11_fallback = true,
        "dxgi_factory_or_swapchain" => signals.dxgi_factory_or_swapchain = true,
        "d3d12_sdk_configuration" => signals.d3d12_sdk_configuration = true,
        "d3d12_sdk_version" => signals.d3d12_sdk_version = true,
        "shader_translation_incomplete" => signals.shader_translation_incomplete = true,
        "shader_translation_failure" => signals.shader_translation_failure = true,
        "state_object_notimpl" => signals.state_object_notimpl = true,
        "pso_failure" => signals.pso_failure = true,
        "pso_bind_failure" => signals.pso_bind_failure = true,
        "draw_or_dispatch_skipped" => signals.draw_or_dispatch_skipped = true,
        _ => {},
    }
}

fn classify_runtime(signals: &RuntimeSignals) -> &'static str {
    if signals.shader_translation_failure {
        return "shader_translation_failure";
    }
    if signals.shader_translation_incomplete {
        return "shader_translation_incomplete";
    }
    if signals.pso_failure {
        return "pso_failure";
    }
    if signals.pso_bind_failure {
        return "pso_bind_failure";
    }
    if signals.draw_or_dispatch_skipped {
        return "draw_or_dispatch_skipped";
    }
    if signals.state_object_notimpl {
        return "state_object_notimpl";
    }
    if signals.d3d12_device_created && signals.d3d11on12_created {
        return "d3d11on12_over_d3d12";
    }
    if signals.d3d12_device_created {
        return "d3d12_device_created";
    }
    if signals.d3d12_dll_loaded {
        return "d3d12_dll_loaded_no_device";
    }
    if signals.d3d11_fallback && signals.dx12_requested {
        return "d3d11_fallback_after_dx12_request";
    }
    if signals.d3d11_fallback || signals.d3d11_loaded {
        return "d3d11_observed";
    }
    if signals.dxgi_factory_or_swapchain {
        return "dxgi_only";
    }
    if signals.dx12_requested {
        return "dx12_requested_unverified";
    }
    "unknown"
}

fn warnings_for_status(status: &str, signals: &RuntimeSignals) -> Vec<&'static str> {
    let mut warnings = Vec::new();
    if signals.dx12_requested && !signals.d3d12_device_created {
        warnings.push("DX12 was requested, but no D3D12 device creation was observed.");
    }
    if matches!(status, "d3d11_fallback_after_dx12_request" | "d3d11_observed") {
        warnings.push("The title appears to be running through D3D11, not the M12 D3D12 path.");
    }
    if status == "pso_failure" {
        warnings.push("Shader or graphics pipeline state creation failed; inspect PSO/shader translation next.");
    }
    if status == "pso_bind_failure" {
        warnings.push("A command stream reached rendering, but no usable Metal PSO was bound.");
    }
    if status == "draw_or_dispatch_skipped" {
        warnings.push("A draw or dispatch call was skipped before useful GPU work was encoded.");
    }
    if status == "shader_translation_failure" {
        warnings.push("DXIL or Metal shader translation failed before a usable shader function was created.");
    }
    if status == "shader_translation_incomplete" {
        warnings.push(
            "DXIL translated to MSL with unsupported intrinsic or opcode fallbacks; visual output may be incomplete.",
        );
    }
    if status == "state_object_notimpl" {
        warnings.push("The title requested a D3D12 state object path that is not implemented yet.");
    }
    warnings
}

fn summary_for_status(status: &str) -> &'static str {
    match status {
        "shader_translation_failure" => "DXIL-to-Metal shader translation failed during launch.",
        "shader_translation_incomplete" => "DXIL-to-Metal shader translation completed with unsupported operations.",
        "pso_failure" => "D3D/Metal PSO creation failed after launch.",
        "pso_bind_failure" => "Rendering reached command replay, but no usable Metal PSO was bound.",
        "draw_or_dispatch_skipped" => "Rendering reached command replay, but a draw or dispatch was skipped.",
        "state_object_notimpl" => "The title hit an unimplemented D3D12 state object path.",
        "d3d11on12_over_d3d12" => "A D3D12 device was created and D3D11On12 was layered on top.",
        "d3d12_device_created" => "A D3D12 device was created through the DXMT Metal path.",
        "d3d11_fallback_after_dx12_request" => "The title was launched with DX12 arguments but initialized D3D11.",
        "d3d11_observed" => "D3D11 was observed; no D3D12 device creation was found.",
        "d3d12_dll_loaded_no_device" => "d3d12.dll was loaded, but D3D12 device creation was not observed.",
        "dxgi_only" => "DXGI activity was observed without D3D12 device creation.",
        "dx12_requested_unverified" => "DX12 was requested, but no renderer evidence was found.",
        _ => "No conclusive graphics runtime evidence was found.",
    }
}

fn truncate_line(line: &str) -> String {
    const MAX: usize = 360;
    if line.chars().count() <= MAX {
        return line.to_string();
    }
    format!("{}...", line.chars().take(MAX).collect::<String>())
}

fn dedupe_logs(logs: &mut Vec<ObservedLog>) {
    logs.sort_by(|a, b| a.path.cmp(&b.path).then(a.kind.cmp(b.kind)));
    logs.dedup_by(|a, b| a.path == b.path);
}

fn modified_unix(path: &Path) -> Option<u64> {
    path.metadata().ok()?.modified().ok()?.duration_since(UNIX_EPOCH).ok().map(|duration| duration.as_secs())
}

fn persist_observation(
    ms_home: &Path,
    appid: u32,
    report: &serde_json::Value,
) -> Result<(), Box<dyn std::error::Error>> {
    let report_dir = ms_home.join("reports").join("m12");
    std::fs::create_dir_all(&report_dir)?;
    let data = serde_json::to_string_pretty(report)?;
    std::fs::write(report_dir.join(format!("latest-title-observation-{}.json", appid)), data)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn classify(lines: &[&str]) -> &'static str {
        let mut signals = RuntimeSignals::default();
        for line in lines {
            for signal in detect_signals(line) {
                apply_signal(&mut signals, signal);
            }
        }
        classify_runtime(&signals)
    }

    #[test]
    fn classifies_unity_d3d11_fallback_after_dx12_request() {
        assert_eq!(
            classify(&[
                "AutomatedProfiler CommandLineArguments: SubnauticaZero.exe -force-d3d12",
                "graphicsDeviceType = Direct3D11",
            ]),
            "d3d11_fallback_after_dx12_request"
        );
    }

    #[test]
    fn classifies_d3d12_device_creation() {
        assert_eq!(classify(&["info:  D3D12 device created via DXMT Metal backend"]), "d3d12_device_created");
    }

    #[test]
    fn d3d12_dll_load_takes_priority_over_stale_d3d11_fallback() {
        let mut signals = RuntimeSignals {
            dx12_requested: true,
            d3d12_dll_loaded: true,
            d3d11_fallback: true,
            ..RuntimeSignals::default()
        };
        assert_eq!(classify_runtime(&signals), "d3d12_dll_loaded_no_device");

        signals.d3d12_device_created = true;
        assert_eq!(classify_runtime(&signals), "d3d12_device_created");
    }

    #[test]
    fn lsof_path_extracts_loaded_module_path() {
        let line = "Subnautic 83764 alexmondello  txt REG 1,24 123 456 /Volumes/AverySSD/SubnauticaZero/d3d12.dll";
        assert_eq!(lsof_path(line).as_deref(), Some("/Volumes/AverySSD/SubnauticaZero/d3d12.dll"));
    }

    #[test]
    fn scan_log_for_signals_honors_start_offset() {
        let path = std::env::temp_dir().join(format!(
            "metalsharp-observe-offset-{}-{}.log",
            std::process::id(),
            UNIX_EPOCH.elapsed().unwrap().as_nanos()
        ));
        let stale = "SubnauticaZero.exe -dx12\ngraphicsDeviceType = Direct3D11\n";
        let current = "SubnauticaZero.exe -force-d3d12\ninfo:  D3D12 device created via DXMT Metal backend\n";
        std::fs::write(&path, format!("{}{}", stale, current)).unwrap();

        let mut signals = RuntimeSignals::default();
        let mut evidence = Vec::new();
        scan_log_for_signals(&path, Some(stale.len() as u64), &mut signals, &mut evidence);
        std::fs::remove_file(&path).ok();

        assert!(signals.dx12_requested);
        assert!(signals.d3d12_device_created);
        assert!(!signals.d3d11_fallback);
        assert_eq!(classify_runtime(&signals), "d3d12_device_created");
    }

    #[test]
    fn classifies_d3d11on12_over_d3d12() {
        assert_eq!(
            classify(&[
                "info:  D3D12CreateDevice: created device with FL 49152",
                "info:  D3D11On12CreateDevice: creating compatibility D3D11 device queues=1 node_mask=0",
            ]),
            "d3d11on12_over_d3d12"
        );
    }

    #[test]
    fn pso_failure_takes_priority() {
        assert_eq!(
            classify(&["info:  D3D12 device created via DXMT Metal backend", "err:   Failed to create PSO:",]),
            "pso_failure"
        );
    }

    #[test]
    fn classifies_shader_translation_failure_before_device_success() {
        assert_eq!(
            classify(&["info:  D3D12 device created via DXMT Metal backend", "DXILToMSL::convert FAILED",]),
            "shader_translation_failure"
        );
    }

    #[test]
    fn classifies_structured_shader_pso_compile_failure() {
        assert_eq!(
            classify(&[
                "info:  D3D12 device created via DXMT Metal backend",
                "PSO COMPILE FAILURE: this=0x123 compute=0 stage=shader/metal_library_source detail=ps_main MSL compile failed",
            ]),
            "shader_translation_failure"
        );
    }

    #[test]
    fn classifies_incomplete_shader_translation() {
        assert_eq!(
            classify(&[
                "info:  D3D12 device created via DXMT Metal backend",
                "DXILToMSL: generated 4096 bytes of MSL unsupported_intrinsics=2 unsupported_opcodes=1",
            ]),
            "shader_translation_incomplete"
        );
    }

    #[test]
    fn classifies_unbound_render_pso() {
        assert_eq!(
            classify(&[
                "info:  D3D12 device created via DXMT Metal backend",
                "EnsureRenderEncoder: RENDER_PSO_NOT_BOUND pso=0x123 compiled=0 render_handle=0 stage=pso/metal_render_pso detail=unknown",
            ]),
            "pso_bind_failure"
        );
    }

    #[test]
    fn classifies_skipped_draw_or_dispatch() {
        assert_eq!(
            classify(&[
                "info:  D3D12 device created via DXMT Metal backend",
                "DrawInstanced SKIPPED v=3 i=1 enc_open=0 pso=0x123 compiled=0 stage=pso/metal_render_pso detail=unknown",
            ]),
            "draw_or_dispatch_skipped"
        );
    }

    #[test]
    fn classifies_unimplemented_state_object_path() {
        assert_eq!(classify(&["ID3D12Device5::CreateStateObject -> E_NOTIMPL"]), "state_object_notimpl");
    }

    #[test]
    fn detects_agility_sdk_configuration_without_changing_success_status() {
        let mut signals = RuntimeSignals::default();
        for signal in detect_signals("D3D12GetInterface SDKConfiguration riid={...} -> 0x0 out=0x1234") {
            apply_signal(&mut signals, signal);
        }
        for signal in detect_signals("D3D12SDKVersion() -> 620") {
            apply_signal(&mut signals, signal);
        }

        assert!(signals.d3d12_sdk_configuration);
        assert!(signals.d3d12_sdk_version);
        assert_eq!(classify_runtime(&signals), "unknown");
    }
}
