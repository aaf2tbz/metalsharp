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
        PipelineId::MonoGeneric => launch_mono_generic(appid),
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

    if let Some(subdir) = node.shader_cache_subdir {
        super::shader_cache::deploy_preset_cache(&home, subdir, appid);
    }

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
    let (exe_name, work_dir) = resolve_d3d9_exe(appid, &game_dir);

    deploy_dlls_for_pipeline(&game_dir, node);
    deploy_goldberg(&home, &game_dir, appid);

    let dyld_path = build_dyld(&ms_root, &node.dyld_paths);

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&work_dir)
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

    let (exe_name, work_dir) = resolve_d3d9_exe(appid, &game_dir);

    if let Some(d3d9) = find_dll_deploy(&node.deploy_dlls, "d3d9.dll") {
        let src = ms_root.join(d3d9.source_subpath).join(d3d9.filename);
        let _ = std::fs::copy(&src, work_dir.join("d3d9.dll"));
    }

    let dyld_wine = build_dyld(&ms_root, &node.dyld_paths);
    let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
    let moltenvk_str = if moltenvk_icd.exists() {
        moltenvk_icd.to_string_lossy().to_string()
    } else {
        ms_root.join("vulkan").join("icd.d").join("MoltenVK_icd.json").to_string_lossy().to_string()
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
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");

    let gptk_dyld = "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-unix";
    let gptk_exists = std::path::Path::new(gptk_dyld).exists();

    let mut env: Vec<(String, String)> =
        node.env_vars.iter().map(|e| (e.key.to_string(), e.value.to_string())).collect();

    if gptk_exists {
        let ms_dyld = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
        let dyld = format!("{}:{}", gptk_dyld, ms_dyld);
        env.push(("DYLD_FALLBACK_LIBRARY_PATH".to_string(), dyld));
    }

    let env_refs: Vec<(&str, &str)> = env.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect();
    let pid = crate::launch::launch_via_steam_with_env(appid, &env_refs)?;
    Ok((pid, "steam_metalfx"))
}

fn launch_steam_d3dmetal_perf(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::SteamD3DMetalPerf);
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");

    let gptk_dll = "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-windows";
    let gptk_dyld = "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-unix";
    let gptk_exists = std::path::Path::new(gptk_dll).exists();

    let mut env: Vec<(String, String)> =
        node.env_vars.iter().map(|e| (e.key.to_string(), e.value.to_string())).collect();

    if gptk_exists {
        let ms_dyld = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
        let dyld = format!("{}:{}", gptk_dyld, ms_dyld);
        let wine_dll_path =
            format!("{}:{}", gptk_dll, ms_root.join("lib").join("wine").join("x86_64-windows").to_string_lossy());
        env.push(("WINEDLLPATH".to_string(), wine_dll_path));
        env.push(("DYLD_FALLBACK_LIBRARY_PATH".to_string(), dyld));
    }

    let env_refs: Vec<(&str, &str)> = env.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect();
    let pid = crate::launch::launch_via_steam_with_env(appid, &env_refs)?;
    Ok((pid, "steam_d3dmetal_perf"))
}

fn launch_fna_arm64(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::FnaArm64);
    let game_dir = resolve_fna_game_dir(appid)?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };

    ensure_launcher_exe(appid, dir);
    deploy_fna_assemblies(dir);

    let exe = match appid {
        105600 => find_preferred_exe(dir, &["TerrariaLauncher.exe", "Terraria.exe"])?,
        _ => resolve_game_exe(dir).into(),
    };

    let mono_bin = find_mono_binary()?;
    let mono_config = find_config("terraria-mono.config");
    let shims_dir = find_shims_dir();
    let mono_lib = mono_bin.parent().unwrap_or(std::path::Path::new("")).join("..").join("lib");
    let dyld = format!("{}:{}:{}:/opt/homebrew/lib", dir.to_string_lossy(), shims_dir, mono_lib.to_string_lossy());

    let mut cmd = Command::new(&mono_bin);
    cmd.current_dir(dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("MONO_ENV_OPTIONS", "--runtime=v4.0")
        .env("MONO_PATH", dir.to_string_lossy().to_string());

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe);

    let child = cmd.spawn()?;
    Ok((child.id(), "xna_fna_arm64"))
}

fn launch_fna_x86(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::FnaX86);
    let game_dir = resolve_fna_game_dir(appid)?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };
    let mono_x86 = home.join(".metalsharp").join("runtime").join("mono-x86").join("bin").join("mono");

    if !mono_x86.exists() {
        return Err("x86 mono not found — install via setup or run: metalsharp setup --mono-x86".into());
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

    if !exe.exists() {
        return Err(format!("game exe not found: {}", exe.display()).into());
    }

    ensure_launcher_exe(appid, dir);
    deploy_fna_assemblies(dir);

    let mut cmd = Command::new("arch");
    cmd.args(["-x86_64", &mono_x86.to_string_lossy()])
        .current_dir(dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("MONO_PATH", mono_path.to_string_lossy().to_string());

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe);

    let child = cmd.spawn()?;
    Ok((child.id(), "xna_fna_x86"))
}

