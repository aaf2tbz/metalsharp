use super::engine::{get_pipeline, PipelineId, PipelineNode};
use std::fs::OpenOptions;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use walkdir::WalkDir;

const DEFAULT_BRIDGE_PORT: u16 = 18733;
const FNA_CARBON_SHIM: &str = "libCarbon.dylib";
const FNA_CARBON_INTERPOSE_SHIM: &str = "libmetalsharp_carbon_interpose.dylib";
const FNA_XNA_WRAPPER_VERSION: &str = "22.12.2";

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum MonoArch {
    Native,
    X86,
}

pub struct FnaGameProfile {
    pub appid: u32,
    pub mono_config: &'static str,
    pub mono_arch: MonoArch,
    pub preferred_exes: &'static [&'static str],
    pub method_label: &'static str,
    pub setup_script: Option<&'static str>,
    pub deploy_macos_steam_libs: bool,
    pub launcher_exe: Option<&'static str>,
    pub launcher_source: Option<&'static str>,
    pub deploy_terraria_post: bool,
    pub include_runtime_shims_in_library_path: bool,
}

const FNA_GAME_PROFILES: &[FnaGameProfile] = &[
    FnaGameProfile {
        appid: 105600,
        mono_config: "generic-fna-mono.config",
        mono_arch: MonoArch::X86,
        preferred_exes: &["TerrariaLauncher.exe", "Terraria.exe"],
        method_label: "xna_fna_x86",
        setup_script: Some("setup-terraria-deps.sh"),
        deploy_macos_steam_libs: true,
        launcher_exe: Some("TerrariaLauncher.exe"),
        launcher_source: Some("TerrariaLauncher.cs"),
        deploy_terraria_post: true,
        include_runtime_shims_in_library_path: true,
    },
    FnaGameProfile {
        appid: 504230,
        mono_config: "celeste-x86-mono.config",
        mono_arch: MonoArch::X86,
        preferred_exes: &[],
        method_label: "xna_fna_x86",
        setup_script: Some("setup-celeste-deps.sh"),
        deploy_macos_steam_libs: false,
        launcher_exe: None,
        launcher_source: None,
        deploy_terraria_post: false,
        include_runtime_shims_in_library_path: false,
    },
    FnaGameProfile {
        appid: 413150,
        mono_config: "stardew-mono.config",
        mono_arch: MonoArch::Native,
        preferred_exes: &[],
        method_label: "xna_fna_arm64",
        setup_script: None,
        deploy_macos_steam_libs: false,
        launcher_exe: None,
        launcher_source: None,
        deploy_terraria_post: false,
        include_runtime_shims_in_library_path: true,
    },
];

const DEFAULT_FNA_PROFILE: FnaGameProfile = FnaGameProfile {
    appid: 0,
    mono_config: "generic-fna-mono.config",
    mono_arch: MonoArch::X86,
    preferred_exes: &[],
    method_label: "xna_fna_x86",
    setup_script: None,
    deploy_macos_steam_libs: false,
    launcher_exe: None,
    launcher_source: None,
    deploy_terraria_post: false,
    include_runtime_shims_in_library_path: false,
};

pub fn find_fna_profile(appid: u32) -> &'static FnaGameProfile {
    FNA_GAME_PROFILES.iter().find(|p| p.appid == appid).unwrap_or(&DEFAULT_FNA_PROFILE)
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FnaFlavor {
    Fna,
    MonoGame,
    Xna,
    Unknown,
}

pub fn detect_fna_flavor(game_dir: &PathBuf) -> FnaFlavor {
    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.to_lowercase().ends_with("_data") && entry.path().is_dir() {
                let managed = entry.path().join("Managed");
                if !managed.exists() {
                    continue;
                }
                if let Ok(managed_entries) = std::fs::read_dir(&managed) {
                    let mut has_fna = false;
                    let mut has_monogame = false;
                    let mut has_xna = false;
                    for me in managed_entries.flatten() {
                        let dll_name = me.file_name().to_string_lossy().to_string();
                        let dll_lower = dll_name.to_lowercase();
                        if dll_lower == "fna.dll" {
                            has_fna = true;
                        }
                        if dll_lower.starts_with("monogame") || dll_lower.contains("mg.framework") {
                            has_monogame = true;
                        }
                        if dll_lower.starts_with("microsoft.xna.framework") {
                            has_xna = true;
                        }
                    }
                    if has_fna {
                        return FnaFlavor::Fna;
                    }
                    if has_monogame {
                        return FnaFlavor::MonoGame;
                    }
                    if has_xna {
                        return FnaFlavor::Xna;
                    }
                }
            }
        }
    }
    if game_dir.join("FNA.dll").exists() {
        return FnaFlavor::Fna;
    }
    FnaFlavor::Unknown
}

#[derive(Clone, Copy)]
enum FnaShimSource {
    RepoC { parts: &'static [&'static str], undefined_dynamic_lookup: bool },
    RepoObjC { parts: &'static [&'static str], frameworks: &'static [&'static str] },
    BundledNative,
}

#[derive(Clone, Copy)]
struct FnaNativeShimSpec {
    output: &'static str,
    source: FnaShimSource,
    symlinks: &'static [&'static str],
    required_for_launch: bool,
}

const FNA_NATIVE_SHIMS: &[FnaNativeShimSpec] = &[
    FnaNativeShimSpec {
        output: "libkernel32.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "shims", "kernel32_shim.c"],
            undefined_dynamic_lookup: false,
        },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: "libuser32.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "shims", "user32_shim.c"],
            undefined_dynamic_lookup: false,
        },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: FNA_CARBON_SHIM,
        source: FnaShimSource::RepoObjC {
            parts: &["src", "fna", "shims", "carbon_hiview_shim.m"],
            frameworks: &["Cocoa", "Carbon"],
        },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: FNA_CARBON_INTERPOSE_SHIM,
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "shims", "carbon_interpose.c"],
            undefined_dynamic_lookup: false,
        },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: "xaudio2_9.dylib",
        source: FnaShimSource::BundledNative,
        symlinks: &["xaudio2_8.dylib", "xaudio2_7.dylib"],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: "libSystem.Native.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "shims", "system_native_stub.c"],
            undefined_dynamic_lookup: false,
        },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: "xinput1_4.dylib",
        source: FnaShimSource::BundledNative,
        symlinks: &["xinput1_3.dylib", "xinput9_1_0.dylib"],
        required_for_launch: false,
    },
    FnaNativeShimSpec {
        output: "libgdiplus.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "terraria", "gdiplus_stub.c"],
            undefined_dynamic_lookup: false,
        },
        symlinks: &[],
        required_for_launch: false,
    },
    FnaNativeShimSpec {
        output: "libFAudio.0.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "terraria", "faudio_stub.c"],
            undefined_dynamic_lookup: false,
        },
        symlinks: &["libFAudio.dylib"],
        required_for_launch: false,
    },
    FnaNativeShimSpec {
        output: "libCSteamworks.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "shims", "csteamworks_shim.c"],
            undefined_dynamic_lookup: true,
        },
        symlinks: &[],
        required_for_launch: false,
    },
    FnaNativeShimSpec {
        output: "libfmod.dylib",
        source: FnaShimSource::RepoC { parts: &["src", "fna", "shims", "fmod_stub.c"], undefined_dynamic_lookup: true },
        symlinks: &[],
        required_for_launch: false,
    },
    FnaNativeShimSpec {
        output: "libfmodstudio.dylib",
        source: FnaShimSource::RepoC {
            parts: &["src", "fna", "shims", "fmodstudio_stub.c"],
            undefined_dynamic_lookup: true,
        },
        symlinks: &[],
        required_for_launch: false,
    },
];

pub fn bridge_port() -> u16 {
    parse_bridge_port(std::env::var("METALSHARP_STEAM_BRIDGE_PORT").ok().as_deref()).unwrap_or(DEFAULT_BRIDGE_PORT)
}

fn parse_bridge_port(value: Option<&str>) -> Option<u16> {
    let parsed = value?.parse::<u16>().ok()?;
    if parsed == 0 {
        None
    } else {
        Some(parsed)
    }
}

struct CachePaths {
    shader: String,
    pipeline: String,
}

struct LaunchLogContext<'a> {
    appid: u32,
    node: &'a PipelineNode,
    prefix: &'a Path,
    cwd: &'a Path,
    exe_name: &'a str,
    args: &'a [String],
    cache_paths: Option<&'a CachePaths>,
}

#[derive(Default)]
pub struct CustomLaunchOptions {
    pub prefix_path: Option<PathBuf>,
    pub log_path: Option<PathBuf>,
}

pub fn bridge_is_running() -> bool {
    let port = bridge_port();
    if let Ok(mut stream) =
        TcpStream::connect_timeout(&format!("127.0.0.1:{}", port).parse().unwrap(), Duration::from_millis(500))
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

    let home = dirs::home_dir().ok_or("no home dir")?;
    let port = bridge_port();
    let bridge_exe =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("steam-bridge").join("steambridge.exe");
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");

    if !bridge_exe.exists() {
        return Err("steambridge.exe not found — Wine-side Steam API bridge is not yet available".into());
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
        .env("WINEDEBUGGER", "none")
        .env("METALSHARP_STEAM_BRIDGE_PORT", port.to_string())
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
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return,
    };
    let ms_home = crate::platform::metalsharp_home_dir_for(&home);
    let shim_src = ms_home.join("runtime").join("steam-bridge").join("libsteam_api.dylib");
    let shim_fallback = ms_home.join("runtime").join("shims").join("libsteam_api.dylib");
    let src = if shim_src.exists() {
        shim_src
    } else if shim_fallback.exists() {
        shim_fallback
    } else {
        return;
    };
    let dest = game_dir.join("libsteam_api.dylib");
    if dest.exists() {
        return;
    }
    let _ = std::fs::copy(&src, &dest);
}

pub fn launch_with_pipeline(
    appid: u32,
    pipeline_id: PipelineId,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_requested_pipeline(appid, Some(pipeline_id));
    let node = get_pipeline(pipeline_id);

    prepare_steam_api_for_pipeline(appid, pipeline_id);

    match pipeline_id {
        PipelineId::Dxmt | PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 => {
            launch_dxmt_metal(appid, node)
        },
        PipelineId::M13 => launch_dxmt_metal(appid, node),
        PipelineId::D3DMetal => launch_d3dmetal_gptk(appid, node),
        PipelineId::M32 => launch_wine_bare(appid, node),
        PipelineId::FnaArm64 => launch_fna_arm64(appid).map(|(pid, method, _)| (pid, method)),
        PipelineId::Steam => launch_steam(appid),
        PipelineId::MacSteam => launch_macos_steam(appid),
        PipelineId::WineBare => launch_wine_bare(appid, node),
    }
}

pub fn launch_steam_bottle_with_pipeline(
    appid: u32,
    pipeline_id: PipelineId,
    prefix_path: &Path,
    extra_env: &[(String, String)],
) -> Result<(u32, &'static str, PathBuf), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_requested_pipeline(appid, Some(pipeline_id));
    let node = get_pipeline(pipeline_id);
    let log_path = crate::bottles::steam_compatdata_launch_log_path(appid);

    prepare_steam_api_for_pipeline(appid, pipeline_id);

    match pipeline_id {
        PipelineId::Dxmt | PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 | PipelineId::M13 => {
            launch_dxmt_metal_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
                .map(|(pid, method)| (pid, method, log_path))
        },
        PipelineId::D3DMetal => {
            launch_d3dmetal_gptk_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
                .map(|(pid, method)| (pid, method, log_path))
        },
        PipelineId::M32 | PipelineId::WineBare => {
            launch_wine_bare_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
                .map(|(pid, method)| (pid, method, log_path))
        },
        PipelineId::FnaArm64 => launch_fna_arm64(appid),
        PipelineId::Steam | PipelineId::MacSteam => Err("Steam bottle launch only supports MTSP game pipelines".into()),
    }
}

pub fn launch_auto(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    launch_with_pipeline(appid, pipeline_id)
}

pub fn prepare_pipeline(appid: u32) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let mut timing = crate::diagnostics::LaunchTiming::start();
    timing.mark("pipeline_resolution_start");
    let pipeline_id = super::rules::resolve_pipeline(appid);
    let node = get_pipeline(pipeline_id);
    timing.mark("pipeline_resolution_done");
    prepare_start_protected_game_for_pipeline(appid, pipeline_id);
    timing.mark("start_protected_game_prepare_done");
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    timing.mark("recipe_build_done");
    let deployed_sources: Vec<String> = {
        validate_recipe_runtime(&recipe)?;
        timing.mark("recipe_runtime_validate_done");
        if let Some(game_dir) = recipe.game_dir.as_ref() {
            cleanup_legacy_injections(game_dir)?;
            timing.mark("legacy_injection_cleanup_done");
        }
        let sources = recipe.dlls.iter().map(|dll| dll.source_subpath.clone()).collect();
        deploy_recipe_dlls(&recipe)?;
        timing.mark("dll_staging_done");
        let home = dirs::home_dir().ok_or("no home dir")?;
        let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
        deploy_prefix_route_dlls(&recipe, &prefix)?;
        timing.mark("prefix_route_dll_deploy_done");
        sources
    };
    timing.mark("prepare_complete");

    // Persist launch timing so performance deltas can be compared between PRs
    // via GET /diagnostics/launch/timing?appid=...
    let home_for_timing = dirs::home_dir();
    if let Some(home) = home_for_timing {
        timing.record_for_bottle(&home, &format!("steam_{}", appid));
    }

    let timing_json = timing.to_json();

    Ok(serde_json::json!({
        "ok": true,
        "appid": appid,
        "pipeline": node.id,
        "pipeline_name": node.name,
        "recipe": recipe,
        "deployed_dlls": deployed_sources.len(),
        "deployed_sources": deployed_sources,
        "timing": timing_json,
    }))
}

pub fn prepare_steam_pipeline_env(
    appid: u32,
    pipeline_id: PipelineId,
) -> Result<(Vec<(String, String)>, super::recipe::LaunchRecipe), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_requested_pipeline(appid, Some(pipeline_id));
    let node = get_pipeline(pipeline_id);
    match pipeline_id {
        PipelineId::Dxmt
        | PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M13
        | PipelineId::D3DMetal
        | PipelineId::M32
        | PipelineId::WineBare => {},
        PipelineId::FnaArm64 => {
            let recipe = super::recipe::build_launch_recipe(appid, node)?;
            validate_recipe_runtime(&recipe)?;
            return Ok((Vec::new(), recipe));
        },
        PipelineId::Steam | PipelineId::MacSteam => {
            return Err("Steam route handoff only supports Wine-backed MTSP pipelines".into());
        },
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    prepare_start_protected_game_for_pipeline(appid, pipeline_id);
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    validate_recipe_runtime(&recipe)?;
    if node.backend == "dxmt" {
        repair_metalsharp_wine_wrapper_env_order()?;
    }
    if let Some(game_dir) = recipe.game_dir.as_ref() {
        cleanup_legacy_injections(game_dir)?;
        if matches!(pipeline_id, PipelineId::M12 | PipelineId::M13) {
            crate::setup::stage_agility_sdk_for_game(appid, game_dir, &home)?;
        }
    }
    if pipeline_id == PipelineId::D3DMetal {
        if let Some(game_dir) = recipe.game_dir.as_ref() {
            cleanup_metalsharp_dlls_from_game_dir(game_dir)?;
        }
    }
    deploy_recipe_dlls(&recipe)?;
    let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
    deploy_prefix_route_dlls(&recipe, &prefix)?;

    let env = steam_pipeline_env_pairs(&home, node, appid);
    Ok((env, recipe))
}

/// Phase 3: M12 artifact and launch verification (dry-run).
///
/// Proves the M12 route would load the intended DXMT/winemetal artifacts
/// WITHOUT launching Steam or the game. This runs through the same
/// environment builder (`steam_pipeline_env_pairs`) as `launch_dxmt_metal`,
/// so the env pairs and artifact sources reported here are exactly what a
/// real M12 launch would use. Nothing is deployed or spawned.
pub fn m12_verify_dry_run(appid: u32) -> serde_json::Value {
    match dirs::home_dir() {
        Some(home) => pipeline_dry_run_for(&home, appid, Some(PipelineId::M12)),
        None => serde_json::json!({"ok": false, "appid": appid, "error": "home directory could not be resolved"}),
    }
}

