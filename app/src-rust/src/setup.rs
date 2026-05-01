use serde_json::{json, Map, Value};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

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
    if let Ok(mut f) = std::fs::File::open("/dev/urandom") {
        use std::io::Read;
        let _ = f.read_exact(&mut buf);
    } else {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos()
            .to_ne_bytes();
        for (i, b) in nanos.iter().take(4).enumerate() {
            buf[i] = *b;
        }
    }
    let adj_idx = (u32::from_be_bytes([0, buf[0], buf[1], buf[2]]) as usize) % adjectives.len();
    let noun_idx = (buf[3] as usize) % nouns.len();

    format!("{}-{}", adjectives[adj_idx], nouns[noun_idx])
}

pub fn dependencies() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let mono = check_command("mono") || check_path(&PathBuf::from("/opt/homebrew/bin/mono"));
    let sdl3 = check_dylib(&home, "libSDL3.dylib")
        || check_framework("SDL3")
        || check_brew("sdl3");
    let steamcmd = check_path(&home.join("steamcmd/steamcmd.sh"))
        || check_command("steamcmd");
    let steam = check_path(&home.join("Library/Application Support/Steam/Steam.app/Contents/MacOS/steam_osx"))
        || check_path(&PathBuf::from("/Applications/Steam.app/Contents/MacOS/steam_osx"))
        || check_command("steam");
    let homebrew = check_command("brew") || check_path(&PathBuf::from("/opt/homebrew/bin/brew")) || check_path(&PathBuf::from("/usr/local/bin/brew"));

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
        let _ = std::fs::write(game_dir.join("steam_appid.txt"), appid.to_string());
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
    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            let name_lower = name.to_lowercase();
            if name_lower.ends_with("_data") && entry.path().is_dir() {
                let managed = entry.path().join("Managed");
                if managed.exists() {
                    return !has_native_windows_dlls(game_dir);
                }
            }
        }
    }

    false
}

fn has_native_windows_dlls(game_dir: &PathBuf) -> bool {
    let managed_extensions = ["cs.dll", ".managed.dll", ".net.dll"];
    if let Ok(entries) = std::fs::read_dir(game_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if let Some(ext) = path.extension() {
                if ext == "dll" {
                    let name = entry.file_name().to_string_lossy().to_string();
                    let name_lower = name.to_lowercase();
                    let is_managed_wrapper = name_lower.contains("steamworks.net")
                        || name_lower.contains("fmod")
                        || managed_extensions.iter().any(|e| name_lower.ends_with(e));
                    if !is_managed_wrapper {
                        let output = std::process::Command::new("file")
                            .arg(&path)
                            .output();
                        if let Ok(o) = output {
                            let desc = String::from_utf8_lossy(&o.stdout);
                            if desc.contains("PE32") && !desc.contains(".Net") && !desc.contains("Mono/.Net") {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    false
}

fn setup_fna_runtime(game_dir: &PathBuf, _runtime_dir: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let fna_src = home.join("metalsharp").join("src").join("fna");

    let fna_build = fna_src.join("FNA").join("bin").join("Release").join("net4.0");
    let fna3d_build = fna_src.join("FNA3D").join("build");
    let shims_src = fna_src.join("shims");

    if fna_build.exists() {
        if let Ok(entries) = std::fs::read_dir(&fna_build) {
            for entry in entries.flatten() {
                let src = entry.path();
                if let Some(ext) = src.extension() {
                    if ext == "dll" {
                        let _ = std::fs::copy(&src, game_dir.join(entry.file_name()));
                    }
                }
            }
        }
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

    if fna_build.exists() {
        let fna_dll = fna_build.join("FNA.dll");
        if fna_dll.exists() {
            for name in &xna_assemblies {
                let _ = std::fs::copy(&fna_dll, game_dir.join(name));
            }
        }
    }

    if fna3d_build.exists() {
        let src = fna3d_build.join("libFNA3D.dylib");
        if src.exists() {
            let _ = std::fs::copy(&src, game_dir.join("libFNA3D.dylib"));
        }
    }

    if shims_src.exists() {
        for shim in &["csteamworks_shim.c", "fmod_stub.c", "fmodstudio_stub.c"] {
            let src = shims_src.join(shim);
            if src.exists() {
                build_shim(&src, game_dir);
            }
        }
    }

    let sdl3_candidates = [
        PathBuf::from("/opt/homebrew/lib/libSDL3.0.dylib"),
        PathBuf::from("/usr/local/lib/libSDL3.0.dylib"),
    ];
    for sdl3 in &sdl3_candidates {
        if sdl3.exists() {
            let dst = game_dir.join("libSDL3.0.dylib");
            let _ = std::fs::copy(sdl3, &dst);
            let _ = std::process::Command::new("install_name_tool")
                .args(["-id", "@loader_path/libSDL3.0.dylib"])
                .arg(&dst)
                .output();

            let fna3d = game_dir.join("libFNA3D.dylib");
            if fna3d.exists() {
                let _ = std::process::Command::new("install_name_tool")
                    .args(["-change", "/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib", "@loader_path/libSDL3.0.dylib"])
                    .arg(&fna3d)
                    .output();
            }

            let _ = std::os::unix::fs::symlink("libSDL3.0.dylib", game_dir.join("libSDL3.dylib"));
            break;
        }
    }

    let steam_dylib_candidates = [
        home.join("Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/Frameworks/Steam Helper.app/Contents/MacOS/libsteam_api.dylib"),
        home.join("Library/Application Support/Steam/steamapps/common/Nidhogg 2/Nidhogg_2.app/Contents/MacOS/libsteam_api.dylib"),
    ];
    for dylib in &steam_dylib_candidates {
        if dylib.exists() {
            let _ = std::fs::copy(dylib, game_dir.join("libsteam_api.dylib"));
            break;
        }
    }

    let _ = std::fs::write(game_dir.join("steam_appid.txt"), "");

    let _ = std::process::Command::new("codesign")
        .args(["--force", "-s", "-"])
        .arg(game_dir.join("libCSteamworks.dylib"))
        .arg(game_dir.join("libfmod.dylib"))
        .arg(game_dir.join("libfmodstudio.dylib"))
        .output();

    Ok(())
}

fn build_shim(src: &PathBuf, game_dir: &PathBuf) {
    let name = src.file_stem().unwrap_or_default().to_string_lossy();
    let (output_name, install_name) = if name.contains("csteamworks") {
        ("libCSteamworks.dylib", "@loader_path/libCSteamworks.dylib")
    } else if name.contains("fmodstudio") {
        ("libfmodstudio.dylib", "@loader_path/libfmodstudio.dylib")
    } else if name.contains("fmod") {
        ("libfmod.dylib", "@loader_path/libfmod.dylib")
    } else {
        return;
    };

    let output = game_dir.join(output_name);
    let result = std::process::Command::new("clang")
        .args(["-shared", "-arch", "arm64"])
        .arg("-o").arg(&output)
        .arg(src)
        .arg("-install_name").arg(install_name)
        .output();

    if let Ok(o) = result {
        if o.status.success() {
            let _ = std::process::Command::new("codesign")
                .args(["--force", "-s", "-"])
                .arg(&output)
                .output();
        }
    }
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
