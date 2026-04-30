use serde_json::{json, Map, Value};
use std::path::PathBuf;
use std::process::Command;

#[derive(Clone, PartialEq)]
pub enum LaunchMode {
    Native,
    Wine,
}

impl LaunchMode {
    pub fn from_str(s: &str) -> Self {
        match s {
            "wine" => LaunchMode::Wine,
            _ => LaunchMode::Native,
        }
    }
}

pub struct LaunchOptions {
    pub fullscreen: bool,
    pub debug_metal: bool,
    pub verbose: bool,
    pub prefix: Option<String>,
    pub custom_args: Vec<String>,
    pub mode: LaunchMode,
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
            mode: LaunchMode::from_str(
                map.get("launchMode").and_then(|v| v.as_str()).unwrap_or("native"),
            ),
        }
    }
}

pub fn launch(exe_path: &str, opts: &LaunchOptions) -> Result<u32, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let prefix = opts
        .prefix
        .clone()
        .unwrap_or_else(|| home.join(".metalsharp").join("prefix").to_string_lossy().to_string());

    match opts.mode {
        LaunchMode::Wine => launch_via_wine(exe_path, &prefix, opts),
        LaunchMode::Native => launch_native(exe_path, opts),
    }
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

fn launch_native(exe_path: &str, opts: &LaunchOptions) -> Result<u32, Box<dyn std::error::Error>> {
    let metalsharp_bin = find_metalsharp_native()?;

    let mut args: Vec<String> = Vec::new();
    args.push(exe_path.into());

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

fn find_metalsharp_native() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
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
    let home = dirs::home_dir().unwrap_or_default();
    let config_path = home.join(".metalsharp").join("config.json");
    let mode = if config_path.exists() {
        std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str::<serde_json::Map<String, Value>>(&s).ok())
            .and_then(|m| m.get("launchMode").and_then(|v| v.as_str()).map(String::from))
            .unwrap_or_else(|| "native".into())
    } else {
        "native".into()
    };

    json!({
        "ok": true,
        "launch_mode": mode,
        "wine_available": find_wine().is_ok(),
        "native_available": find_metalsharp_native().is_ok(),
    })
}

pub fn set_config(mode: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let config_dir = home.join(".metalsharp");
    std::fs::create_dir_all(&config_dir)?;
    let config_path = config_dir.join("config.json");

    let existing = if config_path.exists() {
        std::fs::read_to_string(&config_path)?
    } else {
        "{}".into()
    };

    let mut config: serde_json::Map<String, Value> = serde_json::from_str(&existing)?;
    config.insert("launchMode".into(), json!(mode));
    std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;

    Ok(get_config())
}