/// Read-only pipeline dry-run with an explicit home (used by tests so they
/// never mutate the process-global METALSHARP_HOME).
///
/// Artifact sources are derived from the pipeline node's `deploy_dlls`
/// (resolved against the runtime wine root) so verification reflects the
/// RUNTIME readiness, independent of whether a specific game is installed.
/// The env pairs come from the same `steam_pipeline_env_pairs` builder the
/// launch path uses.
pub fn pipeline_dry_run_for(home: &Path, appid: u32, requested: Option<PipelineId>) -> serde_json::Value {
    let home = home.to_path_buf();
    let pipeline = super::rules::resolve_requested_pipeline(appid, requested);
    let node = get_pipeline(pipeline);
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");

    // Build the SAME env pairs the launch path uses (read-only).
    let env = steam_pipeline_env_pairs(&home, node, appid);
    let env_keys: std::collections::HashSet<&str> = env.iter().map(|(k, _)| k.as_str()).collect();
    let env_pairs_json: Vec<serde_json::Value> =
        env.iter().map(|(k, v)| serde_json::json!({ "key": k, "value": v })).collect();

    // Artifact sources derived from the pipeline node's deploy list, resolved
    // against the runtime wine root. Optional stubs (nvapi/nvngx/atidxx) are
    // tolerated as missing.
    let mut deploy_dlls: Vec<serde_json::Value> = Vec::new();
    let mut missing: Vec<serde_json::Value> = Vec::new();
    let mut windows_dll_dir: Option<PathBuf> = None;
    for deploy in &node.deploy_dlls {
        let source_path = ms_root.join(deploy.source_subpath).join(deploy.filename);
        if windows_dll_dir.is_none() {
            windows_dll_dir = source_path.parent().map(|p| p.to_path_buf());
        }
        let present = source_path.exists();
        let optional_stub = deploy.filename.starts_with("nvapi")
            || deploy.filename.starts_with("nvngx")
            || deploy.filename.starts_with("atidxx");
        let sha = if present { crate::diagnostics::file_sha256(&source_path) } else { None };
        let size = if present { std::fs::metadata(&source_path).ok().map(|m| m.len()) } else { None };
        deploy_dlls.push(serde_json::json!({
            "filename": deploy.filename,
            "source_subpath": deploy.source_subpath,
            "source_path": source_path.to_string_lossy(),
            "present": present,
            "optional": optional_stub,
            "sha256": sha,
            "size_bytes": size,
        }));
        if !present && !optional_stub {
            missing.push(serde_json::json!({
                "filename": deploy.filename,
                "source_subpath": deploy.source_subpath,
                "source_path": source_path.to_string_lossy(),
            }));
        }
    }

    // For the isolated M12/M13 lane, also verify the x86_64-unix sidecars that
    // winemetal requires at runtime.
    let mut unix_sidecars: Vec<serde_json::Value> = Vec::new();
    let unix_lib_dir = if matches!(pipeline, PipelineId::M12 | PipelineId::M13) {
        let dir = ms_root.join("lib").join("dxmt-m12").join("x86_64-unix");
        for sidecar in ["winemetal.so", "libc++.1.dylib", "libc++abi.1.dylib", "libunwind.1.dylib"] {
            let path = dir.join(sidecar);
            let present = path.exists();
            let sha = if present { crate::diagnostics::file_sha256(&path) } else { None };
            unix_sidecars.push(serde_json::json!({
                "filename": sidecar,
                "path": path.to_string_lossy(),
                "present": present,
                "sha256": sha,
            }));
            if !present {
                missing.push(serde_json::json!({
                    "filename": sidecar,
                    "source_path": path.to_string_lossy(),
                    "category": "unix_sidecar",
                }));
            }
        }
        Some(dir)
    } else {
        None
    };

    serde_json::json!({
        "ok": missing.is_empty(),
        "schema_version": 1,
        "dry_run": true,
        "appid": appid,
        "pipeline": pipeline,
        "pipeline_name": node.name,
        "runtime_root": ms_root.to_string_lossy(),
        "windows_dll_dir": windows_dll_dir.as_ref().map(|d| d.to_string_lossy()).unwrap_or_default(),
        "windows_dll_dir_exists": windows_dll_dir.as_ref().map(|d| d.exists()).unwrap_or(false),
        "unix_lib_dir": unix_lib_dir.as_ref().map(|d| d.to_string_lossy()),
        "unix_lib_dir_exists": unix_lib_dir.as_ref().map(|d| d.exists()),
        "deploy_dlls": deploy_dlls,
        "unix_sidecars": unix_sidecars,
        "env_pairs": env_pairs_json,
        "env_keys_present": {
            "WINEDLLOVERRIDES": env_keys.contains("WINEDLLOVERRIDES"),
            "DXMT_SHADER_CACHE_PATH": env_keys.contains("DXMT_SHADER_CACHE_PATH"),
            "DYLD_FALLBACK_LIBRARY_PATH": env_keys.contains("DYLD_FALLBACK_LIBRARY_PATH") || env_keys.contains("LD_LIBRARY_PATH"),
            "SteamAppId": env_keys.contains("SteamAppId"),
            "DXMT_WINEMETAL_UNIXLIB": env_keys.contains("DXMT_WINEMETAL_UNIXLIB"),
        },
        "missing": missing,
    })
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
            let is_optional_stub = deploy.filename.starts_with("nvapi")
                || deploy.filename.starts_with("nvngx")
                || deploy.filename.starts_with("atidxx");
            if is_optional_stub {
                continue;
            }
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
        let staged_sha256 = crate::diagnostics::file_sha256(&deploy.dest_path);
        let source_sha256 =
            if staged_sha256.is_some() { crate::diagnostics::file_sha256(&deploy.source_path) } else { None };
        manifest_dlls.push(serde_json::json!({
            "filename": deploy.filename,
            "source_path": deploy.source_path,
            "dest_path": deploy.dest_path,
            "backup_path": if backup_path.exists() { Some(backup_path) } else { None },
            "sha256": staged_sha256,
            "source_sha256": source_sha256,
            "matches_source": matches!((&staged_sha256, &source_sha256), (Some(a), Some(b)) if a == b),
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

fn deploy_prefix_route_dlls(
    recipe: &super::recipe::LaunchRecipe,
    prefix: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    if recipe.pipeline != PipelineId::M12 {
        return Ok(());
    }

    let system32 = prefix.join("drive_c").join("windows").join("system32");
    std::fs::create_dir_all(&system32)?;
    for deploy in &recipe.dlls {
        if !deploy.source_present {
            continue;
        }
        if !matches!(
            deploy.filename.as_str(),
            "d3d12.dll" | "dxgi.dll" | "dxgi_dxmt.dll" | "d3d11.dll" | "d3d10core.dll" | "winemetal.dll"
        ) {
            continue;
        }
        std::fs::copy(&deploy.source_path, system32.join(&deploy.filename))?;
    }
    Ok(())
}

fn wine_debug_value() -> String {
    std::env::var("METALSHARP_WINEDEBUG").unwrap_or_else(|_| "-all".to_string())
}

fn repair_metalsharp_wine_wrapper_env_order() -> Result<(), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wrapper = crate::platform::metalsharp_home_dir_for(&home)
        .join("runtime")
        .join("wine")
        .join("bin")
        .join("metalsharp-wine");
    if !wrapper.exists() {
        return Ok(());
    }

    let script = std::fs::read_to_string(&wrapper)?;
    let mut repaired = script.clone();
    let winedll_block = r#"if [ -n "${WINEDLLPATH:-}" ]; then
  export WINEDLLPATH="$WINEDLLPATH:$MS_LIB/wine/x86_64-windows:$MS_LIB/wine/i386-windows"
else
  export WINEDLLPATH="$MS_LIB/wine/x86_64-windows:$MS_LIB/wine/i386-windows"
fi
"#;
    if let (Some(start), Some(end)) =
        (repaired.find("export WINELOADER=\"$MS_WINE\"\n"), repaired.find("export WINEDEBUG=\"${WINEDEBUG:--all}\""))
    {
        let replace_start = start + "export WINELOADER=\"$MS_WINE\"\n".len();
        repaired.replace_range(replace_start..end, winedll_block);
    }

    let dyld_block = r#"if [ -n "${DYLD_FALLBACK_LIBRARY_PATH:-}" ]; then
  export DYLD_FALLBACK_LIBRARY_PATH="$DYLD_FALLBACK_LIBRARY_PATH:$MS_LIB:$MS_LIB/wine/x86_64-unix"
else
  export DYLD_FALLBACK_LIBRARY_PATH="$MS_LIB:$MS_LIB/wine/x86_64-unix"
fi
"#;
    if let (Some(start), Some(end)) =
        (repaired.find("export WINEDATADIR=\"$MS_ROOT/share\"\n"), repaired.find("export MS_FWD_COMPAT_GL_CTX=1"))
    {
        let replace_start = start + "export WINEDATADIR=\"$MS_ROOT/share\"\n".len();
        repaired.replace_range(replace_start..end, dyld_block);
    }

    if repaired != script {
        std::fs::write(&wrapper, repaired)?;
    }
    Ok(())
}

pub fn launch_custom_with_pipeline(
    launch_id: u32,
    game_dir: &std::path::Path,
    exe_path: &std::path::Path,
    pipeline_id: PipelineId,
    launch_args: &[String],
) -> Result<(u32, &'static str, super::recipe::LaunchRecipe), Box<dyn std::error::Error>> {
    launch_custom_with_options(launch_id, game_dir, exe_path, pipeline_id, launch_args, CustomLaunchOptions::default())
}

pub fn launch_custom_with_options(
    launch_id: u32,
    game_dir: &std::path::Path,
    exe_path: &std::path::Path,
    pipeline_id: PipelineId,
    launch_args: &[String],
    options: CustomLaunchOptions,
) -> Result<(u32, &'static str, super::recipe::LaunchRecipe), Box<dyn std::error::Error>> {
    let pipeline_id =
        if pipeline_id == PipelineId::Dxmt { super::rules::resolve_pipeline(launch_id) } else { pipeline_id };
    let node = get_pipeline(pipeline_id);
    match pipeline_id {
        PipelineId::Dxmt
        | PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M13
        | PipelineId::D3DMetal
        | PipelineId::M32
        | PipelineId::WineBare => {},
        PipelineId::FnaArm64 | PipelineId::Steam | PipelineId::MacSteam => {
            return Err("Sharp Library apps must use Auto, Wine, M9, M10, M11, M12, M13, D3DMetal, or M32".into());
        },
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = runtime_wine_binary_for_launch(&ms_root, node);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }
    if node.backend == "dxmt" {
        repair_metalsharp_wine_wrapper_env_order()?;
    }

    let mut recipe = super::recipe::build_custom_launch_recipe(launch_id, node, game_dir, Some(exe_path))?;
    recipe.launch_args.extend(launch_args.iter().cloned());
    if node.uses_winedllpath_routing() || node.deploy_dlls.is_empty() {
        validate_recipe_runtime(&recipe)?;
    } else {
        deploy_recipe_dlls(&recipe)?;
    }

    let prefix =
        options.prefix_path.unwrap_or_else(|| crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam"));
    std::fs::create_dir_all(&prefix)?;
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let exe_name = exe_path.file_name().ok_or("game exe not found")?.to_string_lossy().to_string();
    let cache_paths = build_cache_paths(&home, node, launch_id);
    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", wine_debug_value())
        .env("WINEDEBUGGER", "none");
    apply_direct_wine_runtime_env(&mut cmd, &home, &ms_root, node);
    apply_route_library_env(&mut cmd, &ms_root, &node.dyld_paths);

    if node.uses_winedllpath_routing() {
        let winedllpath = build_winedllpath(&ms_root, &node.winedllpath_dirs);
        cmd.env("WINEDLLPATH", &winedllpath);
    }

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    if node.backend == "dxmt" {
        cmd.env("DXMT_CONFIG_FILE", ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string());
        cmd.env("DXMT_WINEMETAL_UNIXLIB", dxmt_winemetal_unixlib_path(&ms_root));
    }
    cmd.env("MS_GRAPHICS_BACKEND", node.graphics_backend);
    cmd.env("WINEMSYNC", "1");
    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    if let Some(log_path) = options.log_path {
        if let Some(parent) = log_path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let mut log = OpenOptions::new().create(true).append(true).open(&log_path)?;
        writeln!(log, "launch_id={}", launch_id)?;
        writeln!(log, "pipeline={}", node.name)?;
        writeln!(log, "prefix={}", prefix.display())?;
        write_runtime_identity(&mut log, &prefix, None)?;
        writeln!(log, "cwd={}", exe_dir.display())?;
        writeln!(log, "exe={}", exe_name)?;
        writeln!(log, "args={:?}", recipe.launch_args)?;
        if let Some(cache) = cache_paths.as_ref() {
            writeln!(log, "shader_cache={}/", cache.shader)?;
            writeln!(log, "pipeline_cache={}/", cache.pipeline)?;
        }
        writeln!(log, "--- wine output ---")?;
        let stdout = log.try_clone()?;
        cmd.stdout(Stdio::from(stdout)).stderr(Stdio::from(log));
    }
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

fn gptk_ensure_dependencies() -> Result<(), Box<dyn std::error::Error>> {
    if !crate::platform::rosetta_is_installed() {
        eprintln!("d3dmetal: Installing Rosetta 2...");
        let status =
            std::process::Command::new("softwareupdate").args(["--install-rosetta", "--agree-to-license"]).status()?;
        if !status.success() {
            return Err(
                "Failed to install Rosetta 2. Install manually: softwareupdate --install-rosetta --agree-to-license"
                    .into(),
            );
        }
    }
    if !crate::platform::gptk_is_installed() {
        eprintln!("d3dmetal: Installing Game Porting Toolkit via Homebrew...");
        let status = std::process::Command::new("brew").args(["install", "game-porting-toolkit"]).status()?;
        if !status.success() {
            return Err(
                "Failed to install GPTK via Homebrew. Install manually: brew install game-porting-toolkit".into()
            );
        }
        if !crate::platform::gptk_is_installed() {
            return Err("GPTK installed via brew but wine64 binary not found at expected path".into());
        }
    }
    Ok(())
}

fn launch_d3dmetal_gptk(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    launch_d3dmetal_gptk_with_context(appid, node, None, &[], None)
}

fn launch_d3dmetal_gptk_with_context(
    appid: u32,
    node: &PipelineNode,
    prefix_override: Option<&Path>,
    extra_env: &[(String, String)],
    log_path: Option<&Path>,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    gptk_ensure_dependencies()?;

    let home = dirs::home_dir().ok_or("no home dir")?;

    if !crate::platform::gptk_prefix_ready(&home) {
        return Err("GPTK prefix is not ready — open the bottle settings and repair 'gptk_prefix' first".into());
    }
    let gptk_prefix = crate::platform::gptk_prefix_path(&home);
    crate::platform::sync_gptk_prefix(&home)?;
    let gptk_root = crate::platform::gptk_wine_root();
    let gptk_wine64 = crate::platform::gptk_wine64_binary();
    let gptk_wineserver = crate::platform::gptk_wineserver_binary();

    let default_log_path;
    let log_path = match log_path {
        Some(path) => Some(path),
        None => {
            default_log_path = mtsp_launch_log_path(appid);
            Some(default_log_path.as_path())
        },
    };

    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    apply_start_protected_game_bypass(appid, game_dir);

    let prefix = gptk_prefix;
    let prefix_str = prefix.to_string_lossy().to_string();
    let offline_mode = extra_env.iter().any(|(key, value)| key == "METALSHARP_OFFLINE_MODE" && value == "1");
    if offline_mode {
        if crate::platform::disable_gptk_steam_launcher_for_offline(&prefix)? {
            eprintln!(
                "d3dmetal offline: disabled GPTK Steam launcher at {}",
                crate::platform::gptk_disabled_steam_exe(&prefix).display()
            );
        }
        let _ = Command::new(&gptk_wineserver).env("WINEPREFIX", &prefix_str).arg("-k").status();
    } else {
        crate::platform::restore_gptk_steam_launcher(&prefix)?;
    }

    deploy_steam_appid(game_dir, appid);

    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");

    if node.uses_winedllpath_routing() {
        validate_recipe_runtime(&recipe)?;
    }
    deploy_recipe_dlls(&recipe)?;
    deploy_prefix_route_dlls(&recipe, &prefix)?;

    let cache_paths = build_cache_paths(&home, node, appid);

    let mut dyld_parts = vec![
        gptk_root.join("lib").to_string_lossy().to_string(),
        gptk_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string(),
        gptk_root.join("lib").join("external").to_string_lossy().to_string(),
    ];
    for dyld_dir in &node.dyld_paths {
        let full = ms_root.join(dyld_dir);
        dyld_parts.push(full.to_string_lossy().to_string());
    }
    let dyld = dyld_parts.join(":");

    let mut cmd = Command::new(&gptk_wine64);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", wine_debug_value())
        .env("WINEDEBUGGER", "none")
        .env("WINESERVER", &gptk_wineserver)
        .env("WINELOADER", &gptk_wine64)
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .env("MS_GRAPHICS_BACKEND", node.graphics_backend)
        .env("WINEMSYNC", "1");

    if node.uses_winedllpath_routing() {
        let winedllpath = build_winedllpath(&ms_root, &node.winedllpath_dirs);
        cmd.env("WINEDLLPATH", &winedllpath);
    }

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    apply_app_launch_env(&mut cmd, appid, node.id);
    for (key, value) in extra_env {
        cmd.env(key, value);
    }

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    attach_launch_log(
        &mut cmd,
        log_path,
        LaunchLogContext {
            appid,
            node,
            prefix: &prefix,
            cwd: exe_dir,
            exe_name: &exe_name,
            args: &recipe.launch_args,
            cache_paths: cache_paths.as_ref(),
        },
    )?;
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_dxmt_metal(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    launch_dxmt_metal_with_context(appid, node, None, &[], None)
}

fn launch_dxmt_metal_with_context(
    appid: u32,
    node: &PipelineNode,
    prefix_override: Option<&Path>,
    extra_env: &[(String, String)],
    log_path: Option<&Path>,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = runtime_wine_binary_for_launch(&ms_root, node);
    let default_log_path;
    let log_path = match log_path {
        Some(path) => Some(path),
        None => {
            default_log_path = mtsp_launch_log_path(appid);
            Some(default_log_path.as_path())
        },
    };

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }
    repair_metalsharp_wine_wrapper_env_order()?;

    prepare_start_protected_game_for_pipeline(appid, node.id);
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix = prefix_override
        .map(Path::to_path_buf)
        .unwrap_or_else(|| crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam"));
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    if node.uses_winedllpath_routing() {
        validate_recipe_runtime(&recipe)?;
        cleanup_legacy_injections(game_dir)?;
    }
    deploy_recipe_dlls(&recipe)?;
    deploy_prefix_route_dlls(&recipe, &prefix)?;

    deploy_d3d12_agility_sidecars(appid, node, game_dir)?;

    if !recipe.anti_cheat.is_empty() {
        deploy_steam_appid(game_dir, appid);
    }

    let cache_paths = build_cache_paths(&home, node, appid);
    if node.id == PipelineId::M12
        && std::env::var("METALSHARP_M12_LIVE_MSC_SIDECAR")
            .map(|value| !value.is_empty() && value != "0")
            .unwrap_or(false)
    {
        spawn_metalshaderconverter_sidecar(appid, &home, cache_paths.as_ref());
    }
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", wine_debug_value())
        .env("WINEDEBUGGER", "none");
    apply_direct_wine_runtime_env(&mut cmd, &home, &ms_root, node);
    apply_route_library_env(&mut cmd, &ms_root, &node.dyld_paths);

    if node.uses_winedllpath_routing() {
        let winedllpath = build_winedllpath(&ms_root, &node.winedllpath_dirs);
        cmd.env("WINEDLLPATH", &winedllpath);
    }

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    if node.backend == "dxmt" {
        cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);
        cmd.env("DXMT_WINEMETAL_UNIXLIB", dxmt_winemetal_unixlib_path(&ms_root));
    }

    cmd.env("MS_GRAPHICS_BACKEND", node.graphics_backend);
    cmd.env("WINEMSYNC", "1");

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }
    apply_app_launch_env(&mut cmd, appid, node.id);
    for (key, value) in extra_env {
        cmd.env(key, value);
    }

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    attach_launch_log(
        &mut cmd,
        log_path,
        LaunchLogContext {
            appid,
            node,
            prefix: &prefix,
            cwd: exe_dir,
            exe_name: &exe_name,
            args: &recipe.launch_args,
            cache_paths: cache_paths.as_ref(),
        },
    )?;
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn deploy_d3d12_agility_sidecars(
    appid: u32,
    node: &PipelineNode,
    game_dir: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    if !matches!(node.id, PipelineId::M12 | PipelineId::M13) {
        return Ok(());
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    crate::setup::stage_agility_sdk_for_game(appid, game_dir, &home)
}

fn launch_wine_bare(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    launch_wine_bare_with_context(appid, node, None, &[], None)
}

fn launch_wine_bare_with_context(
    appid: u32,
    node: &PipelineNode,
    prefix_override: Option<&Path>,
    extra_env: &[(String, String)],
    log_path: Option<&Path>,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix = prefix_override
        .map(Path::to_path_buf)
        .unwrap_or_else(|| crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam"));
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    validate_recipe_runtime(&recipe)?;

    if !recipe.anti_cheat.is_empty() {
        deploy_steam_appid(game_dir, appid);
    }

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", wine_debug_value())
        .env("WINEDEBUGGER", "none");
    apply_route_library_env(&mut cmd, &ms_root, &node.dyld_paths);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    let cache_paths = build_cache_paths(&home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    cmd.env("MS_GRAPHICS_BACKEND", node.graphics_backend);
    cmd.env("WINEMSYNC", "1");
    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }
    apply_app_launch_env(&mut cmd, appid, node.id);
    for (key, value) in extra_env {
        cmd.env(key, value);
    }

    cmd.arg(&exe_name);
    cmd.args(&recipe.launch_args);
    attach_launch_log(
        &mut cmd,
        log_path,
        LaunchLogContext {
            appid,
            node,
            prefix: &prefix,
            cwd: exe_dir,
            exe_name: &exe_name,
            args: &recipe.launch_args,
            cache_paths: cache_paths.as_ref(),
        },
    )?;
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn attach_launch_log(
    cmd: &mut Command,
    log_path: Option<&Path>,
    context: LaunchLogContext<'_>,
) -> Result<(), Box<dyn std::error::Error>> {
    let Some(log_path) = log_path else {
        return Ok(());
    };
    if let Some(parent) = log_path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let mut log = OpenOptions::new().create(true).append(true).open(log_path)?;
    writeln!(log, "appid={}", context.appid)?;
    writeln!(log, "pipeline={}", context.node.name)?;
    writeln!(log, "prefix={}", context.prefix.display())?;
    write_runtime_identity(&mut log, context.prefix, Some(context.appid))?;
    writeln!(log, "cwd={}", context.cwd.display())?;
    writeln!(log, "exe={}", context.exe_name)?;
    writeln!(log, "args={:?}", context.args)?;
    writeln!(log, "graphics_backend={}", context.node.graphics_backend)?;
    if let Some(cache) = context.cache_paths {
        writeln!(log, "shader_cache={}/", cache.shader)?;
        writeln!(log, "pipeline_cache={}/", cache.pipeline)?;
    }
    writeln!(log, "--- wine output ---")?;
    let stdout = log.try_clone()?;
    cmd.stdout(Stdio::from(stdout)).stderr(Stdio::from(log));
    Ok(())
}

fn write_runtime_identity(log: &mut dyn Write, prefix: &Path, appid: Option<u32>) -> std::io::Result<()> {
    let home = dirs::home_dir().unwrap_or_default();
    let metalsharp_home = crate::platform::metalsharp_home_dir_for(&home);
    writeln!(log, "metalsharp_home={}", metalsharp_home.display())?;
    writeln!(log, "host_abi=1.0")?;
    writeln!(log, "host_runtime={}", metalsharp_home.join("runtime").join("host").display())?;
    writeln!(log, "wine_runtime={}", metalsharp_home.join("runtime").join("wine").display())?;
    writeln!(log, "steam_bridge_port={}", bridge_port())?;
    if let Some(appid) = appid {
        let compatdata_dir = crate::bottles::steam_compatdata_dir(appid);
        writeln!(log, "compatdata={}", compatdata_dir.display())?;
        writeln!(log, "compatdata_manifest={}", crate::bottles::steam_compatdata_manifest_path(appid).display())?;
    }
    if prefix == metalsharp_home.join("prefix-steam") {
        writeln!(log, "steam_identity_mode=wine_steam_background")?;
    }
    Ok(())
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

fn launch_fna_arm64(appid: u32) -> Result<(u32, &'static str, PathBuf), Box<dyn std::error::Error>> {
    let profile = find_fna_profile(appid);
    let node = get_pipeline(PipelineId::FnaArm64);
    let game_dir = resolve_fna_game_dir(appid)?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_home = crate::platform::metalsharp_home_dir_for(&home);
    let local_dir = ms_home.join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };

    ensure_launcher_exe(appid, dir);
    deploy_fna_assemblies(appid, dir);
    if appid != 504230 {
        deploy_steam_shim(dir);
    }

    let _ = ensure_bridge_running();

    let exe = if !profile.preferred_exes.is_empty() {
        find_preferred_exe(dir, profile.preferred_exes)?
    } else {
        resolve_game_exe(dir).into()
    };

    let mono_bin = find_mono_binary_for_app(appid)?;
    let mono_config = rewrite_config_with_absolute_paths(profile.mono_config, dir)?;
    let shims_dir = find_shims_dir();
    let mono_root =
        mono_bin.parent().and_then(|p| p.parent()).map(|p| p.to_path_buf()).unwrap_or_else(|| PathBuf::from(""));
    let mono_lib = mono_root.join("lib");
    let mono_profile = mono_lib.join("mono").join("4.5");
    let mut library_paths = vec![dir.to_string_lossy().to_string()];
    if profile.include_runtime_shims_in_library_path {
        library_paths.push(shims_dir);
    }
    library_paths.push(mono_lib.to_string_lossy().to_string());
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

    let mut cmd =
        if profile.mono_arch == MonoArch::X86 && crate::platform::current() == crate::platform::HostPlatform::Macos {
            let mut arch_cmd = Command::new("arch");
            arch_cmd.arg("-x86_64").arg(&mono_bin);
            arch_cmd
        } else {
            Command::new(&mono_bin)
        };
    let mono_path = format!("{}:{}", dir.to_string_lossy(), mono_profile.to_string_lossy());
    let log_path = fna_launch_log_path(appid);
    if let Some(parent) = log_path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let stdout = OpenOptions::new().create(true).append(true).open(&log_path)?;
    let stderr = stdout.try_clone()?;

    cmd.current_dir(dir).env(runtime_lib_key, &runtime_lib_path).env("DYLD_FALLBACK_LIBRARY_PATH", &runtime_lib_path);
    if !mono_config.is_empty() {
        cmd.env("MONO_CONFIG", &mono_config);
    }
    cmd.env("MONO_ENV_OPTIONS", "--runtime=v4.0")
        .env("MONO_PATH", mono_path)
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(stderr));
    for (key, value) in fna_native_launch_env(dir) {
        cmd.env(key, value);
    }

    let cache_paths = build_cache_paths(&home, node, appid);
    apply_cache_env(
        &mut cmd,
        node,
        cache_paths.as_ref(),
        &crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine"),
    );

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.env("SteamAppId", appid.to_string());
    cmd.env("SteamGameId", appid.to_string());

    cmd.arg(&exe);

    let mut child = cmd.spawn()?;
    std::thread::sleep(Duration::from_millis(900));
    if let Some(status) = child.try_wait()? {
        let log_tail = tail_text(&log_path, 4096);
        return Err(format!("FNA/Mono/XNA launch exited early with status {}. Log: {}", status, log_tail).into());
    }
    let method = profile.method_label;
    Ok((child.id(), method, log_path))
}

#[allow(clippy::too_many_arguments)]
fn launch_fna_kickstart(
    appid: u32,
    profile: &FnaGameProfile,
    node: &PipelineNode,
    dir: &PathBuf,
    exe: &PathBuf,
    home: &PathBuf,
    ms_home: &PathBuf,
    kickstart_dir: &PathBuf,
) -> Result<(u32, &'static str, PathBuf), Box<dyn std::error::Error>> {
    let kick_bin = kickstart_dir.join("kick.bin.osx");
    let exe_name = exe.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "Game.exe".to_string());
    let bin_name = exe_name.replace(".exe", ".bin.osx");
    let game_kick = dir.join(&bin_name);

    let _ = std::fs::copy(&kick_bin, &game_kick);

    let source_libmono = kickstart_dir.join("osx").join("libmonosgen-2.0.1.dylib");
    if source_libmono.exists() {
        let _ = Command::new("/usr/bin/install_name_tool")
            .arg("-id")
            .arg("@rpath/libmonosgen-2.0.1.dylib")
            .arg(&source_libmono)
            .output();
    }

    let game_osx = dir.join("osx");
    let _ = std::fs::create_dir_all(&game_osx);

    let kick_osx = kickstart_dir.join("osx");
    if kick_osx.exists() {
        for entry in std::fs::read_dir(&kick_osx)? {
            let entry = entry?;
            let src = entry.path();
            if src.is_file() || src.is_symlink() {
                let name = entry.file_name();
                let dst = game_osx.join(&name);
                if !dst.exists() {
                    if src.is_symlink() {
                        if let Ok(target) = std::fs::read_link(&src) {
                            let _ = std::os::unix::fs::symlink(target, dst);
                        }
                    } else {
                        let _ = std::fs::copy(&src, &dst);
                    }
                }
            }
        }
        let game_libmono = game_osx.join("libmonosgen-2.0.1.dylib");
        if game_libmono.exists() {
            let _ = Command::new("/usr/bin/install_name_tool")
                .arg("-id")
                .arg("@rpath/libmonosgen-2.0.1.dylib")
                .arg(&game_libmono)
                .output();
        }
    }

    let fnalibs_dir = ms_home.join("runtime").join("fnalibs");
    if fnalibs_dir.exists() {
        for entry in std::fs::read_dir(&fnalibs_dir)? {
            let entry = entry?;
            let src = entry.path();
            if src.is_file() && !game_osx.join(entry.file_name()).exists() {
                let _ = std::fs::copy(&src, game_osx.join(entry.file_name()));
            }
        }
    }

    let shims_dir = PathBuf::from(find_shims_dir());
    deploy_fna_native_shims(&dir.to_path_buf(), &shims_dir);
    for spec in FNA_NATIVE_SHIMS {
        let shim = dir.join(spec.output);
        if shim.exists() {
            let dst = game_osx.join(spec.output);
            if !dst.exists() {
                let _ = std::fs::copy(&shim, &dst);
            }
            for symlink in spec.symlinks {
                let link = game_osx.join(symlink);
                if !link.exists() {
                    let _ = std::os::unix::fs::symlink(spec.output, link);
                }
            }
        }
    }

    let shared_libs =
        ["libsteam_api.dylib", "libCSteamworks.dylib", "libfmod.dylib", "libfmodstudio.dylib", "libnfd.dylib"];
    for lib in &shared_libs {
        let src = shims_dir.join(lib);
        if src.exists() && !game_osx.join(lib).exists() {
            let _ = std::fs::copy(&src, game_osx.join(lib));
        }
        if dir.join(lib).exists() && !game_osx.join(lib).exists() {
            let _ = std::fs::copy(dir.join(lib), game_osx.join(lib));
        }
    }

    let bcl_dlls = [
        "mscorlib.dll",
        "System.dll",
        "System.Core.dll",
        "System.Xml.dll",
        "System.Xml.Linq.dll",
        "System.Configuration.dll",
        "System.Security.dll",
        "System.Runtime.Serialization.dll",
        "System.Data.dll",
        "System.Drawing.dll",
        "Mono.Security.dll",
        "Mono.Posix.dll",
        "Microsoft.CSharp.dll",
    ];
    for dll in &bcl_dlls {
        let src = kickstart_dir.join(dll);
        if src.exists() && !dir.join(dll).exists() {
            let _ = std::fs::copy(&src, dir.join(dll));
        }
    }

    let fna_dll = kickstart_dir.join("FNA.dll");
    if fna_dll.exists() && !dir.join("FNA.dll").exists() {
        let _ = std::fs::copy(&fna_dll, dir.join("FNA.dll"));
    }

    let monoconfig_src = kickstart_dir.join("monoconfig");
    if monoconfig_src.exists() {
        let game_monoconfig = dir.join("monoconfig");
        let custom_config = find_config(profile.mono_config);
        let custom_path = PathBuf::from(&custom_config);
        if custom_path.exists() {
            let _ = std::fs::copy(&custom_path, &game_monoconfig);
        } else {
            let _ = std::fs::copy(&monoconfig_src, &game_monoconfig);
        }
    }

    let machine_config_src = kickstart_dir.join("mono").join("4.5").join("machine.config");
    let machine_config_dst = dir.join("mono").join("4.5").join("machine.config");
    if machine_config_src.exists() && !machine_config_dst.exists() {
        let _ = std::fs::create_dir_all(machine_config_dst.parent().unwrap());
        let _ = std::fs::copy(&machine_config_src, &machine_config_dst);
    }

    let log_path = fna_launch_log_path(appid);
    if let Some(parent) = log_path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let stdout = OpenOptions::new().create(true).append(true).open(&log_path)?;
    let stderr = stdout.try_clone()?;

    let mut cmd = Command::new(&game_kick);
    cmd.current_dir(dir).env("MONO_DISABLE_SHARED_AREA", "1").stdout(Stdio::from(stdout)).stderr(Stdio::from(stderr));

    for (key, value) in fna_native_launch_env(dir) {
        cmd.env(key, value);
    }

    let cache_paths = build_cache_paths(home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_home.join("runtime").join("wine"));

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.env("SteamAppId", appid.to_string());
    cmd.env("SteamGameId", appid.to_string());

    if profile.mono_arch == MonoArch::X86 && crate::platform::current() == crate::platform::HostPlatform::Macos {
        let mut arch_cmd = Command::new("arch");
        arch_cmd.arg("-x86_64").arg(&game_kick);
        arch_cmd.current_dir(dir).stdout(Stdio::from(OpenOptions::new().create(true).append(true).open(&log_path)?));
        arch_cmd.stderr(Stdio::from(OpenOptions::new().create(true).append(true).open(&log_path)?));
        for (key, value) in fna_native_launch_env(dir) {
            arch_cmd.env(key, value);
        }
        for ev in &node.env_vars {
            arch_cmd.env(ev.key, ev.value);
        }
        arch_cmd.env("SteamAppId", appid.to_string());
        arch_cmd.env("SteamGameId", appid.to_string());
        arch_cmd.env("MONO_DISABLE_SHARED_AREA", "1");
        let mut child = arch_cmd.spawn()?;
        std::thread::sleep(Duration::from_millis(900));
        if let Some(status) = child.try_wait()? {
            let log_tail = tail_text(&log_path, 4096);
            return Err(
                format!("FNA/MonoKickstart launch exited early with status {}. Log: {}", status, log_tail).into()
            );
        }
        return Ok((child.id(), profile.method_label, log_path));
    }

    let mut child = cmd.spawn()?;
    std::thread::sleep(Duration::from_millis(900));
    if let Some(status) = child.try_wait()? {
        let log_tail = tail_text(&log_path, 4096);
        return Err(format!("FNA/MonoKickstart launch exited early with status {}. Log: {}", status, log_tail).into());
    }
    Ok((child.id(), profile.method_label, log_path))
}

fn find_mono_binary_for_app(appid: u32) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let profile = find_fna_profile(appid);
    if profile.mono_arch == MonoArch::X86 {
        let mono_bin = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-x86").join("bin");
        for name in ["mono-sgen64", "mono-sgen", "mono"] {
            let candidate = mono_bin.join(name);
            if candidate.exists() {
                return Ok(candidate);
            }
        }
    }
    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/mono"),
        PathBuf::from("/usr/local/bin/mono"),
        PathBuf::from("/usr/bin/mono"),
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-arm64").join("bin").join("mono"),
    ];
    for c in candidates {
        if c.exists() {
            return Ok(c);
        }
    }
    Err("Mono not found — install Mono or use setup to install runtime support".into())
}

fn fna_launch_log_path(appid: u32) -> PathBuf {
    let ts = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs();
    crate::bottles::steam_compatdata_dir(appid).join("logs").join(format!("fna-launch-{}.log", ts))
}

fn mtsp_launch_log_path(appid: u32) -> PathBuf {
    let ts = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs();
    crate::bottles::steam_compatdata_dir(appid).join("logs").join(format!("mtsp-launch-{}.log", ts))
}

fn tail_text(path: &Path, max_bytes: usize) -> String {
    let Ok(bytes) = std::fs::read(path) else {
        return format!("{} not readable", path.display());
    };
    let start = bytes.len().saturating_sub(max_bytes);
    String::from_utf8_lossy(&bytes[start..]).trim().to_string()
}

fn build_dyld(ms_root: &PathBuf, paths: &[&str]) -> String {
    paths.iter().map(|p| ms_root.join(p).to_string_lossy().to_string()).collect::<Vec<_>>().join(":")
}

fn dxmt_winemetal_unixlib_path(_ms_root: &Path) -> String {
    "winemetal.so".to_string()
}

fn runtime_wine_binary_for_launch(ms_root: &Path, node: &PipelineNode) -> PathBuf {
    if node.id == PipelineId::M12 {
        let direct = ms_root.join("bin").join("wine");
        if direct.exists() {
            return direct;
        }
    }

    crate::platform::runtime_wine_binary(ms_root)
}

fn apply_direct_wine_runtime_env(cmd: &mut Command, home: &Path, ms_root: &Path, node: &PipelineNode) {
    for (key, value) in direct_wine_runtime_env_pairs(home, ms_root, node) {
        cmd.env(key, value);
    }
    if node.id == PipelineId::M12 {
        cmd.env_remove("MS_ROOT");
    }
}

fn direct_wine_runtime_env_pairs(home: &Path, ms_root: &Path, node: &PipelineNode) -> Vec<(String, String)> {
    if node.id != PipelineId::M12 {
        return Vec::new();
    }

    let metalsharp_home = crate::platform::metalsharp_home_dir_for(home);
    vec![
        ("CX_ROOT".to_string(), ms_root.to_string_lossy().to_string()),
        ("WINESERVER".to_string(), ms_root.join("bin").join("wineserver").to_string_lossy().to_string()),
        ("WINELOADER".to_string(), ms_root.join("bin").join("wine").to_string_lossy().to_string()),
        ("WINEDATADIR".to_string(), ms_root.join("share").to_string_lossy().to_string()),
        ("MS_FWD_COMPAT_GL_CTX".to_string(), "1".to_string()),
        ("VK_ICD_FILENAMES".to_string(), "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json".to_string()),
        ("MVK_PRESENT_MODE".to_string(), "1".to_string()),
        ("DXVK_STATE_CACHE_PATH".to_string(), metalsharp_home.join("dxvk-cache").to_string_lossy().to_string()),
        ("DXVK_LOG_PATH".to_string(), metalsharp_home.join("dxvk-logs").to_string_lossy().to_string()),
    ]
}

fn route_library_env_pairs(ms_root: &PathBuf, paths: &[&str]) -> Vec<(String, String)> {
    if paths.is_empty() {
        return Vec::new();
    }

    let path = build_dyld(ms_root, paths);
    if crate::platform::current() == crate::platform::HostPlatform::Macos {
        vec![("DYLD_LIBRARY_PATH".to_string(), path.clone()), ("DYLD_FALLBACK_LIBRARY_PATH".to_string(), path)]
    } else {
        let key = crate::platform::runtime_library_env(ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");
        vec![(key.to_string(), path)]
    }
}

fn apply_route_library_env(cmd: &mut Command, ms_root: &PathBuf, paths: &[&str]) {
    for (key, value) in route_library_env_pairs(ms_root, paths) {
        cmd.env(key, value);
    }
}

fn cleanup_metalsharp_dlls_from_game_dir(game_dir: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let backup_dir = game_dir.join(".metalsharp").join("pipeline-backup");
    std::fs::create_dir_all(&backup_dir)?;

    let dll_names = [
        "d3d12.dll",
        "d3d11.dll",
        "dxgi.dll",
        "d3d10core.dll",
        "nvapi64.dll",
        "nvngx.dll",
        "winemetal.dll",
        "metalsharp_ntdll_hook.dll",
        "dxgi_dxmt.dll",
    ];

    for dll in &dll_names {
        let src = game_dir.join(dll);
        if src.exists() {
            let dest = backup_dir.join(dll);
            let _ = std::fs::rename(&src, &dest);
        }
    }
    Ok(())
}

fn build_winedllpath(ms_root: &PathBuf, dirs: &[&str]) -> String {
    dirs.iter().map(|d| ms_root.join(d).to_string_lossy().to_string()).collect::<Vec<_>>().join(":")
}

fn cleanup_legacy_injections(game_dir: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let injection_dir = game_dir.join(".metalsharp");
    let manifest_path = injection_dir.join("injections.json");
    if !manifest_path.exists() {
        return Ok(());
    }

    let canonical_game_dir = game_dir.canonicalize().unwrap_or_else(|_| game_dir.to_path_buf());

    let manifest_str = std::fs::read_to_string(&manifest_path).unwrap_or_default();
    let manifest: serde_json::Value = match serde_json::from_str(&manifest_str) {
        Ok(v) => v,
        Err(_) => {
            let _ = std::fs::remove_dir_all(&injection_dir);
            return Ok(());
        },
    };

    let dlls = match manifest.get("dlls").and_then(|d| d.as_array()) {
        Some(d) => d,
        None => {
            let _ = std::fs::remove_dir_all(&injection_dir);
            return Ok(());
        },
    };

    let mut any_copy_failed = false;
    for dll in dlls {
        if let Some(backup) = dll.get("backup_path").and_then(|b| b.as_str()) {
            let backup_path = PathBuf::from(backup);
            let canonical_backup = match backup_path.canonicalize() {
                Ok(p) => p,
                Err(_) => continue,
            };
            if !canonical_backup.starts_with(&canonical_game_dir) {
                continue;
            }
            if let Some(dest) = dll.get("dest_path").and_then(|d| d.as_str()) {
                let dest_path = PathBuf::from(dest);
                let canonical_dest = if dest_path.exists() {
                    match dest_path.canonicalize() {
                        Ok(p) => p,
                        Err(_) => continue,
                    }
                } else {
                    match dest_path.parent().and_then(|p| p.canonicalize().ok()) {
                        Some(parent) => parent.join(dest_path.file_name().unwrap_or_default()),
                        None => continue,
                    }
                };
                if !canonical_dest.starts_with(&canonical_game_dir) {
                    continue;
                }
                if std::fs::copy(&canonical_backup, &canonical_dest).is_err() {
                    any_copy_failed = true;
                    continue;
                }
                let _ = std::fs::remove_file(&canonical_backup);
            }
        }
    }

    if !any_copy_failed {
        let _ = std::fs::remove_dir_all(&injection_dir);
    }
    Ok(())
}

fn build_cache_paths(home: &PathBuf, node: &PipelineNode, appid: u32) -> Option<CachePaths> {
    let subdir = node.shader_cache_subdir?;
    let shader_base = preferred_shader_cache_base(home, subdir, appid);
    let pipeline_base =
        crate::platform::metalsharp_home_dir_for(&home).join("pipeline-cache").join(subdir).join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_base);
    let _ = std::fs::create_dir_all(&pipeline_base);
    super::shader_cache::deploy_preset_cache(home, subdir, appid);
    Some(CachePaths {
        shader: shader_base.to_string_lossy().to_string(),
        pipeline: pipeline_base.to_string_lossy().to_string(),
    })
}

fn preferred_shader_cache_base(home: &PathBuf, subdir: &str, appid: u32) -> PathBuf {
    if appid == 1962700 && subdir == "m12" {
        let game_dirs = crate::scan::resolve_dual_game_dir(appid);
        for game_dir in [game_dirs.wine_dir.as_ref(), game_dirs.macos_dir.as_ref()].into_iter().flatten() {
            let candidate =
                game_dir.join(".metalsharp-cache").join("shader-cache").join(subdir).join(appid.to_string());
            if shader_cache_has_runtime_artifacts(&candidate) {
                return candidate;
            }
        }
    }

    crate::platform::metalsharp_home_dir_for(&home).join("shader-cache").join(subdir).join(appid.to_string())
}

fn shader_cache_has_runtime_artifacts(path: &PathBuf) -> bool {
    let Ok(entries) = std::fs::read_dir(path) else {
        return false;
    };
    entries.flatten().any(|entry| {
        entry
            .path()
            .extension()
            .and_then(|ext| ext.to_str())
            .map(|ext| matches!(ext, "metallib" | "msl" | "dxbc" | "json"))
            .unwrap_or(false)
    })
}

fn steam_pipeline_env_pairs(home: &PathBuf, node: &PipelineNode, appid: u32) -> Vec<(String, String)> {
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let cache_paths = build_cache_paths(home, node, appid);
    let appid_string = appid.to_string();
    let mut env = vec![("SteamAppId".to_string(), appid_string.clone()), ("SteamGameId".to_string(), appid_string)];

    if let Some(steam_id) = crate::steam::get_steam_id() {
        env.push(("SteamUserSteamID".to_string(), steam_id));
    }

    env.extend(direct_wine_runtime_env_pairs(home, &ms_root, node));
    env.extend(route_library_env_pairs(&ms_root, &node.dyld_paths));
    if let Some(overrides) = node.wine_overrides {
        env.push(("WINEDLLOVERRIDES".to_string(), overrides.to_string()));
    }
    if node.uses_winedllpath_routing() {
        env.push(("WINEDLLPATH".to_string(), build_winedllpath(&ms_root, &node.winedllpath_dirs)));
    }
    if node.backend == "dxmt" {
        env.push(("DXMT_CONFIG_FILE".to_string(), ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string()));
        env.push(("DXMT_WINEMETAL_UNIXLIB".to_string(), dxmt_winemetal_unixlib_path(&ms_root)));
    }
    env.push(("MS_GRAPHICS_BACKEND".to_string(), node.graphics_backend.to_string()));
    env.push(("WINEMSYNC".to_string(), "1".to_string()));
    env.extend(cache_env_pairs(node, cache_paths.as_ref(), &ms_root));
    env.extend(node.env_vars.iter().map(|ev| (ev.key.to_string(), ev.value.to_string())));
    env.extend(app_compat_env_pairs(appid, node.id));
    if let Some(recipe) = super::rules::get_game_recipe(appid) {
        for (key, value) in recipe.env {
            env.push((key, value));
        }
    }
    env
}

fn app_compat_env_pairs(appid: u32, pipeline_id: PipelineId) -> Vec<(String, String)> {
    if pipeline_id == PipelineId::M9 && is_m9_stuck_loading_title(appid) {
        return vec![
            ("DXMT_ASYNC_PIPELINE_COMPILE".to_string(), "0".to_string()),
            ("DXMT_METALFX_SPATIAL_SWAPCHAIN".to_string(), "0".to_string()),
            ("DXMT_METALFX_SPATIAL".to_string(), "0".to_string()),
            ("DXMT_CONFIG".to_string(), "d3d11.preferredMaxFrameRate=60".to_string()),
            ("METALSHARP_M9_SYNC_LOADING".to_string(), "1".to_string()),
        ];
    }

    if appid == 1962700 && pipeline_id == PipelineId::M12 {
        let mut env = vec![
            ("DXMT_DXGI_TRACE".to_string(), "1".to_string()),
            ("DXMT_WINEMETAL_DEBUG".to_string(), "1".to_string()),
            ("DXMT_D3D12_TRACE".to_string(), "1".to_string()),
            ("DXMT_D3D12_TRACE_COMPONENTS".to_string(), "Device,Queue,SwapChain,Presenter,PSO".to_string()),
            ("DXMT_D3D12_TRACE_MAX_MB".to_string(), "16".to_string()),
            ("DXMT_D3D12_TIMING_MIN_MS".to_string(), "0".to_string()),
            ("DXMT_D3D12_ENABLE_GEOMETRY_MESH".to_string(), "1".to_string()),
            ("DXMT_D3D12_FORCE_SWAPCHAIN_BLIT".to_string(), "1".to_string()),
            ("DXMT_D3D12_AUTOPRESENT_SWAPCHAIN".to_string(), "1".to_string()),
            ("DXMT_D3D12_LIVE_PRESENT".to_string(), "1".to_string()),
            ("DXMT_D3D12_REASSERT_WINDOW_HANDOFF".to_string(), "1".to_string()),
            ("DXMT_D3D12_PRESENT_LOG_INTERVAL".to_string(), "120".to_string()),
            ("DXMT_D3D12_DISABLE_RUNTIME_MSC".to_string(), "1".to_string()),
            ("DXMT_D3D12_FORCE_COLOR_WRITE_STATE".to_string(), "1".to_string()),
            ("DXMT_METALFX_SPATIAL_SWAPCHAIN".to_string(), "0".to_string()),
            ("DXMT_METALFX_SPATIAL".to_string(), "0".to_string()),
            ("DXMT_METALFX_TEMPORAL".to_string(), "0".to_string()),
            ("DXMT_CONFIG".to_string(), "d3d11.preferredMaxFrameRate=60".to_string()),
            ("DXMT_DUMP_MSL".to_string(), "1".to_string()),
        ];
        if std::env::var("METALSHARP_M12_DIAGNOSTIC_CAPTURE")
            .map(|value| !value.is_empty() && value != "0")
            .unwrap_or(false)
        {
            env.push(("DXMT_D3D12_SWAPCHAIN_READBACK".to_string(), "1".to_string()));
            env.push(("DXMT_D3D12_SWAPCHAIN_READBACK_INTERVAL".to_string(), "30".to_string()));
            env.push(("DXMT_D3D12_FINAL_RENDER_SNAPSHOT".to_string(), "1".to_string()));
            env.push(("DXMT_D3D12_LIVE_PRESENT".to_string(), "0".to_string()));
            env.push(("DXMT_D3D12_PRESENT_LOG_INTERVAL".to_string(), "30".to_string()));
        }
        if std::env::var("METALSHARP_M12_FORCE_SWAPCHAIN_COLOR")
            .map(|value| !value.is_empty() && value != "0")
            .unwrap_or(false)
        {
            env.push(("DXMT_D3D12_FORCE_SWAPCHAIN_COLOR".to_string(), "1".to_string()));
        }
        if std::env::var("METALSHARP_M12_FORCE_COLOR_WRITE_STATE")
            .map(|value| !value.is_empty() && value != "0")
            .unwrap_or(false)
        {
            env.push(("DXMT_D3D12_FORCE_COLOR_WRITE_STATE".to_string(), "1".to_string()));
        }
        if std::env::var("METALSHARP_M12_FORCE_DIAGNOSTIC_FRAGMENT")
            .map(|value| !value.is_empty() && value != "0")
            .unwrap_or(false)
        {
            env.push(("DXMT_D3D12_FORCE_DIAGNOSTIC_FRAGMENT".to_string(), "1".to_string()));
        }
        if std::env::var("METALSHARP_M12_FORCE_DIAGNOSTIC_FULLSCREEN")
            .map(|value| !value.is_empty() && value != "0")
            .unwrap_or(false)
        {
            env.push(("DXMT_D3D12_FORCE_DIAGNOSTIC_FULLSCREEN".to_string(), "1".to_string()));
        }
        return env;
    }
    Vec::new()
}

fn is_m9_stuck_loading_title(appid: u32) -> bool {
    matches!(appid, 774361 | 17410 | 49520)
}

fn apply_app_launch_env(cmd: &mut Command, appid: u32, pipeline_id: PipelineId) {
    for (key, value) in app_compat_env_pairs(appid, pipeline_id) {
        cmd.env(key, value);
    }
    if let Some(recipe) = super::rules::get_game_recipe(appid) {
        for (key, value) in recipe.env {
            cmd.env(key, value);
        }
    }
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
        ("METALSHARP_CACHE_SUMMARY".to_string(), format!("shader={};pipeline={}", shader_dir, pipeline_dir)),
        ("MTL_SHADER_CACHE_DIR".to_string(), shader_dir.clone()),
    ];

    match node.backend {
        "dxmt" => {
            env.push(("DXMT_SHADER_CACHE_PATH".to_string(), shader_dir));
            env.push(("DXMT_PIPELINE_CACHE_PATH".to_string(), pipeline_dir.clone()));
            env.push(("DXMT_LOG_PATH".to_string(), pipeline_dir));
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
            env.push(("DXMT_PIPELINE_CACHE_PATH".to_string(), pipeline_dir.clone()));
            env.push(("DXMT_LOG_PATH".to_string(), pipeline_dir));
        },
        "mono" | "macos-steam" => {
            env.push(("FNA3D_SHADER_CACHE_PATH".to_string(), shader_dir));
            env.push(("FNA3D_PIPELINE_CACHE_PATH".to_string(), pipeline_dir));
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
    let metalsharp_home = crate::platform::metalsharp_home_dir_for(&home);
    let runtime_config = metalsharp_home.join("configs").join(name);
    let candidates = vec![
        runtime_config.clone(),
        crate::platform::metalsharp_home_dir_for(&home).join("configs").join(name),
        home.join("metalsharp").join("configs").join(name),
        std::env::current_dir().unwrap_or_default().join("configs").join(name),
    ];
    for c in candidates {
        if c.exists() {
            if c != runtime_config {
                if let Some(parent) = runtime_config.parent() {
                    let _ = std::fs::create_dir_all(parent);
                }
                let _ = std::fs::copy(&c, &runtime_config);
            }
            return c.to_string_lossy().to_string();
        }
    }
    if let Some(c) = find_repo_source(&["configs", name]) {
        if let Some(parent) = runtime_config.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let _ = std::fs::copy(&c, &runtime_config);
        return c.to_string_lossy().to_string();
    }
    runtime_config.to_string_lossy().to_string()
}

fn rewrite_config_with_absolute_paths(
    template_config: &str,
    game_dir: &Path,
) -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_home = crate::platform::metalsharp_home_dir_for(&home);
    let runtime_config = ms_home.join("configs").join(template_config);

    let content = std::fs::read_to_string(&runtime_config).unwrap_or_default();
    if content.is_empty() {
        // Template not found in deployed configs — skip rewrite, return empty
        // so the caller treats it as "no config to apply"
        return Ok(String::new());
    }
    let game_dir_str = game_dir.to_string_lossy();
    let native_libs_needing_abs_path = [
        "libSDL2-2.0.0.dylib",
        "libFNA3D.0.dylib",
        "libFNA3D.dylib",
        "libFAudio.0.dylib",
        "libFAudio.dylib",
        "libfmod.dylib",
        "libfmodstudio.dylib",
    ];
    let mut rewritten = content;
    for lib in &native_libs_needing_abs_path {
        if game_dir.join(lib).exists() {
            let abs = format!("{}/{}", game_dir_str, lib);
            rewritten = rewritten.replace(&format!("target=\"{}\"", lib), &format!("target=\"{}\"", abs));
        }
    }

    let deployed_name = format!("{}.{}", template_config, game_dir.file_name().unwrap_or_default().to_string_lossy());
    let deployed_path = ms_home.join("configs").join(&deployed_name);
    std::fs::write(&deployed_path, &rewritten)?;
    Ok(deployed_path.to_string_lossy().to_string())
}

fn resolve_fna_game_dir(appid: u32) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let local_dir = crate::platform::metalsharp_home_dir_for(&home).join("games").join(appid.to_string());
    if local_dir.join(".metalsharp_prepared").exists() {
        return Ok(local_dir);
    }

    if let Some(windows_dir) = crate::setup::resolve_windows_game_dir(appid) {
        return Ok(windows_dir);
    }

    if local_dir.exists() {
        return Ok(local_dir);
    }
    if let Some(native_dir) = crate::setup::resolve_native_game_dir(appid) {
        return Err(format!(
            "FNA/Mono/XNA requires the Windows Steam install for appid {}; found only native macOS install at {}",
            appid,
            native_dir.display()
        )
        .into());
    }
    Err(format!("FNA/Mono/XNA requires a Windows Steam install; no Windows game dir found for appid {}", appid).into())
}

fn has_exe_files(dir: &PathBuf) -> bool {
    crate::scan::is_windows_game_dir(dir)
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
    let profile = find_fna_profile(appid);
    let launcher_name = match profile.launcher_exe {
        Some(n) => n,
        None => return,
    };
    let source_file = match profile.launcher_source {
        Some(s) => s,
        None => return,
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

    let mono_runtime = match profile.mono_arch {
        MonoArch::X86 => "mono-x86",
        MonoArch::Native => "mono-arm64",
    };
    let mono_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join(mono_runtime);
    let mono_bin = mono_root.join("bin").join("mono");
    let mcs_exe = home
        .join(".metalsharp")
        .join("runtime")
        .join(mono_runtime)
        .join("lib")
        .join("mono")
        .join("4.5")
        .join("mcs.exe");
    if !mono_bin.exists() || !mcs_exe.exists() {
        return;
    }

    let _ = compile_csharp_with_mono(&mono_bin, &mcs_exe, &source, &launcher, "winexe", &[]);
}

fn find_shims_dir() -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates = vec![
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("shims"),
        crate::platform::metalsharp_home_dir_for(&home).join("shims"),
    ];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("shims").to_string_lossy().to_string()
}

fn deploy_fna_assemblies(appid: u32, game_dir: &PathBuf) {
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return,
    };
    let metalsharp_home = crate::platform::metalsharp_home_dir_for(&home);
    let fna_dll = metalsharp_home.join("runtime").join("fna").join("FNA.dll");

    let shims_dir = PathBuf::from(find_shims_dir());
    let _ = std::fs::create_dir_all(&shims_dir);
    deploy_fna_native_shims(game_dir, &shims_dir);
    let mut shared_native_libs = vec![
        ("libFNA3D.dylib", None),
        ("libFNA3D.0.dylib", Some("libFNA3D.dylib")),
        ("libSDL2-2.0.0.dylib", Some("libSDL2.dylib")),
        ("libSDL2.dylib", None),
        ("libFAudio.0.dylib", Some("libFAudio.dylib")),
        ("libFAudio.dylib", None),
        ("libCSteamworks.dylib", None),
        ("libfmod.dylib", None),
        ("libfmodstudio.dylib", None),
        ("libnfd.dylib", None),
    ];
    if appid != 504230 {
        shared_native_libs.push(("libsteam_api.dylib", None));
    }

    for (lib, symlink) in shared_native_libs {
        copy_fna_native_lib(game_dir, &shims_dir, lib, symlink);
    }

    let fmod_libs: &[(&str, Option<&str>)] = &[("libfmod.dylib", None), ("libfmodstudio.dylib", None)];
    for (lib, symlink) in fmod_libs {
        let dst = game_dir.join(lib);
        if dst.exists() && !fna_native_lib_needs_refresh(lib, &dst) {
            continue;
        }
        let fmod_dir = metalsharp_home.join("runtime").join("fnalibs").join("fmod");
        if fmod_dir.join(lib).exists() {
            let _ = std::fs::copy(fmod_dir.join(lib), &dst);
            fix_dylib_install_names(&dst);
            if let Some(sym) = symlink {
                ensure_fna_symlink(game_dir, lib, sym);
            }
        }
    }

    let profile = find_fna_profile(appid);

    if profile.deploy_macos_steam_libs {
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

        let terraria_native_libs = [
            ("libFNA3D.0.dylib", Some("libFNA3D.dylib")),
            ("libSDL2-2.0.0.dylib", Some("libSDL2.dylib")),
            ("libFAudio.0.dylib", Some("libFAudio.dylib")),
            ("libsteam_api.dylib", None),
            ("libnfd.dylib", None),
        ];

        for (lib, symlink) in &terraria_native_libs {
            let src = mac_terrarria_libs.join(lib).to_string_lossy().to_string();
            if game_dir.join(lib).exists() {
                continue;
            }
            if std::path::Path::new(&src).exists() {
                let _ = std::fs::copy(std::path::Path::new(&src), game_dir.join(lib));
                if let Some(sym) = symlink {
                    ensure_fna_symlink(game_dir, lib, sym);
                }
            }
        }
    }

    let gdiplus_src =
        home.join("repos").join("metalsharp").join("src").join("fna").join("terraria").join("gdiplus_stub.c");
    if !game_dir.join("libgdiplus.dylib").exists() {
        let cached = shims_dir.join("libgdiplus.dylib");
        if cached.exists() {
            let _ = std::fs::copy(&cached, game_dir.join("libgdiplus.dylib"));
        } else if gdiplus_src.exists() {
            let mut gdi_cmd = Command::new("clang");
            gdi_cmd.arg("-shared");
            for arch_arg in fna_native_arch_args() {
                gdi_cmd.arg(arch_arg);
            }
            let _ = gdi_cmd
                .arg("-o")
                .arg(game_dir.join("libgdiplus.dylib"))
                .arg(&gdiplus_src)
                .args(["-install_name", "@loader_path/libgdiplus.dylib"])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
    }

    if profile.deploy_terraria_post {
        deploy_terraria_runtime(game_dir, &metalsharp_home);
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

    if fna_dll.exists() {
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

    let _ = std::fs::write(game_dir.join("steam_appid.txt"), appid.to_string());
    if let Err(err) = deploy_offline_steamworks_net(game_dir, &metalsharp_home) {
        eprintln!("fna: offline Steamworks deploy failed: {}", err);
    }
}

pub fn repair_fna_game_runtime_assets(appid: u32, game_dir: &Path) -> Result<usize, Box<dyn std::error::Error>> {
    if !game_dir.is_dir() {
        return Err(format!("FNA game directory is missing: {}", game_dir.display()).into());
    }

    let game_dir = game_dir.to_path_buf();
    repair_fna_native_runtime_shims()?;
    deploy_fna_assemblies(appid, &game_dir);
    deploy_offline_steamworks_net(&game_dir, &crate::platform::metalsharp_home_dir())?;

    let profile = find_fna_profile(appid);
    let mut required = vec![
        "libSystem.Native.dylib",
        "libSDL2-2.0.0.dylib",
        "libFNA3D.0.dylib",
        "libFAudio.0.dylib",
        "FNA.dll",
        "Microsoft.Xna.Framework.dll",
        "Microsoft.Xna.Framework.Game.dll",
        "Microsoft.Xna.Framework.Graphics.dll",
        "Microsoft.Xna.Framework.Audio.dll",
        "Microsoft.Xna.Framework.Input.dll",
        "Microsoft.Xna.Framework.Media.dll",
        "Microsoft.Xna.Framework.Storage.dll",
        "steam_appid.txt",
    ];
    if profile.mono_arch == MonoArch::X86 {
        required.push("libfmod.dylib");
        required.push("libfmodstudio.dylib");
    }
    if game_dir.join("Steamworks.NET.dll.metalsharp-original").exists() || game_dir.join("Steamworks.NET.dll").exists()
    {
        required.push("Steamworks.NET.dll");
    }

    let ready = required
        .iter()
        .filter(|name| {
            let path = game_dir.join(name);
            path.exists() && path.metadata().map(|metadata| metadata.len() > 0).unwrap_or(false)
        })
        .count();
    if ready != required.len() {
        return Err(
            format!("FNA game runtime repair incomplete: {}/{} required assets staged", ready, required.len()).into()
        );
    }

    for (lib, name) in [
        ("libFNA3D.0.dylib", "FNA3D"),
        ("libFAudio.0.dylib", "FAudio"),
        ("libfmod.dylib", "FMOD"),
        ("libfmodstudio.dylib", "FMOD Studio"),
    ] {
        let path = game_dir.join(lib);
        if path.exists() && !fna_native_lib_source_valid(lib, &path) {
            return Err(format!("{} runtime asset is invalid: {}", name, path.display()).into());
        }
    }
    if game_dir.join("Steamworks.NET.dll.metalsharp-original").exists()
        && !offline_steamworks_net_deployed(
            &game_dir.join("Steamworks.NET.dll"),
            &game_dir.join("Steamworks.NET.dll.metalsharp-original"),
        )
    {
        return Err(format!("offline Steamworks.NET.dll repair did not replace {}", game_dir.display()).into());
    }

    Ok(ready)
}

fn deploy_fna_native_shims(game_dir: &PathBuf, shims_dir: &PathBuf) {
    for spec in FNA_NATIVE_SHIMS {
        ensure_fna_native_shim_in_cache(spec, shims_dir);
        if matches!(spec.output, "libFAudio.0.dylib" | "libfmod.dylib" | "libfmodstudio.dylib") {
            continue;
        }
        copy_fna_native_lib(game_dir, shims_dir, spec.output, None);
        if game_dir.join(spec.output).exists() {
            for symlink in spec.symlinks {
                ensure_fna_symlink(game_dir, spec.output, symlink);
            }
        }
    }
}

pub fn repair_fna_native_runtime_shims() -> Result<usize, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_home = crate::platform::metalsharp_home_dir_for(&home);
    let shims_dir = ms_home.join("runtime").join("shims");
    let fna_dir = ms_home.join("runtime").join("fna");
    std::fs::create_dir_all(&shims_dir)?;
    std::fs::create_dir_all(&fna_dir)?;
    let refreshed = crate::installer::repair_fna_support_assets().unwrap_or(0);

    for spec in FNA_NATIVE_SHIMS {
        ensure_fna_native_shim_in_cache(spec, &shims_dir);
    }

    ensure_fna_runtime_assembly(&fna_dir, &ms_home)?;

    let fna3d_build = find_repo_source(&["src", "fna", "FNA3D", "build"])
        .unwrap_or_else(|| home.join("metalsharp").join("src").join("fna").join("FNA3D").join("build"));

    if fna3d_build.exists() {
        let src = fna3d_build.join("libFNA3D.dylib");
        if fna_native_lib_source_valid("libFNA3D.dylib", &src) {
            let _ = std::fs::copy(&src, shims_dir.join("libFNA3D.dylib"));
        }
    }

    let fnalibs_dir = ms_home.join("runtime").join("fnalibs");
    if fnalibs_dir.exists() {
        let sdl2_src = fnalibs_dir.join("libSDL2-2.0.0.dylib");
        if fna_native_lib_source_valid("libSDL2-2.0.0.dylib", &sdl2_src) {
            let dst = shims_dir.join("libSDL2-2.0.0.dylib");
            let _ = std::fs::copy(&sdl2_src, &dst);
            let _ = Command::new("/usr/bin/install_name_tool")
                .args(["-id", "@rpath/libSDL2-2.0.0.dylib"])
                .arg(&dst)
                .output();
            let symlink = shims_dir.join("libSDL2.dylib");
            if !symlink.exists() {
                let _ = std::os::unix::fs::symlink("libSDL2-2.0.0.dylib", symlink);
            }
        }
        let fna3d_src = fnalibs_dir.join("libFNA3D.0.dylib");
        if fna_native_lib_source_valid("libFNA3D.0.dylib", &fna3d_src) {
            let dst = shims_dir.join("libFNA3D.0.dylib");
            let _ = std::fs::copy(&fna3d_src, &dst);
            fix_dylib_install_names(&dst);
            let symlink = shims_dir.join("libFNA3D.dylib");
            let _ = std::fs::remove_file(&symlink);
            let _ = std::os::unix::fs::symlink("libFNA3D.0.dylib", symlink);
        }
        let faudio_src = fnalibs_dir.join("libFAudio.0.dylib");
        if fna_native_lib_source_valid("libFAudio.0.dylib", &faudio_src) {
            let dst = shims_dir.join("libFAudio.0.dylib");
            let _ = std::fs::copy(&faudio_src, &dst);
            fix_dylib_install_names(&dst);
            let symlink = shims_dir.join("libFAudio.dylib");
            let _ = std::fs::remove_file(&symlink);
            let _ = std::os::unix::fs::symlink("libFAudio.0.dylib", symlink);
        }
    }

    let all_required = fna_required_runtime_assets(&ms_home.join("runtime"));
    let present = all_required.iter().filter(|p| p.exists()).count();
    if present != all_required.len() {
        return Err(format!(
            "FNA runtime repair incomplete: {}/{} required assets staged",
            present,
            all_required.len()
        )
        .into());
    }
    for path in [
        ms_home.join("runtime").join("fnalibs").join("libFNA3D.0.dylib"),
        ms_home.join("runtime").join("fna-kickstart").join("osx").join("libFNA3D.0.dylib"),
        shims_dir.join("libFNA3D.0.dylib"),
    ] {
        if !fna_native_lib_source_valid("libFNA3D.0.dylib", &path) {
            return Err(format!("FNA3D runtime asset is not SDL2-linked: {}", path.display()).into());
        }
    }
    Ok(present + refreshed)
}

pub fn precompile_all_fna_shims() -> Result<usize, String> {
    let home = dirs::home_dir().ok_or("no home dir".to_string())?;
    let ms_home = crate::platform::metalsharp_home_dir_for(&home);
    let shims_dir = ms_home.join("runtime").join("shims");
    let _ = std::fs::create_dir_all(&shims_dir);

    let mut compiled = 0usize;
    for spec in FNA_NATIVE_SHIMS {
        let dst = shims_dir.join(spec.output);
        if dst.exists() {
            continue;
        }
        ensure_fna_native_shim_in_cache(spec, &shims_dir);
        if dst.exists() {
            compiled += 1;
        }
    }

    Ok(compiled)
}

fn fna_required_runtime_assets(runtime: &Path) -> Vec<PathBuf> {
    vec![
        runtime.join("fna-kickstart").join("kick.bin.osx"),
        runtime.join("fna-kickstart").join("FNA.dll"),
        runtime.join("fna-kickstart").join("osx").join("libmonosgen-2.0.1.dylib"),
        runtime.join("fna-kickstart").join("osx").join("libSDL2-2.0.0.dylib"),
        runtime.join("fna-kickstart").join("osx").join("libFNA3D.0.dylib"),
        runtime.join("fna-kickstart").join("osx").join("libFAudio.0.dylib"),
        runtime.join("fnalibs").join("libFNA3D.0.dylib"),
        runtime.join("fnalibs").join("libSDL2-2.0.0.dylib"),
        runtime.join("shims").join("libkernel32.dylib"),
        runtime.join("shims").join("libuser32.dylib"),
        runtime.join("shims").join(FNA_CARBON_SHIM),
        runtime.join("shims").join(FNA_CARBON_INTERPOSE_SHIM),
    ]
}

fn ensure_fna_runtime_assembly(fna_dir: &Path, ms_home: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let dst = fna_dir.join("FNA.dll");
    if file_has_payload(&dst) {
        return Ok(());
    }

    let mut candidates = Vec::new();
    if let Some(source) = find_repo_source(&["src", "fna", "FNA", "bin", "Release", "net4.0", "FNA.dll"]) {
        candidates.push(source);
    }
    if let Some(resources) = crate::platform::app_resources_dir() {
        candidates.push(resources.join("runtime").join("fna").join("FNA.dll"));
    }
    candidates.push(ms_home.join("runtime").join("redist").join("fna").join("FNA.dll"));

    if let Some(source) = candidates.into_iter().find(|path| file_has_payload(path)) {
        std::fs::copy(source, dst)?;
        return Ok(());
    }

    fetch_fna_runtime_assembly(ms_home, &dst)
}

fn fetch_fna_runtime_assembly(ms_home: &Path, dst: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let download_dir = ms_home.join("cache").join("downloads");
    std::fs::create_dir_all(&download_dir)?;
    let package_file = download_dir.join(format!("FNA-XNA-Wrapper.{}.nupkg", FNA_XNA_WRAPPER_VERSION));
    if !package_file.exists() {
        download_fna_package(&package_file)?;
    }
    extract_fna_assembly_from_package(&package_file, dst)
}

fn download_fna_package(package_file: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let url = fna_package_download_url();
    let config =
        ureq::config::Config::builder().user_agent(format!("MetalSharp/{}", env!("CARGO_PKG_VERSION"))).build();
    let agent = ureq::Agent::new_with_config(config);
    let resp = agent.get(&url).call().map_err(|err| format!("FNA runtime download failed: {}", err))?;
    let tmp_file = package_file.with_extension("nupkg.tmp");
    let mut input = resp.into_body().into_reader();
    let mut output = std::fs::File::create(&tmp_file)?;
    std::io::copy(&mut input, &mut output)?;
    std::fs::rename(tmp_file, package_file)?;
    Ok(())
}

fn fna_package_download_url() -> String {
    format!(
        "https://api.nuget.org/v3-flatcontainer/fna-xna-wrapper/{0}/fna-xna-wrapper.{0}.nupkg",
        FNA_XNA_WRAPPER_VERSION
    )
}

fn extract_fna_assembly_from_package(package_file: &Path, dst: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let file = std::fs::File::open(package_file)?;
    let mut archive = zip::ZipArchive::new(file)?;
    let mut entry = archive.by_name("lib/net45/FNA.dll")?;
    let tmp_file = dst.with_extension("dll.tmp");
    if let Some(parent) = dst.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let mut out = std::fs::File::create(&tmp_file)?;
    std::io::copy(&mut entry, &mut out)?;
    std::fs::rename(tmp_file, dst)?;
    Ok(())
}

fn ensure_fna_native_shim_in_cache(spec: &FnaNativeShimSpec, shims_dir: &PathBuf) {
    let dst = shims_dir.join(spec.output);
    if dst.exists() {
        return;
    }

    match spec.source {
        FnaShimSource::RepoC { parts, undefined_dynamic_lookup } => {
            if let Some(source) = find_fna_shim_source(parts) {
                let _ = build_fna_c_shim(&source, &dst, spec.output, &[], undefined_dynamic_lookup);
            }
        },
        FnaShimSource::RepoObjC { parts, frameworks } => {
            if let Some(source) = find_fna_shim_source(parts) {
                let _ = build_fna_c_shim(&source, &dst, spec.output, frameworks, false);
            }
        },
        FnaShimSource::BundledNative => {
            if let Some(source) = find_bundled_native_shim(spec.output) {
                let _ = std::fs::copy(source, &dst);
                codesign_fna_shim(&dst);
            }
        },
    }

    if dst.exists() {
        for symlink in spec.symlinks {
            let link = shims_dir.join(symlink);
            if !link.exists() {
                #[cfg(unix)]
                {
                    if std::fs::symlink_metadata(&link).is_ok() {
                        let _ = std::fs::remove_file(&link);
                    }
                    let _ = std::os::unix::fs::symlink(spec.output, &link);
                }
            }
        }
    }
}

fn build_fna_c_shim(
    source: &PathBuf,
    output: &PathBuf,
    install_name: &str,
    frameworks: &[&str],
    undefined_dynamic_lookup: bool,
) -> bool {
    if let Some(parent) = output.parent() {
        let _ = std::fs::create_dir_all(parent);
    }

    let mut cmd = Command::new("clang");
    cmd.arg("-shared");
    for arg in fna_native_arch_args() {
        cmd.arg(arg);
    }
    if source.extension().and_then(|ext| ext.to_str()) == Some("m") {
        cmd.arg("-fobjc-arc");
    }
    if undefined_dynamic_lookup {
        cmd.args(["-undefined", "dynamic_lookup"]);
    }
    cmd.arg("-o").arg(output).arg(source).args(["-install_name", &format!("@loader_path/{}", install_name)]);
    for framework in frameworks {
        cmd.args(["-framework", framework]);
    }
    let success = cmd
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false);
    if success {
        codesign_fna_shim(output);
    }
    success
}

fn fna_native_arch_args() -> Vec<&'static str> {
    if crate::platform::current() == crate::platform::HostPlatform::Macos {
        vec!["-arch", "arm64", "-arch", "x86_64"]
    } else {
        Vec::new()
    }
}

fn find_bundled_native_shim(name: &str) -> Option<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(resources) = crate::platform::app_resources_dir() {
        candidates.push(resources.join("scripts").join("tools").join("native").join(name));
        candidates.push(resources.join("runtime").join("shims").join(name));
    }
    if let Some(source) = find_repo_source(&["app", "native", name]) {
        candidates.push(source);
    }
    if let Some(source) = find_repo_source(&["build", name]) {
        candidates.push(source);
    }
    candidates.into_iter().find(|candidate| candidate.is_file() && file_has_payload(candidate))
}

fn find_fna_shim_source(parts: &[&str]) -> Option<PathBuf> {
    if let Some(source) = find_repo_source(parts) {
        return Some(source);
    }
    let filename = parts.last()?;
    let resources = crate::platform::app_resources_dir()?;
    let candidates = [
        resources.join("runtime").join("shim-sources").join("fna").join("shims").join(filename),
        resources.join("scripts").join("tools").join("fna").join("shims").join(filename),
    ];
    candidates.into_iter().find(|candidate| candidate.is_file())
}

fn file_has_payload(path: &Path) -> bool {
    std::fs::metadata(path).map(|metadata| metadata.len() > 0).unwrap_or(false)
}

fn codesign_fna_shim(path: &PathBuf) {
    if crate::platform::current() != crate::platform::HostPlatform::Macos || !path.exists() {
        return;
    }
    let _ = Command::new("codesign")
        .args(["--force", "-s", "-"])
        .arg(path)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
}

fn fna_native_launch_env(game_dir: &PathBuf) -> Vec<(String, String)> {
    let carbon = game_dir.join(FNA_CARBON_SHIM);
    let interpose = game_dir.join(FNA_CARBON_INTERPOSE_SHIM);
    let mut env = Vec::new();
    if carbon.exists() {
        env.push(("METALSHARP_CARBON_SHIM".to_string(), carbon.to_string_lossy().to_string()));
    }
    if interpose.exists() {
        let existing = std::env::var("DYLD_INSERT_LIBRARIES").ok();
        env.push((
            "DYLD_INSERT_LIBRARIES".to_string(),
            append_path_env(existing.as_deref(), &interpose.to_string_lossy()),
        ));
    }
    env
}

fn append_path_env(existing: Option<&str>, value: &str) -> String {
    match existing.map(str::trim).filter(|v| !v.is_empty()) {
        Some(existing) if existing.split(':').any(|item| item == value) => existing.to_string(),
        Some(existing) => format!("{}:{}", existing, value),
        None => value.to_string(),
    }
}

fn deploy_terraria_runtime(game_dir: &PathBuf, metalsharp_home: &PathBuf) {
    if let Some(source) = find_repo_source(&["src", "fna", "terraria", "XactStub.cs"]) {
        let output = game_dir.join("Microsoft.Xna.Framework.Xact.dll");
        let _ = compile_repo_csharp_to_game(&source, &output, "library", &[]);
    }

    let faudio_dst = game_dir.join("libFAudio.0.dylib");
    if !faudio_dst.exists() {
        let home = dirs::home_dir().unwrap_or_default();
        let ms_home = crate::platform::metalsharp_home_dir_for(&home);
        let cached = ms_home.join("runtime").join("shims").join("libFAudio.0.dylib");
        if cached.exists() {
            let _ = std::fs::copy(&cached, &faudio_dst);
            ensure_fna_symlink(game_dir, "libFAudio.0.dylib", "libFAudio.dylib");
        } else if let Some(source) = find_repo_source(&["src", "fna", "terraria", "faudio_stub.c"]) {
            let mut faudio_cmd = Command::new("clang");
            faudio_cmd.arg("-shared");
            for arch_arg in fna_native_arch_args() {
                faudio_cmd.arg(arch_arg);
            }
            let _ = faudio_cmd
                .arg("-o")
                .arg(&faudio_dst)
                .arg(&source)
                .args(["-install_name", "@loader_path/libFAudio.0.dylib"])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
            ensure_fna_symlink(game_dir, "libFAudio.0.dylib", "libFAudio.dylib");
        }
    }

    if let Some(source) = find_repo_source(&["src", "fna", "terraria", "TerrariaOfflinePatcher.cs"]) {
        let patcher = game_dir.join("TerrariaOfflinePatcher.exe");
        let cecil = metalsharp_home
            .join("runtime")
            .join("mono-x86")
            .join("lib")
            .join("mono")
            .join("gac")
            .join("Mono.Cecil")
            .join("0.11.1.0__0738eb9f132ed756")
            .join("Mono.Cecil.dll");
        if compile_repo_csharp_to_game(&source, &patcher, "exe", &[format!("-r:{}", cecil.to_string_lossy())]) {
            let mono = metalsharp_home.join("runtime").join("mono-x86").join("bin").join("mono");
            let terraria = game_dir.join("Terraria.exe");
            if mono.exists() && terraria.exists() {
                let _ = Command::new(&mono)
                    .current_dir(game_dir)
                    .arg(&patcher)
                    .arg(&terraria)
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .status();
            }
        }
    }
}

fn compile_repo_csharp_to_game(source: &PathBuf, output: &PathBuf, target: &str, extra_args: &[String]) -> bool {
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return false,
    };
    let mono_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-x86");
    let mono = mono_root.join("bin").join("mono");
    let mcs = mono_root.join("lib").join("mono").join("4.5").join("mcs.exe");
    compile_csharp_with_mono(&mono, &mcs, source, output, target, extra_args)
}

