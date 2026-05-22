use super::engine::{get_pipeline, PipelineId, PipelineNode};
use std::fs::OpenOptions;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Duration;

const DEFAULT_BRIDGE_PORT: u16 = 18733;

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
    args: &'a [&'a str],
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
    let bridge_exe = home.join(".metalsharp").join("runtime").join("steam-bridge").join("steambridge.exe");
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    let prefix = home.join(".metalsharp").join("prefix-steam");

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
    let shim_src = home.join(".metalsharp").join("runtime").join("steam-bridge").join("libsteam_api.dylib");
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
        PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 | PipelineId::M13 => {
            launch_dxmt_metal(appid, node)
        },
        PipelineId::M32 => launch_wine_bare(appid, node),
        PipelineId::FnaArm64 => launch_fna_arm64(appid),
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
    let node = get_pipeline(pipeline_id);
    let log_path = crate::bottles::steam_compatdata_launch_log_path(appid);

    let result = match pipeline_id {
        PipelineId::M9 | PipelineId::M10 | PipelineId::M11 | PipelineId::M12 | PipelineId::M13 => {
            launch_dxmt_metal_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
        },
        PipelineId::M32 | PipelineId::WineBare => {
            launch_wine_bare_with_context(appid, node, Some(prefix_path), extra_env, Some(&log_path))
        },
        PipelineId::FnaArm64 | PipelineId::Steam | PipelineId::MacSteam => {
            Err("Steam bottle launch only supports Wine-backed MTSP game pipelines".into())
        },
    }?;

    Ok((result.0, result.1, log_path))
}

pub fn launch_auto(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    launch_with_pipeline(appid, pipeline_id)
}

