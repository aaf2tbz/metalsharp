use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::Command;

fn cx_wine() -> PathBuf {
    PathBuf::from("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/wine")
}

fn cx_root() -> PathBuf {
    PathBuf::from("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver")
}

fn steam_prefix() -> PathBuf {
    dirs::home_dir().unwrap_or_default().join(".metalsharp").join("prefix-steam-cx")
}

fn steam_exe_path() -> PathBuf {
    steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam").join("Steam.exe")
}

pub fn status() -> Value {
    let home = dirs::home_dir().unwrap_or_default();

    let wine_steam_dir = steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam");
    let wine_steam_exe = wine_steam_dir.join("Steam.exe");
    let windows_installed = wine_steam_exe.exists();
    let windows_path = if windows_installed {
        Some(wine_steam_dir.to_string_lossy().to_string())
    } else {
        None
    };

    let login_state = detect_login_state();

    let mac_paths = vec![
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
        home.join("Library/Application Support/Steam/steamapps"),
    ];
    let mac_installed = mac_paths.iter().any(|p| p.exists());

    let steamcmd = which_steamcmd();
    let running = is_wine_steam_running();
    let cx_available = cx_wine().exists();

    json!({
        "installed": windows_installed,
        "path": windows_path,
        "login_state": login_state,
        "mac_installed": mac_installed,
        "steam_cmd_path": steamcmd,
        "running": running,
        "crossover_available": cx_available
    })
}