fn compile_csharp_with_mono(
    mono: &PathBuf,
    mcs: &PathBuf,
    source: &PathBuf,
    output: &PathBuf,
    target: &str,
    extra_args: &[String],
) -> bool {
    if !mono.exists() || !mcs.exists() || !source.exists() {
        return false;
    }
    let mut cmd = Command::new(mono);
    cmd.arg(mcs).arg(format!("-target:{}", target)).arg(format!("-out:{}", output.to_string_lossy()));
    for arg in extra_args {
        cmd.arg(arg);
    }
    cmd.arg(source).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null());
    cmd.status().map(|s| s.success()).unwrap_or(false)
}

fn copy_fna_native_lib(game_dir: &PathBuf, shims_dir: &PathBuf, lib: &str, symlink: Option<&str>) {
    let dst = game_dir.join(lib);
    if dst.exists() {
        if fna_native_lib_needs_refresh(lib, &dst) {
            let _ = std::fs::remove_file(&dst);
        } else {
            fix_dylib_install_names(&dst);
            if let Some(sym) = symlink {
                ensure_fna_symlink(game_dir, lib, sym);
            }
            return;
        }
    }

    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return,
    };
    let fnalibs_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("fnalibs");
    let fnalibs = if matches!(lib, "libfmod.dylib" | "libfmodstudio.dylib") {
        fnalibs_dir.join("fmod").join(lib)
    } else {
        fnalibs_dir.join(lib)
    };
    if fna_native_lib_source_valid(lib, &fnalibs) {
        let _ = std::fs::copy(&fnalibs, &dst);
    } else {
        let cached = shims_dir.join(lib);
        if fna_native_lib_source_valid(lib, &cached) {
            let _ = std::fs::copy(&cached, &dst);
        } else {
            let homebrew_candidates =
                [PathBuf::from(format!("/opt/homebrew/lib/{}", lib)), PathBuf::from(format!("/usr/local/lib/{}", lib))];
            for candidate in &homebrew_candidates {
                if fna_native_lib_source_valid(lib, candidate) {
                    let _ = std::fs::copy(candidate, &dst);
                    break;
                }
            }
        }
    }

    if dst.exists() {
        fix_dylib_install_names(&dst);
    }

    if let Some(sym) = symlink {
        ensure_fna_symlink(game_dir, lib, sym);
    }
}

