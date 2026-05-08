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

fn rules() -> &'static crate::rules::Rules {
    static RULES: std::sync::OnceLock<crate::rules::Rules> = std::sync::OnceLock::new();
    RULES.get_or_init(crate::rules::Rules::load)
}

pub fn recommended_method_for_appid(appid: u32) -> String {
    rules().find_method(appid)
}

pub fn get_engine_for_appid(appid: u32) -> String {
    if let Some(rule) = rules().find(appid) {
        return rule.engine;
    }
    let game_dir = crate::setup::resolve_game_dir(appid);
    crate::rules::detect_engine_from_dir(&game_dir)
}

pub fn launch_auto(appid: u32) -> Result<(u32, String), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let game_dir = crate::setup::resolve_game_dir(appid);
    let method = rules().find_method(appid);
    let env = rules().find_env(appid);
    let dir = game_dir.as_ref().unwrap_or(&local_dir);

    match method.as_str() {
        "xna_fna_arm64" => {
            let exe_name = rules().find_exe(appid)
                .unwrap_or_else(|| "TerrariaLauncher.exe".into());
            let exe = dir.join(&exe_name);
            let pid = launch_fna_arm64(&exe.to_string_lossy(), dir)?;
            Ok((pid, method))
        }
        "xna_fna_x86" => {
            let exe_name = rules().find_exe(appid)
                .unwrap_or_else(|| "Celeste.exe".into());
            let exe = dir.join(&exe_name);
            let pid = launch_fna_x86(&exe.to_string_lossy(), dir)?;
            Ok((pid, method))
        }
        "gptk_wine" => {
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_gptk(&exe)?;
            Ok((pid, method))
        }
        "metalsharp_wine" => {
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_metalsharp_wine(&exe, dir)?;
            Ok((pid, method))
        }
        "d3dmetal_wine" => {
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_d3dmetal_wine(&exe, dir)?;
            Ok((pid, method))
        }
        "wine_devel" => {
            let exe = resolve_game_exe_fallback(dir);
            let pid = launch_wine_devel(&exe, dir)?;
            Ok((pid, method))
        }
        "steam" => {
            let pid = launch_via_steam(appid)?;
            Ok((pid, method))
        }
        "steam_metalfx" => {
            let mut env_vars: Vec<(&str, &str)> = vec![
                ("D3DM_ENABLE_METALFX", "1"),
                ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                ("MVK_ALLOW_METAL_FENCES", "1"),
            ];
            for (k, _) in &env {
                if let Some(ov) = env.get(k) {
                    env_vars.push((k.as_str(), ov.as_str()));
                }
            }
            let pid = launch_via_steam_with_env(appid, &env_vars)?;
            Ok((pid, method))
        }
        "steam_d3dmetal_perf" => {
            let mut env_vars: Vec<(&str, &str)> = vec![
                ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
            ];
            for (k, _) in &env {
                if let Some(ov) = env.get(k) {
                    env_vars.push((k.as_str(), ov.as_str()));
                }
            }
            let pid = launch_via_steam_with_env(appid, &env_vars)?;
            Ok((pid, method))
        }
        _ => {
            let pid = launch_via_steam_with_env(appid, &[
                ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
            ])?;
            Ok((pid, method))
        }
    }
}

