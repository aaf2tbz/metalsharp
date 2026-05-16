use super::engine::{get_pipeline, PipelineId, PipelineNode};
use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::PathBuf;
use std::process::Command;
use std::time::Duration;

static BRIDGE_PORT: u16 = 18733;

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

    let home = dirs::home_dir().ok_or("no home dir")?;
    let bridge_exe = home.join(".metalsharp").join("runtime").join("steam-bridge").join("steambridge.exe");
    let wine = home.join(".metalsharp").join("runtime").join("wine").join("bin").join("metalsharp-wine");
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
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

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

    let cache_paths = build_cache_paths(&home, node, appid);
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(&game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path);

    if let Some(overrides) = node.wine_overrides {
        cmd.env("WINEDLLOVERRIDES", overrides);
    }

    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);
    cmd.env("DXMT_CONFIG_FILE", &dxmt_config_file);

    for ev in &node.env_vars {
        cmd.env(ev.key, ev.value);
    }

    cmd.arg(&exe_name);
    cmd.args(&node.launch_args);
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

    let cache_paths = build_cache_paths(&home, node, appid);
    apply_cache_env(&mut cmd, node, cache_paths.as_ref(), &ms_root);

    cmd.arg(&exe_name);
    cmd.args(&node.launch_args);
    let child = cmd.spawn()?;
    Ok((child.id(), node.id.to_legacy_method()))
}

fn launch_steam(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let node = get_pipeline(PipelineId::Steam);
    let cache_paths = build_cache_paths(&home, node, appid);
    let env = cache_env_pairs(node, cache_paths.as_ref(), &ms_root);
    let pid = crate::launch::launch_via_steam_with_env(appid, &env)?;
    Ok((pid, "steam"))
}

fn launch_macos_steam(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let node = get_pipeline(PipelineId::MacSteam);
    let cache_paths = build_cache_paths(&home, node, appid);
    let env = cache_env_pairs(node, cache_paths.as_ref(), &ms_root);
    let result = crate::steam::launch_macos_steam_game_with_env(appid, &env)?;
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
    let dyld = format!("{}:{}:{}:/opt/homebrew/lib", dir.to_string_lossy(), shims_dir, mono_lib.to_string_lossy());

    let mut cmd = Command::new(&mono_bin);
    cmd.current_dir(dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
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
        home.join(".metalsharp").join("runtime").join("mono-arm64").join("bin").join("mono"),
    ];
    for c in candidates {
        if c.exists() {
            return Ok(c);
        }
    }
    Err("Mono not found — install with: brew install mono".into())
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
