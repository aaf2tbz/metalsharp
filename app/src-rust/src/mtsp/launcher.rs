use super::engine::{get_pipeline, PipelineId, PipelineNode};
use std::fs::OpenOptions;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

const DEFAULT_BRIDGE_PORT: u16 = 18733;
const FNA_CARBON_SHIM: &str = "libCarbon.dylib";
const FNA_CARBON_INTERPOSE_SHIM: &str = "libmetalsharp_carbon_interpose.dylib";
const FNA_XNA_WRAPPER_VERSION: &str = "22.12.2";

#[derive(Clone, Copy)]
enum FnaShimSource {
    RepoC { parts: &'static [&'static str] },
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
        source: FnaShimSource::RepoC { parts: &["src", "fna", "shims", "kernel32_shim.c"] },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: "libuser32.dylib",
        source: FnaShimSource::RepoC { parts: &["src", "fna", "shims", "user32_shim.c"] },
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
        source: FnaShimSource::RepoC { parts: &["src", "fna", "shims", "carbon_interpose.c"] },
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
        source: FnaShimSource::RepoC { parts: &["src", "fna", "shims", "system_native_stub.c"] },
        symlinks: &[],
        required_for_launch: true,
    },
    FnaNativeShimSpec {
        output: "xinput1_4.dylib",
        source: FnaShimSource::BundledNative,
        symlinks: &["xinput1_3.dylib", "xinput9_1_0.dylib"],
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

    match pipeline_id {
        PipelineId::Dxmt | PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 => {
            launch_dxmt_metal(appid, node)
        },
        PipelineId::M13 => launch_dxmt_metal(appid, node),
        PipelineId::Gptk => launch_gptk(appid, node),
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

    match pipeline_id {
        PipelineId::Dxmt | PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 | PipelineId::M13 => {
            launch_dxmt_metal_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
                .map(|(pid, method)| (pid, method, log_path))
        },
        PipelineId::Gptk => launch_gptk_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
            .map(|(pid, method)| (pid, method, log_path)),
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
    let pipeline_id = super::rules::resolve_pipeline(appid);
    let node = get_pipeline(pipeline_id);
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let deployed_sources: Vec<String> = {
        validate_recipe_runtime(&recipe)?;
        if let Some(game_dir) = recipe.game_dir.as_ref() {
            cleanup_legacy_injections(game_dir)?;
        }
        let sources = recipe.dlls.iter().map(|dll| dll.source_subpath.clone()).collect();
        deploy_recipe_dlls(&recipe)?;
        sources
    };

    Ok(serde_json::json!({
        "ok": true,
        "appid": appid,
        "pipeline": node.id,
        "pipeline_name": node.name,
        "recipe": recipe,
        "deployed_dlls": deployed_sources.len(),
        "deployed_sources": deployed_sources,
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
        | PipelineId::Gptk
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
    deploy_recipe_dlls(&recipe)?;

    let env = steam_pipeline_env_pairs(&home, node, appid);
    Ok((env, recipe))
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
        | PipelineId::Gptk
        | PipelineId::M32
        | PipelineId::WineBare => {},
        PipelineId::FnaArm64 | PipelineId::Steam | PipelineId::MacSteam => {
            return Err("Sharp Library apps must use Auto, Wine, M9, M10, M11, M12, M13, GPTK, or M32".into());
        },
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
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
    let wine = crate::platform::runtime_wine_binary(&ms_root);
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

pub fn gptk_wine64_binary() -> PathBuf {
    let candidates = ["/opt/homebrew/bin/wine64", "/usr/local/bin/wine64"];
    for c in &candidates {
        let p = PathBuf::from(c);
        if p.exists() {
            return p;
        }
    }
    PathBuf::from("/opt/homebrew/bin/wine64")
}

fn launch_gptk(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    launch_gptk_with_context(appid, node, None, &[], None)
}

fn launch_gptk_with_context(
    appid: u32,
    node: &PipelineNode,
    prefix_override: Option<&Path>,
    extra_env: &[(String, String)],
    log_path: Option<&Path>,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = gptk_wine64_binary();

    if !wine.exists() {
        return Err("GPTK Wine not found — install via: brew install --cask game-porting-toolkit".into());
    }

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

    let prefix = prefix_override
        .map(Path::to_path_buf)
        .unwrap_or_else(|| crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam"));
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    deploy_recipe_dlls(&recipe)?;

    if !recipe.anti_cheat.is_empty() {
        deploy_steam_appid(game_dir, appid);
    }

    let cache_paths = build_cache_paths(&home, node, appid);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", wine_debug_value())
        .env("WINEDEBUGGER", "none");

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    cmd.env("MS_GRAPHICS_BACKEND", node.graphics_backend);

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
    let node = get_pipeline(PipelineId::FnaArm64);
    let game_dir = resolve_fna_game_dir(appid)?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = crate::platform::metalsharp_home_dir_for(&home).join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };

    ensure_launcher_exe(appid, dir);
    deploy_fna_assemblies(appid, dir);
    deploy_steam_shim(dir);

    let _ = ensure_bridge_running();

    let exe = match appid {
        105600 => find_preferred_exe(dir, &["TerrariaLauncher.exe", "Terraria.exe"])?,
        _ => resolve_game_exe(dir).into(),
    };

    let mono_bin = find_mono_binary_for_app(appid)?;
    let mono_config = find_config(match appid {
        504230 => "celeste-x86-mono.config",
        _ => "terraria-mono.config",
    });
    let shims_dir = find_shims_dir();
    let mono_root =
        mono_bin.parent().and_then(|p| p.parent()).map(|p| p.to_path_buf()).unwrap_or_else(|| PathBuf::from(""));
    let mono_lib = mono_root.join("lib");
    let mono_profile = mono_lib.join("mono").join("4.5");
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

    let mut cmd =
        if matches!(appid, 504230 | 105600) && crate::platform::current() == crate::platform::HostPlatform::Macos {
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

    cmd.current_dir(dir)
        .env(runtime_lib_key, &runtime_lib_path)
        .env("DYLD_FALLBACK_LIBRARY_PATH", &runtime_lib_path)
        .env("MONO_CONFIG", mono_config)
        .env("MONO_ENV_OPTIONS", "--runtime=v4.0")
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

    cmd.arg(&exe);

    let mut child = cmd.spawn()?;
    std::thread::sleep(Duration::from_millis(900));
    if let Some(status) = child.try_wait()? {
        let log_tail = tail_text(&log_path, 4096);
        return Err(format!("FNA/Mono/XNA launch exited early with status {}. Log: {}", status, log_tail).into());
    }
    let method = match appid {
        105600 | 504230 => "xna_fna_x86",
        _ => "xna_fna_arm64",
    };
    Ok((child.id(), method, log_path))
}

fn find_mono_binary_for_app(appid: u32) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    if matches!(appid, 504230 | 105600) {
        let mono_x86 =
            crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-x86").join("bin").join("mono");
        if mono_x86.exists() {
            return Ok(mono_x86);
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
    let (launcher_name, source_file) = match appid {
        105600 => ("TerrariaLauncher.exe", "TerrariaLauncher.cs"),
        _ => return,
    };

    let launcher = game_dir.join(launcher_name);
    if launcher.exists() && appid != 105600 {
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

    let mono_root = match appid {
        105600 => crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-x86"),
        _ => crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("mono-arm64"),
    };
    let mono_bin = mono_root.join("bin").join("mono");
    let mcs_exe = home
        .join(".metalsharp")
        .join("runtime")
        .join(match appid {
            105600 => "mono-x86",
            _ => "mono-arm64",
        })
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
    if !fna_dll.exists() {
        return;
    }

    let shims_dir = PathBuf::from(find_shims_dir());
    let _ = std::fs::create_dir_all(&shims_dir);
    deploy_fna_native_shims(game_dir, &shims_dir);
    let shared_native_libs = [
        ("libFNA3D.dylib", None),
        ("libFNA3D.0.dylib", Some("libFNA3D.dylib")),
        ("libSDL3.0.dylib", Some("libSDL3.dylib")),
        ("libSDL3.dylib", None),
        ("libFAudio.0.dylib", Some("libFAudio.dylib")),
        ("libFAudio.dylib", None),
        ("libCSteamworks.dylib", None),
        ("libfmod.dylib", None),
        ("libfmodstudio.dylib", None),
        ("libsteam_api.dylib", None),
        ("libnfd.dylib", None),
    ];

    for (lib, symlink) in &shared_native_libs {
        copy_fna_native_lib(game_dir, &shims_dir, lib, *symlink);
    }

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
        ("libSDL3.0.dylib", Some("libSDL3.dylib")),
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

    let gdiplus_src =
        home.join("repos").join("metalsharp").join("src").join("fna").join("terraria").join("gdiplus_stub.c");
    if !game_dir.join("libgdiplus.dylib").exists() && gdiplus_src.exists() {
        let _ = Command::new("clang")
            .args(["-shared", "-arch", "x86_64", "-o"])
            .arg(game_dir.join("libgdiplus.dylib"))
            .arg(&gdiplus_src)
            .args(["-install_name", "@loader_path/libgdiplus.dylib"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    if appid == 105600 {
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

    if !game_dir.join("FNA.dll").exists() {
        let _ = std::fs::copy(&fna_dll, game_dir.join("FNA.dll"));
    }
    for name in &xna_assemblies {
        let dst = game_dir.join(name);
        if !dst.exists() {
            let _ = std::fs::copy(&fna_dll, dst);
        }
    }

    let _ = std::fs::write(game_dir.join("steam_appid.txt"), appid.to_string());
    deploy_offline_steamworks_net(game_dir, &metalsharp_home);
}

fn deploy_fna_native_shims(game_dir: &PathBuf, shims_dir: &PathBuf) {
    for spec in FNA_NATIVE_SHIMS {
        ensure_fna_native_shim_in_cache(spec, shims_dir);
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

    for spec in FNA_NATIVE_SHIMS {
        ensure_fna_native_shim_in_cache(spec, &shims_dir);
    }

    ensure_fna_runtime_assembly(&fna_dir, &ms_home)?;

    let fna3d_build = find_repo_source(&["src", "fna", "FNA3D", "build"])
        .unwrap_or_else(|| home.join("metalsharp").join("src").join("fna").join("FNA3D").join("build"));

    if fna3d_build.exists() {
        let src = fna3d_build.join("libFNA3D.dylib");
        if src.exists() {
            let _ = std::fs::copy(&src, shims_dir.join("libFNA3D.dylib"));
        }
    }

    let sdl3_candidates =
        [PathBuf::from("/opt/homebrew/lib/libSDL3.0.dylib"), PathBuf::from("/usr/local/lib/libSDL3.0.dylib")];
    for sdl3 in &sdl3_candidates {
        if sdl3.exists() {
            let dst = shims_dir.join("libSDL3.0.dylib");
            let _ = std::fs::copy(sdl3, &dst);
            let _ = Command::new("/usr/bin/install_name_tool")
                .args(["-id", "@loader_path/libSDL3.0.dylib"])
                .arg(&dst)
                .output();

            let fna3d = shims_dir.join("libFNA3D.dylib");
            if fna3d.exists() {
                let _ = Command::new("/usr/bin/install_name_tool")
                    .args(["-change", "/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib", "@loader_path/libSDL3.0.dylib"])
                    .arg(&fna3d)
                    .output();
            }

            let symlink = shims_dir.join("libSDL3.dylib");
            if !symlink.exists() {
                let _ = std::os::unix::fs::symlink("libSDL3.0.dylib", symlink);
            }
            break;
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
    Ok(present)
}

fn fna_required_runtime_assets(runtime: &Path) -> Vec<PathBuf> {
    vec![
        runtime.join("fna").join("FNA.dll"),
        runtime.join("shims").join("libFNA3D.dylib"),
        runtime.join("shims").join("libSDL3.dylib"),
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
        FnaShimSource::RepoC { parts } => {
            if let Some(source) = find_fna_shim_source(parts) {
                let _ = build_fna_c_shim(&source, &dst, spec.output, &[]);
            }
        },
        FnaShimSource::RepoObjC { parts, frameworks } => {
            if let Some(source) = find_fna_shim_source(parts) {
                let _ = build_fna_c_shim(&source, &dst, spec.output, frameworks);
            }
        },
        FnaShimSource::BundledNative => {
            if let Some(source) = find_bundled_native_shim(spec.output) {
                let _ = std::fs::copy(source, &dst);
                codesign_fna_shim(&dst);
            }
        },
    }
}

fn build_fna_c_shim(source: &PathBuf, output: &PathBuf, install_name: &str, frameworks: &[&str]) -> bool {
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

    if let Some(source) = find_repo_source(&["src", "fna", "terraria", "faudio_stub.c"]) {
        let output = game_dir.join("libFAudio.0.dylib");
        let _ = Command::new("clang")
            .args(["-shared", "-arch", "x86_64", "-o"])
            .arg(&output)
            .arg(&source)
            .args(["-install_name", "@loader_path/libFAudio.0.dylib"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
        ensure_fna_symlink(game_dir, "libFAudio.0.dylib", "libFAudio.dylib");
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
    let src = shims_dir.join(lib);
    if src.exists() && !game_dir.join(lib).exists() {
        let _ = std::fs::copy(&src, game_dir.join(lib));
    }
    if let Some(sym) = symlink {
        ensure_fna_symlink(game_dir, lib, sym);
    }
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

fn deploy_offline_steamworks_net(game_dir: &PathBuf, metalsharp_home: &PathBuf) {
    let steamworks = game_dir.join("Steamworks.NET.dll");
    if !steamworks.exists() {
        return;
    }
    let backup = game_dir.join("Steamworks.NET.dll.metalsharp-original");
    if !backup.exists() {
        let _ = std::fs::copy(&steamworks, &backup);
    }

    let source = match find_repo_source(&["src", "fna", "shims", "SteamworksOffline.cs"]) {
        Some(path) => path,
        None => return,
    };
    let output = steamworks;
    let mono_roots =
        [metalsharp_home.join("runtime").join("mono-x86"), metalsharp_home.join("runtime").join("mono-arm64")];
    for mono_root in &mono_roots {
        let mono = mono_root.join("bin").join("mono");
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
            return;
        }
    }
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

pub fn deploy_goldberg_internal(home: &PathBuf, game_dir: &PathBuf, appid: u32) {
    let goldberg_dir = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("goldberg");
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
    fn fna_runtime_repair_requires_managed_assembly_and_native_shims() {
        let runtime = PathBuf::from("/tmp/metalsharp-runtime");
        let required = fna_required_runtime_assets(&runtime);

        assert!(required.contains(&runtime.join("fna").join("FNA.dll")));
        assert!(required.contains(&runtime.join("shims").join("libFNA3D.dylib")));
        assert!(required.contains(&runtime.join("shims").join("libSDL3.dylib")));
        assert!(required.contains(&runtime.join("shims").join(FNA_CARBON_SHIM)));
        assert!(required.contains(&runtime.join("shims").join(FNA_CARBON_INTERPOSE_SHIM)));
        assert_eq!(required.len(), 7);
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

    fn test_dir(name: &str) -> PathBuf {
        let mut dir = std::env::temp_dir();
        dir.push(format!("metalsharp-launcher-{}-{}-{}", name, std::process::id(), unique_suffix()));
        dir
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
