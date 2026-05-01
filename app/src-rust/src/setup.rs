use serde_json::{json, Map, Value};
use std::path::PathBuf;

pub fn state() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    let config_path = home.join(".metalsharp").join("setup.json");

    if config_path.exists() {
        if let Ok(contents) = std::fs::read_to_string(&config_path) {
            if let Ok(cfg) = serde_json::from_str::<Map<String, Value>>(&contents) {
                return json!({
                    "ok": true,
                    "completed": cfg.get("completed").and_then(|v| v.as_bool()).unwrap_or(false),
                    "step": cfg.get("step").and_then(|v| v.as_u64()).unwrap_or(0),
                    "deviceName": cfg.get("deviceName").and_then(|v| v.as_str()).unwrap_or(""),
                    "steamApiKeySet": cfg.get("steamApiKeySet").and_then(|v| v.as_bool()).unwrap_or(false),
                    "steamcmdLoggedIn": cfg.get("steamcmdLoggedIn").and_then(|v| v.as_bool()).unwrap_or(false),
                });
            }
        }
    }

    json!({
        "ok": true,
        "completed": false,
        "step": 0,
        "deviceName": "",
        "steamApiKeySet": false,
        "steamcmdLoggedIn": false,
    })
}

pub fn save_step(body: &Map<String, Value>) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let config_dir = home.join(".metalsharp");
    std::fs::create_dir_all(&config_dir)?;
    let config_path = config_dir.join("setup.json");

    let mut cfg: Map<String, Value> = if config_path.exists() {
        std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or_default()
    } else {
        Map::new()
    };

    if let Some(step) = body.get("step").and_then(|v| v.as_u64()) {
        cfg.insert("step".into(), json!(step));
    }
    if let Some(completed) = body.get("completed").and_then(|v| v.as_bool()) {
        cfg.insert("completed".into(), json!(completed));
    }
    if let Some(name) = body.get("deviceName").and_then(|v| v.as_str()) {
        cfg.insert("deviceName".into(), json!(name));
    }
    if let Some(set) = body.get("steamApiKeySet").and_then(|v| v.as_bool()) {
        cfg.insert("steamApiKeySet".into(), json!(set));
    }
    if let Some(logged) = body.get("steamcmdLoggedIn").and_then(|v| v.as_bool()) {
        cfg.insert("steamcmdLoggedIn".into(), json!(logged));
    }

    std::fs::write(&config_path, serde_json::to_string_pretty(&cfg)?)?;

    Ok(state())
}

pub fn generate_device_name() -> String {
    let adjectives = [
        "Swift", "Crimson", "Silent", "Bright", "Shadow",
        "Frost", "Ember", "Storm", "Lunar", "Solar",
        "Nova", "Pixel", "Cyber", "Iron", "Neon",
        "Blaze", "Drift", "Pulse", "Glitch", "Volt",
    ];
    let nouns = [
        "Wolf", "Falcon", "Tiger", "Raven", "Phoenix",
        "Cobra", "Panther", "Hawk", "Lynx", "Viper",
        "Fox", "Bear", "Eagle", "Shark", "Dragon",
        "Knight", "Blade", "Spark", "Forge", "Core",
    ];

    let mut buf: [u8; 4] = [0; 4];
    let _ = std::fs::read("/dev/urandom").map(|v| {
        for (i, b) in v.iter().take(4).enumerate() {
            buf[i] = *b;
        }
    });
    let adj_idx = (u32::from_be_bytes([0, buf[0], buf[1], buf[2]]) as usize) % adjectives.len();
    let noun_idx = (buf[3] as usize) % nouns.len();

    format!("{}-{}", adjectives[adj_idx], nouns[noun_idx])
}

