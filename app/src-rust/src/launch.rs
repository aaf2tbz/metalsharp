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
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    if !crate::steam::is_wine_steam_running() {
        crate::steam::launch_wine_steam()?;
        let mut ready = false;
        for _ in 0..12 {
            if crate::steam::is_wine_steam_running() {
                std::thread::sleep(std::time::Duration::from_secs(2));
                ready = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_secs(1));
        }
        if !ready {
            return Err("Wine Steam was started but did not become ready for game launch".into());
        }
    }

    let url = format!("steam://run/{}", appid);

    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", &prefix_str)
        .env("WINEDEBUG", "-all")
        .env("WINEDEBUGGER", "none")
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    spawn_and_reap(cmd)
}

pub fn launch_via_steam_with_env(
    appid: u32,
    extra_env: &[(String, String)],
) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let wine = crate::platform::runtime_wine_binary(&ms_root);
    if !wine.exists() {
        return Err("MetalSharp Wine not found".into());
    }

    let prefix = home.join(".metalsharp").join("prefix-steam");
    let prefix_str = prefix.to_string_lossy().to_string();

    let steam_running = crate::steam::is_wine_steam_running();
    ensure_steam_env_handoff_supported(steam_running, extra_env)?;

    if !steam_running {
        crate::steam::launch_wine_steam_with_env(extra_env)?;
        let mut ready = false;
        for _ in 0..12 {
            if crate::steam::is_wine_steam_running() {
                std::thread::sleep(std::time::Duration::from_secs(2));
                ready = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_secs(1));
        }
        if !ready {
            return Err("Wine Steam was started but did not become ready for game launch".into());
        }
    }

    let url = format!("steam://run/{}", appid);

    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", &prefix_str).env("WINEDEBUG", "-all").env("WINEDEBUGGER", "none");
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);

    for (key, val) in extra_env {
        cmd.env(key, val);
    }

    cmd.args(["start", &url]).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null());
    spawn_and_reap(cmd)
}

fn ensure_steam_env_handoff_supported(steam_running: bool, extra_env: &[(String, String)]) -> Result<(), String> {
    if steam_running && !extra_env.is_empty() {
        return Err("Wine Steam is already running; per-game environment cannot be inherited without restarting Steam. Use a direct MTSP pipeline for env-dependent launches.".into());
    }
    Ok(())
}

pub fn kill_process_tree(pid: i32) -> Result<(), Box<dyn std::error::Error>> {
    if pid <= 0 {
        return Ok(());
    }

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
    if pid > 0 {
        kill_process_tree(pid)?;
    }

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

pub fn is_process_active(pid: i32) -> bool {
    if pid <= 0 {
        return false;
    }

    match Command::new("ps").args(["-p", &pid.to_string(), "-o", "stat="]).output() {
        Ok(output) if output.status.success() => {
            let stat = String::from_utf8_lossy(&output.stdout);
            !stat.trim().is_empty() && !stat.contains('Z')
        },
        _ => false,
    }
}

fn spawn_and_reap(mut cmd: Command) -> Result<u32, Box<dyn std::error::Error>> {
    let mut child = cmd.spawn()?;
    let pid = child.id();
    std::thread::spawn(move || {
        let _ = child.wait();
    });
    Ok(pid)
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

    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let ms_wine = crate::platform::runtime_wine_binary(&ms_root);
    if ms_wine.exists() {
        return Ok(ms_wine.to_string_lossy().to_string());
    }

    let candidates = vec![
        PathBuf::from("/opt/homebrew/bin/wine64"),
        PathBuf::from("/usr/bin/wine"),
        PathBuf::from("/usr/local/bin/wine"),
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
    let ms_root = dirs::home_dir().unwrap_or_default().join(".metalsharp").join("runtime").join("wine");
    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", prefix.to_string_lossy().to_string())
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let status = cmd.status()?;

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

    let ms_root = home.join(".metalsharp").join("runtime").join("wine");
    let mut cmd = Command::new(&wine);
    cmd.env("WINEPREFIX", &prefix_str).arg(exe_path);
    crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
    let child = cmd.spawn()?;

    Ok(child.id())
}

fn launch_via_fna_mono(exe_path: &str) -> Result<u32, Box<dyn std::error::Error>> {
    let mono = find_mono()?;
    let exe = PathBuf::from(exe_path);
    let game_dir = exe.parent().ok_or("no parent dir for exe")?;

    let mut cmd = Command::new(&mono);
    cmd.current_dir(game_dir).env("METAL_DEVICE_WRAPPER_TYPE", "0").arg(&exe);
    if crate::platform::current() == crate::platform::HostPlatform::Macos {
        cmd.env("DYLD_LIBRARY_PATH", ".");
    } else if crate::platform::current() == crate::platform::HostPlatform::Linux {
        cmd.env("LD_LIBRARY_PATH", ".");
    }
    let child = cmd.spawn()?;

    Ok(child.id())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn non_positive_pids_are_not_active() {
        assert!(!is_process_active(0));
        assert!(!is_process_active(-1));
    }

    #[test]
    fn spawned_handoff_process_is_reaped_after_exit() {
        let cmd = Command::new("/usr/bin/true");
        let pid = spawn_and_reap(cmd).expect("spawn handoff process");

        for _ in 0..20 {
            if !is_process_active(pid as i32) {
                return;
            }
            std::thread::sleep(Duration::from_millis(50));
        }

        assert!(!is_process_active(pid as i32));
    }

    #[test]
    fn rejects_env_dependent_handoff_when_steam_is_already_running() {
        let env = vec![("DXMT_SHADER_CACHE_PATH".to_string(), "/tmp/cache".to_string())];

        assert!(ensure_steam_env_handoff_supported(true, &env).is_err());
        assert!(ensure_steam_env_handoff_supported(true, &[]).is_ok());
        assert!(ensure_steam_env_handoff_supported(false, &env).is_ok());
    }
}
