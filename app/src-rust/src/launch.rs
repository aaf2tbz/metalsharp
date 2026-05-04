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

pub fn launch_auto(appid: u32) -> Result<(u32, &'static str), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    let game_dir = crate::setup::resolve_game_dir(appid);

    match appid {
        504230 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("Celeste.exe");
            let pid = launch_fna_x86(&exe.to_string_lossy(), dir)?;
            Ok((pid, "xna_fna_x86"))
        }
        105600 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("TerrariaLauncher.exe");
            let pid = launch_fna_arm64(&exe.to_string_lossy(), dir)?;
            Ok((pid, "xna_fna_arm64"))
        }
        312520 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("RainWorld.exe");
            let pid = launch_gptk(&exe.to_string_lossy())?;
            Ok((pid, "gptk_wine"))
        }
        535520 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("Nidhogg_2.exe");
            let pid = launch_dxvk_wine(&exe.to_string_lossy(), dir, 535520)?;
            Ok((pid, "dxvk_wine"))
        }
        945360 | 1139900 => {
            let pid = launch_via_steam(appid)?;
            Ok((pid, "steam"))
        }
        620 => {
            let dir = game_dir.as_ref().unwrap_or(&local_dir);
            let exe = dir.join("portal2.exe");
            let pid = launch_wine_devel(&exe.to_string_lossy(), dir, 620)?;
            Ok((pid, "wine_devel"))
        }
        2050650 => {
            let pid = launch_via_steam(appid)?;
            Ok((pid, "steam"))
        }
        _ => {
            if game_dir.is_none() {
                let pid = launch_via_steam(appid)?;
                return Ok((pid, "steam"));
            }
            let dir = game_dir.as_ref().unwrap();
            let exe = resolve_game_exe_fallback(dir);
            let game_type = detect_game_type(dir);
            let pid = launch(&exe, game_type)?;
            Ok((pid, game_type))
        }
    }
}

