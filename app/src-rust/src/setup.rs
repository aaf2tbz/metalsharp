use serde_json::{json, Map, Value};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

pub fn state() -> Value {
    let config_path = crate::platform::metalsharp_home().join("setup.json");

    if config_path.exists() {
        if let Ok(contents) = std::fs::read_to_string(&config_path) {
            if let Ok(cfg) = serde_json::from_str::<Map<String, Value>>(&contents) {
                return json!({
                    "ok": true,
                    "completed": cfg.get("completed").and_then(|v| v.as_bool()).unwrap_or(false),
                    "step": cfg.get("step").and_then(|v| v.as_u64()).unwrap_or(0),
                    "deviceName": cfg.get("deviceName").and_then(|v| v.as_str()).unwrap_or(""),
                    "steamApiKeySet": cfg.get("steamApiKeySet").and_then(|v| v.as_bool()).unwrap_or(false),
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
    })
}

pub fn save_step(body: &Map<String, Value>) -> Result<Value, Box<dyn std::error::Error>> {
    let config_dir = crate::platform::metalsharp_home();
    std::fs::create_dir_all(&config_dir)?;
    let config_path = config_dir.join("setup.json");

    let mut cfg: Map<String, Value> = if config_path.exists() {
        std::fs::read_to_string(&config_path).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default()
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
    std::fs::write(&config_path, serde_json::to_string_pretty(&cfg)?)?;

    Ok(state())
}

pub fn generate_device_name() -> String {
    let adjectives = [
        "Swift", "Crimson", "Silent", "Bright", "Shadow", "Frost", "Ember", "Storm", "Lunar", "Solar", "Nova", "Pixel",
        "Cyber", "Iron", "Neon", "Blaze", "Drift", "Pulse", "Glitch", "Volt",
    ];
    let nouns = [
        "Wolf", "Falcon", "Tiger", "Raven", "Phoenix", "Cobra", "Panther", "Hawk", "Lynx", "Viper", "Fox", "Bear",
        "Eagle", "Shark", "Dragon", "Knight", "Blade", "Spark", "Forge", "Core",
    ];

    let mut buf: [u8; 4] = [0; 4];
    if let Ok(mut f) = std::fs::File::open("/dev/urandom") {
        use std::io::Read;
        let _ = f.read_exact(&mut buf);
    } else {
        let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos().to_ne_bytes();
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
    let runtime_dir = crate::platform::runtime_dir();

    if crate::platform::current() == crate::platform::HostPlatform::Linux {
        let tar = check_command("tar");
        let curl = check_command("curl");
        let clang = check_command("clang") || check_command("gcc");
        let mono = check_command("mono") || check_path(&runtime_dir.join("mono-arm64/bin/mono"));
        let wine = check_command("wine");
        let steam =
            check_path(&home.join(".steam/steam/steamapps")) || check_path(&home.join(".local/share/Steam/steamapps"));
        let metalsharp_wine =
            check_path(&runtime_dir.join("wine/bin/wine")) || check_path(&runtime_dir.join("wine/bin/metalsharp-wine"));
        let all_ok = tar && curl && (metalsharp_wine || wine);

        return json!({
            "ok": true,
            "allInstalled": all_ok,
            "platform": crate::platform::name(),
            "dependencies": [
                {
                    "id": "system_tools",
                    "name": "Linux runtime tools",
                    "desc": "Provides tar and curl for installing bundled Wine runtime assets.",
                    "installed": tar && curl,
                    "required": true,
                    "installCmd": "sudo apt install -y tar curl",
                },
                {
                    "id": "build_tools",
                    "name": "Build tools",
                    "desc": "Provides clang or gcc for optional native shims.",
                    "installed": clang,
                    "required": false,
                    "installCmd": "sudo apt install -y build-essential clang",
                },
                {
                    "id": "wine",
                    "name": "Wine",
                    "desc": "System Wine fallback used when no bundled Linux runtime is packaged.",
                    "installed": wine,
                    "required": true,
                    "installCmd": "sudo apt install -y wine",
                },
                {
                    "id": "metalsharp_wine",
                    "name": "MetalSharp Wine Runtime",
                    "desc": "Bundled Linux Wine runtime used for Windows Steam and game launches.",
                    "installed": metalsharp_wine,
                    "required": true,
                    "installCmd": "metalsharp-setup-wine",
                },
                {
                    "id": "mono",
                    "name": "Mono Runtime",
                    "desc": "Optional runtime for XNA/FNA games.",
                    "installed": mono,
                    "required": false,
                    "installCmd": "sudo apt install -y mono-complete",
                },
                {
                    "id": "steam",
                    "name": "Steam Client (Linux)",
                    "desc": "Optional native Linux Steam library source and game detector.",
                    "installed": steam,
                    "required": false,
                    "installCmd": "sudo apt install -y steam",
                },
            ],
        });
    }

    let mono = check_command("mono") || check_path(&PathBuf::from("/opt/homebrew/bin/mono"));
    let rosetta = check_rosetta();
    let xcode_cli = check_command("clang") || check_command("xcodebuild");
    let steam = check_path(&home.join("Library/Application Support/Steam/Steam.app/Contents/MacOS/steam_osx"))
        || check_path(&PathBuf::from("/Applications/Steam.app/Contents/MacOS/steam_osx"));
    let homebrew = check_command("brew");
    let moltenvk = check_path(&PathBuf::from("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"));
    let metalsharp_wine =
        check_path(&runtime_dir.join("wine/bin/wine")) || check_path(&runtime_dir.join("wine/bin/metalsharp-wine"));

    let all_ok = homebrew && rosetta && xcode_cli && metalsharp_wine;

    json!({
        "ok": true,
        "allInstalled": all_ok,
        "platform": crate::platform::name(),
        "dependencies": [
            {
                "id": "homebrew",
                "name": "Homebrew",
                "desc": "Package manager — required to install other dependencies",
                "installed": homebrew,
                "required": true,
                "installCmd": "/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"",
            },
            {
                "id": "rosetta",
                "name": "Rosetta 2",
                "desc": "x86_64 translation layer — needed for 32-bit Windows games and x86 mono",
                "installed": rosetta,
                "required": true,
                "installCmd": "softwareupdate --install-rosetta --agree-to-license",
            },
            {
                "id": "xcode_cli",
                "name": "Xcode Command Line Tools",
                "desc": "Provides clang for building native shims (CSteamworks, gdiplus stub)",
                "installed": xcode_cli,
                "required": true,
                "installCmd": "xcode-select --install",
            },
            {
                "id": "metalsharp_wine",
                "name": "MetalSharp Wine",
                "desc": "From-source Wine 11.5 with DXMT Metal D3D11, gnutls TLS, MoltenVK. Runs Windows Steam and launches games with native Metal rendering.",
                "installed": metalsharp_wine,
                "required": true,
                "installCmd": "metalsharp-setup-wine",
            },
            {
                "id": "mono",
                "name": "Mono Runtime (arm64)",
                "desc": "Required for Terraria and other arm64 FNA/XNA games",
                "installed": mono,
                "required": false,
                "installCmd": "brew install mono",
            },
            {
                "id": "moltenvk",
                "name": "MoltenVK",
                "desc": "Vulkan→Metal translation. Optional fallback graphics backend.",
                "installed": moltenvk,
                "required": false,
                "installCmd": "brew install molten-vk",
            },
            {
                "id": "steam",
                "name": "Steam Client (macOS)",
                "desc": "Provides native macOS libraries (libsteam_api.dylib) for FNA games. Install Terraria for macOS to get the best compatibility.",
                "installed": steam,
                "required": false,
                "installCmd": "https://store.steampowered.com/about/",
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

    let _home = dirs::home_dir().ok_or("no home dir")?;
    let mut results = Vec::new();

    for id in ids {
        let result = if crate::platform::current() == crate::platform::HostPlatform::Linux {
            match id {
                "system_tools" => run_apt_install(&["tar", "curl"]),
                "build_tools" => run_apt_install(&["build-essential", "clang"]),
                "wine" => run_apt_install(&["wine"]),
                "mono" => run_apt_install(&["mono-complete"]),
                "steam" => run_apt_install(&["steam"]),
                _ => json!({"id": id, "ok": false, "error": "unknown dependency"}),
            }
        } else {
            match id {
                "mono" => run_brew_install("mono"),
                "sdl3" => run_brew_install("sdl3"),
                _ => json!({"id": id, "ok": false, "error": "unknown dependency"}),
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

fn run_apt_install(packages: &[&str]) -> Value {
    let mut args = vec!["apt-get", "install", "-y"];
    args.extend(packages);

    let output = std::process::Command::new("pkexec")
        .args(&args)
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .output()
        .or_else(|_| {
            let mut sudo_args = vec!["apt-get", "install", "-y"];
            sudo_args.extend(packages);
            std::process::Command::new("sudo")
                .args(&sudo_args)
                .stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::piped())
                .output()
        });

    match output {
        Ok(o) => {
            let success = o.status.success();
            let stdout = String::from_utf8_lossy(&o.stdout).to_string();
            let stderr = String::from_utf8_lossy(&o.stderr).to_string();
            let combined = format!("{}{}", stdout, stderr);
            json!({
                "id": packages.join(","),
                "ok": success,
                "error": if success { Value::Null } else { json!(combined.lines().last().unwrap_or("apt install failed")) },
            })
        },
        Err(e) => json!({"id": packages.join(","), "ok": false, "error": e.to_string()}),
    }
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
        },
        Err(e) => json!({"id": package, "ok": false, "error": e.to_string()}),
    }
}

pub fn resolve_game_dir(appid: u32) -> Option<PathBuf> {
    let local_dir = crate::platform::metalsharp_home().join("games").join(appid.to_string());
    if local_dir.join(".metalsharp_prepared").exists() {
        return Some(local_dir);
    }

    let dual = crate::scan::resolve_dual_game_dir(appid);

    if let Some(ref wine_dir) = dual.wine_dir {
        if wine_dir.exists() {
            return Some(wine_dir.clone());
        }
    }

    if let Some(ref macos_dir) = dual.macos_dir {
        if macos_dir.exists() {
            return Some(macos_dir.clone());
        }
    }

    if local_dir.exists() {
        return Some(local_dir);
    }

    None
}

pub fn resolve_native_game_dir(appid: u32) -> Option<PathBuf> {
    crate::scan::resolve_dual_game_dir(appid).macos_dir
}

pub fn prepare_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let game_dir = resolve_game_dir(appid).ok_or_else(|| format!("game directory not found for appid {}", appid))?;

    let marker = game_dir.join(".metalsharp_prepared");

    let is_dotnet = detect_dotnet_game(&game_dir);
    let game_type = match appid {
        105600 => "xna_fna_arm64",
        504230 => "xna_fna_x86",
        312520 | 375520 => "dxmt_metal",
        2050650 | 3164500 | 848450 => "dxmt_metal12",
        535520 => "wined3d_32",
        945360 | 1139900 => "steam",
        620 | 265930 => "d3d9_metal",
        _ => {
            if is_dotnet {
                "xna_fna"
            } else {
                let pipeline = crate::mtsp::rules::resolve_pipeline(appid);
                pipeline.to_legacy_method()
            }
        },
    };

    if !marker.exists() {
        let _ = std::fs::write(game_dir.join("steam_appid.txt"), appid.to_string());
    }

    match appid {
        105600 => prepare_terrarria(&game_dir, &home)?,
        504230 => prepare_celeste(&game_dir, &home)?,
        312520 | 375520 => prepare_rain_world(&game_dir, &home)?,
        2050650 | 3164500 | 848450 => prepare_dxmt_metal12(&game_dir, &home)?,
        535520 => prepare_nidhogg_2(&game_dir, &home)?,
        945360 | 1139900 => prepare_metalsharp_game(&game_dir, &home, appid)?,
        620 | 265930 => prepare_goldberg_game(&game_dir, &home, appid)?,
        _ => {
            if is_dotnet {
                setup_fna_runtime(&game_dir, &home)?;
            }
        },
    }

    let pipeline = crate::mtsp::rules::resolve_pipeline(appid);
    let node = crate::mtsp::engine::get_pipeline(pipeline);
    if let Some(subdir) = node.shader_cache_subdir {
        crate::mtsp::shader_cache::deploy_preset_cache(&crate::platform::metalsharp_home(), subdir, appid);
    }

    std::fs::write(&marker, format!("prepared: game_type={}", game_type))?;

    Ok(json!({
        "ok": true,
        "alreadyPrepared": false,
        "gameType": game_type,
        "appid": appid,
    }))
}

fn prepare_terrarria(game_dir: &PathBuf, home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let mac_libs =
        home.join("Library/Application Support/Steam/steamapps/common/Terraria/Terraria.app/Contents/MacOS/osx");

    if mac_libs.exists() {
        for lib in &["libsteam_api.dylib", "libSDL3.0.dylib", "libFAudio.0.dylib", "libFNA3D.0.dylib", "libnfd.dylib"] {
            let src = mac_libs.join(lib);
            if src.exists() {
                let _ = std::fs::copy(&src, game_dir.join(lib));
            }
        }
        let _ = std::os::unix::fs::symlink("libSDL3.0.dylib", game_dir.join("libSDL3.dylib"));
        let _ = std::os::unix::fs::symlink("libFAudio.0.dylib", game_dir.join("libFAudio.dylib"));
        let _ = std::os::unix::fs::symlink("libFNA3D.0.dylib", game_dir.join("libFNA3D.dylib"));
    }

    let gdiplus = game_dir.join("libgdiplus.dylib");
    if !gdiplus.exists() {
        let repo = home.join("metalsharp");
        let stub_src = repo.join("src/fna/terraria/gdiplus_stub.c");
        if stub_src.exists() {
            let _ = std::process::Command::new("clang")
                .args(["-shared", "-arch", "arm64", "-o"])
                .arg(&gdiplus)
                .arg(&stub_src)
                .args(["-install_name", "@loader_path/libgdiplus.dylib"])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
    }

    let launcher = game_dir.join("TerrariaLauncher.exe");
    if !launcher.exists() {
        let repo = home.join("metalsharp");
        let src = repo.join("src/fna/terraria/TerrariaLauncher.cs");
        if src.exists() {
            let _ = std::process::Command::new("mcs")
                .args(["-out"])
                .arg(&launcher)
                .args(["-target:winexe"])
                .arg(&src)
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
    }

    let pipeline = game_dir.join("Microsoft.Xna.Framework.Content.Pipeline.dll");
    if !pipeline.exists() {
        let repo = home.join("metalsharp");
        let src = repo.join("src/fna/terraria/ContentPipelineStub.cs");
        if src.exists() {
            let _ = std::process::Command::new("mcs")
                .args(["-out"])
                .arg(&pipeline)
                .args(["-target:library"])
                .arg(&src)
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();
        }
    }

    let xact = game_dir.join("Microsoft.Xna.Framework.Xact.dll");
    if !xact.exists() {
        let repo = home.join("metalsharp");
        let src = repo.join("src/fna/terraria/Microsoft.Xna.Framework.Xact.dll");
        if src.exists() {
            let _ = std::fs::copy(&src, &xact);
        }
    }

    Ok(())
}

fn prepare_celeste(game_dir: &PathBuf, home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let ms_home = crate::platform::metalsharp_home();
    let mono_x86 = ms_home.join("runtime").join("mono-x86").join("bin").join("mono");
    if !mono_x86.exists() {
        let _ = crate::launch::run_game_setup_script(504230);
    }

    let shims_dir = ms_home.join("runtime").join("shims");
    let _ = std::fs::create_dir_all(&shims_dir);

    let steam_dylib = find_steam_api(home);
    if let Some(ref src) = steam_dylib {
        let _ = std::fs::copy(src, game_dir.join("libsteam_api.dylib"));
        let _ = std::fs::copy(src, shims_dir.join("libsteam_api.dylib"));
    }

    let csteamworks = game_dir.join("libCSteamworks.dylib");
    if !csteamworks.exists() {
        let repo = home.join("metalsharp");
        let shim_src = repo.join("src/fna/shims/csteamworks_shim.c");
        let alias_file = repo.join("src/fna/shims/csteamworks_aliases.txt");
        if shim_src.exists() {
            let mut cmd = std::process::Command::new("clang");
            cmd.args(["-shared", "-arch", "x86_64"]).arg("-o").arg(&csteamworks).arg(&shim_src);

            if alias_file.exists() {
                if let Ok(aliases) = std::fs::read_to_string(&alias_file) {
                    let flags: Vec<&str> = aliases.split_whitespace().collect();
                    if !flags.is_empty() {
                        cmd.args(["-L"]).arg(game_dir).arg("-lsteam_api");
                        cmd.args(&flags);
                    }
                }
            }

            let result = cmd
                .args(["-install_name", "@loader_path/libCSteamworks.dylib"])
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status();

            if result.is_err() || !result.unwrap().success() {
                let _ = std::process::Command::new("clang")
                    .args(["-shared", "-arch", "x86_64"])
                    .arg("-o")
                    .arg(&csteamworks)
                    .arg(&shim_src)
                    .args(["-undefined", "dynamic_lookup", "-install_name", "@loader_path/libCSteamworks.dylib"])
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .status();
            }

            let _ = std::fs::copy(&csteamworks, shims_dir.join("libCSteamworks.dylib"));
        }
    }

    for fmod in &["libfmod.dylib", "libfmodstudio.dylib"] {
        let dst = game_dir.join(fmod);
        if !dst.exists() {
            let stub_name = fmod.replace(".dylib", "_stub.c");
            let repo = home.join("metalsharp");
            let stub_src = repo.join("src/fna/shims").join(&stub_name);
            if stub_src.exists() {
                let _ = std::process::Command::new("clang")
                    .args(["-shared", "-arch", "x86_64"])
                    .arg("-o")
                    .arg(&dst)
                    .arg(&stub_src)
                    .args(["-undefined", "dynamic_lookup", "-install_name", &format!("@loader_path/{}", fmod)])
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .status();
                let _ = std::fs::copy(&dst, shims_dir.join(fmod));
            }
        }
    }

    Ok(())
}

fn prepare_rain_world(game_dir: &PathBuf, _home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let marker = game_dir.join(".metalsharp_prepared");
    if !marker.exists() {
        let _ = std::fs::write(&marker, "dxmt_metal");
    }
    Ok(())
}

fn prepare_dxmt_metal12(game_dir: &PathBuf, _home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let marker = game_dir.join(".metalsharp_prepared");
    if !marker.exists() {
        let _ = std::fs::write(&marker, "dxmt_metal12");
    }
    Ok(())
}

fn prepare_nidhogg_2(game_dir: &PathBuf, _home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let marker = game_dir.join(".metalsharp_prepared");
    if !marker.exists() {
        let _ = std::fs::write(&marker, "wined3d_32");
    }
    Ok(())
}

fn prepare_metalsharp_game(game_dir: &PathBuf, home: &PathBuf, appid: u32) -> Result<(), Box<dyn std::error::Error>> {
    let ms_home = crate::platform::metalsharp_home();
    let ms_root = crate::platform::wine_runtime_root();
    let wine = ms_root.join("bin").join("wine");

    let prefix = ms_home.join(format!("prefix-{}", appid));
    let prefix_str = prefix.to_string_lossy().to_string();

    if !prefix.exists() {
        std::fs::create_dir_all(&prefix)?;
        if wine.exists() {
            let mut cmd = std::process::Command::new(&wine);
            cmd.env("WINEPREFIX", &prefix_str)
                .arg("wineboot")
                .arg("--init")
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null());
            crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
            let _ = cmd.status();
        }
    }

    let marker = game_dir.join(".metalsharp_prepared");
    if !marker.exists() {
        let _ = std::fs::write(&marker, format!("metalsharp_wine_{}", appid));
    }

    Ok(())
}

fn prepare_goldberg_game(game_dir: &PathBuf, home: &PathBuf, appid: u32) -> Result<(), Box<dyn std::error::Error>> {
    let goldberg_dir = crate::platform::metalsharp_home().join("runtime").join("goldberg");
    let goldberg_x86 = goldberg_dir.join("x86").join("steam_api.dll");
    let goldberg_x64 = goldberg_dir.join("x64").join("steam_api64.dll");

    if !goldberg_x86.exists() || !goldberg_x64.exists() {
        ensure_goldberg_downloaded(home)?;
    }

    let deploy_dir = match appid {
        620 => {
            let bin = game_dir.join("bin");
            if bin.exists() {
                bin
            } else {
                game_dir.clone()
            }
        },
        265930 => {
            let bin = game_dir.join("Binaries").join("Win32");
            if bin.exists() {
                bin
            } else {
                game_dir.clone()
            }
        },
        _ => game_dir.clone(),
    };

    deploy_goldberg_to_dir(&deploy_dir, &goldberg_dir, appid)?;

    let prefix = crate::platform::steam_prefix_dir();
    let ms_root = crate::platform::wine_runtime_root();
    let ms_wine = crate::platform::runtime_wine_binary(&ms_root);
    if !prefix.join("drive_c/windows/system32").exists() {
        let _ = std::fs::create_dir_all(&prefix);
        if ms_wine.exists() {
            let mut cmd = std::process::Command::new(&ms_wine);
            cmd.env("WINEPREFIX", prefix.to_string_lossy().to_string())
                .arg("wineboot")
                .arg("--init")
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null());
            crate::platform::set_runtime_library_env(&mut cmd, &ms_root);
            let _ = cmd.status();
        }
    }

    Ok(())
}

fn ensure_goldberg_downloaded(home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let ms_home = crate::platform::metalsharp_home();
    let goldberg_dir = ms_home.join("runtime").join("goldberg");
    let goldberg_x86 = goldberg_dir.join("x86").join("steam_api.dll");
    let goldberg_x64 = goldberg_dir.join("x64").join("steam_api64.dll");

    if goldberg_x86.exists() && goldberg_x64.exists() {
        return Ok(());
    }

    std::fs::create_dir_all(goldberg_dir.join("x86"))?;
    std::fs::create_dir_all(goldberg_dir.join("x64"))?;

    let tmpdir = ms_home.join("tmp").join("goldberg-download");
    let _ = std::fs::create_dir_all(&tmpdir);

    let gbe_fork_url = "https://api.github.com/repos/Detanup01/gbe_fork/releases/latest";
    let output =
        std::process::Command::new("curl").args(["-sL", gbe_fork_url]).stdout(std::process::Stdio::piped()).output()?;

    let json_str = String::from_utf8_lossy(&output.stdout);
    let mut download_url: Option<String> = None;

    if let Ok(parsed) = serde_json::from_str::<serde_json::Value>(&json_str) {
        if let Some(assets) = parsed.get("assets").and_then(|a| a.as_array()) {
            for asset in assets {
                if let Some(name) = asset.get("name").and_then(|n| n.as_str()) {
                    if name.contains("win-release") && name.ends_with(".7z") {
                        if let Some(url) = asset.get("browser_download_url").and_then(|u| u.as_str()) {
                            download_url = Some(url.to_string());
                            break;
                        }
                    }
                }
            }
        }
    }

    let url = match download_url {
        Some(u) => u,
        None => "https://gitlab.com/Mr_Goldberg/goldberg_emulator/-/jobs/artifacts/master/download?job=win_release"
            .to_string(),
    };

    let archive_path = tmpdir.join("goldberg.7z");
    let dl_status = std::process::Command::new("curl")
        .args(["-sL", "-o"])
        .arg(&archive_path)
        .arg(&url)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()?;

    if !dl_status.success() {
        return Err("failed to download Goldberg emulator".into());
    }

    let extract_dir = tmpdir.join("extracted");
    let _ = std::fs::create_dir_all(&extract_dir);

    let has_7z = std::process::Command::new("which").arg("7z").output().map(|o| o.status.success()).unwrap_or(false);
    let has_bsdtar =
        std::process::Command::new("which").arg("bsdtar").output().map(|o| o.status.success()).unwrap_or(false);

    if has_7z {
        let _ = std::process::Command::new("7z")
            .args(["x", "-y"])
            .arg(format!("-o{}", extract_dir.to_string_lossy()))
            .arg(&archive_path)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    } else if has_bsdtar {
        let _ = std::process::Command::new("bsdtar")
            .arg("-xf")
            .arg(&archive_path)
            .arg("-C")
            .arg(&extract_dir)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
    } else {
        return Err("need 7z or bsdtar to extract Goldberg archive".into());
    }

    let x86_dll = find_file_recursive(&extract_dir, "steam_api.dll");
    let x64_dll = find_file_recursive(&extract_dir, "steam_api64.dll");

    match (x86_dll, x64_dll) {
        (Some(x86), Some(x64)) => {
            std::fs::copy(&x86, &goldberg_x86)?;
            std::fs::copy(&x64, &goldberg_x64)?;
        },
        _ => return Err("Goldberg DLLs not found in downloaded archive".into()),
    }

    let _ = std::fs::remove_dir_all(&tmpdir);

    Ok(())
}

fn deploy_goldberg_to_dir(
    target_dir: &PathBuf,
    goldberg_dir: &PathBuf,
    appid: u32,
) -> Result<(), Box<dyn std::error::Error>> {
    let goldberg_x86 = goldberg_dir.join("x86").join("steam_api.dll");
    let goldberg_x64 = goldberg_dir.join("x64").join("steam_api64.dll");

    if !goldberg_x86.exists() || !goldberg_x64.exists() {
        return Err("Goldberg DLLs not found — download failed or was skipped".into());
    }

    std::fs::create_dir_all(target_dir)?;

    let x86_dst = target_dir.join("steam_api.dll");
    let (x64_dst, settings_dir) = {
        let win64 = target_dir.join("win64");
        if win64.exists() {
            (win64.join("steam_api64.dll"), target_dir.join("steam_settings"))
        } else {
            (target_dir.join("steam_api64.dll"), target_dir.join("steam_settings"))
        }
    };

    if x86_dst.exists() && !target_dir.join("steam_api.dll.orig").exists() {
        let _ = std::fs::rename(&x86_dst, target_dir.join("steam_api.dll.orig"));
    }
    if x64_dst.exists() && !x64_dst.with_extension("orig").exists() {
        let _ = std::fs::rename(&x64_dst, x64_dst.with_extension("orig"));
    }

    std::fs::copy(&goldberg_x86, &x86_dst)?;
    std::fs::copy(&goldberg_x64, &x64_dst)?;

    std::fs::create_dir_all(&settings_dir)?;
    std::fs::write(settings_dir.join("force_steam_appid.txt"), appid.to_string())?;

    Ok(())
}

fn find_file_recursive(dir: &PathBuf, name: &str) -> Option<PathBuf> {
    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                if let Some(found) = find_file_recursive(&path, name) {
                    return Some(found);
                }
            } else if path.file_name().map(|n| n == name).unwrap_or(false) {
                return Some(path);
            }
        }
    }
    None
}

pub fn deploy_goldberg_for_launch(
    home: &PathBuf,
    game_dir: &PathBuf,
    appid: u32,
) -> Result<(), Box<dyn std::error::Error>> {
    let goldberg_dir = crate::platform::metalsharp_home().join("runtime").join("goldberg");
    let goldberg_x86 = goldberg_dir.join("x86").join("steam_api.dll");
    let goldberg_x64 = goldberg_dir.join("x64").join("steam_api64.dll");

    if !goldberg_x86.exists() || !goldberg_x64.exists() {
        ensure_goldberg_downloaded(home)?;
    }

    let deploy_dir = match appid {
        620 => {
            let bin = game_dir.join("bin");
            if bin.exists() {
                bin
            } else {
                game_dir.clone()
            }
        },
        265930 => {
            let bin = game_dir.join("Binaries").join("Win32");
            if bin.exists() {
                bin
            } else {
                game_dir.clone()
            }
        },
        _ => game_dir.clone(),
    };

    let settings_dir = deploy_dir.join("steam_settings");
    let appid_ok = settings_dir.join("force_steam_appid.txt").exists();

    let x86_dst = deploy_dir.join("steam_api.dll");
    let x64_dst = if deploy_dir.join("win64").exists() {
        deploy_dir.join("win64").join("steam_api64.dll")
    } else {
        deploy_dir.join("steam_api64.dll")
    };

    let dlls_ok = x86_dst.exists() && x64_dst.exists();

    if !dlls_ok || !appid_ok {
        deploy_goldberg_to_dir(&deploy_dir, &goldberg_dir, appid)?;
    }

    Ok(())
}

fn find_steam_api(home: &PathBuf) -> Option<PathBuf> {
    let candidates = vec![
        home.join("Library/Application Support/Steam/steamapps/common/Terraria/Terraria.app/Contents/MacOS/osx/libsteam_api.dylib"),
        home.join("Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/Frameworks/Steam Helper.app/Contents/MacOS/libsteam_api.dylib"),
    ];
    candidates.into_iter().find(|p| p.exists())
}

pub fn detect_dotnet_game(game_dir: &PathBuf) -> bool {
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
                        let output = std::process::Command::new("file").arg(&path).output();
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

fn setup_fna_runtime(game_dir: &PathBuf, home: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
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

    let sdl3_candidates =
        [PathBuf::from("/opt/homebrew/lib/libSDL3.0.dylib"), PathBuf::from("/usr/local/lib/libSDL3.0.dylib")];
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
        .arg("-o")
        .arg(&output)
        .arg(src)
        .arg("-install_name")
        .arg(install_name)
        .output();

    if let Ok(o) = result {
        if o.status.success() {
            let _ = std::process::Command::new("codesign").args(["--force", "-s", "-"]).arg(&output).output();
        }
    }
}

fn check_command(cmd: &str) -> bool {
    std::process::Command::new("which").arg(cmd).output().map(|o| o.status.success()).unwrap_or(false)
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

fn check_rosetta() -> bool {
    PathBuf::from("/Library/Apple/System/Library/LaunchDaemons/com.apple.oahd.plist").exists()
        || std::process::Command::new("pgrep").arg("-q").arg("oahd").status().map(|s| s.success()).unwrap_or(false)
}