pub fn is_wine_steam_running() -> bool {
    let prefix = steam_prefix();
    Command::new("pgrep")
        .args(["-f", &prefix.to_string_lossy()])
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

pub fn launch_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let wine = cx_wine();
    if !wine.exists() {
        return Err("CrossOver Wine not found — install with: brew install --cask crossover".into());
    }

    let exe = steam_exe_path();
    if !exe.exists() {
        return Err("Windows Steam not installed — run install first".into());
    }

    if is_wine_steam_running() {
        return Ok(json!({"ok": true, "message": "Steam already running"}));
    }

    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let cx = cx_root();

    let child = Command::new(&wine)
        .current_dir(steam_prefix().join("drive_c").join("Program Files (x86)").join("Steam"))
        .env("WINEPREFIX", &prefix_str)
        .env("CX_ROOT", cx.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .arg(&exe)
        .args(["-no-cef-sandbox", "--disable-gpu", "-console"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": child.id()}))
}

pub fn stop_wine_steam() -> Result<Value, Box<dyn std::error::Error>> {
    let prefix = steam_prefix();
    let prefix_str = prefix.to_string_lossy().to_string();

    let _ = Command::new("pkill")
        .args(["-9", "-f", &prefix_str])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-f", "steamwebhelper.exe"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-f", "steamservice.exe"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-f", "Steam.exe"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-f", "winedevice.exe"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    std::thread::sleep(std::time::Duration::from_secs(2));

    let _ = Command::new("pkill")
        .args(["-9", "-f", "steamwebhelper.exe"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let _ = Command::new("pkill")
        .args(["-9", "-f", "winedevice.exe"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    Ok(json!({"ok": true}))
}

pub fn install_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    if !is_wine_steam_running() {
        launch_wine_steam()?;
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    Ok(json!({"ok": true, "appid": appid, "method": "steam_ui"}))
}

fn download_game_to_steam_prefix(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let steamcmd = which_steamcmd().ok_or("steamcmd not found — install with: scripts/install-steamcmd.sh")?;

    let wine_steamapps = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");
    std::fs::create_dir_all(&wine_steamapps)?;

    let game_name = resolve_game_name(appid).unwrap_or_else(|| format!("game_{}", appid));
    let install_dir = wine_steamapps.join("common").join(&game_name);
    std::fs::create_dir_all(&install_dir)?;

    let progress_file = dirs::home_dir()
        .map(|h| h.join(".metalsharp").join("download_progress.json"))
        .unwrap_or_else(|| PathBuf::from("/tmp/metalsharp_download.json"));

    let _ = std::fs::write(&progress_file, serde_json::json!({
        "appId": appid,
        "progress": 0.0,
        "status": "downloading",
    }).to_string());

    let cmd = steamcmd;
    let pf = progress_file.clone();
    let pw = get_steamcmd_password();
    let appid_str = appid.to_string();
    let manifest_dir = wine_steamapps.clone();
    let manifest_game_name = game_name.clone();

    std::thread::spawn(move || {
        let username = get_steamcmd_username()
            .or_else(|| get_wine_steam_username())
            .unwrap_or_else(|| "anonymous".into());

        let base_args: Vec<String> = vec![
            "+@sSteamCmdForcePlatformType".into(),
            "windows".into(),
            "+force_install_dir".into(),
            install_dir.to_str().unwrap_or("").into(),
        ];
        let update_args: Vec<String> = vec![
            "+app_update".into(),
            appid_str.clone(),
            "validate".into(),
            "+quit".into(),
        ];

        let login_args: Vec<String> = vec!["+login".into(), username.clone()];

        let mut child = match Command::new(&cmd)
            .args(&base_args)
            .args(&login_args)
            .args(&update_args)
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
        {
            Ok(c) => c,
            Err(_) => {
                let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                return;
            }
        };

        let mut needs_retry_with_password = false;

        if let Some(stdout) = child.stdout.take() {
            use std::io::BufRead;
            let reader = std::io::BufReader::new(stdout);
            for line in reader.lines().flatten() {
                let lower = line.to_lowercase();
                if lower.contains("invalid password") || lower.contains("invalid login") || lower.contains("rate limit") {
                    needs_retry_with_password = true;
                    break;
                }
                if let Some(pct) = parse_progress_line(&line) {
                    let _ = std::fs::write(&pf, json!({
                        "appId": appid,
                        "progress": pct,
                        "status": "downloading",
                    }).to_string());
                }
            }
        }

        let _ = child.wait();

        if needs_retry_with_password {
            if let Some(ref p) = pw {
                let retry_login: Vec<String> = vec!["+login".into(), username.clone(), p.clone(), "+remember_password".into()];
                let mut retry_child = match Command::new(&cmd)
                    .args(&base_args)
                    .args(&retry_login)
                    .args(&update_args)
                    .stdout(std::process::Stdio::piped())
                    .stderr(std::process::Stdio::piped())
                    .spawn()
                {
                    Ok(c) => c,
                    Err(_) => {
                        let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                        return;
                    }
                };

                if let Some(stdout) = retry_child.stdout.take() {
                    use std::io::BufRead;
                    let reader = std::io::BufReader::new(stdout);
                    for line in reader.lines().flatten() {
                        if let Some(pct) = parse_progress_line(&line) {
                            let _ = std::fs::write(&pf, json!({
                                "appId": appid,
                                "progress": pct,
                                "status": "downloading",
                            }).to_string());
                        }
                    }
                }

                let _ = retry_child.wait();
            } else {
                let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error", "message": "login required"}).to_string());
                return;
            }
        }

        let _ = std::fs::write(&pf, json!({
            "appId": appid,
            "progress": 100.0,
            "status": "setting_up",
        }).to_string());

        let has_exe = walkdir::WalkDir::new(&install_dir)
            .max_depth(3)
            .into_iter()
            .flatten()
            .any(|e| e.path().extension().map(|ext| ext == "exe").unwrap_or(false));

        if has_exe {
            write_appmanifest(&manifest_dir, appid, &manifest_game_name, &install_dir);
            let _ = crate::setup::prepare_game(appid);

            if is_wine_steam_running() {
                let wine = cx_wine();
                let prefix_str = steam_prefix().to_string_lossy().to_string();
                let cx = cx_root();
                let _ = Command::new(&wine)
                    .env("WINEPREFIX", &prefix_str)
                    .env("CX_ROOT", cx.to_string_lossy().to_string())
                    .env("WINEDEBUG", "-all")
                    .args(["start", &format!("steam://app/{}/", appid)])
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .spawn();
            }
        }

        let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 100.0, "status": "complete"}).to_string());
    });

    Ok(json!({"ok": true, "appId": appid, "status": "started", "method": "steamcmd"}))
}

fn write_appmanifest(steamapps_dir: &PathBuf, appid: u32, name: &str, install_dir: &PathBuf) {
    let manifest_path = steamapps_dir.join(format!("appmanifest_{}.acf", appid));
    if manifest_path.exists() {
        return;
    }

    let install_dir_str = install_dir.to_string_lossy();
    let game_dir_relative = format!("common/{}", name);
    let bytes_on_disk = walkdir::WalkDir::new(install_dir)
        .into_iter()
        .flatten()
        .filter_map(|e| e.metadata().ok().map(|m| m.len()))
        .sum::<u64>();

    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    let manifest = format!(
        "\"AppState\"\n{{\n\t\"appid\"\t\t\"{}\"\n\t\"Universe\"\t\t\"1\"\n\t\"name\"\t\t\"{}\"\n\t\"StateFlags\"\t\t\"4\"\n\t\"installdir\"\t\t\"{}\"\n\t\"StagingSize\"\t\t\"0\"\n\t\"buildid\"\t\t\"0\"\n\t\"UpdateResult\"\t\t\"0\"\n\t\"TargetBuildID\"\t\t\"0\"\n\t\"AutoUpdateBehavior\"\t\t\"0\"\n\t\"AllowOtherDownloadsWhileRunning\"\t\t\"0\"\n\t\"ScheduledAutoUpdate\"\t\t\"0\"\n\t\"BytesToDownload\"\t\t\"0\"\n\t\"BytesDownloaded\"\t\t\"0\"\n\t\"BytesToStage\"\t\t\"{}\"\n\t\"BytesStaged\"\t\t\"{}\"\n\t\"TargetBuildID\"\t\t\"0\"\n\t\"LastUpdated\"\t\t\"{}\"\n\t\"SizeOnDisk\"\t\t\"{}\"\n}}\n",
        appid, name, name, bytes_on_disk, bytes_on_disk, now, bytes_on_disk
    );

    let _ = std::fs::write(&manifest_path, manifest);
}

fn resolve_game_name(appid: u32) -> Option<String> {
    let names = [
        (945360, "Among Us"),
        (2050650, "RESIDENT EVIL 4  BIOHAZARD RE4"),
        (504230, "Celeste"),
        (105600, "Terraria"),
        (312520, "Rain World"),
        (535520, "Nidhogg 2"),
        (620, "Portal 2"),
        (1139900, "Ghostrunner"),
        (1245620, "ELDEN RING"),
        (1091500, "Cyberpunk 2077"),
        (814380, "Sekiro: Shadows Die Twice"),
        (1593500, "God of War"),
        (397540, "Borderlands 3"),
        (548430, "Deep Rock Galactic"),
        (990080, "Hogwarts Legacy"),
        (1172470, "Apex Legends"),
        (292030, "The Witcher 3: Wild Hunt"),
        (1172380, "STAR WARS Jedi: Fallen Order"),
        (1282100, "Remnant II"),
        (892970, "Valheim"),
        (367520, "Hollow Knight"),
        (413150, "Stardew Valley"),
        (1145360, "Hades"),
        (588650, "Dead Cells"),
        (1313140, "Cult of the Lamb"),
        (1637320, "Dome Keeper"),
        (1222680, "Need for Speed Heat"),
        (750920, "Shadow of the Tomb Raider"),
        (289070, "Sid Meier's Civilization VI"),
        (1092790, "Inscryption"),
        (1229490, "ULTRAKILL"),
        (1562430, "DREDGE"),
        (1868140, "DAVE THE DIVER"),
        (1583230, "High On Life"),
        (1888160, "ARMORED CORE VI"),
        (870780, "Control Ultimate Edition"),
        (1551360, "Forza Horizon 5"),
        (1623730, "Palworld"),
        (1716740, "Starfield"),
        (553850, "HELLDIVERS 2"),
        (1203620, "Enshrouded"),
        (252490, "Rust"),
        (230410, "Warframe"),
        (730, "Counter-Strike 2"),
        (264710, "Subnautica"),
        (848450, "Subnautica: Below Zero"),
        (1971650, "OCTOPATH TRAVELER II"),
        (1809540, "Nine Sols"),
        (1237320, "Sonic Frontiers"),
        (1326470, "Sons Of The Forest"),
        (275850, "No Man's Sky"),
        (1643320, "S.T.A.L.K.E.R. 2"),
        (379720, "DOOM"),
        (782330, "DOOM Eternal"),
        (976310, "Mortal Kombat 11"),
        (1276390, "Bloons TD Battles 2"),
        (1196590, "Resident Evil Village"),
        (1283700, "SUPERVIVE"),
        (2784470, "9 Kings"),
        (673130, "AMID EVIL"),
        (346110, "ARK: Survival Evolved"),
        (1669000, "Age of Wonders 4"),
        (1902490, "Aperture Desk Job"),
        (2062430, "BALL x PIT"),
        (924970, "Back 4 Blood"),
        (2379780, "Balatro"),
        (284160, "BeamNG.drive"),
        (774361, "Blasphemous"),
        (291550, "Brawlhalla"),
        (311210, "Call of Duty: Black Ops III"),
        (1938090, "Call of Duty"),
        (774801, "Crab Champions"),
        (1782210, "Crab Game"),
        (535690, "Crash Force"),
        (322330, "Don't Starve Together"),
        (506900, "Downward: Enhanced Edition"),
        (3175860, "Driving Is Hard"),
        (407730, "Dungeons Of Kragmor"),
        (400, "Portal"),
        (2943650, "FragPunk"),
        (4000, "Garry's Mod"),
        (322170, "Geometry Dash"),
        (265930, "Goat Simulator"),
        (410570, "Gunjack"),
        (271290, "HAWKEN"),
        (365450, "Hacknet"),
        (220, "Half-Life 2"),
        (320, "Half-Life 2: Deathmatch"),
        (360, "Half-Life Deathmatch: Source"),
        (400250, "Heaven Island - VR MMO"),
        (334070, "Hektor"),
        (1030300, "Hollow Knight: Silksong"),
        (43160, "Metro: Last Light Complete Edition"),
        (1389550, "Mind Scanners"),
        (519140, "Minds Eyes"),
        (1928870, "Minecraft Legends"),
        (17410, "Mirror's Edge"),
        (1818750, "MultiVersus"),
        (1203220, "NARAKA: BLADEPOINT"),
        (1169040, "Necesse"),
        (2139460, "Once Human"),
        (261570, "Ori and the Blind Forest"),
        (387290, "Ori and the Blind Forest: Definitive Edition"),
        (218620, "PAYDAY 2"),
        (1260320, "Party Animals"),
        (224220, "Pressure"),
        (211500, "RaceRoom Racing Experience"),
        (1236300, "Resident Evil Re:Verse"),
        (2290180, "Riders Republic"),
        (252950, "Rocket League"),
        (700580, "Rust - Staging Branch"),
        (2918300, "SPLITGATE: Arena Reloaded"),
        (387990, "Scrap Mechanic"),
        (1147560, "Skul: The Hero Slayer"),
        (50300, "Spec Ops: The Line"),
        (1677740, "Stumble Guys"),
        (843380, "Super Animal Royale"),
        (2073850, "THE FINALS"),
        (646910, "The Crew 2"),
        (2074920, "The First Descendant"),
        (450390, "The Lab"),
        (447850, "The Next Door"),
        (377310, "The Tower Of Elements"),
        (2239150, "Thronefall"),
        (460930, "Tom Clancy's Ghost Recon Wildlands"),
        (359550, "Tom Clancy's Rainbow Six Siege"),
        (623990, "Tom Clancy's Rainbow Six Siege - Test Server"),
        (508440, "Totally Accurate Battle Simulator"),
        (232910, "TrackMania Stadium"),
        (1930, "Two Worlds: Epic Edition"),
        (871720, "Ultimate Custom Night"),
        (391540, "Undertale"),
        (489220, "Unforgiving Trials: The Darkest Crusade"),
        (535760, "Unforgiving Trials: The Space Crusade"),
        (438100, "VRChat"),
        (552500, "Warhammer: Vermintide 2"),
        (674020, "World War 3"),
        (1281930, "tModLoader"),
        (2767030, "Marvel Rivals"),
        (1966720, "Lethal Company"),
        (233610, "Distance"),
        (356650, "Death's Gambit: Afterlife"),
        (503820, "A Detective's Novel"),
        (1097150, "Fall Guys"),
        (17410, "Mirror's Edge"),
        (19900, "Far Cry 2"),
        (298110, "Far Cry 4"),
        (552520, "Far Cry 5"),
        (371660, "Far Cry Primal"),
        (220240, "Far Cry 3"),
        (13520, "Far Cry"),
        (1408720, "Krunker"),
        (300340, "Lemma"),
        (436320, "Raw Data"),
        (1611740, "BattleBit Remastered Playtest"),
        (2281730, "Combat Master"),
        (222880, "Insurgency"),
        (552500, "Warhammer: Vermintide 2"),
    ];

    for &(id, name) in &names {
        if id == appid {
            return Some(name.to_string());
        }
    }

    let owned = fetch_owned_games(get_steam_id().as_deref()).unwrap_or_default();
    for (id, name) in owned {
        if id == appid {
            return Some(name);
        }
    }

    None
}

fn get_wine_steam_username() -> Option<String> {
    let vdf = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("config")
        .join("loginusers.vdf");
    let contents = std::fs::read_to_string(vdf).ok()?;
    for line in contents.lines() {
        let trimmed = line.trim();
        if let Some(name) = parse_vdf_value(trimmed, "AccountName") {
            return Some(name);
        }
    }
    None
}

pub fn launch_game_via_steam(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let wine = cx_wine();
    if !wine.exists() {
        return Err("CrossOver Wine not found".into());
    }
    if !is_wine_steam_running() {
        launch_wine_steam()?;
        for _ in 0..30 {
            std::thread::sleep(std::time::Duration::from_secs(2));
            if is_wine_steam_running() { break; }
        }
        std::thread::sleep(std::time::Duration::from_secs(5));
    }

    let prefix_str = steam_prefix().to_string_lossy().to_string();
    let cx = cx_root();
    let url = format!("steam://run/{}", appid);

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("CX_ROOT", cx.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .args(["start", &url])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(json!({"ok": true, "pid": child.id(), "appid": appid}))
}

pub fn get_wine_steam_installed_games() -> Vec<u32> {
    let steamapps = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

    let mut appids = Vec::new();
    if let Ok(entries) = std::fs::read_dir(&steamapps) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.starts_with("appmanifest_") && name.ends_with(".acf") {
                if let Some(id_str) = name.strip_prefix("appmanifest_").and_then(|s| s.strip_suffix(".acf")) {
                    if let Ok(id) = id_str.parse::<u32>() {
                        appids.push(id);
                    }
                }
            }
        }
    }
    appids
}

pub fn uninstall_game(appid: u32) -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let steamapps = steam_prefix()
        .join("drive_c")
        .join("Program Files (x86)")
        .join("Steam")
        .join("steamapps");

    let _ = crate::launch::kill_game(appid);

    let manifest_path = steamapps.join(format!("appmanifest_{}.acf", appid));
    if manifest_path.exists() {
        let contents = std::fs::read_to_string(&manifest_path).unwrap_or_default();
        let install_dir = contents.lines()
            .find(|l| l.contains("\"installdir\""))
            .and_then(|l| {
                let parts: Vec<&str> = l.splitn(2, |c: char| c == '\t' || c == ' ').collect();
                parts.last().map(|s| s.trim().trim_matches('"').to_string())
            });

        if let Some(dir_name) = install_dir {
            let game_dir = steamapps.join("common").join(&dir_name);
            if game_dir.exists() {
                let _ = std::fs::remove_dir_all(&game_dir);
            }
        }
        let _ = std::fs::remove_file(&manifest_path);
    }

    let local_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    if local_dir.exists() {
        let _ = std::fs::remove_dir_all(&local_dir);
    }

    Ok(json!({"ok": true, "appid": appid}))
}

pub fn steamcmd_status() -> Value {
    let steamcmd = which_steamcmd();
    let home = dirs::home_dir().unwrap_or_default();
    let config_path = home.join(".metalsharp/cache/steam_config.json");

    let logged_in = if config_path.exists() {
        std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str::<serde_json::Map<String, Value>>(&s).ok())
            .and_then(|m| m.get("steamcmd_logged_in").and_then(|v| v.as_bool()))
            .unwrap_or(false)
    } else {
        false
    };

    let username = if config_path.exists() {
        std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str::<serde_json::Map<String, Value>>(&s).ok())
            .and_then(|m| m.get("steam_username").and_then(|v| v.as_str()).map(String::from))
    } else {
        None
    };

    json!({
        "ok": true,
        "steamcmd_path": steamcmd,
        "logged_in": logged_in,
        "username": username,
    })
}

