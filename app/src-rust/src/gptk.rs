use serde_json::{json, Value};
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

static GPTK_INSTALLING: AtomicBool = AtomicBool::new(false);

fn progress_path() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("gptk_install_progress.json")
}

fn write_progress(step: usize, total: usize, name: &str, status: &str, log_line: &str, error: Option<&str>) {
    let data = json!({
        "step": step,
        "total": total,
        "current": name,
        "status": status,
        "log": log_line,
        "error": error,
    });
    let _ = fs::write(progress_path(), serde_json::to_string(&data).unwrap_or_default());
}

pub fn read_progress() -> Value {
    let path = progress_path();
    if path.exists() {
        if let Ok(contents) = fs::read_to_string(&path) {
            if let Ok(v) = serde_json::from_str::<Value>(&contents) {
                return v;
            }
        }
    }
    json!({"step": 0, "total": 0, "current": "", "status": "idle", "log": "", "error": null})
}

pub struct GptkStatus {
    pub wine_available: bool,
    pub installed: bool,
    pub running: bool,
}

fn gptk_prefix() -> PathBuf {
    crate::platform::metalsharp_home_dir().join("prefix-gptk")
}

fn gptk_wine() -> PathBuf {
    PathBuf::from("/opt/homebrew/bin/wine64")
}

fn gptk_wineserver() -> PathBuf {
    PathBuf::from("/opt/homebrew/bin/wineserver")
}

fn gptk_steam_exe() -> PathBuf {
    gptk_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe")
}

fn rosetta_installed() -> bool {
    let plist = PathBuf::from("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist");
    plist.exists() || Command::new("pgrep").args(["-q", "oahd"]).status().map(|s| s.success()).unwrap_or(false)
}

fn gptk_brew_installed() -> bool {
    gptk_wine().exists()
}

pub fn status() -> GptkStatus {
    GptkStatus { wine_available: gptk_wine().exists(), installed: gptk_steam_exe().exists(), running: is_running() }
}

pub fn is_running() -> bool {
    let prefix = gptk_prefix().to_string_lossy().to_string();
    Command::new("ps")
        .args(["axo", "pid=,command="])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| {
            s.lines().any(|line| {
                if line.contains(" rg ") || line.contains("rg -i") || line.contains("ps axo") {
                    return false;
                }
                line.contains(&prefix) && (line.contains("Steam.exe") || line.contains("steam.exe"))
            })
        })
        .unwrap_or(false)
}

pub fn is_installing() -> bool {
    GPTK_INSTALLING.load(Ordering::SeqCst)
}

pub fn install() -> Result<Value, String> {
    if gptk_steam_exe().exists() {
        return Ok(json!({"ok": true, "status": "already_installed"}));
    }

    if GPTK_INSTALLING.load(Ordering::SeqCst) {
        return Ok(json!({"ok": true, "status": "already_installing"}));
    }

    if GPTK_INSTALLING.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Ok(json!({"ok": true, "status": "already_installing"}));
    }

    let skip_deps = gptk_wine().exists() && rosetta_installed() && gptk_prefix().join("drive_c").exists();

    std::thread::spawn(move || {
        let _ = run_install(skip_deps);
        GPTK_INSTALLING.store(false, Ordering::SeqCst);
    });

    Ok(json!({"ok": true, "status": "installing"}))
}

