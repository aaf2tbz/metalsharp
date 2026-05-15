use super::engine::{get_pipeline, DllDeploy, PipelineId, PipelineNode};
use std::path::PathBuf;
use std::process::Command;

pub fn launch_with_pipeline(
    appid: u32,
    pipeline_id: PipelineId,
) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(pipeline_id);

    match pipeline_id {
        PipelineId::FnaArm64 => launch_fna_arm64(appid),
        PipelineId::FnaX86 => launch_fna_x86(appid),
        PipelineId::M11 => launch_dxmt_metal(appid, node),
        PipelineId::M12 => launch_dxmt_metal12(appid, node),
        PipelineId::M9 => launch_d3d9_metal(appid, node),
        PipelineId::M9Gl => launch_wined3d(appid, node),
        PipelineId::M32Vk => launch_dxvk_metal32(appid, node),
        PipelineId::M32W => launch_wined3d(appid, node),
        PipelineId::M64 | PipelineId::WineBare => launch_wine_bare(appid, node),
        PipelineId::Steam => launch_steam(appid),
        PipelineId::SteamMetalfx => launch_steam_metalfx(appid),
        PipelineId::SteamD3DMetalPerf => launch_steam_d3dmetal_perf(appid),
        PipelineId::MonoGeneric => launch_fna_arm64(appid),
    }
}

pub fn launch_auto(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    launch_with_pipeline(appid, pipeline_id)
}

pub fn prepare_pipeline(appid: u32) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let pipeline_id = super::rules::resolve_pipeline(appid);
    let node = get_pipeline(pipeline_id);

    let game_dir =
        crate::setup::resolve_game_dir(appid).ok_or_else(|| format!("game directory not found for appid {}", appid))?;

    deploy_dlls_for_pipeline(&game_dir, node);

    Ok(serde_json::json!({
        "ok": true,
        "appid": appid,
        "pipeline": node.id,
        "pipeline_name": node.name,
        "deployed_dlls": node.deploy_dlls.len(),
    }))
}

pub fn deploy_dlls_for_pipeline(game_dir: &PathBuf, node: &PipelineNode) {
    if node.deploy_dlls.is_empty() {
        return;
    }

    let home = dirs::home_dir().unwrap_or_default();
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");

    for deploy in &node.deploy_dlls {
        let src = ms_root.join(deploy.source_subpath).join(deploy.filename);
        let dst = game_dir.join(deploy.filename);
        if src.exists() {
            let _ = std::fs::copy(&src, &dst);
        }
    }
}

fn launch_dxmt_metal(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe = resolve_game_exe(&game_dir);
    let exe_name = std::path::Path::new(&exe).file_name().unwrap_or_default().to_string_lossy().to_string();

    deploy_dlls_for_pipeline(&game_dir, node);

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);

    let shader_cache_path = build_shader_cache(&home, node, appid);
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    if let Some(cache) = &shader_cache_path {
        cmd.env("DXMT_SHADER_CACHE_PATH", format!("{}/", cache));
    }
    cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_dxmt_metal12(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    launch_dxmt_metal(appid, node)
}

fn launch_d3d9_metal(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe = resolve_game_exe(&game_dir);
    let exe_name = std::path::Path::new(&exe).file_name().unwrap_or_default().to_string_lossy().to_string();

    deploy_dlls_for_pipeline(&game_dir, node);
    deploy_goldberg(&home, &game_dir, appid);

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_wined3d(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe = resolve_game_exe(&game_dir);
    let exe_name = std::path::Path::new(&exe).file_name().unwrap_or_default().to_string_lossy().to_string();

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_dxvk_metal32(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    deploy_dlls_for_pipeline(&game_dir, node);
    deploy_goldberg(&home, &game_dir, appid);

    let (exe_name, work_dir) = match appid {
        620 => {
            if let Some(d3d9) = find_dll_deploy(&node.deploy_dlls, "d3d9.dll") {
                let home2 = dirs::home_dir().unwrap_or_default();
                let ms_root2 = home2.join(".metalsharp").join("runtime").join("wine");
                let src = ms_root2.join(d3d9.source_subpath).join(d3d9.filename);
                let _ = std::fs::copy(&src, game_dir.join("bin").join("d3d9.dll"));
            }
            (String::from("portal2.exe"), game_dir.clone())
        },
        265930 => {
            let bin = game_dir.join("Binaries").join("Win32");
            if let Some(d3d9) = find_dll_deploy(&node.deploy_dlls, "d3d9.dll") {
                let home2 = dirs::home_dir().unwrap_or_default();
                let ms_root2 = home2.join(".metalsharp").join("runtime").join("wine");
                let src = ms_root2.join(d3d9.source_subpath).join(d3d9.filename);
                let _ = std::fs::copy(&src, bin.join("d3d9.dll"));
            }
            (String::from("GoatGame-Win32-Shipping.exe"), bin.clone())
        },
        _ => {
            let exe = resolve_game_exe(&game_dir);
            let name = std::path::Path::new(&exe).file_name().unwrap_or_default().to_string_lossy().to_string();
            (name, game_dir.clone())
        },
    };

    let dyld_wine = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
    let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
    let moltenvk_str = if moltenvk_icd.exists() {
        moltenvk_icd.to_string_lossy().to_string()
    } else {
        ms_root
            .join("etc")
            .join("etc")
            .join("vulkan")
            .join("icd.d")
            .join("MoltenVK_icd.json")
            .to_string_lossy()
            .to_string()
    };

    let shader_cache_base = home.join(".metalsharp").join("shader-cache").join("dxvk-metal32").join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_cache_base);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&work_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_wine)
        .env("VK_ICD_FILENAMES", &moltenvk_str)
        .env("DXVK_STATE_CACHE_PATH", format!("{}/", shader_cache_base.to_string_lossy()));

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    cmd.arg(&exe_name);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_wine_bare(appid: u32, node: &PipelineNode) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe = resolve_game_exe(&game_dir);
    let exe_name = std::path::Path::new(&exe).file_name().unwrap_or_default().to_string_lossy().to_string();

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    cmd.arg(&exe_name);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_steam(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let pid = crate::launch::launch_via_steam(appid)?;
    Ok((pid, "steam"))
}

fn launch_steam_metalfx(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::SteamMetalfx);
    let env: Vec<(&str, &str)> = node.env_vars.iter().map(|e| (e.key, e.value)).collect();
    let pid = crate::launch::launch_via_steam_with_env(appid, &env)?;
    Ok((pid, "steam_metalfx"))
}

