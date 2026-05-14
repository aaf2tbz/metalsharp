use serde_json::json;
use serde_json::Value;
use std::path::PathBuf;
use std::process::Command;

pub fn launch(exe_path: &str, game_type: &str) -> Result<u32, Box<dyn std::error::Error>> {
    match game_type {
        "xna_fna" => launch_via_fna_mono(exe_path),
        _ => launch_via_wine(exe_path),
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Engine {
    FnaArm64,
    FnaX86,
    DxmtMetal,
    DxmtMetal12,
    Wined3d32,
    DxvkMetal32,
    MetalsharpWine,
    SteamBare,
    SteamMetalfx,
    SteamD3DMetalPerf,
}

pub fn recommended_method_for_appid(appid: u32) -> &'static str {
    engine_method(get_engine_for_appid(appid))
}

pub fn engine_description_for_appid(appid: u32) -> &'static str {
    let engine = get_engine_for_appid(appid);
    match engine {
        Engine::FnaArm64 => "FNA (ARM64) — Mono + OpenGL/Metal",
        Engine::FnaX86 => "FNA (x86) — Mono + OpenGL/Metal",
        Engine::DxmtMetal => "DXMT D3D11 → Metal — native Metal translation",
        Engine::DxmtMetal12 => "DXMT D3D12 → Metal — native Metal translation",
        Engine::Wined3d32 => "WineD3D (32-bit) — CPU fallback renderer",
        Engine::DxvkMetal32 => "DXVK D3D9 → MoltenVK → Metal — 32-bit Vulkan translation",
        Engine::MetalsharpWine => "MetalSharp Wine — bare Wine",
        Engine::SteamBare => "Steam Native — macOS native launch",
        Engine::SteamMetalfx => "D3DMetal + MetalFX — spatial upscaling",
        Engine::SteamD3DMetalPerf => "D3DMetal Perf — Apple D3D→Metal with optimizations",
    }
}

fn engine_method(engine: Engine) -> &'static str {
    match engine {
        Engine::FnaArm64 => "xna_fna_arm64",
        Engine::FnaX86 => "xna_fna_x86",
        Engine::DxmtMetal => "dxmt_metal",
        Engine::DxmtMetal12 => "dxmt_metal12",
        Engine::Wined3d32 => "wined3d_32",
        Engine::DxvkMetal32 => "dxvk_metal32",
        Engine::MetalsharpWine => "metalsharp_wine",
        Engine::SteamBare => "steam",
        Engine::SteamMetalfx => "steam_metalfx",
        Engine::SteamD3DMetalPerf => "steam_d3dmetal_perf",
    }
}

fn get_engine_for_appid(appid: u32) -> Engine {
    match appid {
        105600 => Engine::FnaArm64,
        504230 => Engine::SteamD3DMetalPerf,
        265930 | 620 => Engine::DxvkMetal32,
        312520 | 375520 | 848450 => Engine::DxmtMetal,
        535520 => Engine::Wined3d32,
        391540 => Engine::SteamBare,

        2050650 | 1583230 => Engine::SteamD3DMetalPerf,
        3164500 => Engine::DxmtMetal,

        945360 | 1139900 => Engine::SteamBare,

        1245620 => Engine::SteamMetalfx,

        814380 | 1593500 => Engine::SteamMetalfx,

        397540 | 298110 | 552520 | 1091500 | 1868140 | 1551360 | 1716740 | 1203620 | 1282100 | 750920 | 1172380
        | 870780 | 1196590 | 1236300 | 1888160 | 976310 | 2767030 | 292030 | 990080 | 1172470 | 2290180 => {
            Engine::SteamD3DMetalPerf
        },

        548430 | 892970 | 1313140 | 1623730 | 553850 | 367520 | 413150 | 1145360 | 588650 | 1637320 | 1562430
        | 1092790 | 1229490 | 1971650 | 1809540 | 1237320 | 1326470 | 275850 | 1643320 | 379720 | 782330 | 289070
        | 1147560 | 1222680 | 252950 | 230410 | 252490 | 730 => Engine::SteamD3DMetalPerf,

        _ => {
            let game_dir = crate::setup::resolve_game_dir(appid);
            detect_engine_from_dir(&game_dir)
        },
    }
}