fn launch_mono_generic(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let node = get_pipeline(PipelineId::MonoGeneric);
    let game_dir = resolve_fna_game_dir(appid)?;
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let dir = if game_dir.exists() { &game_dir } else { &local_dir };

    let exe = resolve_game_exe(dir);
    let exe_path = std::path::Path::new(&exe);
    if !exe_path.exists() {
        return Err(format!("game exe not found: {}", exe).into());
    }

    deploy_fna_assemblies(dir);

    let mono_bin = find_mono_binary()?;
    let mono_config = find_config("terraria-mono.config");
    let shims_dir = find_shims_dir();
    let mono_lib = mono_bin.parent().unwrap_or(std::path::Path::new("")).join("..").join("lib");
    let dyld = format!("{}:{}:{}:/opt/homebrew/lib", dir.to_string_lossy(), shims_dir, mono_lib.to_string_lossy());

    let mut cmd = Command::new(&mono_bin);
    cmd.current_dir(dir).env("DYLD_LIBRARY_PATH", &dyld).env("MONO_CONFIG", mono_config);

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe);

    let child = cmd.spawn()?;
    Ok((child.id(), "mono_generic"))
}

fn find_mono_binary() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/mono"),
        PathBuf::from("/usr/local/bin/mono"),
        home.join(".metalsharp").join("runtime").join("mono-arm64").join("bin").join("mono"),
    ];
    for c in candidates {
        if c.exists() {
            return Ok(c);
        }
    }
    Err("Mono not found — install with: brew install mono".into())
}

fn resolve_d3d9_exe(appid: u32, game_dir: &PathBuf) -> (String, PathBuf) {
    match appid {
        620 => (String::from("portal2.exe"), game_dir.clone()),
        265930 => {
            let bin = game_dir.join("Binaries").join("Win32");
            (String::from("GoatGame-Win32-Shipping.exe"), bin)
        },
        _ => {
            let exe = resolve_game_exe(game_dir);
            let name = std::path::Path::new(&exe).file_name().unwrap_or_default().to_string_lossy().to_string();
            (name, game_dir.clone())
        },
    }
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
        vec![home.join(".metalsharp").join("configs").join(name), home.join("metalsharp").join("configs").join(name)];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    home.join(".metalsharp").join("configs").join(name).to_string_lossy().to_string()
}

fn resolve_fna_game_dir(appid: u32) -> Result<PathBuf, Box<dyn std::error::Error>> {
    if let Some(dir) = crate::setup::resolve_game_dir(appid) {
        if dir.join(".metalsharp_prepared").exists() {
            return Ok(dir);
        }
        if has_exe_files(&dir) {
            return Ok(dir);
        }
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine_steamapps = home
        .join(".metalsharp")
        .join("prefix-steam")
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

    let manifest_name = format!("appmanifest_{}.acf", appid);
    let manifest_path = wine_steamapps.join(&manifest_name);
    if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
        for line in contents.lines() {
            let trimmed = line.trim();
            if trimmed.starts_with("\"installdir\"") {
                let parts: Vec<&str> = trimmed.splitn(2, ['\t', ' ']).collect();
                if let Some(dir_name) = parts.last().map(|s| s.trim().trim_matches('"')) {
                    let game_dir = wine_steamapps.join("common").join(dir_name);
                    if game_dir.exists() && has_exe_files(&game_dir) {
                        return Ok(game_dir);
                    }
                }
            }
        }
    }

    for wine_lib_path in crate::scan::wine_steam_library_paths() {
        if wine_lib_path == wine_steamapps {
            continue;
        }
        let manifest_path = wine_lib_path.join(&manifest_name);
        if let Ok(contents) = std::fs::read_to_string(&manifest_path) {
            for line in contents.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with("\"installdir\"") {
                    let parts: Vec<&str> = trimmed.splitn(2, ['\t', ' ']).collect();
                    if let Some(dir_name) = parts.last().map(|s| s.trim().trim_matches('"')) {
                        let game_dir = wine_lib_path.join("common").join(dir_name);
                        if game_dir.exists() && has_exe_files(&game_dir) {
                            return Ok(game_dir);
                        }
                    }
                }
            }
        }
    }

    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    if local_dir.exists() {
        return Ok(local_dir);
    }
    Err(format!("no Windows game dir found for appid {}", appid).into())
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

fn find_dll_deploy<'a>(deploys: &'a [DllDeploy], filename: &str) -> Option<&'a DllDeploy> {
    deploys.iter().find(|d| d.filename == filename)
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