fn launch_steam_d3dmetal_perf(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::SteamD3DMetalPerf);
    let env: Vec<(&str, &str)> = node.env_vars.iter().map(|e| (e.key, e.value)).collect();
    let pid = crate::launch::launch_via_steam_with_env(appid, &env)?;
    Ok((pid, "steam_d3dmetal_perf"))
}

fn launch_fna_arm64(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };

    let exe = match appid {
        105600 => dir.join("TerrariaLauncher.exe"),
        _ => resolve_game_exe(dir).into(),
    };

    let mono_config = find_config("terraria-mono.config");
    let dyld = format!("{}:/opt/homebrew/lib", dir.to_string_lossy());

    let mut cmd = Command::new("mono");
    cmd.current_dir(dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("FNA3D_DRIVER", "OpenGL")
        .arg(&exe);

    let child = cmd.spawn()?;
    Ok((child.id(), "xna_fna_arm64"))
}

fn launch_fna_x86(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let game_dir = crate::setup::resolve_game_dir(appid).ok_or("game dir not found")?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };
    let mono_x86 = home.join(".metalsharp").join("runtime").join("mono-x86").join("bin").join("mono");

    if !mono_x86.exists() {
        return Err("x86 mono not found — run setup first".into());
    }

    let mono_config = find_config("celeste-x86-mono.config");
    let shims_dir = find_shims_dir();
    let dyld = format!(
        "{}:{}/lib:/opt/homebrew/lib:.:{}",
        shims_dir,
        home.join(".metalsharp").join("runtime").join("mono-x86").join("lib").to_string_lossy(),
        shims_dir,
    );
    let mono_path = home.join(".metalsharp").join("runtime").join("mono-x86").join("lib").join("mono").join("4.5");

    let exe = match appid {
        504230 => dir.join("Celeste.exe"),
        _ => resolve_game_exe(dir).into(),
    };

    let child = Command::new("arch")
        .args(["-x86_64", &mono_x86.to_string_lossy()])
        .current_dir(dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("MONO_PATH", mono_path.to_string_lossy().to_string())
        .env("FNA3D_DRIVER", "OpenGL")
        .env("METAL_DEVICE_WRAPPER_TYPE", "0")
        .arg(&exe)
        .spawn()?;

    Ok((child.id(), "xna_fna_x86"))
}

fn build_dyld(ms_root: &PathBuf, paths: &[&str]) -> String {
    paths.iter().map(|p| ms_root.join(p).to_string_lossy().to_string()).collect::<Vec<_>>().join(":")
}

fn build_shader_cache(home: &PathBuf, node: &PipelineNode, appid: u32) -> Option<String> {
    let subdir = node.shader_cache_subdir?;
    let cache_base = home.join(".metalsharp").join("shader-cache").join(subdir).join(appid.to_string());
    let _ = std::fs::create_dir_all(&cache_base);
    Some(cache_base.to_string_lossy().to_string())
}

fn resolve_game_exe(game_dir: &PathBuf) -> String {
    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_lowercase();
            if name.ends_with(".exe")
                && !name.contains("setup")
                && !name.contains("redist")
                && !name.contains("uninstall")
                && !name.contains("vcredist")
                && !name.contains("installer")
                && !name.contains("crashhandler")
            {
                return entry.path().to_string_lossy().to_string();
            }
        }
    }
    game_dir.to_string_lossy().to_string()
}

fn find_config(name: &str) -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates =
        vec![home.join("metalsharp").join("configs").join(name), home.join(".metalsharp").join("configs").join(name)];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    home.join("metalsharp").join("configs").join(name).to_string_lossy().to_string()
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

fn find_dll_deploy<'a>(deploys: &'a [DllDeploy], filename: &str) -> Option<&'a DllDeploy> {
    deploys.iter().find(|d| d.filename == filename)
}

fn deploy_goldberg(home: &PathBuf, game_dir: &PathBuf, appid: u32) {
    let goldberg_dir = home.join(".metalsharp").join("runtime").join("goldberg");
    if !goldberg_dir.exists() {
        return;
    }

    let targets: Vec<PathBuf> = vec![
        game_dir.clone(),
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
    let _ = std::fs::write(
        steam_settings.join("force_steam_appid.txt"),
        appid.to_string(),
    );
}