pub fn launch_with_method(appid: u32, method: &str) -> Result<(u32, String), Box<dyn std::error::Error>> {
    if method.is_empty() || method == "auto" || method == "native" {
        return launch_auto(appid);
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let game_dir = crate::setup::resolve_game_dir(appid);
    let dir = game_dir.as_ref().unwrap_or(&local_dir);

    match method {
        "native_metalfx_low" => {
            let pid = launch_via_steam_with_env(appid, &[
                ("D3DM_ENABLE_METALFX", "1"),
                ("D3DM_METALFX_QUALITY", "low"),
                ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                ("MVK_ALLOW_METAL_FENCES", "1"),
            ])?;
            Ok((pid, "native_metalfx_low".into()))
        }
        "native_metalfx_medium" => {
            let pid = launch_via_steam_with_env(appid, &[
                ("D3DM_ENABLE_METALFX", "1"),
                ("D3DM_METALFX_QUALITY", "medium"),
                ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                ("MVK_ALLOW_METAL_FENCES", "1"),
            ])?;
            Ok((pid, "native_metalfx_medium".into()))
        }
        "native_metalfx_high" => {
            let pid = launch_via_steam_with_env(appid, &[
                ("D3DM_ENABLE_METALFX", "1"),
                ("D3DM_METALFX_QUALITY", "high"),
                ("D3DM_ENABLE_ASYNC_COMMIT", "1"),
                ("D3DM_MULTITHREADED_INTERFACE_ENABLE", "1"),
                ("D3DM_IGNORE_D3D11_RENDER_BARRIERS", "1"),
                ("D3DM_SAMPLE_NAN_TO_ZERO", "1"),
                ("D3DM_FLUSH_POS_INF_TO_NAN", "1"),
                ("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1"),
                ("MVK_ALLOW_METAL_FENCES", "1"),
            ])?;
            Ok((pid, "native_metalfx_high".into()))
        }
        _ => Err(format!("Unknown launch method: {}", method).into()),
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
            if crate::steam::is_wine_steam_running() { break; }
        }
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    let url = format!("steam://run/{}", appid);

    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all");

    for (key, val) in extra_env {
        cmd.env(key, val);
    }

    let child = cmd
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

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
    let dyld = format!(
        "{}:/opt/homebrew/lib",
        game_dir.to_string_lossy()
    );

    let child = Command::new("mono")
        .current_dir(game_dir)
        .env("DYLD_LIBRARY_PATH", &dyld)
        .env("MONO_CONFIG", mono_config)
        .env("FNA3D_DRIVER", "OpenGL")
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_gptk(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let gptk_wine64 = PathBuf::from(
        "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
    );
    if !gptk_wine64.exists() {
        return Err("GPTK wine64 not found — install with: brew install --cask gcenx/wine/game-porting-toolkit".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-gptk");
    let prefix_str = prefix.to_string_lossy().to_string();
    let game_dir = PathBuf::from(exe_path).parent().ok_or("no parent dir")?.to_path_buf();
    let exe_name = std::path::Path::new(exe_path)
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    let child = Command::new(&wine)
        .current_dir(&game_dir)
        .env("MS_BACKEND", "gptk")
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
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
    let exe_name = std::path::Path::new(exe_path)
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .arg(&exe_name)
        .spawn()?;

    Ok(child.id())
}

fn launch_d3dmetal_wine(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = ms_root.join("bin").join("metalsharp-wine");

    if !wine.exists() {
        return Err("MetalSharp Wine not found — run setup first".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = std::path::Path::new(exe_path)
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    let mut cmd = Command::new(&wine);
    cmd.current_dir(game_dir)
        .env("MS_BACKEND", "d3dmetal")
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all");

    let child = cmd
        .arg(&exe_name)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(child.id())
}

fn launch_wine_devel(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let wine = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine");
    if !wine.exists() {
        return Err("Wine Devel not found — install with: brew install --cask wine@devel".into());
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let prefix = home.join(".metalsharp").join("prefix-620");
    let prefix_str = prefix.to_string_lossy().to_string();
    let exe_name = std::path::Path::new(exe_path)
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
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

    let dirs_to_check = if let Some(ref rd) = resolved {
        vec![rd.clone(), game_dir.clone()]
    } else {
        vec![game_dir.clone()]
    };

    for dir in &dirs_to_check {
        if dir.exists() {
            if let Ok(output) = Command::new("pgrep")
                .args(["-a", "-f", &dir.to_string_lossy()])
                .output()
            {
                for line in String::from_utf8_lossy(&output.stdout).lines() {
                    if let Some(pid_str) = line.split_whitespace().next() {
                        if let Ok(pid) = pid_str.parse::<i32>() {
                            let _ = Command::new("kill")
                                .args(["-9", &pid.to_string()])
                                .status();
                        }
                    }
                }
            }
        }
    }

    let _ = Command::new("pkill")
        .args(["-9", "-f", "UnityCrashHandler"])
        .status();

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
    let candidates = vec![
        home.join("metalsharp").join("scripts"),
        home.join(".metalsharp").join("scripts"),
    ];
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
    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/mono"),
        PathBuf::from("/usr/local/bin/mono"),
    ];

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
    let candidates = vec![
        home.join("metalsharp").join("configs").join(name),
        home.join(".metalsharp").join("configs").join(name),
    ];
    for c in candidates {
        if c.exists() {
            return c.to_string_lossy().to_string();
        }
    }
    home.join("metalsharp").join("configs").join(name).to_string_lossy().to_string()
}

fn find_shims_dir() -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let candidates = vec![
        home.join(".metalsharp").join("runtime").join("shims"),
        home.join(".metalsharp").join("shims"),
    ];
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

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .arg(exe_path)
        .spawn()?;

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
