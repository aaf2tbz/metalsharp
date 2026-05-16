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

pub fn kill_process_tree(pid: i32) -> Result<(), Box<dyn std::error::Error>> {
    let _ = Command::new("pkill")
        .args(["-9", "-P", &pid.to_string()])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("kill")
        .args(["-9", &pid.to_string()])
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

pub fn kill_game_with_pid(appid: u32, pid: i32) -> Result<(), Box<dyn std::error::Error>> {
    kill_process_tree(pid)?;

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
                        if let Ok(p) = pid_str.parse::<i32>() {
                            if p != pid {
                                let _ = Command::new("kill").args(["-9", &p.to_string()]).status();
                            }
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
        620 | 265930 => "setup-goldberg-deps.sh",
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