fn detect_engine_from_dir(game_dir: &Option<PathBuf>) -> Engine {
    let dir = match game_dir {
        Some(d) if d.exists() => d,
        _ => return Engine::SteamD3DMetalPerf,
    };

    if crate::setup::detect_dotnet_game(dir) {
        return Engine::FnaArm64;
    }

    let has_file_ci = |name: &str| -> bool {
        let name_lower = name.to_lowercase();
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                if entry.file_name().to_string_lossy().to_lowercase() == name_lower {
                    return true;
                }
            }
        }
        false
    };
    let has_dir_ci = |name: &str| -> bool {
        let name_lower = name.to_lowercase();
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                if entry.path().is_dir() && entry.file_name().to_string_lossy().to_lowercase() == name_lower {
                    return true;
                }
            }
        }
        false
    };
    let has_glob = |pattern: &str| -> bool {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_lowercase();
                if name.ends_with(&pattern.to_lowercase()) {
                    return true;
                }
            }
        }
        false
    };

    if has_file_ci("unityplayer.dll") || has_file_ci("gameassembly.dll") {
        return Engine::SteamD3DMetalPerf;
    }

    if has_dir_ci("engine") && has_dir_ci("binaries") {
        return Engine::SteamMetalfx;
    }

    if has_glob(".pak") {
        return Engine::SteamD3DMetalPerf;
    }

    if has_dir_ci("engine") && has_dir_ci("content") {
        return Engine::SteamMetalfx;
    }

    if has_glob(".bdt") || has_glob(".bhd") {
        return Engine::SteamMetalfx;
    }

    if has_glob("re_chunk_") || has_file_ci("re2_config.ini") || has_file_ci("re8_config.ini") {
        return Engine::SteamD3DMetalPerf;
    }

    if has_file_ci("d3dx9_43.dll") {
        return Engine::MetalsharpWine;
    }

    if has_file_ci("steam_api64.dll") || has_file_ci("steam_api.dll") {
        return Engine::SteamD3DMetalPerf;
    }

    Engine::SteamD3DMetalPerf
}

pub fn launch_auto(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let game_dir = crate::setup::resolve_game_dir(appid);
    let engine = get_engine_for_appid(appid);

    match engine {
        Engine::FnaArm64 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("TerrariaLauncher.exe");
            let pid = launch_fna_arm64(&exe.to_string_lossy(), dir)?;
            Ok((pid, "xna_fna_arm64"))
        },
        Engine::FnaX86 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("Celeste.exe");
            let pid = launch_fna_x86(&exe.to_string_lossy(), dir)?;
            Ok((pid, "xna_fna_x86"))
        },
        Engine::DxmtMetal => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_dxmt_metal(appid, &exe, dir)?;
            Ok((pid, "dxmt_metal"))
        },
        Engine::DxmtMetal12 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_dxmt_metal12(appid, &exe, dir)?;
            Ok((pid, "dxmt_metal12"))
        },
        Engine::Wined3d32 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_wined3d_32(&exe, dir)?;
            Ok((pid, "wined3d_32"))
        },
        Engine::DxvkMetal32 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_dxvk_metal32(appid, &exe, dir)?;
            Ok((pid, "dxvk_metal32"))
        },
        Engine::MetalsharpWine => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_metalsharp_wine(&exe, dir)?;
            Ok((pid, "metalsharp_wine"))
        },
        Engine::SteamBare => {
            let pid = launch_via_steam(appid)?;
            Ok((pid, "steam"))
        },
        Engine::SteamMetalfx => {
            let pid = launch_via_steam_with_env(
                appid,
                &[
                    ("D3DM_ENABLE_METALFX", "1"),
                    ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                    ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                    ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                    ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                    ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                    ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                    ("MVK_ALLOW_METAL_FENCES", "1"),
                ],
            )?;
            Ok((pid, "steam_metalfx"))
        },
        Engine::SteamD3DMetalPerf => {
            let gptk_dll = "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-windows";
            let gptk_dyld = "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-unix";
            let ms_root = dirs::home_dir().ok_or("no home dir")?.join(".metalsharp").join("runtime").join("wine");
            let ms_dyld = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
            let dyld = format!("{}:{}", gptk_dyld, ms_dyld);
            let wine_dll_path =
                format!("{}:{}", gptk_dll, ms_root.join("lib").join("wine").join("x86_64-windows").to_string_lossy());
            let pid = launch_via_steam_with_env(
                appid,
                &[
                    ("WINEDLLPATH", &wine_dll_path),
                    ("DYLD_FALLBACK_LIBRARY_PATH", &dyld),
                    ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                    ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                    ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                    ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                    ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                ],
            )?;
            Ok((pid, "steam_d3dmetal_perf"))
        },
    }
}

