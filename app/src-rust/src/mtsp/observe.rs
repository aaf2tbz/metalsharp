use super::engine::{get_pipeline, PipelineId};
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;
use walkdir::WalkDir;

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

pub fn observe_m12_title(
    appid: u32,
    extra_logs: Vec<PathBuf>,
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
        scan_log_for_signals(&log.path, &mut signals, &mut evidence);
    }

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
    read_lossy(path).map(|content| content.to_ascii_lowercase().contains(needle)).unwrap_or(false)
}

fn scan_log_for_signals(path: &Path, signals: &mut RuntimeSignals, evidence: &mut Vec<SignalEvidence>) {
    let Ok(content) = read_lossy(path) else {
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

fn read_lossy(path: &Path) -> std::io::Result<String> {
    let bytes = std::fs::read(path)?;
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
    if signals.d3d11_fallback && signals.dx12_requested {
        return "d3d11_fallback_after_dx12_request";
    }
    if signals.d3d11_fallback || signals.d3d11_loaded {
        return "d3d11_observed";
    }
    if signals.d3d12_dll_loaded {
        return "d3d12_dll_loaded_no_device";
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
    if status == "state_object_notimpl" {
        warnings.push("The title requested a D3D12 state object path that is not implemented yet.");
    }
    warnings
}

fn summary_for_status(status: &str) -> &'static str {
    match status {
        "shader_translation_failure" => "DXIL-to-Metal shader translation failed during launch.",
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