pub fn dependencies() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let mono = check_command("mono");
    let sdl3 = check_dylib(&home, "libSDL3.dylib")
        || check_framework("SDL3")
        || check_brew("sdl3");
    let steamcmd = check_path(&home.join("steamcmd/steamcmd.sh"))
        || check_command("steamcmd");
    let steam = check_path(&home.join("Library/Application Support/Steam/Steam.app/Contents/MacOS/steam_osx"))
        || check_command("steam");
    let homebrew = check_command("brew");

    let all_ok = mono && sdl3;

    json!({
        "ok": true,
        "allInstalled": all_ok,
        "dependencies": [
            {
                "id": "mono",
                "name": "Mono Runtime",
                "desc": "Required for XNA/FNA games (.NET Framework)",
                "installed": mono,
                "required": true,
                "installCmd": "brew install mono",
            },
            {
                "id": "sdl3",
                "name": "SDL3",
                "desc": "Graphics and input backend for FNA games",
                "installed": sdl3,
                "required": true,
                "installCmd": "brew install sdl3",
            },
            {
                "id": "steamcmd",
                "name": "SteamCMD",
                "desc": "Downloads Windows game depots from Steam",
                "installed": steamcmd,
                "required": false,
                "installCmd": "Auto-installed by MetalSharp on first use",
            },
            {
                "id": "steam",
                "name": "Steam Client",
                "desc": "Native macOS Steam (for library data)",
                "installed": steam,
                "required": false,
                "installCmd": "https://store.steampowered.com/about/",
            },
            {
                "id": "homebrew",
                "name": "Homebrew",
                "desc": "Package manager for installing dependencies",
                "installed": homebrew,
                "required": false,
                "installCmd": "/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"",
            },
        ],
    })
}

pub fn install_dependencies(body: &Map<String, Value>) -> Result<Value, Box<dyn std::error::Error>> {
    let ids: Vec<&str> = body
        .get("ids")
        .and_then(|v| v.as_array())
        .map(|a| a.iter().filter_map(|v| v.as_str()).collect())
        .unwrap_or_default();

    let home = dirs::home_dir().ok_or("no home dir")?;
    let mut results = Vec::new();

    for id in ids {
        let result = match id {
            "mono" => run_brew_install("mono"),
            "sdl3" => run_brew_install("sdl3"),
            "steamcmd" => install_steamcmd(&home),
            _ => {
                json!({"id": id, "ok": false, "error": "unknown dependency"})
            }
        };
        results.push(result);
    }

    let all_ok = results.iter().all(|r| r.get("ok").and_then(|v| v.as_bool()).unwrap_or(false));

    Ok(json!({
        "ok": all_ok,
        "results": results,
    }))
}

fn run_brew_install(package: &str) -> Value {
    let output = std::process::Command::new("brew")
        .args(["install", package])
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .output();

    match output {
        Ok(o) => {
            let success = o.status.success();
            let stdout = String::from_utf8_lossy(&o.stdout).to_string();
            let stderr = String::from_utf8_lossy(&o.stderr).to_string();
            let combined = format!("{}{}", stdout, stderr);

            if success || combined.contains("already installed") {
                json!({"id": package, "ok": true})
            } else {
                json!({"id": package, "ok": false, "error": combined.lines().last().unwrap_or("unknown error")})
            }
        }
        Err(e) => json!({"id": package, "ok": false, "error": e.to_string()}),
    }
}

fn install_steamcmd(home: &PathBuf) -> Value {
    let steamcmd_dir = home.join("steamcmd");
    let steamcmd_sh = steamcmd_dir.join("steamcmd.sh");

    if steamcmd_sh.exists() {
        return json!({"id": "steamcmd", "ok": true});
    }

    let _ = std::fs::create_dir_all(&steamcmd_dir);

    let output = std::process::Command::new("curl")
        .args([
            "-sL",
            "-o",
            steamcmd_dir.join("steamcmd.tar.gz").to_str().unwrap_or(""),
            "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz",
        ])
        .output();

    match output {
        Ok(o) if o.status.success() => {
            let tar_output = std::process::Command::new("tar")
                .args([
                    "-xzf",
                    steamcmd_dir.join("steamcmd.tar.gz").to_str().unwrap_or(""),
                    "-C",
                    steamcmd_dir.to_str().unwrap_or(""),
                ])
                .output();

            let _ = std::fs::remove_file(steamcmd_dir.join("steamcmd.tar.gz"));

            match tar_output {
                Ok(t) if t.status.success() => json!({"id": "steamcmd", "ok": true}),
                _ => json!({"id": "steamcmd", "ok": false, "error": "failed to extract steamcmd"}),
            }
        }
        _ => json!({"id": "steamcmd", "ok": false, "error": "failed to download steamcmd"}),
    }
}