pub fn steamcmd_login(username: &str, password: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let steamcmd = which_steamcmd().ok_or("steamcmd not found")?;
    let home = dirs::home_dir().ok_or("no home dir")?;

    use std::io::BufRead;
    use std::process::Stdio;

    let mut child = Command::new(&steamcmd)
        .args(["+login", username, password, "+remember_password", "+quit"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;

    let stdout = child.stdout.take().ok_or("no stdout")?;
    let stderr = child.stderr.take().ok_or("no stderr")?;

    let stderr_handle = std::thread::spawn(move || {
        let reader = std::io::BufReader::new(stderr);
        let mut combined = String::new();
        for line in reader.lines().flatten() {
            combined.push_str(&line);
            combined.push('\n');
        }
        combined
    });

    let reader = std::io::BufReader::new(stdout);
    let mut combined_output = String::new();
    let mut logged_in = false;
    let mut login_failed = false;
    let mut failure_reason = String::new();

    for line in reader.lines().flatten() {
        combined_output.push_str(&line);
        combined_output.push('\n');

        let lower = line.to_lowercase();

        if lower.contains("logged in ok") || lower.contains("waiting for user info...ok") {
            logged_in = true;
            break;
        }

        if lower.contains("invalid password") {
            login_failed = true;
            failure_reason = "Invalid password".into();
            break;
        }
        if lower.contains("invalid login") {
            login_failed = true;
            failure_reason = "Invalid login credentials".into();
            break;
        }
    }

    drop(child.stdin.take());
    let _ = child.wait();

    let stderr_combined = stderr_handle.join().unwrap_or_default();
    let combined = format!("{}{}", combined_output, stderr_combined);

    let logged_in = logged_in
        || combined.contains("Logged in OK")
        || combined.contains("Logged in user")
        || combined.contains("Waiting for user info...OK");

    let login_failed = login_failed
        || combined.contains("Invalid Password")
        || combined.contains("Invalid Login");

    if logged_in && !login_failed {
        let config_dir = home.join(".metalsharp/cache");
        std::fs::create_dir_all(&config_dir)?;
        let config_path = config_dir.join("steam_config.json");

        let mut config: serde_json::Map<String, Value> = if config_path.exists() {
            std::fs::read_to_string(&config_path)
                .ok()
                .and_then(|s| serde_json::from_str(&s).ok())
                .unwrap_or_default()
        } else {
            serde_json::Map::new()
        };

        config.insert("steamcmd_logged_in".into(), json!(true));
        config.insert("steam_username".into(), json!(username));
        config.insert("steam_password".into(), json!(password));

        if let Some(steam_id) = get_steam_id() {
            config.insert("steam_id".into(), json!(steam_id));
        }

        std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;

        Ok(json!({"ok": true, "username": username}))
    } else {
        let reason = if login_failed && !failure_reason.is_empty() {
            failure_reason
        } else if combined.contains("Invalid Password") {
            "Invalid password".into()
        } else if combined.contains("Invalid Login") {
            "Invalid login credentials".into()
        } else if combined.contains("Steam Guard") || combined.contains("verification") {
            "Steam Guard verification failed — try again and approve the prompt quickly".into()
        } else {
            "Login failed — check your credentials".into()
        };
        Ok(json!({"ok": false, "error": reason}))
    }
}

pub fn steamcmd_logout() -> Result<Value, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let config_path = home.join(".metalsharp/cache/steam_config.json");

    if config_path.exists() {
        let mut config: serde_json::Map<String, Value> = std::fs::read_to_string(&config_path)
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or_default();
        config.insert("steamcmd_logged_in".into(), json!(false));
        config.remove("steam_username");
        std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;
    }

    Ok(json!({"ok": true}))
}

pub fn get_steamcmd_username() -> Option<String> {
    let home = dirs::home_dir()?;
    let config_path = home.join(".metalsharp/cache/steam_config.json");
    let config = std::fs::read_to_string(&config_path).ok()?;
    let map: serde_json::Map<String, Value> = serde_json::from_str(&config).ok()?;
    map.get("steam_username").and_then(|v| v.as_str()).map(String::from)
}

fn get_steamcmd_password() -> Option<String> {
    let home = dirs::home_dir()?;
    let config_path = home.join(".metalsharp/cache/steam_config.json");
    let config = std::fs::read_to_string(&config_path).ok()?;
    let map: serde_json::Map<String, Value> = serde_json::from_str(&config).ok()?;
    map.get("steam_password").and_then(|v| v.as_str()).map(String::from)
}

pub fn get_api_key() -> Value {
    let (key, _) = read_steam_config();
    json!({
        "ok": true,
        "key": key.unwrap_or_default(),
    })
}

pub fn save_api_key(key: &str) -> Result<(), Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let config_dir = home.join(".metalsharp/cache");
    std::fs::create_dir_all(&config_dir)?;
    let config_path = config_dir.join("steam_config.json");

    let steam_id = get_steam_id().unwrap_or_default();

    let config = json!({
        "steam_api_key": key,
        "steam_id": steam_id,
    });

    std::fs::write(&config_path, serde_json::to_string_pretty(&config)?)?;

    let cache_path = config_dir.join("owned_games.json");
    let _ = std::fs::remove_file(cache_path);

    Ok(())
}