pub fn launch_via_steam(appid: u32) -> Result<u32, Box<dyn std::error::Error>> {
    let wine = PathBuf::from("/Applications/external runtime.app/Contents/SharedSupport/external runtime/lib/wine/x86_64-unix/wine");
    if !wine.exists() {
        return Err("external runtime Wine not found — install with: brew install --cask external runtime".into());
    }

    let home = dirs::home_dir().ok_or("no home dir")?;
    let prefix = home.join(".metalsharp").join("prefix-steam-cx");
    let prefix_str = prefix.to_string_lossy().to_string();

    if !crate::steam::is_wine_steam_running() {
        crate::steam::launch_wine_steam()?;
        std::thread::sleep(std::time::Duration::from_secs(15));
    }

    let cx = PathBuf::from("/Applications/external runtime.app/Contents/SharedSupport/external runtime");
    let url = format!("steam://run/{}", appid);

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("CX_ROOT", cx.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
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
        return Err("x86_64 mono not found — run setup-celeste-deps.sh first".into());
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
    let home = dirs::home_dir().ok_or("no home dir")?;
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
    let wine64 = PathBuf::from(
        "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
    );

    if !wine64.exists() {
        return Err("GPTK wine64 not found — install with: brew install --cask gcenx/wine/game-porting-toolkit".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-gptk");
    let game_dir = PathBuf::from(exe_path).parent().ok_or("no parent dir")?.to_path_buf();

    let child = Command::new(&wine64)
        .current_dir(&game_dir)
        .env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_wine_devel(exe_path: &str, game_dir: &PathBuf, appid: u32) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine");

    if !wine.exists() {
        return Err("Wine (devel) not found — install with: brew install --cask wine-external runtime".into());
    }

    let prefix = home.join(".metalsharp").join(format!("prefix-{}", appid));
    let prefix_str = prefix.to_string_lossy().to_string();

    if !prefix.exists() {
        std::fs::create_dir_all(&prefix)?;
        let _ = Command::new(&wine)
            .env("WINEPREFIX", &prefix_str)
            .arg("wineboot")
            .arg("--init")
            .status();
    }

    let wine_lib = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/lib");
    let dyld = format!(
        "/opt/homebrew/lib:{}",
        wine_lib.to_string_lossy()
    );

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_external runtime_wine(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let external runtime_base = PathBuf::from("/Applications/external runtime.app/Contents/SharedSupport/external runtime");
    let wine = external runtime_base.join("lib").join("wine").join("x86_64-unix").join("wine");

    if !wine.exists() {
        return Err("external runtime Wine not found — install with: brew install --cask external runtime".into());
    }

    let appid = game_dir.file_name().unwrap_or_default().to_string_lossy();
    let prefix = home.join(".metalsharp").join(format!("prefix-{}", appid));
    let prefix_str = prefix.to_string_lossy().to_string();

    if !prefix.exists() {
        std::fs::create_dir_all(&prefix)?;
        let _ = Command::new(&wine)
            .env("WINEPREFIX", &prefix_str)
            .arg("wineboot")
            .arg("--init")
            .status();
    }

    for dll in &["d3d11.dll", "dxgi.dll"] {
        let game_dll = game_dir.join(dll);
        if game_dll.exists() {
            let _ = std::fs::remove_file(&game_dll);
        }
    }

    let gptk_external = external runtime_base.join("lib64").join("apple_gptk").join("external");
    let external runtime_lib64 = external runtime_base.join("lib64");
    let dyld = format!(
        "{}:{}:/opt/homebrew/lib",
        gptk_external.to_string_lossy(),
        external runtime_lib64.to_string_lossy()
    );

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("CX_ROOT", external runtime_base.to_string_lossy().to_string())
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_dxvk_wine(exe_path: &str, game_dir: &PathBuf, appid: u32) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine");

    if !wine.exists() {
        return Err("Wine (devel) not found — install with: brew install --cask wine-external runtime || brew install --cask wine-stable".into());
    }

    let prefix = home.join(".metalsharp").join(format!("prefix-{}", appid));
    let prefix_str = prefix.to_string_lossy().to_string();

    if !prefix.exists() {
        std::fs::create_dir_all(&prefix)?;
        let _ = Command::new(&wine)
            .env("WINEPREFIX", &prefix_str)
            .arg("wineboot")
            .arg("--init")
            .status();

        let _ = Command::new(&wine)
            .env("WINEPREFIX", &prefix_str)
            .arg("reg")
            .args(["add", r"HKCU\Software\Wine\X11 Driver", "/v", "Managed", "/d", "N", "/f"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();

        let _ = Command::new(&wine)
            .env("WINEPREFIX", &prefix_str)
            .arg("reg")
            .args(["add", r"HKCU\Software\Wine\DllOverrides", "/v", "xinput1_3", "/d", "", "/f"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    }

    let dxvk_dir = home.join(".metalsharp").join("runtime").join("dxvk-moltenvk");

    for dll in &["d3d11.dll", "dxgi.dll"] {
        let src = dxvk_dir.join(dll);
        let dst = game_dir.join(dll);
        if src.exists() {
            let _ = std::fs::copy(&src, &dst);
        }
    }

    let moltenvk_icd = PathBuf::from("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json");
    if !moltenvk_icd.exists() {
        return Err("MoltenVK ICD not found — install with: brew install molten-vk".into());
    }

    let wine_lib = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/lib");
    let dyld = format!(
        "/opt/homebrew/lib:{}",
        wine_lib.to_string_lossy()
    );

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("VK_ICD_FILENAMES", moltenvk_icd.to_string_lossy().to_string())
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .env("MVK_PRESENT_MODE", "1")
        .env("DXVK_FRAME_RATE", "60")
        .env("DXVK_ASYNC", "1")
        .arg(exe_path)
        .spawn()?;

    Ok(child.id())
}

fn launch_wine32(exe_path: &str, game_dir: &PathBuf) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine");

    if !wine.exists() {
        return Err("Wine (devel) not found".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-amongus");
    let prefix_str = prefix.to_string_lossy().to_string();

    if !prefix.exists() {
        std::fs::create_dir_all(&prefix)?;
        let status = Command::new(&wine)
            .env("WINEPREFIX", &prefix_str)
            .arg("wineboot")
            .arg("--init")
            .status()?;
        if !status.success() {
            return Err("Failed to initialize Wine prefix".into());
        }
    }

    let wine_lib = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/lib");
    let dyld = format!(
        "/opt/homebrew/lib:{}",
        wine_lib.to_string_lossy()
    );

    let child = Command::new(&wine)
        .current_dir(game_dir)
        .env("WINEPREFIX", &prefix_str)
        .env("DYLD_FALLBACK_LIBRARY_PATH", &dyld)
        .arg(exe_path)
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

fn detect_game_type(game_dir: &PathBuf) -> &'static str {
    let marker = game_dir.join(".metalsharp_prepared");
    if let Ok(content) = std::fs::read_to_string(&marker) {
        if content.contains("is_dotnet=true") {
            return "xna_fna";
        }
    }

    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_lowercase();
            if name.ends_with(".exe") && !name.contains("setup") {
                let wine_devel = PathBuf::from("/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine");
                if wine_devel.exists() {
                    return "wine32";
                }
                let gptk = PathBuf::from(
                    "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64"
                );
                if gptk.exists() {
                    return "gptk_wine";
                }
                return "native";
            }
        }
    }

    "native"
}

fn launch_via_wine(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let wine = find_wine()?;
    let prefix = home.join(".metalsharp").join("prefix");
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

    let which = Command::new("which").arg("mono").output()?;
    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("mono not found — install with: brew install mono".into())
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
    let steam_prefix = home.join(".metalsharp").join("prefix-steam-cx");

    if let Ok(output) = Command::new("pgrep")
        .args(["-a", "-f", &game_dir.to_string_lossy()])
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

    let _ = Command::new("pkill")
        .args(["-9", "-f", "UnityCrashHandler"])
        .status();

    let steamapps_common = steam_prefix
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps")
        .join("common");

    let prefixes_to_check = vec![
        steamapps_common.join("Among Us"),
        steamapps_common.join("RESIDENT EVIL 4  BIOHAZARD RE4"),
        steamapps_common.join("Celeste"),
        steamapps_common.join("Terraria"),
        steamapps_common.join("Rain World"),
        steamapps_common.join("Nidhogg 2"),
        steamapps_common.join("Portal 2"),
        steamapps_common.join("Ghostrunner"),
        game_dir.clone(),
    ];

    for dir in &prefixes_to_check {
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

    Ok(())
}

fn find_metalsharp_native() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        PathBuf::from("/Applications/MetalSharp.app/Contents/Resources/metalsharp"),
        home.join(".metalsharp/metalsharp"),
        home.join("metalsharp/build/metalsharp"),
        home.join("metalsharp/build/metalsharp_native"),
        PathBuf::from("/usr/local/bin/metalsharp"),
        PathBuf::from("/opt/homebrew/bin/metalsharp"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    let which = Command::new("which").arg("metalsharp").output()?;
    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("metalsharp binary not found — build with: cmake --build build".into())
}

pub fn get_config() -> Value {
    let native_available = find_metalsharp_native().is_ok();
    let mono_available = find_mono().is_ok();
    let wine_available = find_wine().is_ok();

    json!({
        "ok": true,
        "native_available": native_available,
        "mono_available": mono_available,
        "wine_available": wine_available,
    })
}

fn find_wine() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        PathBuf::from("/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine"),
        PathBuf::from("/opt/homebrew/bin/wine64"),
        PathBuf::from("/opt/homebrew/bin/wine"),
        PathBuf::from("/usr/local/bin/wine64"),
        PathBuf::from("/usr/local/bin/wine"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    let which = Command::new("which").arg("wine64").output()?;
    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    let which = Command::new("which").arg("wine").output()?;
    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("wine not found — install with: brew install --cask wine-stable".into())
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
        620 => "setup-portal2-deps.sh",
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