pub fn prepare_pipeline(appid: u32) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    let node = get_pipeline(pipeline_id);
    if node.backend == "gptk" {
        let home = dirs::home_dir().ok_or("no home dir")?;
        ensure_gptk_unix_links(&home.join(".metalsharp").join("runtime").join("wine"))?;
    }
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let deployed_sources: Vec<String> = recipe.dlls.iter().map(|dll| dll.source_subpath.clone()).collect();
    deploy_recipe_dlls(&recipe)?;

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
    let node = get_pipeline(pipeline_id);
    match pipeline_id {
        PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M13
        | PipelineId::M32
        | PipelineId::WineBare => {},
        PipelineId::FnaArm64 | PipelineId::Steam | PipelineId::MacSteam => {
            return Err("Steam route handoff only supports Wine-backed MTSP pipelines".into());
        },
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    if node.backend == "gptk" {
        ensure_gptk_unix_links(&home.join(".metalsharp").join("runtime").join("wine"))?;
    }
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    validate_recipe_runtime(&recipe)?;
    if !recipe.dlls.is_empty() {
        deploy_recipe_dlls(&recipe)?;
    }

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
    let node = get_pipeline(pipeline_id);
    match pipeline_id {
        PipelineId::M9
        | PipelineId::M10
        | PipelineId::M11
        | PipelineId::M12
        | PipelineId::M13
        | PipelineId::M32
        | PipelineId::WineBare => {},
        PipelineId::FnaArm64 | PipelineId::Steam | PipelineId::MacSteam => {
            return Err("Sharp Library apps must use Auto, Wine, M9, M10, M11, M12, or M32".into());
        },
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    if node.backend == "gptk" {
        ensure_gptk_unix_links(&ms_root)?;
    }
    let mut recipe = super::recipe::build_custom_launch_recipe(launch_id, node, game_dir, Some(exe_path))?;
    recipe.launch_args.extend(launch_args.iter().cloned());
    if node.deploy_dlls.is_empty() {
        validate_recipe_runtime(&recipe)?;
    } else {
        deploy_recipe_dlls(&recipe)?;
    }

    let prefix = options.prefix_path.unwrap_or_else(|| home.join(".metalsharp").join("prefix-steam"));
    std::fs::create_dir_all(&prefix)?;
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let exe_name = exe_path.file_name().ok_or("game exe not found")?.to_string_lossy().to_string();
    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let cache_paths = build_cache_paths(&home, node, launch_id);
    let mut cmd = wine_command_for_node(node, &wine, &dyld_path)?;
    cmd.current_dir(exe_dir).env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all").env("WINEDEBUGGER", "none");
    if node.backend != "gptk" {
        cmd.env(runtime_lib_key, &dyld_path);
    }

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

    apply_steam_identity_env(&mut cmd, launch_id);
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

fn apply_steam_identity_env(cmd: &mut Command, appid: u32) {
    let appid = appid.to_string();
    cmd.env("SteamAppId", &appid).env("SteamGameId", &appid).env("SteamOverlayGameId", &appid);
}

fn launch_dxmt_metal_with_context(
    appid: u32,
    node: &PipelineNode,
    prefix_override: Option<&Path>,
    extra_env: &[(String, String)],
    log_path: Option<&Path>,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    if node.backend == "gptk" {
        ensure_gptk_unix_links(&ms_root)?;
    }
    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix =
        prefix_override.map(Path::to_path_buf).unwrap_or_else(|| home.join(".metalsharp").join("prefix-steam"));
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    deploy_recipe_dlls(&recipe)?;

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let cache_paths = build_cache_paths(&home, node, appid);
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = wine_command_for_node(node, &wine, &dyld_path)?;
    cmd.current_dir(exe_dir).env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all").env("WINEDEBUGGER", "none");
    if node.backend != "gptk" {
        cmd.env(runtime_lib_key, &dyld_path);
    }

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }
    for (key, value) in extra_env {
        cmd.env(key, value);
    }

    apply_steam_identity_env(&mut cmd, appid);
    cmd.arg(&exe_name);
    cmd.args(&node.launch_args);
    attach_launch_log(
        &mut cmd,
        log_path,
        LaunchLogContext {
            appid,
            node,
            prefix: &prefix,
            cwd: exe_dir,
            exe_name: &exe_name,
            args: &node.launch_args,
            cache_paths: cache_paths.as_ref(),
        },
    )?;
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_wine_bare(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    launch_wine_bare_with_context(appid, node, None, &[], None)
}

fn wine_command_for_node(
    node: &PipelineNode,
    default_wine: &Path,
    dyld_path: &str,
) -> Result<Command, Box<dyn std::error::Error>> {
    if node.backend != "gptk" {
        return Ok(Command::new(default_wine));
    }

    let gptk_root = PathBuf::from("/Applications/Game Porting Toolkit.app/Contents/Resources/wine");
    let gptk_wine = gptk_root.join("bin").join("wine64");
    let gptk_server = gptk_root.join("bin").join("wineserver");
    if !gptk_wine.exists() || !gptk_server.exists() {
        return Err("Game Porting Toolkit Wine is missing - install GPTK to use M13".into());
    }

    let gptk_dyld = format!(
        "{}:{}:{}:{}",
        gptk_root.join("lib").join("wine").join("x86_64-unix").display(),
        gptk_root.join("lib").join("external").display(),
        gptk_root.join("lib").display(),
        dyld_path
    );

    let mut cmd = Command::new("arch");
    cmd.arg("-x86_64")
        .arg(&gptk_wine)
        .env("WINESERVER", gptk_server)
        .env("WINELOADER", gptk_wine)
        .env(
            "WINEDLLPATH",
            format!(
                "{}:{}",
                gptk_root.join("lib").join("wine").join("x86_64-windows").display(),
                gptk_root.join("lib").join("wine").join("i386-windows").display()
            ),
        )
        .env("DYLD_FALLBACK_LIBRARY_PATH", gptk_dyld);
    Ok(cmd)
}

fn ensure_gptk_unix_links(ms_root: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let gptk_root = ms_root.join("lib").join("gptk");
    let shared = gptk_root.join("external").join("libd3dshared.dylib");
    if !shared.exists() {
        return Err(format!("GPTK shared library is missing: {}", shared.display()).into());
    }
    let link_dirs = [
        (gptk_root.join("x86_64-unix"), "../external/libd3dshared.dylib"),
        (ms_root.join("lib").join("wine").join("x86_64-unix"), "../../gptk/external/libd3dshared.dylib"),
    ];
    for (dir, target) in link_dirs {
        std::fs::create_dir_all(&dir)?;
        for name in ["d3d12.so", "dxgi.so"] {
            let link = dir.join(name);
            if link.exists() {
                continue;
            }
            if std::fs::symlink_metadata(&link).is_ok() {
                std::fs::remove_file(&link)?;
            }
            std::os::unix::fs::symlink(target, &link)?;
        }
    }
    Ok(())
}

fn launch_wine_bare_with_context(
    appid: u32,
    node: &PipelineNode,
    prefix_override: Option<&Path>,
    extra_env: &[(String, String)],
    log_path: Option<&Path>,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let recipe = super::recipe::build_launch_recipe(appid, node)?;
    let game_dir = recipe.game_dir.as_ref().ok_or("game dir not found")?;
    let exe_path = recipe.exe_path.as_ref().ok_or("game exe not found")?;
    let exe_dir = launch_working_dir(game_dir, exe_path);
    let prefix =
        prefix_override.map(Path::to_path_buf).unwrap_or_else(|| home.join(".metalsharp").join("prefix-steam"));
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = exe_path.file_name().unwrap_or_default().to_string_lossy().to_string();

    validate_recipe_runtime(&recipe)?;

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);
    let runtime_lib_key =
        crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");

    let mut cmd = Command::new(&wine);
    cmd.current_dir(exe_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .env(runtime_lib_key, &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    let cache_paths = build_cache_paths(&home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    for (key, value) in extra_env {
        cmd.env(key, value);
    }

    apply_steam_identity_env(&mut cmd, appid);
    cmd.arg(&exe_name);
    cmd.args(&node.launch_args);
    attach_launch_log(
        &mut cmd,
        log_path,
        LaunchLogContext {
            appid,
            node,
            prefix: &prefix,
            cwd: exe_dir,
            exe_name: &exe_name,
            args: &node.launch_args,
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
    let metalsharp_home = home.join(".metalsharp");
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

fn launch_fna_arm64(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::FnaArm64);
    let game_dir = resolve_fna_game_dir(appid)?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
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

    let cache_paths = build_cache_paths(&home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &home.join(".metalsharp").join("runtime").join("wine"));

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe);

    let child = cmd.spawn()?;
    Ok((child.id(), "xna_fna_arm64"))
}

fn find_mono_binary() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/mono"),
        PathBuf::from("/usr/local/bin/mono"),
        PathBuf::from("/usr/bin/mono"),
        home.join(".metalsharp").join("runtime").join("mono-arm64").join("bin").join("mono"),
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

fn build_cache_paths(home: &PathBuf, node: &PipelineNode, appid: u32) -> Option<CachePaths> {
    let subdir = node.shader_cache_subdir?;
    let shader_base = home.join(".metalsharp").join("shader-cache").join(subdir).join(appid.to_string());
    let pipeline_base = home.join(".metalsharp").join("pipeline-cache").join(subdir).join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_base);
    let _ = std::fs::create_dir_all(&pipeline_base);
    super::shader_cache::deploy_preset_cache(home, subdir, appid);
    Some(CachePaths {
        shader: shader_base.to_string_lossy().to_string(),
        pipeline: pipeline_base.to_string_lossy().to_string(),
    })
}

fn steam_pipeline_env_pairs(home: &PathBuf, node: &PipelineNode, appid: u32) -> Vec<(String, String)> {
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let cache_paths = build_cache_paths(home, node, appid);
    let appid_string = appid.to_string();
    let mut env = vec![("SteamAppId".to_string(), appid_string.clone()), ("SteamGameId".to_string(), appid_string)];

    if node.backend != "gptk" && !node.dyld_paths.is_empty() {
        let runtime_lib_key =
            crate::platform::runtime_library_env(&ms_root).map(|(key, _)| key).unwrap_or("LD_LIBRARY_PATH");
        env.push((runtime_lib_key.to_string(), build_dyld(&ms_root, &node.dyld_paths)));
    }
    if let Some(overrides) = node.wine_overrides {
        env.push(("WINEDLLOVERRIDES".to_string(), overrides.to_string()));
    }
    if node.backend == "dxmt" {
        env.push(("DXMT_CONFIG_FILE".to_string(), ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string()));
    }
    env.extend(cache_env_pairs(node, cache_paths.as_ref(), &ms_root));
    env.extend(node.env_vars.iter().map(|ev| (ev.key.to_string(), ev.value.to_string())));
    env
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
    let candidates =
        vec![home.join(".metalsharp").join("configs").join(name), home.join("metalsharp").join("configs").join(name)];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    home.join(".metalsharp").join("configs").join(name).to_string_lossy().to_string()
}

fn resolve_fna_game_dir(appid: u32) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
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

    let mono_bin = home.join(".metalsharp").join("runtime").join("mono-arm64").join("bin").join("mono");
    let mcs_exe = home
        .join(".metalsharp")
        .join("runtime")
        .join("mono-arm64")
        .join("lib")
        .join("mono")
        .join("4.5")
        .join("mcs.exe");
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
    let candidates =
        vec![home.join(".metalsharp").join("runtime").join("shims"), home.join(".metalsharp").join("shims")];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    home.join(".metalsharp").join("runtime").join("shims").to_string_lossy().to_string()
}

fn deploy_fna_assemblies(game_dir: &PathBuf) {
    let home = match dirs::home_dir() {
        Some(h) => h,
        None => return,
    };
    let fna_dll = home.join(".metalsharp").join("runtime").join("fna").join("FNA.dll");
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
    let goldberg_dir = home.join(".metalsharp").join("runtime").join("goldberg");
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
    let home = dirs::home_dir().unwrap_or_default();
    let eac_dir = home.join(".metalsharp").join("runtime").join("eac-toggle").join("x86_64-windows");
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

        let runtime_lib_key =
            crate::platform::runtime_library_env(&home.join(".metalsharp").join("runtime").join("wine"))
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
        let prefix = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-steam");

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

    fn unique_suffix() -> u128 {
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).expect("system time").as_nanos()
    }
}