pub fn prepare_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let game_dir = home.join(".metalsharp").join("games").join(appid.to_string());

    if !game_dir.exists() {
        return Err(format!("game directory not found: {}", game_dir.display()).into());
    }

    let runtime_dir = home.join(".metalsharp").join("runtime");
    let marker = game_dir.join(".metalsharp_prepared");

    if marker.exists() {
        return Ok(json!({"ok": true, "alreadyPrepared": true}));
    }

    let is_dotnet = detect_dotnet_game(&game_dir);

    if is_dotnet {
        setup_fna_runtime(&game_dir, &runtime_dir)?;
    }

    std::fs::write(&marker, format!("prepared: is_dotnet={}", is_dotnet))?;

    Ok(json!({
        "ok": true,
        "alreadyPrepared": false,
        "gameType": if is_dotnet { "xna_fna" } else { "native" },
        "appid": appid,
    }))
}

fn detect_dotnet_game(game_dir: &PathBuf) -> bool {
    let managed_dir = game_dir.join("Celeste_Data").join("Managed");
    if managed_dir.exists() {
        return true;
    }

    for entry in walkdir::WalkDir::new(game_dir).max_depth(2).into_iter().flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        let name_lower = name.to_lowercase();
        if name_lower.contains("microsoft.xna")
            || name_lower.contains("fna")
            || name_lower == "monogame"
            || name_lower.contains("_data") && entry.path().is_dir()
        {
            return true;
        }
    }

    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            let name_lower = name.to_lowercase();
            if name_lower.contains("steamworks.net")
                || name_lower.contains("fmod")
            {
                return true;
            }
        }
    }

    false
}

fn setup_fna_runtime(game_dir: &PathBuf, runtime_dir: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let fna_dir = runtime_dir.join("fna");
    let shims_dir = runtime_dir.join("shims");

    if fna_dir.exists() {
        for entry in std::fs::read_dir(&fna_dir)?.flatten() {
            let src = entry.path();
            if let Some(ext) = src.extension() {
                if ext == "dll" || ext == "config" {
                    let _ = std::fs::copy(&src, game_dir.join(entry.file_name()));
                }
            }
        }
    }

    let dylibs_to_copy = [
        "libFNA3D.dylib",
        "libSDL3.dylib",
        "libCSteamworks.dylib",
        "libfmod.dylib",
        "libfmodstudio.dylib",
        "libsteam_api.dylib",
    ];

    for dylib in &dylibs_to_copy {
        let src = shims_dir.join(dylib);
        if src.exists() {
            let _ = std::fs::copy(&src, game_dir.join(dylib));
        }
    }

    let steam_appid_src = runtime_dir.join("steam_appid.txt");
    if steam_appid_src.exists() {
        let _ = std::fs::copy(&steam_appid_src, game_dir.join("steam_appid.txt"));
    }

    let steam_appid_fallback = game_dir.join("steam_appid.txt");
    if !steam_appid_fallback.exists() {
        let _ = std::fs::write(&steam_appid_fallback, "");
    }

    Ok(())
}

fn check_command(cmd: &str) -> bool {
    std::process::Command::new("which")
        .arg(cmd)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

fn check_path(path: &PathBuf) -> bool {
    path.exists()
}

fn check_dylib(home: &PathBuf, name: &str) -> bool {
    let candidates = [
        home.join(format!("lib/{}", name)),
        PathBuf::from(format!("/opt/homebrew/lib/{}", name)),
        PathBuf::from(format!("/usr/local/lib/{}", name)),
    ];
    candidates.iter().any(|p| p.exists())
}

fn check_framework(name: &str) -> bool {
    PathBuf::from(format!("/Library/Frameworks/{}.framework", name)).exists()
}

fn check_brew(formula: &str) -> bool {
    std::process::Command::new("brew")
        .args(["list", formula])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}