pub fn launch_with_method(appid: u32, method: &str) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    if method.is_empty() || method == "auto" || method == "native" {
        return launch_auto(appid);
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let game_dir = crate::setup::resolve_game_dir(appid);
    let _dir = game_dir.as_ref().unwrap_or(&local_dir);

    match method {
        "native_metalfx_low" => {
            let pid = launch_via_steam_with_env(
                appid,
                &[
                    ("D3DM_ENABLE_METALFX", "1"),
                    ("D3DM_METALFX_QUALITY", "low"),
                    ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                    ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                    ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                    ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                    ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                    ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                    ("MVK_ALLOW_METAL_FENCES", "1"),
                ],
            )?;
            Ok((pid, "native_metalfx_low"))
        },
        "native_metalfx_medium" => {
            let pid = launch_via_steam_with_env(
                appid,
                &[
                    ("D3DM_ENABLE_METALFX", "1"),
                    ("D3DM_METALFX_QUALITY", "medium"),
                    ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                    ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                    ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                    ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                    ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                    ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                    ("MVK_ALLOW_METAL_FENCES", "1"),
                ],
            )?;
            Ok((pid, "native_metalfx_medium"))
        },
        "native_metalfx_high" => {
            let pid = launch_via_steam_with_env(
                appid,
                &[
                    ("D3DM_ENABLE_METALFX", "1"),
                    ("D3DM_METALFX_QUALITY", "high"),
                    ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                    ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                    ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                    ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                    ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                    ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                    ("MVK_ALLOW_METAL_FENCES", "1"),
                ],
            )?;
            Ok((pid, "native_metalfx_high"))
        },
        _ => {
            let known = [
                "dxmt_metal",
                "dxmt_metal12",
                "wined3d_32",
                "dxvk_metal32",
                "metalsharp_wine",
                "steam",
                "steam_metalfx",
                "steam_d3dmetal_perf",
                "xna_fna_arm64",
                "xna_fna_x86",
            ];
            if known.contains(&method) {
                return launch_auto(appid);
            }
            Err(format!("Unknown launch method: {}", method).into())
        },
    }
}

pub fn launch_via_steam(appid: u32) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = home.join(".metalsharp").join("runtime").join("wine").join("bin").join("metalsharp-wine");
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    if !crate::steam::is_wine_steam_running() {
        crate::steam::launch_wine_steam()?;
        std::thread::sleep(std::time::Duration::from_secs(15));
    }

    let url = format!("steam://run/{}", appid);

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(child.id())
}

pub fn launch_via_steam_with_env(appid: u32, extra_env: &[(&str, &str)]) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = home.join(".metalsharp").join("runtime").join("wine").join("bin").join("metalsharp-wine");
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    if !crate::steam::is_wine_steam_running() {
        crate::steam::launch_wine_steam()?;
        for _ in 0..30 {
            std::thread::sleep(std::time::Duration::from_secs(2));
            if crate::steam::is_wine_steam_running() {
                break;
            }
        }
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    let url = format!("steam://run/{}", appid);

    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all");

    for (key, val) in extra_env {
        cmd.env(key, val);
    }

    let child =
        cmd.args(["start", &url]).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null()).spawn()?;

    Ok(child.id())
}