pub fn get_steam_id() -> Option<String> {
    let home = dirs::home_dir()?;
    let mac_path = home.join("Library/Application Support/Steam/config/loginusers.vdf");
    let contents = std::fs::read_to_string(&mac_path).ok()?;
    for line in contents.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('"') && trimmed.chars().filter(|c| *c == '"').count() == 2 {
            let id = trimmed.trim_matches('"').trim();
            if id.starts_with("7656") {
                return Some(id.to_string());
            }
        }
    }
    None
}

pub fn library() -> Value {
    let installed_appids = get_installed_appids();
    let downloaded_appids = get_downloaded_appids();
    let wine_steam_appids = get_wine_steam_installed_games();

    let owned: Vec<(u32, String)> = match fetch_owned_games(get_steam_id().as_deref()) {
        Ok(games) => games,
        Err(_) => vec![],
    };

    let games: Vec<Value> = owned
        .iter()
        .map(|(appid, name)| {
            let is_installed = installed_appids.contains(appid)
                || downloaded_appids.contains(appid)
                || wine_steam_appids.contains(appid);
            json!({
                "appid": appid,
                "name": name,
                "installed": is_installed,
                "state": if is_installed { "installed" } else { "not_installed" },
                "cover_url": format!("https://steamcdn-a.akamaihd.net/steam/apps/{}/library_600x900.jpg", appid),
                "header_url": format!("https://steamcdn-a.akamaihd.net/steam/apps/{}/header.jpg", appid),
            })
        })
        .collect();

    json!({
        "ok": true,
        "total": games.len(),
        "installed_count": games.iter().filter(|g| g["installed"].as_bool().unwrap_or(false)).count(),
        "games": games,
    })
}