fn fna_native_lib_needs_refresh(lib: &str, path: &Path) -> bool {
    !fna_native_lib_source_valid(lib, path)
}

pub(crate) fn fna_native_lib_source_valid(lib: &str, path: &Path) -> bool {
    if !file_has_payload(path) {
        return false;
    }
    if matches!(lib, "libfmod.dylib" | "libfmodstudio.dylib") {
        return std::fs::metadata(path).map(|metadata| metadata.len() >= 256 * 1024).unwrap_or(false);
    }
    if matches!(lib, "libFNA3D.0.dylib" | "libFNA3D.dylib" | "libFAudio.0.dylib" | "libFAudio.dylib") {
        return !dylib_depends_on(path, "libSDL3") && dylib_depends_on(path, "libSDL2");
    }
    true
}

fn dylib_depends_on(path: &Path, needle: &str) -> bool {
    if crate::platform::current() != crate::platform::HostPlatform::Macos {
        return false;
    }
    let Ok(output) = Command::new("/usr/bin/otool").args(["-L", "-arch", "x86_64"]).arg(path).output() else {
        return false;
    };
    if !output.status.success() {
        return false;
    }
    String::from_utf8_lossy(&output.stdout).contains(needle)
}

fn fix_dylib_install_names(dylib_path: &PathBuf) {
    if crate::platform::current() != crate::platform::HostPlatform::Macos {
        return;
    }
    let name = dylib_path.file_name().unwrap_or_default().to_string_lossy().to_string();
    let _ = Command::new("/usr/bin/install_name_tool")
        .args(["-id", &format!("@loader_path/{}", name)])
        .arg(dylib_path)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    if let Ok(output) = Command::new("/usr/bin/otool").args(["-L", "-arch", "x86_64"]).arg(dylib_path).output() {
        let deps = String::from_utf8_lossy(&output.stdout);
        for line in deps.lines() {
            let trimmed = line.trim();
            if trimmed.starts_with("@rpath/") {
                let dep_name = trimmed.split(' ').next().unwrap_or("");
                let dep_file = dep_name.strip_prefix("@rpath/").unwrap_or(dep_name);
                let _ = Command::new("/usr/bin/install_name_tool")
                    .args(["-change", dep_name, &format!("@loader_path/{}", dep_file)])
                    .arg(dylib_path)
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .status();
            }
        }
    }

    codesign_fna_shim(dylib_path);
}