fn launch_fna_x86(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
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

    let child = Command::new("arch")
        .args(["-x86_64", &mono_x86.to_string_lossy()])
        .current_dir(game_dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("MONO_PATH", mono_path.to_string_lossy().to_string())
        .env("FNA3D_DRIVER", "OpenGL")
        .env("METAL_DEVICE_WRAPPER_TYPE", "0")
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_fna_arm64(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let mono_config = find_config("terraria-mono.config");
    let dyld = format!("{}:/opt/homebrew/lib", game_dir.to_string_lossy());

    let child = Command::new("mono")
        .current_dir(game_dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("FNA3D_DRIVER", "OpenGL")
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_dxmt_metal(appid: u32, exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = std::path::Path::new(exe_path).file_name().unwrap_or_default().to_string_lossy().to_string();

    let dyld_wine = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
    let dyld_dxmt = ms_root.join("lib").join("dxmt").join("x86_64-unix").to_string_lossy().to_string();
    let dyld_path = format!("{}:{}", dyld_wine, dyld_dxmt);

    let dxmt_x64 = ms_root.join("lib").join("dxmt").join("x86_64-windows");
    let game = game_dir.as_path();
    let _ = std::fs::copy(dxmt_x64.join("d3d11.dll"), game.join("d3d11.dll"));
    let _ = std::fs::copy(dxmt_x64.join("dxgi.dll"), game.join("dxgi.dll"));
    let _ = std::fs::copy(dxmt_x64.join("d3d10core.dll"), game.join("d3d10core.dll"));
    let _ = std::fs::copy(dxmt_x64.join("winemetal.dll"), game.join("winemetal.dll"));

    let shader_cache_base = home.join(".metalsharp").join("shader-cache").join("dxmt-metal").join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_cache_base);
    let shader_cache_path = shader_cache_base.to_string_lossy().to_string();
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDLLOVERRIDES", "dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path)
        .env("DXMT_SHADER_CACHE_PATH", format!("{}/", shader_cache_path))
        .env("DXMT_CONFIG_FILE", &dxmt_config_file)
        .env("DXMT_METALFX_SPATIAL_SWAPCHAIN", "1")
        .arg(&exe_name)
        .spawn()?;

    Ok(child.id())
}

fn launch_dxmt_metal12(appid: u32, exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = std::path::Path::new(exe_path).file_name().unwrap_or_default().to_string_lossy().to_string();

    let dyld_wine = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
    let dyld_dxmt = ms_root.join("lib").join("dxmt").join("x86_64-unix").to_string_lossy().to_string();
    let dyld_path = format!("{}:{}", dyld_wine, dyld_dxmt);

    let dxmt_x64 = ms_root.join("lib").join("dxmt").join("x86_64-windows");
    let game = game_dir.as_path();
    let _ = std::fs::copy(dxmt_x64.join("d3d12.dll"), game.join("d3d12.dll"));
    let _ = std::fs::copy(dxmt_x64.join("d3d11.dll"), game.join("d3d11.dll"));
    let _ = std::fs::copy(dxmt_x64.join("dxgi.dll"), game.join("dxgi.dll"));
    let _ = std::fs::copy(dxmt_x64.join("d3d10core.dll"), game.join("d3d10core.dll"));
    let _ = std::fs::copy(dxmt_x64.join("winemetal.dll"), game.join("winemetal.dll"));

    let shader_cache_base = home.join(".metalsharp").join("shader-cache").join("dxmt-metal12").join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_cache_base);
    let shader_cache_path = shader_cache_base.to_string_lossy().to_string();
    let dxmt_config_file = ms_root.join("etc").join("dxmt.conf").to_string_lossy().to_string();

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDLLOVERRIDES", "d3d12,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_path)
        .env("DXMT_SHADER_CACHE_PATH", &shader_cache_path)
        .env("DXMT_CONFIG_FILE", &dxmt_config_file)
        .env("DXMT_METALFX_SPATIAL_SWAPCHAIN", "1")
        .arg(&exe_name)
        .spawn()?;

    Ok(child.id())
}