fn get_downloaded_appids() -> Vec<u32> {
    let home = dirs::home_dir().unwrap_or_default();
    let games_dir = home.join(".metalsharp").join("games");
    let mut appids = Vec::new();

    if let Ok(entries) = std::fs::read_dir(&games_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if !path.is_dir() { continue; }
            if let Some(name) = path.file_name() {
                if let Ok(id) = name.to_string_lossy().parse::<u32>() {
                    let has_exe = walkdir::WalkDir::new(&path)
                        .max_depth(3)
                        .into_iter()
                        .flatten()
                        .any(|e| e.path().extension().map(|ext| ext == "exe").unwrap_or(false));
                    if has_exe {
                        appids.push(id);
                    }
                }
            }
        }
    }

    appids
}

fn read_steam_config() -> (Option<String>, Option<String>) {
    let home = dirs::home_dir().unwrap_or_default();
    let config_path = home.join(".metalsharp/cache/steam_config.json");
    if let Ok(contents) = std::fs::read_to_string(&config_path) {
        if let Ok(cfg) = serde_json::from_str::<serde_json::Map<String, Value>>(&contents) {
            let key = cfg.get("steam_api_key").and_then(|v| v.as_str()).map(String::from);
            let sid = cfg.get("steam_id").and_then(|v| v.as_str()).map(String::from);
            return (key, sid);
        }
    }
    (None, get_steam_id())
}

fn fetch_owned_games(_steam_id: Option<&str>) -> Result<Vec<(u32, String)>, Box<dyn std::error::Error>> {
    let (api_key, steam_id) = read_steam_config();
    let key = api_key.as_deref().unwrap_or("");
    let sid = steam_id.as_deref().or(_steam_id).unwrap_or("");

    if key.is_empty() || sid.is_empty() {
        return Ok(vec![]);
    }

    let cache_path = dirs::home_dir()
        .map(|h| h.join(".metalsharp/cache/owned_games.json"))
        .unwrap_or_default();

    if cache_path.exists() {
        if let Ok(contents) = std::fs::read_to_string(&cache_path) {
            if let Ok(cached) = serde_json::from_str::<serde_json::Map<String, Value>>(&contents) {
                if let Some(ts) = cached.get("timestamp").and_then(|t| t.as_u64()) {
                    let age = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_secs();
                    if age - ts < 3600 {
                        if let Some(arr) = cached.get("games").and_then(|g| g.as_array()) {
                            return Ok(arr.iter().filter_map(|g| {
                                let id = g.get("appid")?.as_u64()? as u32;
                                let name = g.get("name")?.as_str()?.to_string();
                                Some((id, name))
                            }).collect());
                        }
                    }
                }
            }
        }
    }

    let url = format!(
        "https://api.steampowered.com/IPlayerService/GetOwnedGames/v0001/?key={}&steamid={}&include_appinfo=1&include_played_free_games=1&format=json",
        key, sid
    );

    let output = Command::new("curl")
        .args(["-sL", "-m", "15", &url])
        .output()?;

    if !output.status.success() {
        return Err("curl failed".into());
    }

    let body: Value = serde_json::from_slice(&output.stdout)?;

    let games_arr = body
        .get("response")
        .and_then(|r| r.get("games"))
        .and_then(|g| g.as_array());

    let result: Vec<(u32, String)> = match games_arr {
        Some(arr) => arr.iter().filter_map(|g| {
            let id = g.get("appid")?.as_u64()? as u32;
            let name = g.get("name")?.as_str()?.to_string();
            Some((id, name))
        }).collect(),
        None => vec![],
    };

    let _ = save_cache(&cache_path, &result);

    Ok(result)
}

fn save_cache(path: &PathBuf, games: &[(u32, String)]) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let arr: Vec<Value> = games
        .iter()
        .map(|(id, name)| json!({"appid": id, "name": name}))
        .collect();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    std::fs::write(path, serde_json::to_string_pretty(&json!({"timestamp": now, "games": arr}))?)?;
    Ok(())
}