fn ensure_fna_symlink(game_dir: &PathBuf, lib: &str, sym: &str) {
    let link = game_dir.join(sym);
    if link.exists() {
        return;
    }
    #[cfg(unix)]
    {
        if std::fs::symlink_metadata(&link).is_ok() {
            let _ = std::fs::remove_file(&link);
        }
        let _ = std::os::unix::fs::symlink(lib, link);
    }
}

fn deploy_offline_steamworks_net(game_dir: &PathBuf, metalsharp_home: &PathBuf) -> Result<(), String> {
    let steamworks = game_dir.join("Steamworks.NET.dll");
    if !steamworks.exists() {
        return Ok(());
    }
    let backup = game_dir.join("Steamworks.NET.dll.metalsharp-original");
    if !backup.exists() {
        std::fs::copy(&steamworks, &backup)
            .map_err(|e| format!("backup Steamworks.NET.dll for {}: {}", game_dir.display(), e))?;
    }

    let source = find_fna_shim_source(&["src", "fna", "shims", "SteamworksOffline.cs"])
        .ok_or_else(|| "SteamworksOffline.cs not found in repo or bundled shim sources".to_string())?;
    let output = steamworks;
    let mono_roots =
        [metalsharp_home.join("runtime").join("mono-x86"), metalsharp_home.join("runtime").join("mono-arm64")];
    for mono_root in &mono_roots {
        let mono = ["mono-sgen64", "mono-sgen", "mono"]
            .iter()
            .map(|name| mono_root.join("bin").join(name))
            .find(|path| path.exists())
            .unwrap_or_else(|| mono_root.join("bin").join("mono"));
        let mcs = mono_root.join("lib").join("mono").join("4.5").join("mcs.exe");
        if !mono.exists() || !mcs.exists() {
            continue;
        }
        let status = Command::new(&mono)
            .arg(&mcs)
            .args(["-target:library"])
            .arg(format!("-out:{}", output.to_string_lossy()))
            .arg(&source)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
        if status.map(|s| s.success()).unwrap_or(false) {
            if offline_steamworks_net_deployed(&output, &backup) {
                return Ok(());
            }
            return Err(format!("offline Steamworks.NET.dll compile did not replace {}", output.display()));
        }
    }
    Err(format!("could not compile offline Steamworks.NET.dll for {}", game_dir.display()))
}

fn offline_steamworks_net_deployed(output: &Path, backup: &Path) -> bool {
    if !file_has_payload(output) {
        return false;
    }
    let Ok(output_meta) = std::fs::metadata(output) else {
        return false;
    };
    if let Ok(backup_meta) = std::fs::metadata(backup) {
        if output_meta.len() == backup_meta.len() {
            return false;
        }
    }
    true
}

fn find_repo_source(parts: &[&str]) -> Option<PathBuf> {
    if let Ok(mut dir) = std::env::current_dir() {
        for _ in 0..8 {
            let mut candidate = dir.clone();
            for part in parts {
                candidate.push(part);
            }
            if candidate.exists() {
                return Some(candidate);
            }
            match dir.parent() {
                Some(parent) => dir = parent.to_path_buf(),
                None => break,
            }
        }
    }

    if let Ok(exe) = std::env::current_exe() {
        if let Some(mut dir) = exe.parent() {
            for _ in 0..8 {
                let mut candidate = dir.to_path_buf();
                for part in parts {
                    candidate.push(part);
                }
                if candidate.exists() {
                    return Some(candidate);
                }
                match dir.parent() {
                    Some(parent) => dir = parent,
                    None => break,
                }
            }
        }
    }

    None
}

fn deploy_goldberg(home: &PathBuf, game_dir: &PathBuf, appid: u32) {
    deploy_goldberg_internal(home, game_dir, appid);
}

fn prepare_steam_api_for_pipeline(appid: u32, pipeline_id: PipelineId) {
    let Some(home) = dirs::home_dir() else {
        return;
    };
    let Some(game_dir) = crate::setup::resolve_windows_game_dir(appid) else {
        return;
    };

    if goldberg_status_for_pipeline(&home, &game_dir, pipeline_id) {
        ensure_steam_emu_for_pipeline_if_active(&home, &game_dir, appid, pipeline_id);
    } else {
        ensure_real_steam_dlls(&home, &game_dir, appid, super::recipe::uses_steam_launch_model(appid, pipeline_id));
    }
}

fn goldberg_deploy_targets(game_dir: &Path) -> Vec<PathBuf> {
    let mut targets: Vec<PathBuf> = vec![
        game_dir.to_path_buf(),
        game_dir.join("Game"),
        game_dir.join("bin"),
        game_dir.join("Binaries").join("Win32"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("win64"),
    ];
    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let child = entry.path().join("Binaries").join("Win64");
            if child.is_dir() {
                targets.push(child);
            }
        }
    }
    targets.dedup();
    targets
}

fn push_unique_path(paths: &mut Vec<PathBuf>, candidate: PathBuf) {
    if paths.iter().any(|path| path == &candidate) {
        return;
    }
    paths.push(candidate);
}

fn push_unique_physical_path(paths: &mut Vec<PathBuf>, candidate: PathBuf) {
    let candidate_canon = std::fs::canonicalize(&candidate).ok();
    if let Some(candidate_canon) = candidate_canon.as_ref() {
        if paths.iter().any(|path| std::fs::canonicalize(path).ok().as_ref() == Some(candidate_canon)) {
            return;
        }
    } else if paths.iter().any(|path| path == &candidate) {
        return;
    }

    paths.push(candidate);
}

fn resolve_dosdevice_target(dosdevices: &Path, target: PathBuf) -> PathBuf {
    if target.is_absolute() {
        target
    } else {
        dosdevices.join(target)
    }
}

fn gptk_prefix_game_dir_aliases(prefix: &Path, game_dir: &Path) -> Vec<PathBuf> {
    let dosdevices = prefix.join("dosdevices");
    let Ok(game_dir_canon) = std::fs::canonicalize(game_dir) else {
        return Vec::new();
    };
    let Ok(entries) = std::fs::read_dir(&dosdevices) else {
        return Vec::new();
    };

    let mut aliases = Vec::new();
    for entry in entries.flatten() {
        let link_path = entry.path();
        let name = entry.file_name().to_string_lossy().to_string();
        if name.len() != 2 || !name.ends_with(':') {
            continue;
        }
        let Ok(link_target) = std::fs::read_link(&link_path) else {
            continue;
        };
        let resolved_target = resolve_dosdevice_target(&dosdevices, link_target);
        let Ok(target_canon) = std::fs::canonicalize(&resolved_target) else {
            continue;
        };
        if !game_dir_canon.starts_with(&target_canon) {
            continue;
        }
        let Ok(relative) = game_dir_canon.strip_prefix(&target_canon) else {
            continue;
        };
        push_unique_path(&mut aliases, link_path.join(relative));
    }
    aliases
}

fn goldberg_dirs_for_pipeline(home: &Path, game_dir: &Path, pipeline_id: PipelineId) -> Vec<PathBuf> {
    let mut dirs = vec![game_dir.to_path_buf()];
    if matches!(pipeline_id, PipelineId::D3DMetal) {
        let prefix = crate::platform::gptk_prefix_path(home);
        for alias in gptk_prefix_game_dir_aliases(&prefix, game_dir) {
            push_unique_physical_path(&mut dirs, alias);
        }
    }
    dirs
}

fn goldberg_deploy_settings(steam_settings: &Path, appid: u32) {
    if !steam_settings.exists() {
        let _ = std::fs::create_dir_all(steam_settings);
    }
    let _ = std::fs::write(steam_settings.join("force_steam_appid.txt"), appid.to_string());
    let _ = std::fs::write(steam_settings.join("account_name.txt"), "Player\n");
    let _ = std::fs::write(steam_settings.join("user_steam_id.txt"), "76561198000000000\n");
}

pub fn deploy_goldberg_internal(home: &PathBuf, game_dir: &PathBuf, appid: u32) {
    let emu_dir = crate::platform::metalsharp_home_dir_for(home).join("runtime").join("goldberg");
    if !emu_dir.exists() {
        eprintln!("goldberg: runtime directory not found at {}", emu_dir.display());
        return;
    }

    let targets = goldberg_deploy_targets(game_dir);
    let steamclient_dir = emu_dir.join("steamclient");
    let mut deployed_any = false;

    for target in &targets {
        if !target.exists() {
            continue;
        }

        let x86_src = emu_dir.join("x86").join("steam_api.dll");
        let x86_dst = target.join("steam_api.dll");
        if x86_src.exists() && !x86_dst.with_extension("orig").exists() {
            if x86_dst.exists() {
                let _ = std::fs::rename(&x86_dst, target.join("steam_api.dll.orig"));
            }
            let _ = std::fs::copy(&x86_src, &x86_dst);
            deployed_any = true;
        }

        let x64_src = emu_dir.join("x64").join("steam_api64.dll");
        let x64_dst = target.join("steam_api64.dll");
        if x64_src.exists() && !x64_dst.with_extension("orig").exists() {
            if x64_dst.exists() {
                let _ = std::fs::rename(&x64_dst, target.join("steam_api64.dll.orig"));
            }
            let _ = std::fs::copy(&x64_src, &x64_dst);
            deployed_any = true;
        }

        if steamclient_dir.is_dir() {
            let sc64_src = steamclient_dir.join("steamclient64.dll");
            let sc64_dst = target.join("steamclient64.dll");
            if sc64_src.exists() && !sc64_dst.with_extension("orig").exists() {
                if sc64_dst.exists() {
                    let _ = std::fs::rename(&sc64_dst, target.join("steamclient64.dll.orig"));
                }
                let _ = std::fs::copy(&sc64_src, &sc64_dst);
            }

            let sc32_src = steamclient_dir.join("steamclient.dll");
            let sc32_dst = target.join("steamclient.dll");
            if sc32_src.exists() && !sc32_dst.with_extension("orig").exists() {
                if sc32_dst.exists() {
                    let _ = std::fs::rename(&sc32_dst, target.join("steamclient.dll.orig"));
                }
                let _ = std::fs::copy(&sc32_src, &sc32_dst);
            }

            let ov64_src = steamclient_dir.join("GameOverlayRenderer64.dll");
            let ov64_dst = target.join("GameOverlayRenderer64.dll");
            if ov64_src.exists() && !ov64_dst.exists() {
                let _ = std::fs::copy(&ov64_src, &ov64_dst);
            }

            let ov32_src = steamclient_dir.join("GameOverlayRenderer.dll");
            let ov32_dst = target.join("GameOverlayRenderer.dll");
            if ov32_src.exists() && !ov32_dst.exists() {
                let _ = std::fs::copy(&ov32_src, &ov32_dst);
            }
        }
    }

    if !deployed_any {
        eprintln!("goldberg: no new DLLs deployed (all targets already have .orig backups or no source DLLs)");
    }

    goldberg_deploy_settings(&game_dir.join("steam_settings"), appid);

    for target in &targets {
        if target == game_dir {
            continue;
        }
        if !target.join("steam_api64.dll.orig").exists() && !target.join("steam_api.dll.orig").exists() {
            continue;
        }
        goldberg_deploy_settings(&target.join("steam_settings"), appid);
    }

    // Always regenerate interfaces after deployment — the DLLs must be in place first.
    generate_steam_interfaces(game_dir);
}

pub fn cleanup_goldberg(game_dir: &PathBuf) {
    let targets = goldberg_deploy_targets(game_dir);

    for target in &targets {
        if !target.exists() {
            continue;
        }

        let x86_orig = target.join("steam_api.dll.orig");
        let x86_current = target.join("steam_api.dll");
        if x86_orig.exists() {
            let _ = std::fs::rename(&x86_orig, &x86_current);
        }

        let x64_orig = target.join("steam_api64.dll.orig");
        let x64_current = target.join("steam_api64.dll");
        if x64_orig.exists() {
            let _ = std::fs::rename(&x64_orig, &x64_current);
        }

        let sc64_orig = target.join("steamclient64.dll.orig");
        let sc64_current = target.join("steamclient64.dll");
        if sc64_orig.exists() {
            let _ = std::fs::rename(&sc64_orig, &sc64_current);
        } else if sc64_current.exists() {
            let _ = std::fs::remove_file(&sc64_current);
        }

        let sc32_orig = target.join("steamclient.dll.orig");
        let sc32_current = target.join("steamclient.dll");
        if sc32_orig.exists() {
            let _ = std::fs::rename(&sc32_orig, &sc32_current);
        } else if sc32_current.exists() {
            let _ = std::fs::remove_file(&sc32_current);
        }

        let _ = std::fs::remove_file(target.join("GameOverlayRenderer64.dll"));
        let _ = std::fs::remove_file(target.join("GameOverlayRenderer.dll"));

        let ss = target.join("steam_settings");
        if ss.is_dir() && target != game_dir {
            let _ = std::fs::remove_file(ss.join("force_steam_appid.txt"));
            let _ = std::fs::remove_file(ss.join("account_name.txt"));
            let _ = std::fs::remove_file(ss.join("user_steam_id.txt"));
            let _ = std::fs::remove_file(ss.join("steam_interfaces.txt"));
            if std::fs::read_dir(&ss).map(|d| d.count()).unwrap_or(1) == 0 {
                let _ = std::fs::remove_dir(&ss);
            }
        }
    }

    let steam_settings = game_dir.join("steam_settings");
    if steam_settings.exists() {
        let _ = std::fs::remove_file(steam_settings.join("force_steam_appid.txt"));
        let _ = std::fs::remove_file(steam_settings.join("account_name.txt"));
        let _ = std::fs::remove_file(steam_settings.join("user_steam_id.txt"));
        let _ = std::fs::remove_file(steam_settings.join("steam_interfaces.txt"));
        if std::fs::read_dir(&steam_settings).map(|d| d.count()).unwrap_or(1) == 0 {
            let _ = std::fs::remove_dir(&steam_settings);
        }
    }
}