fn run_install(skip_deps: bool) -> Result<(), String> {
    let total = if skip_deps { 4 } else { 6 };
    let mut step = 1;

    if !skip_deps {
        write_progress(step, total, "Rosetta 2", "installing", "Checking Rosetta 2...", None);
        install_deps()?;
        step += 1;
    }

    if !gptk_wine().exists() {
        write_progress(0, total, "Error", "error", "GPTK wine64 not found", Some("GPTK wine64 not found"));
        return Err("GPTK wine64 not found".into());
    }

    let prefix = gptk_prefix();
    let prefix_exists = prefix.join("drive_c").exists();

    if !prefix_exists {
        write_progress(step, total, "Wine Prefix", "installing", "Creating GPTK Wine prefix...", None);
        let _ = fs::remove_dir_all(&prefix);
        fs::create_dir_all(&prefix).map_err(|e| format!("create prefix dir: {}", e))?;

        let prefix_str = prefix.to_string_lossy().to_string();

        let output = Command::new(gptk_wine())
            .env("WINEPREFIX", &prefix_str)
            .arg("wineboot")
            .arg("--init")
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map_err(|e| format!("wineboot --init failed: {}", e))?;

        if !output.success() {
            write_progress(0, total, "Error", "error", "wineboot failed", Some("wineboot failed"));
            return Err("wineboot --init failed for GPTK prefix".into());
        }

        let windows_dir = prefix.join("drive_c").join("windows").join("system32");
        let mut wineboot_ok = false;
        for _ in 0..30 {
            if windows_dir.exists() {
                wineboot_ok = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_secs(2));
        }
        if !wineboot_ok {
            write_progress(0, total, "Error", "error", "wineboot timed out", Some("wineboot timed out"));
            return Err("wineboot --init timed out for GPTK prefix".into());
        }
        step += 1;
    }

    write_progress(step, total, "Steam Setup", "downloading", "Downloading SteamSetup.exe...", None);
    let metalsharp_dir = crate::platform::metalsharp_home_dir();
    let installer = metalsharp_dir.join("gptk-SteamSetup.exe");
    let _ = fs::remove_file(&installer);

    let url = "https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe";
    let dl_result = Command::new("curl")
        .args(["-sL", "-o", &installer.to_string_lossy(), url])
        .status()
        .map_err(|e| format!("curl failed: {}", e))?;

    if !dl_result.success() || !installer.exists() {
        write_progress(0, total, "Error", "error", "Download failed", Some("Failed to download SteamSetup.exe"));
        return Err("Failed to download SteamSetup.exe for GPTK".into());
    }
    step += 1;

    write_progress(step, total, "Steam Setup", "installing", "Installing Steam via GPTK Wine...", None);
    let prefix_str = prefix.to_string_lossy().to_string();

    let _ = Command::new(gptk_wine())
        .env("WINEPREFIX", &prefix_str)
        .arg(&installer)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| format!("failed to launch SteamSetup: {}", e))?;

    let steam_exe = gptk_steam_exe();
    let steam_ui = prefix.join("drive_c").join("Program Files (x86)").join("Steam").join("steamui.dll");
    for _ in 0..90 {
        if steam_exe.exists() && steam_ui.exists() {
            break;
        }
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    if !steam_exe.exists() {
        write_progress(0, total, "Error", "error", "Steam setup failed", Some("SteamSetup did not create Steam.exe"));
        return Err("SteamSetup did not create Steam.exe in GPTK prefix".into());
    }
    step += 1;

    write_progress(step, total, "Steam Updates", "updating", "Running Steam update cycles (2x)...", None);
    run_steam_update_cycles(&prefix_str)?;

    write_progress(total, total, "Complete", "complete", "GPTK Steam installed and updated!", None);
    Ok(())
}

fn run_steam_update_cycles(prefix_str: &str) -> Result<(), String> {
    let steam_exe = gptk_steam_exe();
    let wineserver = gptk_wineserver();

    for cycle in 0..2 {
        let _ = Command::new(gptk_wine())
            .env("WINEPREFIX", prefix_str)
            .arg(&steam_exe)
            .arg("-no-cef-sandbox")
            .arg("-noverifyfiles")
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .map_err(|e| format!("failed to launch Steam for update: {}", e))?;

        std::thread::sleep(std::time::Duration::from_secs(15));

        let _ = Command::new(&wineserver).env("WINEPREFIX", prefix_str).arg("-k").status();

        std::thread::sleep(std::time::Duration::from_secs(3));

        let _ = Command::new(&wineserver)
            .env("WINEPREFIX", prefix_str)
            .arg("-w")
            .status()
            .map_err(|e| format!("wineserver -w failed after cycle {}: {}", cycle + 1, e));
    }

    Ok(())
}

fn install_deps() -> Result<(), String> {
    if !rosetta_installed() {
        let output = Command::new("softwareupdate")
            .args(["--install-rosetta", "--agree-to-license"])
            .output()
            .map_err(|e| format!("failed to run softwareupdate: {}", e))?;
        if !output.status.success() && !String::from_utf8_lossy(&output.stderr).contains("already installed") {
            return Err(format!("Rosetta install failed: {}", String::from_utf8_lossy(&output.stderr)));
        }
    }

    if !gptk_brew_installed() {
        let brew = if PathBuf::from("/opt/homebrew/bin/brew").exists() {
            PathBuf::from("/opt/homebrew/bin/brew")
        } else {
            PathBuf::from("/usr/local/bin/brew")
        };

        if !brew.exists() {
            return Err("Homebrew not found — install Homebrew first".into());
        }

        let output = Command::new(&brew)
            .args(["install", "--cask", "game-porting-toolkit"])
            .output()
            .map_err(|e| format!("failed to run brew: {}", e))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            if !stderr.contains("already installed") {
                return Err(format!("brew install game-porting-toolkit failed: {}", stderr));
            }
        }
    }

    Ok(())
}