fn get_installed_appids() -> Vec<u32> {
    let home = dirs::home_dir().unwrap_or_default();
    let mut appids = Vec::new();

    let steamapps_dirs = vec![
        home.join("Library/Application Support/Steam/steamapps"),
        home.join(".steam/steam/steamapps"),
        home.join(".local/share/Steam/steamapps"),
    ];

    for dir in steamapps_dirs {
        if let Ok(entries) = std::fs::read_dir(&dir) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_string();
                if name.starts_with("appmanifest_") && name.ends_with(".acf") {
                    if let Some(id_str) = name
                        .strip_prefix("appmanifest_")
                        .and_then(|s| s.strip_suffix(".acf"))
                    {
                        if let Ok(id) = id_str.parse::<u32>() {
                            appids.push(id);
                        }
                    }
                }
            }
        }
    }

    appids
}

fn detect_login_state() -> Value {
    let home = dirs::home_dir().unwrap_or_default();
    let mac_path = home.join("Library/Application Support/Steam/config/loginusers.vdf");
    let wine_path = home
        .join(".metalsharp/prefix/drive_c/Program Files (x86)/Steam/config/loginusers.vdf");

    let contents = std::fs::read_to_string(&mac_path)
        .or_else(|_| std::fs::read_to_string(&wine_path))
        .unwrap_or_default();

    if contents.is_empty() {
        return json!({"state": "unknown", "account": null});
    }

    let mut accounts: Vec<Value> = Vec::new();

    for line in contents.lines() {
        let trimmed = line.trim();
        if let Some(name) = parse_vdf_value(trimmed, "PersonaName") {
            let remembered = contents
                .lines()
                .any(|l| l.contains("RememberPassword") && l.contains("1"));
            accounts.push(json!({
                "name": name,
                "remembered": remembered,
            }));
        }
    }

    if accounts.is_empty() {
        json!({"state": "logged_out", "account": null})
    } else {
        json!({"state": "logged_in", "account": accounts})
    }
}

fn parse_vdf_value(line: &str, key: &str) -> Option<String> {
    let prefix = format!("\"{}\"", key);
    if !line.starts_with(&prefix) {
        return None;
    }
    let rest = line.trim_start_matches(&prefix).trim();
    let rest = rest.trim_start_matches('\t').trim_start_matches(' ');
    Some(rest.trim_matches('"').to_string())
}

fn which_steamcmd() -> Option<String> {
    let home = dirs::home_dir()?;

    let candidates = vec![
        home.join(".steam/steamcmd/steamcmd.sh"),
        home.join("steamcmd/steamcmd.sh"),
        PathBuf::from("/usr/local/bin/steamcmd"),
        PathBuf::from("/opt/homebrew/bin/steamcmd"),
    ];

    for c in candidates {
        if c.exists() {
            return Some(c.to_string_lossy().to_string());
        }
    }

    Command::new("which")
        .arg("steamcmd")
        .output()
        .ok()
        .and_then(|o| {
            if o.status.success() {
                Some(String::from_utf8_lossy(&o.stdout).trim().to_string())
            } else {
                None
            }
        })
}