pub fn goldberg_status(game_dir: &PathBuf) -> bool {
    let targets = goldberg_deploy_targets(game_dir);

    for target in &targets {
        if !target.exists() {
            continue;
        }
        if target.join("steam_api.dll.orig").exists()
            || target.join("steam_api64.dll.orig").exists()
            || target.join("steamclient64.dll.orig").exists()
            || target.join("steamclient.dll.orig").exists()
        {
            return true;
        }
    }
    false
}

pub fn goldberg_status_for_pipeline(home: &Path, game_dir: &Path, pipeline_id: PipelineId) -> bool {
    if goldberg_status(&game_dir.to_path_buf()) {
        return true;
    }
    if !matches!(pipeline_id, PipelineId::D3DMetal) {
        return false;
    }

    let prefix = crate::platform::gptk_prefix_path(home);
    gptk_prefix_game_dir_aliases(&prefix, game_dir).iter().any(|dir| goldberg_status(&dir.to_path_buf()))
}

pub fn deploy_goldberg_for_pipeline(home: &PathBuf, game_dir: &PathBuf, appid: u32, pipeline_id: PipelineId) {
    for dir in goldberg_dirs_for_pipeline(home, game_dir, pipeline_id) {
        deploy_goldberg_internal(home, &dir, appid);
    }
}

pub fn cleanup_goldberg_for_pipeline(home: &Path, game_dir: &Path, pipeline_id: PipelineId) {
    for dir in goldberg_dirs_for_pipeline(home, game_dir, pipeline_id) {
        cleanup_goldberg(&dir);
    }
}

pub fn ensure_steam_emu_for_pipeline_if_active(home: &Path, game_dir: &Path, appid: u32, pipeline_id: PipelineId) {
    let dirs = goldberg_dirs_for_pipeline(home, game_dir, pipeline_id);
    if !dirs.iter().any(|dir| goldberg_status(&dir.to_path_buf())) {
        return;
    }

    let home_buf = home.to_path_buf();
    for dir in dirs {
        if goldberg_status(&dir.to_path_buf()) {
            ensure_steam_emu_if_active(home, &dir, appid);
        } else {
            deploy_goldberg_internal(&home_buf, &dir, appid);
        }
    }
}

pub fn ensure_steam_emu_if_active(home: &Path, game_dir: &Path, appid: u32) {
    if !goldberg_status(&game_dir.to_path_buf()) {
        return;
    }

    let emu_dir = crate::platform::metalsharp_home_dir_for(home).join("runtime").join("goldberg");
    if !emu_dir.exists() {
        eprintln!("steam_emu: toggle active but runtime not found");
        return;
    }

    let targets = goldberg_deploy_targets(game_dir);

    for target in &targets {
        if !target.exists() {
            continue;
        }

        let x64_dst = target.join("steam_api64.dll");
        let x64_orig = target.join("steam_api64.dll.orig");
        if x64_orig.exists() && !x64_dst.exists() {
            if let Ok(data) = std::fs::read(emu_dir.join("x64").join("steam_api64.dll")) {
                let _ = std::fs::write(&x64_dst, data);
                eprintln!("steam_emu: repaired steam_api64.dll in {}", target.display());
            }
        }

        let x86_dst = target.join("steam_api.dll");
        let x86_orig = target.join("steam_api.dll.orig");
        if x86_orig.exists() && !x86_dst.exists() {
            if let Ok(data) = std::fs::read(emu_dir.join("x86").join("steam_api.dll")) {
                let _ = std::fs::write(&x86_dst, data);
                eprintln!("steam_emu: repaired steam_api.dll in {}", target.display());
            }
        }

        let steamclient_dir = emu_dir.join("steamclient");
        if steamclient_dir.is_dir() {
            let sc64_dst = target.join("steamclient64.dll");
            let sc64_orig = target.join("steamclient64.dll.orig");
            if sc64_orig.exists() && !sc64_dst.exists() {
                if let Ok(data) = std::fs::read(steamclient_dir.join("steamclient64.dll")) {
                    let _ = std::fs::write(&sc64_dst, data);
                    eprintln!("steam_emu: repaired steamclient64.dll in {}", target.display());
                }
            }
        }
    }

    // Validate steam_interfaces.txt — regenerate if missing or empty.
    // This is the only repair we do at launch time; appid was set at toggle.
    let steam_settings = game_dir.join("steam_settings");
    let interfaces_file = steam_settings.join("steam_interfaces.txt");
    let needs_interfaces = !interfaces_file.exists()
        || std::fs::read_to_string(&interfaces_file).map(|content| content.trim().lines().count() < 3).unwrap_or(true);
    if needs_interfaces {
        eprintln!("steam_emu: steam_interfaces.txt missing or incomplete, regenerating");
        generate_steam_interfaces(game_dir);
    }
}

/// For non-Goldberg launches: ensure the game directory has real Steam DLLs,
/// not leftover Goldberg emulator files. Restores .orig backups if they exist,
/// or deploys from the user's Wine Steam installation.
fn ensure_real_steam_dlls(home: &Path, game_dir: &Path, appid: u32, deploy_steam_model_components: bool) {
    let targets = goldberg_deploy_targets(game_dir);
    let mut restored_any = false;

    for target in &targets {
        if !target.exists() {
            continue;
        }

        // Restore .orig backups (real Steam DLLs saved when Goldberg was toggled on).
        let x86_orig = target.join("steam_api.dll.orig");
        let x86_current = target.join("steam_api.dll");
        if x86_orig.exists() {
            // .orig exists but Goldberg was untoggled — orphaned state.
            // This shouldn't happen (cleanup_goldberg restores .orig) but
            // handle it defensively.
            let _ = std::fs::rename(&x86_orig, &x86_current);
            eprintln!("real_steam: restored orphaned steam_api.dll.orig in {}", target.display());
            restored_any = true;
        }

        let x64_orig = target.join("steam_api64.dll.orig");
        let x64_current = target.join("steam_api64.dll");
        if x64_orig.exists() {
            let _ = std::fs::rename(&x64_orig, &x64_current);
            eprintln!("real_steam: restored orphaned steam_api64.dll.orig in {}", target.display());
            restored_any = true;
        }

        let sc64_orig = target.join("steamclient64.dll.orig");
        let sc64_current = target.join("steamclient64.dll");
        if sc64_orig.exists() {
            let _ = std::fs::rename(&sc64_orig, &sc64_current);
            restored_any = true;
        }

        let sc32_orig = target.join("steamclient.dll.orig");
        let sc32_current = target.join("steamclient.dll");
        if sc32_orig.exists() {
            let _ = std::fs::rename(&sc32_orig, &sc32_current);
            restored_any = true;
        }
    }

    if restored_any {
        // Also clean up Goldberg settings dirs that shouldn't exist when
        // the emulator is off.
        let steam_settings = game_dir.join("steam_settings");
        if steam_settings.is_dir() {
            let _ = std::fs::remove_file(steam_settings.join("force_steam_appid.txt"));
            let _ = std::fs::remove_file(steam_settings.join("account_name.txt"));
            let _ = std::fs::remove_file(steam_settings.join("user_steam_id.txt"));
            let _ = std::fs::remove_file(steam_settings.join("steam_interfaces.txt"));
            if std::fs::read_dir(&steam_settings).map(|d| d.count()).unwrap_or(1) == 0 {
                let _ = std::fs::remove_dir(&steam_settings);
            }
        }
    }

    // Deploy real steam_api64.dll from the user's Wine Steam installation
    // if the game directory doesn't have one at all.
    let wine_steam_dir = crate::platform::metalsharp_home_dir_for(home)
        .join("prefix-steam")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam");
    let real_steam_api64 = wine_steam_dir.join("steam_api64.dll");
    let real_steam_api = wine_steam_dir.join("steam_api.dll");

    if real_steam_api64.exists() {
        for target in &targets {
            if !target.exists() {
                continue;
            }
            // Only deploy if no steam_api64.dll exists (game shipped without one,
            // or it was never installed via Steam).
            let dest = target.join("steam_api64.dll");
            if !dest.exists() {
                let _ = std::fs::copy(&real_steam_api64, &dest);
                eprintln!("real_steam: deployed Wine Steam steam_api64.dll to {}", target.display());
            }
        }
    }

    if real_steam_api.exists() {
        for target in &targets {
            if !target.exists() {
                continue;
            }
            let dest = target.join("steam_api.dll");
            if !dest.exists() {
                let _ = std::fs::copy(&real_steam_api, &dest);
                eprintln!("real_steam: deployed Wine Steam steam_api.dll to {}", target.display());
            }
        }
    }

    if deploy_steam_model_components {
        for filename in real_steam_model_component_names() {
            deploy_real_steam_component(&wine_steam_dir, &targets, filename);
        }
    }

    // Always ensure steam_appid.txt is correct for the real Steam runtime.
    deploy_steam_appid(game_dir, appid);
}

fn real_steam_model_component_names() -> &'static [&'static str] {
    &["steamclient64.dll", "steamclient.dll", "GameOverlayRenderer64.dll", "GameOverlayRenderer.dll"]
}

fn deploy_real_steam_component(wine_steam_dir: &Path, targets: &[PathBuf], filename: &str) {
    let source = wine_steam_dir.join(filename);
    if !source.exists() {
        return;
    }

    for target in targets {
        if !target.exists() {
            continue;
        }
        let dest = target.join(filename);
        if !dest.exists() {
            let _ = std::fs::copy(&source, &dest);
            eprintln!("real_steam: deployed Wine Steam {} to {}", filename, target.display());
        }
    }
}

fn start_protected_game_real_exe_names(appid: u32) -> &'static [&'static str] {
    match appid {
        1245620 => &["eldenring.exe"],
        1888160 => &["armoredcore6.exe"],
        _ => &[],
    }
}

fn prepare_start_protected_game_for_pipeline(appid: u32, pipeline_id: PipelineId) {
    if !matches!(pipeline_id, PipelineId::Dxmt | PipelineId::M12) {
        return;
    }
    let Some(game_dir) = crate::setup::resolve_windows_game_dir(appid) else {
        return;
    };
    apply_start_protected_game_bypass(appid, &game_dir);
}

fn apply_start_protected_game_bypass(appid: u32, game_dir: &Path) {
    let spg = match super::recipe::find_case_insensitive(game_dir, "start_protected_game.exe") {
        Some(path) => path,
        None => return,
    };
    let spg_dir = match spg.parent() {
        Some(dir) => dir,
        None => return,
    };

    if spg_dir.join("start_protected_game.old").exists() {
        return;
    }

    let real_exe = match find_start_protected_real_exe(appid, game_dir, spg_dir) {
        Some(path) => path,
        None => return,
    };

    let old = spg_dir.join("start_protected_game.old");
    if let Err(err) = std::fs::rename(&spg, &old) {
        eprintln!("start_protected_game: failed to rename {} to {}: {}", spg.display(), old.display(), err);
        return;
    }
    if let Err(err) = std::fs::copy(&real_exe, &spg) {
        eprintln!("start_protected_game: failed to copy {} to {}: {}", real_exe.display(), spg.display(), err);
        let _ = std::fs::rename(&old, &spg);
    }
}

fn find_start_protected_real_exe(appid: u32, game_dir: &Path, spg_dir: &Path) -> Option<PathBuf> {
    for real_exe_name in start_protected_game_real_exe_names(appid) {
        if let Some(path) = super::recipe::find_case_insensitive(game_dir, real_exe_name) {
            return Some(path);
        }
    }

    let candidates = WalkDir::new(spg_dir)
        .max_depth(1)
        .into_iter()
        .flatten()
        .filter_map(|entry| {
            let path = entry.path();
            if !path.is_file() {
                return None;
            }
            let name = path.file_name()?.to_string_lossy().to_string();
            if is_start_protected_real_exe_candidate(&name) {
                Some(path.to_path_buf())
            } else {
                None
            }
        })
        .collect::<Vec<_>>();

    if candidates.len() == 1 {
        candidates.into_iter().next()
    } else {
        None
    }
}

fn is_start_protected_real_exe_candidate(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    lower.ends_with(".exe")
        && lower != "start_protected_game.exe"
        && !lower.contains("easyanticheat")
        && !lower.contains("setup")
        && !lower.contains("redist")
        && !lower.contains("installer")
        && !lower.contains("uninstall")
        && !lower.contains("crash")
        && !lower.contains("launcher")
}

fn generate_steam_interfaces(game_dir: &Path) {
    let steam_settings = game_dir.join("steam_settings");
    let interfaces_file = steam_settings.join("steam_interfaces.txt");

    // Don't bail early if the file exists but is empty or nearly empty.
    let existing_ok = interfaces_file.exists()
        && std::fs::read_to_string(&interfaces_file).map(|c| c.trim().lines().count() >= 3).unwrap_or(false);
    if existing_ok {
        return;
    }

    let mut candidates: Vec<PathBuf> = vec![
        game_dir.join("steam_api64.dll.orig"),
        game_dir.join("steam_api.dll.orig"),
        game_dir.join("Game").join("steam_api64.dll.orig"),
        game_dir.join("Game").join("steam_api.dll.orig"),
        game_dir.join("Binaries").join("Win64").join("steam_api64.dll.orig"),
        game_dir.join("Binaries").join("Win32").join("steam_api.dll.orig"),
        game_dir.join("bin").join("steam_api64.dll.orig"),
        game_dir.join("win64").join("steam_api64.dll.orig"),
    ];

    // Also try the current (non-.orig) DLLs — the Goldberg emulator DLL itself
    // ships the same Steam interface exports.
    candidates.push(game_dir.join("steam_api64.dll"));
    candidates.push(game_dir.join("steam_api.dll"));
    candidates.push(game_dir.join("Game").join("steam_api64.dll"));
    candidates.push(game_dir.join("bin").join("steam_api64.dll"));

    let dll_path = match candidates.iter().find(|p| p.exists()) {
        Some(p) => p,
        None => return,
    };

    let data = match std::fs::read(dll_path) {
        Ok(d) => d,
        Err(_) => return,
    };

    let interfaces = extract_steam_interfaces(&data);
    if interfaces.is_empty() {
        return;
    }

    let content = interfaces.join("\n") + "\n";
    let _ = std::fs::create_dir_all(&steam_settings);
    let _ = std::fs::write(&interfaces_file, content);
    eprintln!("goldberg: generated steam_interfaces.txt ({} interfaces) from {}", interfaces.len(), dll_path.display());
}

fn extract_steam_interfaces(data: &[u8]) -> Vec<String> {
    let pattern = b"Steam";
    let mut interfaces = Vec::new();
    let mut seen = std::collections::HashSet::new();

    for i in 0..data.len().saturating_sub(8) {
        if data[i] != b'S' {
            continue;
        }
        if &data[i..i + 5] != pattern {
            continue;
        }

        let end = data[i..].iter().position(|&b| b == 0).unwrap_or(0);
        if !(6..=80).contains(&end) {
            continue;
        }
        let s = match std::str::from_utf8(&data[i..i + end]) {
            Ok(s) => s,
            Err(_) => continue,
        };

        if !s.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '.') {
            continue;
        }
        if !s.chars().any(|c| c.is_ascii_digit()) {
            continue;
        }
        let lower = s.to_ascii_lowercase();
        if !lower.starts_with("steam")
            || lower.contains("steamapps")
            || lower.contains("steam.dll")
            || lower.contains("steam_api")
            || lower.contains("steamclient")
            || lower.contains("steamgame")
            || lower.contains("steamoverlay")
            || lower.contains("steam_appid")
            || lower.contains("steamworks")
        {
            continue;
        }

        if seen.insert(s.to_string()) {
            interfaces.push(s.to_string());
        }
    }

    interfaces.sort();
    interfaces
}

pub fn deploy_steam_appid(game_dir: &Path, appid: u32) {
    let targets: Vec<PathBuf> = vec![
        game_dir.join("Game"),
        game_dir.join("Binaries").join("Win64"),
        game_dir.join("bin"),
        game_dir.join("win64"),
        game_dir.to_path_buf(),
    ];

    for dir in &targets {
        if dir.is_dir() {
            let appid_file = dir.join("steam_appid.txt");
            let _ = std::fs::write(&appid_file, appid.to_string());
        }
    }

    // If Goldberg toggle is active, also update force_steam_appid.txt so the
    // emulator sees the correct appid at launch.
    let goldberg_settings = game_dir.join("steam_settings").join("force_steam_appid.txt");
    if goldberg_settings.exists() {
        let current = std::fs::read_to_string(&goldberg_settings).unwrap_or_default();
        if current.trim() != appid.to_string() {
            let _ = std::fs::write(&goldberg_settings, appid.to_string());
            eprintln!(
                "deploy_steam_appid: updated goldberg force_steam_appid.txt from {:?} to {}",
                current.trim(),
                appid
            );
        }
    }
}