fn launch_wined3d_32(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = std::path::Path::new(exe_path).file_name().unwrap_or_default().to_string_lossy().to_string();

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDLLOVERRIDES", "dxgi,d3d11=b;gameoverlayrenderer,gameoverlayrenderer64=d;steamclient64,steamclient=d")
        .env("SteamOverlayDisabled", "1")
        .env(
            "DYLD_FALLBACK_LIBRARY_PATH",
            ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string(),
        )
        .arg(&exe_name)
        .spawn()?;

    Ok(child.id())
}

fn launch_dxvk_metal32(appid: u32, exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    let dxvk_i386 = ms_root.join("lib").join("dxvk").join("i386-windows");
    let game = game_dir.as_path();

    let (exe_name, work_dir, d3d9_target) = match appid {
        620 => {
            let _ = std::fs::copy(dxvk_i386.join("d3d9.dll"), game.join("bin").join("d3d9.dll"));
            (String::from("portal2.exe"), game_dir.clone(), game.join("bin"))
        },
        265930 => {
            let bin = game.join("Binaries").join("Win32");
            let _ = std::fs::copy(dxvk_i386.join("d3d9.dll"), bin.join("d3d9.dll"));
            (String::from("GoatGame-Win32-Shipping.exe"), bin.clone(), bin)
        },
        _ => {
            let _ = std::fs::copy(dxvk_i386.join("d3d9.dll"), game.join("d3d9.dll"));
            let exe = std::path::Path::new(exe_path).file_name().unwrap_or_default().to_string_lossy().to_string();
            (exe, game_dir.clone(), game.join("."))
        },
    };

    let shader_cache_base = home.join(".metalsharp").join("shader-cache").join("dxvk-metal32").join(appid.to_string());
    let _ = std::fs::create_dir_all(&shader_cache_base);

    let dyld_wine = ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string();
    let moltenvk_icd = ms_root.join("etc").join("vulkan").join("icd.d").join("MoltenVK_icd.json");
    let moltenvk_icd_str = if moltenvk_icd.exists() {
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

    let child = Command::new(&wine)
        .current_dir(&work_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDLLOVERRIDES", "d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d")
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld_wine)
        .env("VK_ICD_FILENAMES", &moltenvk_icd_str)
        .env("DXVK_STATE_CACHE_PATH", format!("{}/", shader_cache_base.to_string_lossy()))
        .arg(&exe_name)
        .spawn()?;

    Ok(child.id())
}

fn launch_metalsharp_wine(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = std::path::Path::new(exe_path).file_name().unwrap_or_default().to_string_lossy().to_string();

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env(
            "DYLD_FALLBACK_LIBRARY_PATH",
            ms_root.join("lib").join("wine").join("x86_64-unix").to_string_lossy().to_string(),
        )
        .arg(&exe_name)
        .spawn()?;

    Ok(child.id())
}

fn resolve_game_exe_fallback(game_dir: &PathBuf) -> String {
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

pub fn kill(pid: i32) -> Result<(), Box<dyn std::error::Error>> {
    let _ = Command::new("kill")
        .args(["-9", &pid.to_string()])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-P", &pid.to_string()])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    std::thread::sleep(std::time::Duration::from_millis(300));

    let _ = Command::new("pkill")
        .args(["-9", "-f", "UnityCrashHandler"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    Ok(())
}

pub fn kill_game(appid: u32) -> Result<(), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let game_dir = home.join(".metalsharp").join("games").join(appid.to_string());

    let resolved = crate::setup::resolve_game_dir(appid);

    let dirs_to_check =
        if let Some(ref rd) = resolved { vec![rd.clone(), game_dir.clone()] } else { vec![game_dir.clone()] };

    for dir in &dirs_to_check {
        if dir.exists() {
            if let Ok(output) = Command::new("pgrep").args(["-a", "-f", &dir.to_string_lossy()]).output() {
                for line in String::from_utf8_lossy(&output.stdout).lines() {
                    if let Some(pid_str) = line.split_whitespace().next() {
                        if let Ok(pid) = pid_str.parse::<i32>() {
                            let _ = Command::new("kill").args(["-9", &pid.to_string()]).status();
                        }
                    }
                }
            }
        }
    }

    let _ = Command::new("pkill").args(["-9", "-f", "UnityCrashHandler"]).status();

    Ok(())
}

pub fn get_config() -> Value {
    let native_available = find_metalsharp_native().is_ok();
    let mono_available = find_mono().is_ok();

    json!({
        "ok": true,
        "native_available": native_available,
        "mono_available": mono_available,
    })
}

fn find_metalsharp_native() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        PathBuf::from("/Applications/MetalSharp.app/Contents/Resources/metalsharp"),
        home.join(".metalsharp/metalsharp"),
        home.join("metalsharp/build/metalsharp"),
        PathBuf::from("/usr/local/bin/metalsharp"),
        PathBuf::from("/opt/homebrew/bin/metalsharp"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    Err("metalsharp binary not found".into())
}

fn find_scripts_dir() -> Option<PathBuf> {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates = vec![home.join("metalsharp").join("scripts"), home.join(".metalsharp").join("scripts")];
    candidates.into_iter().find(|p| p.exists())
}

pub fn run_game_setup_script(appid: u32) -> Result<(), Box<dyn std::error::Error>> {
    let script_name = match appid {
        105600 => "setup-terraria-deps.sh",
        504230 => "setup-celeste-deps.sh",
        312520 => "setup-rainworld-deps.sh",
        535520 => "setup-nidhogg2-deps.sh",
        945360 => "setup-amongus-deps.sh",
        _ => return Ok(()),
    };

    let scripts_dir = match find_scripts_dir() {
        Some(d) => d,
        None => return Err("scripts directory not found".into()),
    };

    let script = scripts_dir.join(script_name);
    if !script.exists() {
        return Err(format!("setup script not found: {}", script.display()).into());
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let env_path = format!(
        "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:{}",
        home.join(".cargo/bin").to_string_lossy()
    );

    let status = Command::new("/bin/bash")
        .arg(&script)
        .arg(appid.to_string())
        .env("PATH", &env_path)
        .env("HOME", &home)
        .env("METALSHARP_HOME", home.join(".metalsharp").to_string_lossy().to_string())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .status()?;

    if !status.success() {
        return Err(format!("setup script {} failed", script_name).into());
    }

    Ok(())
}

fn find_mono() -> Result<String, Box<dyn std::error::Error>> {
    let candidates = vec![PathBuf::from("/opt/homebrew/bin/mono"), PathBuf::from("/usr/local/bin/mono")];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    Err("mono not found — install with: brew install mono".into())
}

fn find_wine() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let ms_wine = home.join(".metalsharp").join("runtime").join("wine").join("bin").join("metalsharp-wine");
    if ms_wine.exists() {
        return Ok(ms_wine.to_string_lossy().to_string());
    }

    let candidates = vec![
        PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"),
        PathBuf::from("/opt/homebrew/bin/wine64"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    Err("wine not found".into())
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

pub fn ensure_wine_prefix(prefix: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let system32 = prefix.join("drive_c").join("windows").join("system32");
    if system32.exists() {
        return Ok(());
    }

    let wine = find_wine()?;
    let status = Command::new(&wine)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()?;

    if !status.success() {
        return Err("failed to initialize Wine prefix".into());
    }

    Ok(())
}

pub fn set_config(_mode: &str) -> Result<Value, Box<dyn std::error::Error>> {
    Ok(get_config())
}

fn launch_via_wine(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = find_wine()?;
    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    ensure_wine_prefix(&prefix)?;

    let child = Command::new(&wine).env("WINEPREFIX", &prefix_str).arg(exe_path).spawn()?;

    Ok(child.id())
}

fn launch_via_fna_mono(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let mono = find_mono()?;
    let exe = PathBuf::from(exe_path);
    let game_dir = exe.parent().ok_or("no parent dir for exe")?;

    let child = Command::new(&mono)
        .current_dir(game_dir)
        .env("DYLD_LIBRARY_PATH", ".")
        .env("METAL_DEVICE_WRAPPER_TYPE", "0")
        .arg(&exe)
        .spawn()?;

    Ok(child.id())
}