pub fn install_steam() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;
    let metalsharp_dir = home.join(".metalsharp");
    std::fs::create_dir_all(&metalsharp_dir)?;

    let installer = metalsharp_dir.join("SteamSetup.exe");

    if !installer.exists() {
        let url = "https://steamcdn-a.akamaihd.net/client/installer/SteamSetup.exe";
        let output = Command::new("curl")
            .args(["-sL", "-o", &installer.to_string_lossy(), url])
            .status()?;
        if !output.success() {
            return Err("Failed to download Steam installer".into());
        }
    }

    let wine = cx_wine();
    if !wine.exists() {
        return Err("CrossOver Wine not found — install with: brew install --cask crossover".into());
    }

    let prefix = steam_prefix();
    std::fs::create_dir_all(&prefix)?;

    let prefix_str = prefix.to_string_lossy().to_string();
    let cx = cx_root();

    let _ = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("CX_ROOT", cx.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .arg("wineboot")
        .arg("--init")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();

    let child = Command::new(&wine)
        .env("WINEPREFIX", &prefix_str)
        .env("CX_ROOT", cx.to_string_lossy().to_string())
        .env("WINEDEBUG", "-all")
        .arg(&installer)
        .args(["/S"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()?;

    Ok(format!("Launched Steam installer via CrossOver Wine (pid {})", child.id()))
}

fn reqwest_https_get(url: &str) -> Result<Box<dyn std::io::Read>, Box<dyn std::error::Error>> {
    let output = Command::new("curl")
        .args(["-sL", "-o", "-", url])
        .stdout(std::process::Stdio::piped())
        .spawn()?;

    match output.stdout {
        Some(out) => Ok(Box::new(out)),
        None => Err("curl failed to start".into()),
    }
}

fn find_metalsharp_launcher() -> Result<String, Box<dyn std::error::Error>> {
    let home = dirs::home_dir().ok_or("no home dir")?;

    let candidates = vec![
        home.join("metalsharp/build/metalsharp_launcher"),
        home.join("metalsharp/build/tools/launcher/metalsharp_launcher"),
        PathBuf::from("/usr/local/bin/metalsharp_launcher"),
        PathBuf::from("/opt/homebrew/bin/metalsharp_launcher"),
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

    Err("metalsharp_launcher not found".into())
}

pub fn download_game(appid: u32, password: Option<&str>) -> Result<Value, Box<dyn std::error::Error>> {
    let steamcmd = which_steamcmd().ok_or("steamcmd not found — install with: scripts/install-steamcmd.sh")?;

    let home = dirs::home_dir().ok_or("no home dir")?;
    let install_dir = home.join(".metalsharp").join("games").join(appid.to_string());
    std::fs::create_dir_all(&install_dir)?;

    let progress_file = home.join(".metalsharp").join("download_progress.json");

    let _ = std::fs::write(&progress_file, serde_json::json!({
        "appId": appid,
        "progress": 0.0,
        "status": "downloading",
    }).to_string());

    let cmd = steamcmd;
    let dir = install_dir.clone();
    let pf = progress_file.clone();
    let pw = password.map(String::from).or_else(|| get_steamcmd_password());
    let appid_str = appid.to_string();

    std::thread::spawn(move || {
        let username = get_steamcmd_username()
            .or_else(|| get_wine_steam_username())
            .unwrap_or_else(|| "anonymous".into());

        let base_args: Vec<String> = vec![
            "+@sSteamCmdForcePlatformType".into(),
            "windows".into(),
            "+force_install_dir".into(),
            dir.to_str().unwrap_or("").into(),
        ];
        let update_args: Vec<String> = vec![
            "+app_update".into(),
            appid_str.clone(),
            "validate".into(),
            "+quit".into(),
        ];

        let login_args: Vec<String> = vec!["+login".into(), username.clone()];

        let mut child = match Command::new(&cmd)
            .args(&base_args)
            .args(&login_args)
            .args(&update_args)
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
        {
            Ok(c) => c,
            Err(_) => {
                let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                return;
            }
        };

        let mut needs_retry_with_password = false;
        let mut needs_depot_fallback = false;

        if let Some(stdout) = child.stdout.take() {
            use std::io::BufRead;
            let reader = std::io::BufReader::new(stdout);
            for line in reader.lines().flatten() {
                let lower = line.to_lowercase();
                if lower.contains("invalid password") || lower.contains("invalid login") || lower.contains("rate limit") {
                    needs_retry_with_password = true;
                    break;
                }
                if lower.contains("state is 0x202") || lower.contains("no subscription") {
                    needs_depot_fallback = true;
                    break;
                }
                if let Some(pct) = parse_progress_line(&line) {
                    let _ = std::fs::write(&pf, json!({
                        "appId": appid,
                        "progress": pct,
                        "status": "downloading",
                    }).to_string());
                }
            }
        }

        let _ = child.wait();

        if needs_retry_with_password {
            if let Some(ref p) = pw {
                let retry_login: Vec<String> = vec!["+login".into(), username.clone(), p.clone(), "+remember_password".into()];
                let mut retry_child = match Command::new(&cmd)
                    .args(&base_args)
                    .args(&retry_login)
                    .args(&update_args)
                    .stdout(std::process::Stdio::piped())
                    .stderr(std::process::Stdio::piped())
                    .spawn()
                {
                    Ok(c) => c,
                    Err(_) => {
                        let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                        return;
                    }
                };

                if let Some(stdout) = retry_child.stdout.take() {
                    use std::io::BufRead;
                    let reader = std::io::BufReader::new(stdout);
                    for line in reader.lines().flatten() {
                        let lower = line.to_lowercase();
                        if lower.contains("state is 0x202") || lower.contains("no subscription") {
                            needs_depot_fallback = true;
                            break;
                        }
                        if let Some(pct) = parse_progress_line(&line) {
                            let _ = std::fs::write(&pf, json!({
                                "appId": appid,
                                "progress": pct,
                                "status": "downloading",
                            }).to_string());
                        }
                    }
                }

                let _ = retry_child.wait();
            } else {
                let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                return;
            }
        }

        if needs_depot_fallback {
            let _ = std::fs::write(&pf, json!({
                "appId": appid,
                "progress": 0.0,
                "status": "downloading depots",
            }).to_string());

            let depot_list = fetch_depots(&cmd, appid);
            if depot_list.is_empty() {
                let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                return;
            }

            let total_download: u64 = depot_list.iter().map(|d| d.download_size).max().unwrap_or(1);
            let num_depots = depot_list.len();
            let steamcmd_content = dirs::home_dir()
                .map(|h| h.join("steamcmd/steamapps/content"))
                .unwrap_or_else(|| PathBuf::from("/tmp/steamcmd_content"));

            for (i, depot) in depot_list.iter().enumerate() {
                let _ = std::fs::write(&pf, json!({
                    "appId": appid,
                    "progress": (i as f64 / num_depots as f64) * 100.0,
                    "status": format!("downloading depot {}/{}", i + 1, num_depots),
                }).to_string());

                let depot_args: Vec<String> = vec![
                    "+login".into(), username.clone(),
                    "+download_depot".into(),
                    appid.to_string(),
                    depot.depot_id.to_string(),
                    depot.manifest_gid.clone(),
                    dir.to_str().unwrap_or("").into(),
                    "+quit".into(),
                ];

                let mut depot_child = match Command::new(&cmd)
                    .args(&depot_args)
                    .stdout(std::process::Stdio::piped())
                    .stderr(std::process::Stdio::piped())
                    .spawn()
                {
                    Ok(c) => c,
                    Err(_) => {
                        let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
                        return;
                    }
                };

                if let Some(stdout) = depot_child.stdout.take() {
                    use std::io::BufRead;
                    let reader = std::io::BufReader::new(stdout);
                    for line in reader.lines().flatten() {
                        if let Some(pct) = parse_progress_line(&line) {
                            let depot_pct = ((i as f64 + pct / 100.0) / num_depots as f64) * 100.0;
                            let _ = std::fs::write(&pf, json!({
                                "appId": appid,
                                "progress": depot_pct,
                                "status": format!("downloading depot {}/{}", i + 1, num_depots),
                            }).to_string());
                        }
                    }
                }

                let _ = depot_child.wait();

                let depot_content = steamcmd_content.join(format!("depot_{}", depot.depot_id));
                if depot_content.exists() {
                    if let Ok(entries) = std::fs::read_dir(&depot_content) {
                        for entry in entries.flatten() {
                            if let Ok(file_type) = entry.file_type() {
                                let src = entry.path();
                                let dst = dir.join(entry.file_name());
                                if file_type.is_dir() {
                                    let _ = copy_dir_recursive(&src, &dst);
                                } else {
                                    let _ = std::fs::copy(&src, &dst);
                                }
                            }
                        }
                    }
                    let _ = std::fs::remove_dir_all(&depot_content);
                }
            }
        }

        let _ = std::fs::write(&pf, json!({
            "appId": appid,
            "progress": 100.0,
            "status": "setting_up",
        }).to_string());

        let has_exe = walkdir::WalkDir::new(&dir)
            .max_depth(3)
            .into_iter()
            .flatten()
            .any(|e| e.path().extension().map(|ext| ext == "exe").unwrap_or(false));

        if has_exe {
            let _ = crate::setup::prepare_game(appid);
            let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 100.0, "status": "complete"}).to_string());
        } else {
            let _ = std::fs::write(&pf, json!({"appId": appid, "progress": 0.0, "status": "error"}).to_string());
        }
    });

    Ok(json!({"ok": true, "appId": appid, "status": "started"}))
}