pub fn deploy_eac_toggle(game_dir: &PathBuf) {
    let home = dirs::home_dir().unwrap_or_default();
    let eac_dir =
        crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("eac-toggle").join("x86_64-windows");
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

fn spawn_metalshaderconverter_sidecar(appid: u32, home: &Path, cache_paths: Option<&CachePaths>) {
    let tool_candidates = [
        PathBuf::from("/usr/local/bin/metal-shaderconverter"),
        PathBuf::from("/opt/homebrew/bin/metal-shaderconverter"),
        PathBuf::from("/opt/metal-shaderconverter/bin/metal-shaderconverter"),
    ];
    let Some(tool_path) = tool_candidates.into_iter().find(|path| path.exists()) else {
        return;
    };

    let log_dir = cache_paths
        .map(|cache| PathBuf::from(&cache.pipeline))
        .unwrap_or_else(|| crate::platform::metalsharp_home_dir_for(&home).join("logs"));
    let _ = std::fs::create_dir_all(&log_dir);
    let log_path = log_dir.join(format!("d3d12-metalshaderconverter-{}.log", appid));
    let cache_dir = cache_paths
        .map(|cache| PathBuf::from(&cache.shader))
        .unwrap_or_else(|| PathBuf::from("/tmp/dxmt_shader_cache"));
    if let Ok(entries) = std::fs::read_dir(&cache_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
                continue;
            };
            if name.ends_with(".msc.fail") || name.ends_with(".msc.lock") {
                let _ = std::fs::remove_file(path);
            }
        }
    }

    std::thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(180);
        let launch_start = SystemTime::now();
        let max_active_compiles = 4usize;

        while Instant::now() < deadline {
            if let Ok(entries) = std::fs::read_dir(&cache_dir) {
                let mut pending = Vec::new();
                let mut active_locks = 0usize;
                for entry in entries.flatten() {
                    let path = entry.path();
                    if path.extension().and_then(|ext| ext.to_str()) == Some("lock")
                        && path.file_name().and_then(|name| name.to_str()).map(|name| name.ends_with(".msc.lock"))
                            == Some(true)
                    {
                        active_locks += 1;
                    }
                    if path.extension().and_then(|ext| ext.to_str()) != Some("dxbc") {
                        continue;
                    }
                    let Ok(modified) = entry.metadata().and_then(|meta| meta.modified()) else {
                        continue;
                    };
                    if modified + Duration::from_secs(2) < launch_start {
                        continue;
                    }
                    pending.push((modified, path));
                }

                pending.sort_by_key(|entry| std::cmp::Reverse(entry.0));

                for (_modified, path) in pending {
                    if active_locks >= max_active_compiles {
                        break;
                    }
                    let metallib_path = path.with_extension("metallib");
                    let reflection_path = path.with_extension("json");
                    let vertex_layout_path = path.with_extension("vertex-layout.json");
                    let is_geometry_mesh_shader = path
                        .file_name()
                        .and_then(|name| name.to_str())
                        .map(|name| name.ends_with(".geom.gsmesh.dxbc"))
                        .unwrap_or(false);
                    let use_gs_ts_emulation = vertex_layout_path.exists() && !is_geometry_mesh_shader;
                    let stage_in_path = path.with_extension("stageIn.metallib");
                    let fail_path = path.with_extension("msc.fail");
                    if metallib_path.exists()
                        && reflection_path.exists()
                        && (!use_gs_ts_emulation || stage_in_path.exists())
                    {
                        continue;
                    }
                    if fail_path.exists() {
                        continue;
                    }

                    let lock_path = path.with_extension("msc.lock");
                    let lock_file = OpenOptions::new().write(true).create_new(true).open(&lock_path);
                    let Ok(_lock_guard) = lock_file else {
                        continue;
                    };
                    active_locks += 1;

                    let tool_path = tool_path.clone();
                    let log_path = log_path.clone();
                    std::thread::spawn(move || {
                        let mut log = OpenOptions::new().create(true).append(true).open(&log_path).ok();
                        if let Some(log) = log.as_mut() {
                            let _ = writeln!(log, "compile_start dxbc={}", path.display());
                        }

                        let mut command = Command::new(&tool_path);
                        command
                            .arg("-o")
                            .arg(&metallib_path)
                            .arg(&path)
                            .arg(format!("--output-reflection-file={}", reflection_path.display()))
                            .arg("--deployment-os=macOS")
                            .arg("--minimum-os-build-version=15.0.0");
                        if use_gs_ts_emulation {
                            command
                                .arg("--enable-gs-ts-emulation")
                                .arg("--vertex-stage-in")
                                .arg(format!("--vertex-input-layout-file={}", vertex_layout_path.display()));
                        }
                        let output = command.output();

                        match output {
                            Ok(result) => {
                                let stdout_text = String::from_utf8_lossy(&result.stdout);
                                let stderr_text = String::from_utf8_lossy(&result.stderr);
                                if result.status.success() {
                                    let _ = std::fs::remove_file(&fail_path);
                                } else {
                                    let failure_summary = if stderr_text.trim().is_empty() {
                                        format!("metal-shaderconverter failed with {}", result.status)
                                    } else {
                                        stderr_text.trim().to_string()
                                    };
                                    let _ = std::fs::write(&fail_path, failure_summary);
                                }
                                if let Some(log) = log.as_mut() {
                                    let _ = writeln!(
                                        log,
                                        "compile_end dxbc={} status={} metallib={} reflection={} gs_ts_emulation={}",
                                        path.display(),
                                        result.status,
                                        metallib_path.exists(),
                                        reflection_path.exists(),
                                        use_gs_ts_emulation
                                    );
                                    if !stdout_text.is_empty() {
                                        let _ = log.write_all(stdout_text.as_bytes());
                                    }
                                    if !stderr_text.is_empty() {
                                        let _ = log.write_all(stderr_text.as_bytes());
                                    }
                                }
                            },
                            Err(err) => {
                                let _ = std::fs::write(
                                    &fail_path,
                                    format!("metal-shaderconverter invocation failed: {}", err),
                                );
                                if let Some(log) = log.as_mut() {
                                    let _ = writeln!(log, "compile_error dxbc={} err={}", path.display(), err);
                                }
                            },
                        }

                        let _ = std::fs::remove_file(&lock_path);
                    });
                }
            }

            std::thread::sleep(Duration::from_millis(100));
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mtsp::recipe;

    #[test]
    fn deploy_steam_appid_writes_visible_appid_file_for_each_known_subdir() {
        // Phase 2: routes that depend on Steam-visible identity must stage
        // steam_appid.txt into every standard binary subdir the game may
        // launch from, plus update an active Goldberg force_steam_appid.txt.
        let game_dir = test_dir("deploy-steam-appid");
        let sub_win64 = game_dir.join("Binaries").join("Win64");
        let sub_bin = game_dir.join("bin");
        std::fs::create_dir_all(&sub_win64).unwrap();
        std::fs::create_dir_all(&sub_bin).unwrap();
        // "Game" subdir absent on purpose: it must be skipped, not panic.

        deploy_steam_appid(&game_dir, 504230);

        assert_eq!(std::fs::read_to_string(sub_win64.join("steam_appid.txt")).unwrap(), "504230");
        assert_eq!(std::fs::read_to_string(sub_bin.join("steam_appid.txt")).unwrap(), "504230");
        assert_eq!(std::fs::read_to_string(game_dir.join("steam_appid.txt")).unwrap(), "504230");
        assert!(!game_dir.join("Game").join("steam_appid.txt").exists(), "absent subdirs must be skipped");

        // An active Goldberg emulator setting must be kept in sync.
        let goldberg_dir = game_dir.join("steam_settings");
        std::fs::create_dir_all(&goldberg_dir).unwrap();
        std::fs::write(goldberg_dir.join("force_steam_appid.txt"), "0").unwrap();
        deploy_steam_appid(&game_dir, 504230);
        assert_eq!(
            std::fs::read_to_string(goldberg_dir.join("force_steam_appid.txt")).unwrap(),
            "504230",
            "goldberg force_steam_appid.txt must track the real appid"
        );
        let _ = std::fs::remove_dir_all(&game_dir);
    }

    #[test]
    fn m12_pipeline_deploy_list_includes_d3d12_and_uses_isolated_dxmt_m12_surface() {
        // Phase 3 contract: M12 must deploy d3d12.dll (plus dxgi/d3d11/
        // d3d10core/winemetal) from the isolated lib/dxmt-m12 surface.
        let node = get_pipeline(PipelineId::M12);
        let filenames: Vec<&str> = node.deploy_dlls.iter().map(|d| d.filename).collect();
        for required in ["d3d12.dll", "dxgi.dll", "d3d11.dll", "d3d10core.dll", "winemetal.dll"] {
            assert!(filenames.contains(&required), "M12 deploy list must include {} (got {:?})", required, filenames);
        }
        for deploy in &node.deploy_dlls {
            if matches!(
                deploy.filename,
                "d3d12.dll" | "d3d11.dll" | "dxgi.dll" | "d3d10core.dll" | "winemetal.dll" | "dxgi_dxmt.dll"
            ) {
                assert!(
                    deploy.source_subpath.starts_with("lib/dxmt-m12/"),
                    "M12 DLL {} must come from lib/dxmt-m12, got {}",
                    deploy.filename,
                    deploy.source_subpath
                );
            }
        }
    }

    #[test]
    fn m11_pipeline_deploy_list_does_not_include_d3d12_and_uses_legacy_dxmt_surface() {
        // Phase 3 contract: M11 must NOT deploy d3d12.dll and must point at the
        // legacy lib/dxmt surface, never lib/dxmt-m12.
        let node = get_pipeline(PipelineId::M11);
        let filenames: Vec<&str> = node.deploy_dlls.iter().map(|d| d.filename).collect();
        assert!(!filenames.contains(&"d3d12.dll"), "M11 deploy list must NOT include d3d12.dll (got {:?})", filenames);
        for deploy in &node.deploy_dlls {
            assert!(
                !deploy.source_subpath.starts_with("lib/dxmt-m12/"),
                "M11 DLL {} must not come from lib/dxmt-m12 (got {})",
                deploy.filename,
                deploy.source_subpath
            );
        }
    }

    #[test]
    fn m12_dry_run_includes_d3d12_dll_and_m11_dry_run_does_not() {
        // Phase 3 contract: the dry-run verifier's deploy list must reflect
        // the pipeline node. M12 dry-run includes d3d12.dll; M11 does not.
        // Uses an explicit temp home so no global env is mutated.
        let home = std::env::temp_dir().join("ms-m12-dryrun-contract");
        let _ = std::fs::remove_dir_all(&home);
        std::fs::create_dir_all(&home).unwrap();

        let m12 = pipeline_dry_run_for(&home, 2379780, Some(PipelineId::M12));
        let m11 = pipeline_dry_run_for(&home, 17300, Some(PipelineId::M11));

        let m12_filenames: Vec<String> = m12
            .get("deploy_dlls")
            .and_then(|v| v.as_array())
            .unwrap()
            .iter()
            .map(|d| d.get("filename").unwrap().as_str().unwrap().to_string())
            .collect();
        let m11_filenames: Vec<String> = m11
            .get("deploy_dlls")
            .and_then(|v| v.as_array())
            .unwrap()
            .iter()
            .map(|d| d.get("filename").unwrap().as_str().unwrap().to_string())
            .collect();

        assert!(
            m12_filenames.contains(&"d3d12.dll".to_string()),
            "M12 dry-run must include d3d12.dll: {:?}",
            m12_filenames
        );
        assert!(
            !m11_filenames.contains(&"d3d12.dll".to_string()),
            "M11 dry-run must NOT include d3d12.dll: {:?}",
            m11_filenames
        );

        // Both dry-runs must report the env keys the launch path sets.
        let m12_env = m12.get("env_keys_present").unwrap();
        assert_eq!(m12_env.get("WINEDLLOVERRIDES").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(m12_env.get("SteamAppId").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(m12.get("dry_run").and_then(|v| v.as_bool()), Some(true));

        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn m12_dry_run_verifies_unix_sidecars_and_marks_missing_artifacts() {
        // Phase 3: the M12 dry-run must enumerate the x86_64-unix sidecars and
        // report missing required artifacts as a structured failure (not a
        // silent ok=true). With an empty temp home, all artifacts are absent.
        let home = std::env::temp_dir().join("ms-m12-dryrun-empty");
        let _ = std::fs::remove_dir_all(&home);
        std::fs::create_dir_all(&home).unwrap();

        let dry = pipeline_dry_run_for(&home, 2379780, Some(PipelineId::M12));

        // Unix sidecars must be enumerated for the M12 lane.
        let sidecar_names: Vec<String> = dry
            .get("unix_sidecars")
            .and_then(|v| v.as_array())
            .unwrap()
            .iter()
            .map(|s| s.get("filename").unwrap().as_str().unwrap().to_string())
            .collect();
        for required in ["winemetal.so", "libc++.1.dylib", "libc++abi.1.dylib", "libunwind.1.dylib"] {
            assert!(
                sidecar_names.contains(&required.to_string()),
                "M12 dry-run must verify {}: {:?}",
                required,
                sidecar_names
            );
        }

        // d3d12.dll is a required (non-optional) M12 artifact, so it must be
        // listed as missing when absent.
        let missing_filenames: Vec<String> = dry
            .get("missing")
            .and_then(|v| v.as_array())
            .unwrap()
            .iter()
            .map(|m| m.get("filename").unwrap().as_str().unwrap().to_string())
            .collect();
        assert!(
            missing_filenames.contains(&"d3d12.dll".to_string()),
            "M12 dry-run must flag missing d3d12.dll: {:?}",
            missing_filenames
        );
        assert_eq!(dry.get("ok").and_then(|v| v.as_bool()), Some(false), "empty home must yield ok=false");

        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn m12_pipeline_env_vars_set_winemetal_overrides_and_shader_cache() {
        // Phase 3 contract: the M12 env builder must set the winemetal
        // WINEDLLOVERRIDES, route the wine DLL path to dxmt-m12, and point the
        // shader cache at the isolated m12 lane.
        let node = get_pipeline(PipelineId::M12);
        assert!(node.wine_overrides.unwrap_or("").contains("winemetal"));
        assert!(
            node.winedllpath_dirs.iter().any(|d| d.starts_with("lib/dxmt-m12")),
            "M12 winedllpath must route to dxmt-m12"
        );
        assert_eq!(node.shader_cache_subdir, Some("m12"), "M12 shader cache must be isolated under m12");
    }

    #[test]
    fn m9_cache_env_uses_dxmt_family_not_dxvk() {
        let node = get_pipeline(PipelineId::M9);
        let cache = CachePaths { shader: "/tmp/m9-shaders".into(), pipeline: "/tmp/m9-pipelines".into() };

        let env = cache_env_pairs(node, Some(&cache), &PathBuf::from("/tmp/metalsharp-runtime"));
        let keys: std::collections::HashSet<_> = env.iter().map(|(key, _)| key.as_str()).collect();

        assert!(keys.contains("DXMT_SHADER_CACHE_PATH"));
        assert!(keys.contains("DXMT_PIPELINE_CACHE_PATH"));
        assert!(keys.contains("DXMT_LOG_PATH"));
        assert!(keys.contains("METALSHARP_CACHE_SUMMARY"));
        assert!(!keys.contains("DXVK_STATE_CACHE_PATH"));
        assert!(!keys.contains("DXVK_LOG_PATH"));
        assert!(!keys.contains("VK_ICD_FILENAMES"));
    }

    #[test]
    fn dxmt_family_env_uses_seventy_percent_upscale_and_cache_paths() {
        for pipeline_id in [PipelineId::M9, PipelineId::M10, PipelineId::M11, PipelineId::M12] {
            let home = test_dir(&format!("dxmt-env-{:?}", pipeline_id));
            let node = get_pipeline(pipeline_id);

            let env = steam_pipeline_env_pairs(&home, node, 42);
            let config = env.iter().find(|(key, _)| key == "DXMT_CONFIG").map(|(_, value)| value.as_str());
            let summary =
                env.iter().find(|(key, _)| key == "METALSHARP_CACHE_SUMMARY").map(|(_, value)| value.as_str());

            assert!(config.unwrap_or_default().contains("d3d11.metalSpatialUpscaleFactor=1.43"));
            assert!(config.unwrap_or_default().contains("d3d11.preferredMaxFrameRate=60"));
            assert!(summary.unwrap_or_default().contains("/shader-cache/"));
            assert!(summary.unwrap_or_default().contains("/pipeline-cache/"));
            assert!(env.iter().any(|(key, value)| key == "DXMT_LOG_PATH" && value.ends_with('/')));
            let _ = std::fs::remove_dir_all(home);
        }
    }

    #[test]
    fn m9_stuck_loading_titles_disable_async_loading_features() {
        for appid in [774361, 17410, 49520] {
            let env = app_compat_env_pairs(appid, PipelineId::M9);

            assert_eq!(env_value(&env, "DXMT_ASYNC_PIPELINE_COMPILE"), Some("0"));
            assert_eq!(env_value(&env, "DXMT_METALFX_SPATIAL_SWAPCHAIN"), Some("0"));
            assert_eq!(env_value(&env, "DXMT_METALFX_SPATIAL"), Some("0"));
            assert_eq!(env_value(&env, "DXMT_CONFIG"), Some("d3d11.preferredMaxFrameRate=60"));
            assert_eq!(env_value(&env, "METALSHARP_M9_SYNC_LOADING"), Some("1"));
        }

        assert!(app_compat_env_pairs(123456, PipelineId::M9).is_empty());
        assert!(app_compat_env_pairs(774361, PipelineId::M10).is_empty());
    }

    #[test]
    fn steam_pipeline_env_applies_m9_stuck_loading_overrides_after_defaults() {
        let home = test_dir("m9-stuck-loading-env");
        let node = get_pipeline(PipelineId::M9);

        let env = steam_pipeline_env_pairs(&home, node, 774361);

        assert_eq!(last_env_value(&env, "DXMT_ASYNC_PIPELINE_COMPILE"), Some("0"));
        assert_eq!(last_env_value(&env, "DXMT_METALFX_SPATIAL_SWAPCHAIN"), Some("0"));
        assert_eq!(last_env_value(&env, "DXMT_CONFIG"), Some("d3d11.preferredMaxFrameRate=60"));
        assert_eq!(last_env_value(&env, "METALSHARP_M9_SYNC_LOADING"), Some("1"));
        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn m32_env_keeps_wine_fallback_cache_without_dxmt_config() {
        let home = test_dir("m32-env");
        let node = get_pipeline(PipelineId::M32);

        let env = steam_pipeline_env_pairs(&home, node, 77);
        let keys: std::collections::HashSet<_> = env.iter().map(|(key, _)| key.as_str()).collect();

        assert!(keys.contains("METALSHARP_SHADER_CACHE_PATH"));
        assert!(keys.contains("METALSHARP_PIPELINE_CACHE_PATH"));
        assert!(keys.contains("METALSHARP_CACHE_SUMMARY"));
        assert!(keys.contains("DXMT_SHADER_CACHE_PATH"));
        assert!(keys.contains("DXMT_PIPELINE_CACHE_PATH"));
        assert!(keys.contains("DXVK_STATE_CACHE_PATH"));
        assert!(!keys.contains("DXMT_CONFIG"));
        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn steam_pipeline_env_includes_route_overrides_and_cache_keys() {
        let home = test_dir("steam-env");
        let node = get_pipeline(PipelineId::M12);

        let env = steam_pipeline_env_pairs(&home, node, 1583230);
        let keys: std::collections::HashSet<_> = env.iter().map(|(key, _)| key.as_str()).collect();

        assert!(keys.contains("WINEDLLOVERRIDES"));
        assert!(keys.contains("DXMT_CONFIG_FILE"));
        assert!(keys.contains("SteamAppId"));
        assert!(keys.contains("SteamGameId"));
        assert!(keys.contains("DXMT_SHADER_CACHE_PATH"));
        assert!(keys.contains("DXMT_PIPELINE_CACHE_PATH"));
        assert!(keys.contains("DXMT_LOG_PATH"));
        assert!(keys.contains("METALSHARP_CACHE_SUMMARY"));
        assert!(keys.contains("DXMT_CONFIG"));
        let unixlib =
            env.iter().find(|(key, _)| key == "DXMT_WINEMETAL_UNIXLIB").map(|(_, value)| value.as_str()).unwrap();
        assert_eq!(unixlib, "winemetal.so");
        assert!(keys.contains("DXMT_ASYNC_PIPELINE_COMPILE"));
        assert_eq!(env.iter().find(|(key, _)| key == "SteamAppId").map(|(_, value)| value.as_str()), Some("1583230"));
        assert_eq!(env.iter().find(|(key, _)| key == "SteamGameId").map(|(_, value)| value.as_str()), Some("1583230"));
        let overrides = env.iter().find(|(key, _)| key == "WINEDLLOVERRIDES").map(|(_, value)| value).unwrap();
        assert!(overrides.contains("d3d12"));
        assert!(overrides.contains("mscompatdb,gameoverlayrenderer,gameoverlayrenderer64=d"));
        assert!(keys.contains("CX_ROOT"));
        assert!(keys.contains("WINESERVER"));
        assert!(keys.contains("WINELOADER"));
        assert!(keys.contains("WINEDATADIR"));
        assert!(!keys.contains("MS_ROOT"));
        assert!(env_value(&env, "WINELOADER").unwrap_or_default().ends_with("/runtime/wine/bin/wine"));
        assert!(env_value(&env, "WINEDLLPATH").unwrap_or_default().contains("dxmt-m12/x86_64-windows"));
        assert!(env_value(&env, "DYLD_LIBRARY_PATH").unwrap_or_default().contains("dxmt-m12/x86_64-unix"));
        assert!(env_value(&env, "DYLD_FALLBACK_LIBRARY_PATH").unwrap_or_default().contains("dxmt-m12/x86_64-unix"));
        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn m12_launch_uses_direct_wine_binary_to_bypass_wrapper_ms_root() {
        let home = test_dir("m12-direct-wine");
        let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
        let bin = ms_root.join("bin");
        std::fs::create_dir_all(&bin).unwrap();
        std::fs::write(bin.join("metalsharp-wine"), b"#!/bin/sh\n").unwrap();
        std::fs::write(bin.join("wine"), b"#!/bin/sh\n").unwrap();

        assert_eq!(runtime_wine_binary_for_launch(&ms_root, get_pipeline(PipelineId::M12)), bin.join("wine"));
        assert_eq!(runtime_wine_binary_for_launch(&ms_root, get_pipeline(PipelineId::M9)), bin.join("metalsharp-wine"));
        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn steam_pipeline_env_allows_plain_wine_fallback_context() {
        let home = test_dir("steam-wine-env");
        let node = get_pipeline(PipelineId::WineBare);

        let env = steam_pipeline_env_pairs(&home, node, 1);
        let keys: std::collections::HashSet<_> = env.iter().map(|(key, _)| key.as_str()).collect();

        let runtime_lib_key = crate::platform::runtime_library_env(
            &crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine"),
        )
        .map(|(key, _)| key)
        .unwrap_or("LD_LIBRARY_PATH");
        assert!(keys.contains(runtime_lib_key));
        assert!(keys.contains("SteamAppId"));
        assert!(keys.contains("SteamGameId"));
        assert!(keys.contains("METALSHARP_SHADER_CACHE_PATH"));
        assert!(keys.contains("DXMT_SHADER_CACHE_PATH"));
        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn dxmt_pipelines_use_winedllpath_routing() {
        for pipeline_id in [PipelineId::M9, PipelineId::M10, PipelineId::M11, PipelineId::M12] {
            let node = get_pipeline(pipeline_id);
            assert!(node.uses_winedllpath_routing(), "{:?} should use WINEDLLPATH routing", pipeline_id);
        }
    }

    #[test]
    fn non_dxmt_pipelines_do_not_use_winedllpath_routing() {
        for pipeline_id in
            [PipelineId::M32, PipelineId::FnaArm64, PipelineId::Steam, PipelineId::MacSteam, PipelineId::WineBare]
        {
            let node = get_pipeline(pipeline_id);
            assert!(!node.uses_winedllpath_routing(), "{:?} should not use WINEDLLPATH routing", pipeline_id);
        }
    }

    #[test]
    fn fna_native_shim_manifest_includes_macos_framework_routes() {
        let outputs: std::collections::HashSet<_> = FNA_NATIVE_SHIMS.iter().map(|spec| spec.output).collect();

        assert!(outputs.contains("libkernel32.dylib"));
        assert!(outputs.contains("libuser32.dylib"));
        assert!(outputs.contains(FNA_CARBON_SHIM));
        assert!(outputs.contains(FNA_CARBON_INTERPOSE_SHIM));
        assert!(outputs.contains("xaudio2_9.dylib"));
        assert!(outputs.contains("xinput1_4.dylib"));
        assert!(FNA_NATIVE_SHIMS
            .iter()
            .any(|spec| spec.output == "xaudio2_9.dylib" && spec.symlinks.contains(&"xaudio2_7.dylib")));
        assert!(FNA_NATIVE_SHIMS
            .iter()
            .any(|spec| spec.output == "xinput1_4.dylib" && spec.symlinks.contains(&"xinput1_3.dylib")));
    }

    #[test]
    fn celeste_and_default_profiles_isolate_game_dir_dylibs_from_runtime_shims() {
        let celeste = find_fna_profile(504230);
        assert!(matches!(celeste.mono_arch, MonoArch::X86));
        assert!(!celeste.include_runtime_shims_in_library_path);

        let default = find_fna_profile(0);
        assert!(matches!(default.mono_arch, MonoArch::X86));
        assert!(!default.include_runtime_shims_in_library_path);
    }

    #[test]
    fn fna_runtime_validation_rejects_sdl3_linked_core_libs() {
        let path = PathBuf::from("/tmp/libFNA3D.0.dylib");
        assert!(fna_native_lib_source_valid("libSDL2-2.0.0.dylib", &path) || !path.exists());
        assert!(!fna_native_lib_source_valid("libFNA3D.0.dylib", &path));
    }

    #[test]
    fn fna_runtime_validation_rejects_tiny_fmod_stubs() {
        let game_dir = test_dir("fna-runtime-validation-fmod");
        std::fs::create_dir_all(&game_dir).expect("test dir");
        let stub = game_dir.join("libfmod.dylib");
        std::fs::write(&stub, vec![0u8; 32 * 1024]).expect("stub dylib");

        let real = game_dir.join("libfmodstudio.dylib");
        std::fs::write(&real, vec![0u8; 512 * 1024]).expect("real dylib");

        assert!(!fna_native_lib_source_valid("libfmod.dylib", &stub));
        assert!(fna_native_lib_source_valid("libfmodstudio.dylib", &real));
    }

    #[test]
    fn fna_runtime_repair_requires_managed_assembly_and_native_shims() {
        let runtime = PathBuf::from("/tmp/metalsharp-runtime");
        let required = fna_required_runtime_assets(&runtime);

        assert!(required.contains(&runtime.join("fna-kickstart").join("kick.bin.osx")));
        assert!(required.contains(&runtime.join("fna-kickstart").join("FNA.dll")));
        assert!(required.contains(&runtime.join("fna-kickstart").join("osx").join("libmonosgen-2.0.1.dylib")));
        assert!(required.contains(&runtime.join("fna-kickstart").join("osx").join("libSDL2-2.0.0.dylib")));
        assert!(required.contains(&runtime.join("fna-kickstart").join("osx").join("libFNA3D.0.dylib")));
        assert!(required.contains(&runtime.join("fna-kickstart").join("osx").join("libFAudio.0.dylib")));
        assert!(required.contains(&runtime.join("fnalibs").join("libFNA3D.0.dylib")));
        assert!(required.contains(&runtime.join("fnalibs").join("libSDL2-2.0.0.dylib")));
        assert!(required.contains(&runtime.join("shims").join("libkernel32.dylib")));
        assert!(required.contains(&runtime.join("shims").join("libuser32.dylib")));
        assert!(required.contains(&runtime.join("shims").join(FNA_CARBON_SHIM)));
        assert!(required.contains(&runtime.join("shims").join(FNA_CARBON_INTERPOSE_SHIM)));
        assert_eq!(required.len(), 12);
    }

    #[test]
    fn fna_flavor_detection_identifies_fna_by_managed_dll() {
        let game_dir = test_dir("fna-flavor-fna");
        let managed = game_dir.join("Game_Data").join("Managed");
        std::fs::create_dir_all(&managed).expect("managed dir");
        std::fs::write(managed.join("FNA.dll"), b"fna").expect("fna dll");

        assert_eq!(detect_fna_flavor(&game_dir), FnaFlavor::Fna);
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn fna_flavor_detection_identifies_monogame_by_managed_dll() {
        let game_dir = test_dir("fna-flavor-mg");
        let managed = game_dir.join("Game_Data").join("Managed");
        std::fs::create_dir_all(&managed).expect("managed dir");
        std::fs::write(managed.join("MonoGame.Framework.dll"), b"mg").expect("mg dll");

        assert_eq!(detect_fna_flavor(&game_dir), FnaFlavor::MonoGame);
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn fna_flavor_detection_identifies_xna_by_managed_dll() {
        let game_dir = test_dir("fna-flavor-xna");
        let managed = game_dir.join("Game_Data").join("Managed");
        std::fs::create_dir_all(&managed).expect("managed dir");
        std::fs::write(managed.join("Microsoft.Xna.Framework.dll"), b"xna").expect("xna dll");

        assert_eq!(detect_fna_flavor(&game_dir), FnaFlavor::Xna);
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn fna_flavor_detection_falls_back_to_top_level_fna_dll() {
        let game_dir = test_dir("fna-flavor-toplevel");
        std::fs::create_dir_all(&game_dir).expect("game dir");
        std::fs::write(game_dir.join("FNA.dll"), b"fna").expect("fna dll");

        assert_eq!(detect_fna_flavor(&game_dir), FnaFlavor::Fna);
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn fna_flavor_detection_returns_unknown_for_empty_dir() {
        let game_dir = test_dir("fna-flavor-empty");
        std::fs::create_dir_all(&game_dir).expect("game dir");

        assert_eq!(detect_fna_flavor(&game_dir), FnaFlavor::Unknown);
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn default_fna_profile_uses_generic_config() {
        let profile = find_fna_profile(u32::MAX);

        assert_eq!(profile.mono_config, "generic-fna-mono.config");
        assert!(matches!(profile.mono_arch, MonoArch::X86));
        assert_eq!(profile.method_label, "xna_fna_x86");
        assert!(!profile.include_runtime_shims_in_library_path);
    }

    #[test]
    fn fna_runtime_download_uses_nuget_flat_container() {
        assert_eq!(
            fna_package_download_url(),
            "https://api.nuget.org/v3-flatcontainer/fna-xna-wrapper/22.12.2/fna-xna-wrapper.22.12.2.nupkg"
        );
    }

    #[test]
    fn fna_native_launch_env_enables_carbon_interpose_when_deployed() {
        let game_dir = test_dir("fna-native-env");
        std::fs::create_dir_all(&game_dir).expect("game dir");
        std::fs::write(game_dir.join(FNA_CARBON_SHIM), b"carbon").expect("carbon shim");
        std::fs::write(game_dir.join(FNA_CARBON_INTERPOSE_SHIM), b"interpose").expect("interpose shim");

        let env = fna_native_launch_env(&game_dir);
        assert_eq!(
            env.iter().find(|(key, _)| key == "METALSHARP_CARBON_SHIM").map(|(_, value)| value.as_str()),
            Some(game_dir.join(FNA_CARBON_SHIM).to_string_lossy().as_ref())
        );
        assert_eq!(
            env.iter().find(|(key, _)| key == "DYLD_INSERT_LIBRARIES").map(|(_, value)| value.as_str()),
            Some(game_dir.join(FNA_CARBON_INTERPOSE_SHIM).to_string_lossy().as_ref())
        );
        let _ = std::fs::remove_dir_all(game_dir);
    }

    #[test]
    fn append_path_env_preserves_existing_insert_libraries() {
        assert_eq!(append_path_env(None, "/tmp/new.dylib"), "/tmp/new.dylib");
        assert_eq!(append_path_env(Some("/tmp/old.dylib"), "/tmp/new.dylib"), "/tmp/old.dylib:/tmp/new.dylib");
        assert_eq!(
            append_path_env(Some("/tmp/old.dylib:/tmp/new.dylib"), "/tmp/new.dylib"),
            "/tmp/old.dylib:/tmp/new.dylib"
        );
    }

    #[test]
    fn steam_pipeline_env_includes_winedllpath_for_dxmt_pipelines() {
        for pipeline_id in [PipelineId::M9, PipelineId::M10, PipelineId::M11, PipelineId::M12] {
            let home = test_dir(&format!("winedllpath-env-{:?}", pipeline_id));
            let node = get_pipeline(pipeline_id);
            let env = steam_pipeline_env_pairs(&home, node, 42);
            let winedllpath = env.iter().find(|(key, _)| key == "WINEDLLPATH");
            assert!(winedllpath.is_some(), "{:?} missing WINEDLLPATH in env pairs", pipeline_id);
            let path = winedllpath.unwrap().1.as_str();
            assert!(!path.is_empty(), "{:?} WINEDLLPATH is empty", pipeline_id);
            assert!(path.contains("x86_64-windows"), "{:?} WINEDLLPATH missing windows dir: {}", pipeline_id, path);
            if pipeline_id == PipelineId::M12 {
                assert!(path.contains("dxmt-m12"), "M12 should use isolated M12 DLL path: {}", path);
            } else {
                assert!(!path.contains("dxmt-m12"), "{:?} should not use isolated M12 DLL path: {}", pipeline_id, path);
            }
            let _ = std::fs::remove_dir_all(&home);
        }
    }

    #[test]
    fn steam_pipeline_env_has_no_winedllpath_for_non_dxmt_pipelines() {
        let home = test_dir("no-winedllpath");
        let node = get_pipeline(PipelineId::WineBare);
        let env = steam_pipeline_env_pairs(&home, node, 1);
        assert!(!env.iter().any(|(key, _)| key == "WINEDLLPATH"));
        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn m12_prefix_route_dlls_stage_into_system32() {
        let root = test_dir("m12-prefix-route");
        let source_dir = root.join("runtime").join("wine").join("lib").join("dxmt-m12").join("x86_64-windows");
        let prefix = root.join("prefix-steam");
        std::fs::create_dir_all(&source_dir).expect("create source dir");

        let dlls = ["d3d12.dll", "dxgi.dll", "dxgi_dxmt.dll", "d3d11.dll", "d3d10core.dll", "winemetal.dll"];
        let recipe_dlls: Vec<recipe::RecipeDll> = dlls
            .iter()
            .map(|dll| {
                let source_path = source_dir.join(dll);
                std::fs::write(&source_path, format!("m12-{dll}")).expect("write route dll");
                recipe::RecipeDll {
                    source_subpath: "lib/dxmt-m12/x86_64-windows".to_string(),
                    filename: (*dll).to_string(),
                    source_path,
                    dest_path: root.join("game").join(dll),
                    source_present: true,
                }
            })
            .collect();

        let mut recipe = empty_test_recipe(PipelineId::M12);
        recipe.dlls = recipe_dlls;

        deploy_prefix_route_dlls(&recipe, &prefix).expect("stage M12 route DLLs");

        let system32 = prefix.join("drive_c").join("windows").join("system32");
        for dll in dlls {
            assert_eq!(std::fs::read_to_string(system32.join(dll)).unwrap(), format!("m12-{dll}"));
        }

        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn non_m12_prefix_route_dlls_do_not_stage_into_system32() {
        let root = test_dir("m11-prefix-route");
        let source_dir = root.join("runtime").join("wine").join("lib").join("dxmt").join("x86_64-windows");
        let prefix = root.join("prefix-steam");
        std::fs::create_dir_all(&source_dir).expect("create source dir");
        let source_path = source_dir.join("d3d12.dll");
        std::fs::write(&source_path, "legacy-dxmt-d3d12").expect("write route dll");

        let mut recipe = empty_test_recipe(PipelineId::M11);
        recipe.dlls.push(recipe::RecipeDll {
            source_subpath: "lib/dxmt/x86_64-windows".to_string(),
            filename: "d3d12.dll".to_string(),
            source_path,
            dest_path: root.join("game").join("d3d12.dll"),
            source_present: true,
        });

        deploy_prefix_route_dlls(&recipe, &prefix).expect("non-M12 route is no-op");

        assert!(!prefix.join("drive_c").join("windows").join("system32").join("d3d12.dll").exists());

        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn cleanup_legacy_injections_restores_backups_within_game_dir() {
        let game_dir = test_dir("cleanup-restore");
        let injection_dir = game_dir.join(".metalsharp");
        let originals_dir = injection_dir.join("originals");
        std::fs::create_dir_all(&originals_dir).unwrap();

        let original_dll = game_dir.join("d3d12.dll");
        let backup_dll = originals_dir.join("d3d12.dll");
        std::fs::write(&backup_dll, b"original-d3d12-content").unwrap();

        let manifest = serde_json::json!({
            "appid": 1234,
            "dlls": [{
                "filename": "d3d12.dll",
                "dest_path": original_dll.to_string_lossy().to_string(),
                "backup_path": backup_dll.to_string_lossy().to_string(),
            }]
        });
        std::fs::write(injection_dir.join("injections.json"), serde_json::to_string_pretty(&manifest).unwrap())
            .unwrap();

        cleanup_legacy_injections(&game_dir).unwrap();

        assert!(original_dll.exists());
        assert_eq!(std::fs::read(&original_dll).unwrap(), b"original-d3d12-content");
        assert!(!injection_dir.exists());
        let _ = std::fs::remove_dir_all(&game_dir);
    }

    #[test]
    fn cleanup_legacy_injections_ignores_paths_outside_game_dir() {
        let game_dir = test_dir("cleanup-traversal");
        let injection_dir = game_dir.join(".metalsharp");
        let originals_dir = injection_dir.join("originals");
        std::fs::create_dir_all(&originals_dir).unwrap();

        let outside_path = std::env::temp_dir().join("metalsharp-traversal-test-target.dll");
        let _ = std::fs::remove_file(&outside_path);

        let manifest = serde_json::json!({
            "appid": 1234,
            "dlls": [{
                "filename": "evil.dll",
                "dest_path": outside_path.to_string_lossy().to_string(),
                "backup_path": originals_dir.join("evil.dll").to_string_lossy().to_string(),
            }]
        });
        std::fs::write(injection_dir.join("injections.json"), serde_json::to_string_pretty(&manifest).unwrap())
            .unwrap();

        cleanup_legacy_injections(&game_dir).unwrap();

        assert!(!outside_path.exists());
        assert!(!injection_dir.exists());
        let _ = std::fs::remove_dir_all(&game_dir);
    }

    #[test]
    fn d3dmetal_goldberg_dirs_include_gptk_dosdevice_alias() {
        let home = test_dir("gptk-goldberg-alias");
        let library = home.join("SteamLibrary");
        let game_dir = library.join("steamapps").join("common").join("Celeste");
        let prefix = crate::platform::gptk_prefix_path(&home);
        let dosdevices = prefix.join("dosdevices");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::create_dir_all(&dosdevices).expect("create dosdevices");
        std::os::unix::fs::symlink(&library, dosdevices.join("d:")).expect("create library drive");

        let aliases = gptk_prefix_game_dir_aliases(&prefix, &game_dir);
        let dirs = goldberg_dirs_for_pipeline(&home, &game_dir, PipelineId::D3DMetal);

        assert!(aliases.contains(&dosdevices.join("d:").join("steamapps").join("common").join("Celeste")));
        assert_eq!(dirs, vec![game_dir.clone()]);

        let m9_dirs = goldberg_dirs_for_pipeline(&home, &game_dir, PipelineId::M9);
        assert_eq!(m9_dirs, vec![game_dir]);

        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn gptk_dosdevice_alias_resolves_relative_c_drive() {
        let home = test_dir("gptk-relative-c");
        let prefix = crate::platform::gptk_prefix_path(&home);
        let dosdevices = prefix.join("dosdevices");
        let game_dir = prefix.join("drive_c").join("Games").join("Portal 2");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::create_dir_all(&dosdevices).expect("create dosdevices");
        std::os::unix::fs::symlink("../drive_c", dosdevices.join("c:")).expect("create c drive");

        let aliases = gptk_prefix_game_dir_aliases(&prefix, &game_dir);

        assert_eq!(aliases, vec![dosdevices.join("c:").join("Games").join("Portal 2")]);

        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn d3dmetal_offline_disables_and_restores_gptk_steam_launcher() {
        let home = test_dir("gptk-offline-steam-exe");
        let prefix = crate::platform::gptk_prefix_path(&home);
        let steam_dir = prefix.join("drive_c").join("Program Files (x86)").join("Steam");
        std::fs::create_dir_all(&steam_dir).expect("create steam dir");
        let steam_exe = steam_dir.join("Steam.exe");
        let disabled = crate::platform::gptk_disabled_steam_exe(&prefix);
        std::fs::write(&steam_exe, b"steam").expect("write steam exe");

        assert!(crate::platform::disable_gptk_steam_launcher_for_offline(&prefix).expect("disable steam exe"));

        assert!(!steam_exe.exists());
        assert_eq!(std::fs::read(&disabled).expect("read disabled"), b"steam");

        assert!(crate::platform::restore_gptk_steam_launcher(&prefix).expect("restore steam exe"));

        assert!(steam_exe.exists());
        assert!(!disabled.exists());

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn steam_model_launch_deploys_real_steam_client_components() {
        let home = test_dir("secure-real-steam");
        let steam_dir = crate::platform::metalsharp_home_dir_for(&home)
            .join("prefix-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam");
        let game_dir = home.join("SteamLibrary").join("steamapps").join("common").join("Team Fortress 2");
        let bin_dir = game_dir.join("bin");
        std::fs::create_dir_all(&steam_dir).expect("create steam dir");
        std::fs::create_dir_all(&bin_dir).expect("create game bin");

        for filename in ["steam_api64.dll", "steam_api.dll"] {
            std::fs::write(steam_dir.join(filename), filename.as_bytes()).expect("write steam component");
        }
        for filename in real_steam_model_component_names() {
            std::fs::write(steam_dir.join(filename), filename.as_bytes()).expect("write steam component");
        }

        ensure_real_steam_dlls(&home, &game_dir, 440, true);

        for target in [&game_dir, &bin_dir] {
            assert_eq!(std::fs::read(target.join("steam_api64.dll")).expect("read api64"), b"steam_api64.dll");
            assert_eq!(std::fs::read(target.join("steam_api.dll")).expect("read api"), b"steam_api.dll");
            assert_eq!(
                std::fs::read(target.join("steamclient64.dll")).expect("read steamclient64"),
                b"steamclient64.dll"
            );
            assert_eq!(std::fs::read(target.join("steamclient.dll")).expect("read steamclient"), b"steamclient.dll");
            assert_eq!(
                std::fs::read(target.join("GameOverlayRenderer64.dll")).expect("read overlay64"),
                b"GameOverlayRenderer64.dll"
            );
            assert_eq!(
                std::fs::read(target.join("GameOverlayRenderer.dll")).expect("read overlay"),
                b"GameOverlayRenderer.dll"
            );
            assert_eq!(std::fs::read_to_string(target.join("steam_appid.txt")).expect("read appid"), "440");
        }

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn steam_only_launches_deploy_real_steam_client_components() {
        let home = test_dir("steam-only-real-steam");
        let steam_dir = crate::platform::metalsharp_home_dir_for(&home)
            .join("prefix-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam");
        let game_dir = home.join("SteamLibrary").join("steamapps").join("common").join("Party Animals");
        std::fs::create_dir_all(&steam_dir).expect("create steam dir");
        std::fs::create_dir_all(&game_dir).expect("create game dir");

        for filename in ["steam_api64.dll", "steam_api.dll"] {
            std::fs::write(steam_dir.join(filename), filename.as_bytes()).expect("write steam component");
        }
        for filename in real_steam_model_component_names() {
            std::fs::write(steam_dir.join(filename), filename.as_bytes()).expect("write steam component");
        }

        ensure_real_steam_dlls(&home, &game_dir, 1260320, true);

        assert_eq!(std::fs::read(game_dir.join("steam_api64.dll")).expect("read api64"), b"steam_api64.dll");
        assert_eq!(std::fs::read(game_dir.join("steam_api.dll")).expect("read api"), b"steam_api.dll");
        for filename in real_steam_model_component_names() {
            assert_eq!(
                std::fs::read(game_dir.join(filename)).expect("read steam model component"),
                filename.as_bytes()
            );
        }
        assert_eq!(std::fs::read_to_string(game_dir.join("steam_appid.txt")).expect("read appid"), "1260320");

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn ordinary_real_steam_launch_keeps_client_components_conservative() {
        let home = test_dir("ordinary-real-steam");
        let steam_dir = crate::platform::metalsharp_home_dir_for(&home)
            .join("prefix-steam")
            .join("drive_c")
            .join("Program Files (x86)")
            .join("Steam");
        let game_dir = home.join("SteamLibrary").join("steamapps").join("common").join("Ordinary Game");
        std::fs::create_dir_all(&steam_dir).expect("create steam dir");
        std::fs::create_dir_all(&game_dir).expect("create game dir");

        for filename in ["steam_api64.dll", "steamclient64.dll", "GameOverlayRenderer64.dll"] {
            std::fs::write(steam_dir.join(filename), filename.as_bytes()).expect("write steam component");
        }

        ensure_real_steam_dlls(&home, &game_dir, 999999, false);

        assert!(game_dir.join("steam_api64.dll").exists());
        assert!(!game_dir.join("steamclient64.dll").exists());
        assert!(!game_dir.join("GameOverlayRenderer64.dll").exists());
        assert_eq!(std::fs::read_to_string(game_dir.join("steam_appid.txt")).expect("read appid"), "999999");

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn steam_pipeline_env_rejects_client_only_routes() {
        let error = prepare_steam_pipeline_env(1, PipelineId::Steam).expect_err("steam should not be handoff");

        assert!(error.to_string().contains("Steam route handoff only supports Wine-backed MTSP pipelines"));
    }

    #[test]
    fn bridge_port_parser_rejects_invalid_values() {
        assert_eq!(parse_bridge_port(Some("19001")), Some(19001));
        assert_eq!(parse_bridge_port(Some("0")), None);
        assert_eq!(parse_bridge_port(Some("65536")), None);
        assert_eq!(parse_bridge_port(Some("abc")), None);
        assert_eq!(parse_bridge_port(None), None);
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
            anti_cheat_status: vec![],
            warnings: vec![],
        };

        deploy_recipe_dlls(&recipe).expect("no-op deploy should succeed");
    }

    #[test]
    fn nested_executables_launch_from_their_parent_directory() {
        let game_dir = PathBuf::from("/tmp/Game");
        let exe_path = game_dir.join("Engine").join("Binaries").join("Win64").join("Game-Win64-Shipping.exe");

        assert_eq!(launch_working_dir(&game_dir, &exe_path), exe_path.parent().unwrap());
    }

    #[test]
    fn launch_log_identity_records_host_runtime_and_compatdata_paths() {
        let mut log = Vec::new();
        let prefix = crate::platform::metalsharp_home_dir().join("prefix-steam");

        write_runtime_identity(&mut log, &prefix, Some(620)).expect("write runtime identity");

        let text = String::from_utf8(log).expect("utf8 log");
        assert!(text.contains("host_abi=1.0"));
        assert!(text.contains("host_runtime="));
        assert!(text.contains("steam_bridge_port="));
        assert!(text.contains("compatdata_manifest="));
        assert!(text.contains("steam_identity_mode=wine_steam_background"));
    }

    #[test]
    fn start_protected_game_bypass_renames_and_copies_real_exe() {
        let home = test_dir("spg-bypass");
        let game_dir = home.join("Game");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::write(game_dir.join("start_protected_game.exe"), b"EAC_STUB").expect("write stub");
        std::fs::write(game_dir.join("eldenring.exe"), b"REAL_GAME").expect("write real exe");

        apply_start_protected_game_bypass(1245620, &home);

        assert!(game_dir.join("start_protected_game.old").exists());
        assert_eq!(std::fs::read(game_dir.join("start_protected_game.old")).unwrap(), b"EAC_STUB");
        assert_eq!(std::fs::read(game_dir.join("start_protected_game.exe")).unwrap(), b"REAL_GAME");

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn start_protected_game_bypass_skips_if_old_already_exists() {
        let home = test_dir("spg-skip-old");
        let game_dir = home.join("Game");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::write(game_dir.join("start_protected_game.old"), b"PREVIOUS_STUB").expect("write old");
        std::fs::write(game_dir.join("start_protected_game.exe"), b"REAL_GAME_ALREADY").expect("write current");
        std::fs::write(game_dir.join("eldenring.exe"), b"REAL_GAME").expect("write real exe");

        apply_start_protected_game_bypass(1245620, &home);

        assert_eq!(std::fs::read(game_dir.join("start_protected_game.exe")).unwrap(), b"REAL_GAME_ALREADY");
        assert_eq!(std::fs::read(game_dir.join("start_protected_game.old")).unwrap(), b"PREVIOUS_STUB");

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn start_protected_game_bypass_handles_armored_core_vi() {
        let home = test_dir("spg-ac6");
        let game_dir = home.join("Game");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::write(game_dir.join("start_protected_game.exe"), b"EAC_STUB").expect("write stub");
        std::fs::write(game_dir.join("armoredcore6.exe"), b"AC6_REAL_GAME").expect("write real exe");

        apply_start_protected_game_bypass(1888160, &home);

        assert_eq!(std::fs::read(game_dir.join("start_protected_game.old")).unwrap(), b"EAC_STUB");
        assert_eq!(std::fs::read(game_dir.join("start_protected_game.exe")).unwrap(), b"AC6_REAL_GAME");

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn start_protected_game_bypass_can_infer_single_sibling_real_exe() {
        let home = test_dir("spg-generic");
        let game_dir = home.join("Game");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::write(game_dir.join("start_protected_game.exe"), b"EAC_STUB").expect("write stub");
        std::fs::write(game_dir.join("realgame.exe"), b"REAL_GAME").expect("write real exe");

        apply_start_protected_game_bypass(99999, &home);

        assert_eq!(std::fs::read(game_dir.join("start_protected_game.old")).unwrap(), b"EAC_STUB");
        assert_eq!(std::fs::read(game_dir.join("start_protected_game.exe")).unwrap(), b"REAL_GAME");

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn start_protected_game_bypass_skips_ambiguous_generic_siblings() {
        let home = test_dir("spg-ambiguous");
        let game_dir = home.join("Game");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::write(game_dir.join("start_protected_game.exe"), b"EAC_STUB").expect("write stub");
        std::fs::write(game_dir.join("first.exe"), b"FIRST").expect("write first exe");
        std::fs::write(game_dir.join("second.exe"), b"SECOND").expect("write second exe");

        apply_start_protected_game_bypass(99999, &home);

        assert_eq!(std::fs::read(game_dir.join("start_protected_game.exe")).unwrap(), b"EAC_STUB");
        assert!(!game_dir.join("start_protected_game.old").exists());

        let _ = std::fs::remove_dir_all(home);
    }

    #[test]
    fn start_protected_game_bypass_skips_unknown_appid() {
        let home = test_dir("spg-skip");
        let game_dir = home.join("Game");
        std::fs::create_dir_all(&game_dir).expect("create game dir");
        std::fs::write(game_dir.join("start_protected_game.exe"), b"EAC_STUB").expect("write stub");
        std::fs::write(game_dir.join("first.exe"), b"FIRST").expect("write first exe");
        std::fs::write(game_dir.join("second.exe"), b"SECOND").expect("write second exe");

        apply_start_protected_game_bypass(99999, &home);

        assert_eq!(std::fs::read(game_dir.join("start_protected_game.exe")).unwrap(), b"EAC_STUB");
        assert!(!game_dir.join("start_protected_game.old").exists());

        let _ = std::fs::remove_dir_all(home);
    }

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-launcher-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
    }

    fn empty_test_recipe(pipeline: PipelineId) -> recipe::LaunchRecipe {
        recipe::LaunchRecipe {
            appid: 1,
            pipeline,
            pipeline_name: format!("{pipeline:?}"),
            backend: "dxmt".to_string(),
            game_dir: None,
            exe_path: None,
            exe_name: None,
            launch_args: Vec::new(),
            env: Vec::new(),
            dlls: Vec::new(),
            runtime_assets: Vec::new(),
            anti_cheat: Vec::new(),
            anti_cheat_status: Vec::new(),
            warnings: Vec::new(),
        }
    }

    fn env_value<'a>(env: &'a [(String, String)], key: &str) -> Option<&'a str> {
        env.iter().find(|(env_key, _)| env_key == key).map(|(_, value)| value.as_str())
    }

    fn last_env_value<'a>(env: &'a [(String, String)], key: &str) -> Option<&'a str> {
        env.iter().rev().find(|(env_key, _)| env_key == key).map(|(_, value)| value.as_str())
    }

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
