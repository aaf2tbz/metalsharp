use serde_json::Map;
use std::path::PathBuf;
use std::process::Command;

pub struct LaunchOptions {
    pub fullscreen: bool,
    pub debug_metal: bool,
    pub verbose: bool,
    pub prefix: Option<String>,
    pub custom_args: Vec<String>,
}

impl LaunchOptions {
    pub fn from_map(map: &Map<String, serde_json::Value>) -> Self {
        Self {
            fullscreen: map.get("fullscreen").and_then(|v| v.as_bool()).unwrap_or(true),
            debug_metal: map.get("debugMetal").and_then(|v| v.as_bool()).unwrap_or(false),
            verbose: map.get("verbose").and_then(|v| v.as_bool()).unwrap_or(false),
            prefix: map.get("prefix").and_then(|v| v.as_str()).map(String::from),
            custom_args: map
                .get("customArgs")
                .and_then(|v| v.as_array())
                .map(|a| {
                    a.iter()
                        .filter_map(|v| v.as_str().map(String::from))
                        .collect()
                })
                .unwrap_or_default(),
        }
    }
}

pub fn launch(exe_path: &str, opts: &LaunchOptions) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let prefix = opts
        .prefix
        .clone()
        .unwrap_or_else(|| home.join(".metalsharp").join("prefix").to_string_lossy().to_string());

    if is_steam_client(exe_path) {
        return launch_via_wine(exe_path, &prefix, opts);
    }

    launch_via_metalsharp(exe_path, &prefix, opts)
}

fn is_steam_client(exe_path: &str) -> bool {
    let lower = exe_path.to_lowercase().replace("\\", "/");
    lower.contains("steam.exe") && !lower.contains("steamsetup") && !lower.contains("uninstall")
}

fn launch_via_wine(exe_path: &str, prefix: &str, opts: &LaunchOptions) -> Result<u32, Box<dyn std::error::Error>> {
    let wine = find_wine()?;

    let mut args: Vec<String> = Vec::new();
    args.push(exe_path.into());
    args.extend(opts.custom_args.iter().cloned());

    let child = Command::new(&wine)
        .env("WINEPREFIX", prefix)
        .args(&args)
        .spawn()?;

    Ok(child.id())
}

fn launch_via_metalsharp(exe_path: &str, prefix: &str, opts: &LaunchOptions) -> Result<u32, Box<dyn std::error::Error>> {
    let metalsharp_bin = find_metalsharp_binary()?;

    let mut args: Vec<String> = Vec::new();
    args.push(exe_path.into());
    args.push("--prefix".into());
    args.push(prefix.into());

    if opts.fullscreen {
        args.push("--fullscreen".into());
    }
    if opts.debug_metal {
        args.push("--debug-metal".into());
    }
    if opts.verbose {
        args.push("--verbose".into());
    }
    args.extend(opts.custom_args.iter().cloned());

    let child = Command::new(&metalsharp_bin).args(&args).spawn()?;

    Ok(child.id())
}

pub fn kill(pid: i32) -> Result<(), Box<dyn std::error::Error>> {
    Command::new("kill").arg(pid.to_string()).output()?;
    Ok(())
}

fn find_wine() -> Result<String, Box<dyn std::error::Error>> {
    let candidates = vec![
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

fn find_metalsharp_binary() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        home.join("metalsharp/build/metalsharp_launcher"),
        home.join("metalsharp/build/tools/launcher/metalsharp_launcher"),
        PathBuf::from("/usr/local/bin/metalsharp_launcher"),
    ];

    for c in candidates {
        if c.exists() {
            return Ok(c.to_string_lossy().to_string());
        }
    }

    let which = Command::new("which")
        .arg("metalsharp_launcher")
        .output()?;

    if which.status.success() {
        return Ok(String::from_utf8_lossy(&which.stdout).trim().to_string());
    }

    Err("metalsharp_launcher binary not found".into())
}