fn parse_progress_line(line: &str) -> Option<f64> {
    let lower = line.to_lowercase();
    if lower.contains("fully installed") || lower.contains("success") {
        return Some(100.0);
    }
    if !lower.contains("progress") {
        return None;
    }
    let idx = lower.find("progress")?;
    let rest = &lower[idx + 8..].trim_start_matches(|c: char| c == ':' || c == ' ' || c == '=');
    let end = rest.find(|c: char| !c.is_ascii_digit() && c != '.').unwrap_or(rest.len());
    if end == 0 {
        return None;
    }
    rest[..end].parse().ok()
}

struct DepotInfo {
    depot_id: u32,
    manifest_gid: String,
    size: u64,
    download_size: u64,
}

fn fetch_depots(cmd: &str, appid: u32) -> Vec<DepotInfo> {
    let username = get_steamcmd_username().unwrap_or_else(|| "anonymous".into());
    let appid_str = appid.to_string();
    let output = match Command::new(cmd)
        .args(["+login", &username, "+app_info_print", &appid_str, "+quit"])
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .output()
    {
        Ok(o) => o,
        Err(_) => return Vec::new(),
    };
    let stdout = String::from_utf8_lossy(&output.stdout);
    parse_depots_from_app_info(&stdout)
}

fn parse_depots_from_app_info(output: &str) -> Vec<DepotInfo> {
    let mut depots = Vec::new();
    let mut in_depots = false;
    let mut current_depot_id: Option<u32> = None;
    let mut in_manifests = false;
    let mut in_public = false;
    let mut current_gid: Option<String> = None;
    let mut current_size: u64 = 0;
    let mut current_download: u64 = 0;
    let mut is_windows = false;
    let mut brace_depth = 0;
    let mut depot_brace_depth = 0;

    for line in output.lines() {
        let trimmed = line.trim();
        let opens = trimmed.matches('{').count();
        let closes = trimmed.matches('}').count();
        let old_depth = brace_depth;
        brace_depth += opens as i32 - closes as i32;

        if trimmed.starts_with("\"depots\"") && trimmed.contains('{') {
            in_depots = true;
            depot_brace_depth = old_depth;
            continue;
        }

        if !in_depots {
            continue;
        }

        if brace_depth <= depot_brace_depth {
            in_depots = false;
            if let Some(did) = current_depot_id.take() {
                if is_windows {
                    if let Some(gid) = current_gid.take() {
                        depots.push(DepotInfo {
                            depot_id: did,
                            manifest_gid: gid,
                            size: current_size,
                            download_size: current_download,
                        });
                    }
                }
            }
            continue;
        }

        if opens > 0 && closes == 0 && current_depot_id.is_none() {
            if let Some(did) = parse_quoted_u32(trimmed) {
                current_depot_id = Some(did);
                in_manifests = false;
                in_public = false;
                current_gid = None;
                current_size = 0;
                current_download = 0;
                is_windows = false;
            }
        }

        if current_depot_id.is_some() {
            if trimmed.starts_with("\"oslist\"") {
                is_windows = trimmed.contains("windows");
            }
            if trimmed.starts_with("\"manifests\"") {
                in_manifests = true;
            }
            if in_manifests && trimmed.starts_with("\"public\"") {
                in_public = true;
            }
            if in_public {
                if trimmed.starts_with("\"gid\"") {
                    current_gid = parse_quoted_string(trimmed, "\"gid\"");
                }
                if trimmed.starts_with("\"size\"") {
                    current_size = parse_quoted_u64(trimmed).unwrap_or(0);
                }
                if trimmed.starts_with("\"download\"") {
                    current_download = parse_quoted_u64(trimmed).unwrap_or(0);
                }
            }
            if trimmed == "}" && in_public {
                in_public = false;
            }
            if trimmed == "}" && in_manifests {
                in_manifests = false;
            }
            if trimmed == "}" && current_depot_id.is_some() && !in_manifests && !in_public {
                if is_windows {
                    if let Some(gid) = current_gid.take() {
                        depots.push(DepotInfo {
                            depot_id: current_depot_id.unwrap(),
                            manifest_gid: gid,
                            size: current_size,
                            download_size: current_download,
                        });
                    }
                }
                current_depot_id = None;
            }
        }
    }

    depots
}

fn parse_quoted_u32(s: &str) -> Option<u32> {
    let s = s.trim();
    if !s.starts_with('"') { return None; }
    let end = s[1..].find('"').map(|i| i + 1)?;
    s[1..end].parse().ok()
}

fn parse_quoted_string(s: &str, key: &str) -> Option<String> {
    let after_key = s.find(key)?;
    let rest = &s[after_key + key.len()..];
    let rest = rest.trim_start_matches(|c: char| c == '\t' || c == ' ');
    if !rest.starts_with('"') { return None; }
    let end = rest[1..].find('"').map(|i| i + 1)?;
    Some(rest[1..end].to_string())
}

fn parse_quoted_u64(s: &str) -> Option<u64> {
    let s = s.trim();
    let parts: Vec<&str> = s.splitn(2, |c: char| c == '\t' || c == ' ').collect();
    if parts.len() < 2 { return None; }
    let val = parts[1].trim().trim_matches('"');
    val.parse().ok()
}

fn copy_dir_recursive(src: &PathBuf, dst: &PathBuf) -> std::io::Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            if dst_path.exists() {
                let src_len = std::fs::metadata(&src_path).map(|m| m.len()).unwrap_or(0);
                let dst_len = std::fs::metadata(&dst_path).map(|m| m.len()).unwrap_or(1);
                if src_len == dst_len { continue; }
            }
            std::fs::copy(&src_path, &dst_path)?;
        }
    }
    Ok(())
}

fn scan_downloaded_dir(dir: &PathBuf, _appid: u32) -> Vec<serde_json::Value> {
    let mut results = Vec::new();
    let mut exe_name = String::new();

    for entry in walkdir::WalkDir::new(dir).max_depth(4).into_iter().flatten() {
        if let Some(ext) = entry.path().extension() {
            if ext == "exe" {
                let name = entry.file_name().to_string_lossy().to_string();
                results.push(json!({
                    "name": name,
                    "exePath": entry.path().to_string_lossy().to_string()
                }));
                if exe_name.is_empty() {
                    exe_name = name;
                }
            }
        }
    }

    results
}
