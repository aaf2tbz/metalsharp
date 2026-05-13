#![allow(
    clippy::ptr_arg,
    clippy::unnecessary_unwrap,
    clippy::useless_vec,
    clippy::manual_div_ceil,
    clippy::redundant_closure,
    clippy::bool_assert_comparison,
    clippy::needless_bool,
    clippy::manual_strip,
    clippy::let_unit_value,
    clippy::char_lit_as_u8,
    clippy::type_complexity,
    clippy::single_match,
    clippy::match_single_binding,
    clippy::redundant_pattern_matching,
    dead_code,
    unused_variables
)]

mod installer;
mod launch;
mod scan;
mod setup;
mod steam;
mod updater;

use serde_json::json;
use std::sync::Arc;
use tiny_http::{Header, Method, Response, Server};

fn main() {
    let port = std::env::var("METALSHARP_PORT").unwrap_or_else(|_| "9274".into());
    let addr = format!("127.0.0.1:{}", port);
    let server = Arc::new(Server::http(&addr).unwrap_or_else(|e| {
        eprintln!("failed to bind {}: {}", addr, e);
        std::process::exit(1);
    }));

    eprintln!("metalsharp-backend listening on {}", addr);
    app_log(&format!("MetalSharp v{} backend started on {}", env!("CARGO_PKG_VERSION"), addr));

    let cors_header = Header::from_bytes(&b"Access-Control-Allow-Origin"[..], &b"*"[..]).unwrap();
    let json_header = Header::from_bytes(&b"Content-Type"[..], &b"application/json"[..]).unwrap();

    loop {
        let mut request = match server.recv() {
            Ok(r) => r,
            Err(_) => break,
        };

        let (code, body) = route(&mut request);
        let resp = Response::from_data(body)
            .with_header(cors_header.clone())
            .with_header(json_header.clone())
            .with_status_code(code);
        let _ = request.respond(resp);
    }
}

fn route(req: &mut tiny_http::Request) -> (u16, Vec<u8>) {
    let method = req.method().clone();
    let url = req.url().to_string();

    match (method, url.as_str()) {
        (Method::Get, "/status") => {
            app_log("Backend status checked");
            resp(
                200,
                json!({
                    "ok": true,
                    "version": env!("CARGO_PKG_VERSION")
                }),
            )
        },
        (Method::Get, "/update/check") => resp(200, updater::check_for_update()),
        (Method::Post, "/update/start") => match updater::start_update() {
            Ok(v) => resp(200, v),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Get, "/update/progress") => resp(200, updater::read_update_progress()),
        (Method::Get, "/setup/state") => resp(200, setup::state()),
        (Method::Post, "/setup/save") => {
            let body = read_body(req);
            match setup::save_step(&body) {
                Ok(v) => resp(200, v),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Get, "/setup/device-name") => resp(
            200,
            json!({
                "ok": true,
                "name": setup::generate_device_name(),
            }),
        ),
        (Method::Get, "/setup/dependencies") => resp(200, setup::dependencies()),
        (Method::Post, "/setup/install-deps") => {
            let body = read_body(req);
            match setup::install_dependencies(&body) {
                Ok(v) => resp(200, v),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Post, "/setup/install-all") => match installer::start_install_all() {
            Ok(v) => resp(200, v),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Get, "/setup/install-progress") => resp(200, installer::read_progress()),
        (Method::Get, "/setup/installing") => resp(200, json!({"installing": installer::is_installing()})),
        (Method::Post, "/game/prepare") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    app_log(&format!("Preparing game runtime: appid {}", id));
                    match setup::prepare_game(id as u32) {
                        Ok(v) => resp(200, v),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Get, "/scan") => {
            app_log("Scanning for installed games...");
            match scan::scan_all() {
                Ok(result) => resp(200, result),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Get, "/steam/status") => resp(200, steam::status()),
        (Method::Get, "/steam/library") => {
            app_log("Loading Steam library...");
            let result = steam::library();
            app_log(&format!("Loaded {} games", result.get("total").and_then(|t| t.as_u64()).unwrap_or(0)));
            resp(200, result)
        },
        (Method::Get, "/steam/api-key") => resp(200, steam::get_api_key()),
        (Method::Post, "/steam/save-api-key") => {
            let body = read_body(req);
            let key = body.get("key").and_then(|v| v.as_str()).unwrap_or("");
            app_log("Steam API key saved");
            match steam::save_api_key(key) {
                Ok(_) => resp(200, json!({"ok": true})),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Post, "/steam/install") => match steam::install_steam() {
            Ok(p) => resp(200, json!({"ok": true, "path": p})),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Post, "/steam/launch") => {
            app_log("Launching Wine Steam...");
            match steam::launch_wine_steam() {
                Ok(v) => resp(200, v),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Post, "/steam/stop") => {
            app_log("Stopping Wine Steam...");
            match steam::stop_wine_steam() {
                Ok(v) => resp(200, v),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Get, "/steam/is-running") => resp(200, json!({"ok": true, "running": steam::is_wine_steam_running()})),
        (Method::Get, "/steam/watch-steamapps") => match steam::watch_steamapps() {
            Some(new_ids) => resp(200, json!({"ok": true, "new_appids": new_ids})),
            None => resp(200, json!({"ok": true, "new_appids": []})),
        },
        (Method::Post, "/steam/install-game") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    app_log(&format!("Installing game via Wine Steam: appid {}", id));
                    match steam::install_game_via_steam(id as u32) {
                        Ok(v) => resp(200, v),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/steam/launch-game") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    app_log(&format!("Launching game via Wine Steam: appid {}", id));
                    match steam::launch_game_via_steam(id as u32) {
                        Ok(v) => resp(200, v),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/steam/view-game") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    app_log(&format!("Opening game in Steam library: appid {}", id));
                    match steam::view_game_in_steam(id as u32) {
                        Ok(v) => resp(200, v),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Get, "/logs") => {
            let home = dirs::home_dir().unwrap_or_default();
            let log_path = home.join(".metalsharp").join("logs");
            let mut entries = Vec::new();
            if let Ok(rd) = std::fs::read_dir(&log_path) {
                let mut files: Vec<std::path::PathBuf> = rd.flatten().map(|e| e.path()).collect();
                files.sort_by(|a, b| b.cmp(a));
                for p in files.iter().take(3) {
                    if p.extension().map(|e| e == "log").unwrap_or(false) {
                        if let Ok(content) = std::fs::read_to_string(p) {
                            let lines: Vec<&str> = content.lines().rev().take(500).collect();
                            entries.push(json!({
                                "name": p.file_name().unwrap_or_default().to_string_lossy(),
                                "lines": lines.into_iter().rev().collect::<Vec<&str>>(),
                            }));
                        }
                    }
                }
            }
            if entries.is_empty() {
                entries.push(json!({
                    "name": "app.log",
                    "lines": ["No logs yet. Logs will appear here as you use MetalSharp."],
                }));
            }
            resp(200, json!({"ok": true, "logs": entries}))
        },
        (Method::Get, "/config") => resp(200, launch::get_config()),
        (Method::Post, "/config") => {
            let body = read_body(req);
            let mode = body.get("launchMode").and_then(|v| v.as_str()).unwrap_or("native");
            match launch::set_config(mode) {
                Ok(cfg) => resp(200, cfg),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Post, "/launch") => {
            let body = read_body(req);
            let exe = body.get("exePath").and_then(|v| v.as_str()).unwrap_or("");
            let steam_app_id = body.get("steamAppId").and_then(|v| v.as_u64());
            let resolved = if steam_app_id.is_some() && !exe.contains(".exe") {
                resolve_game_exe(steam_app_id.unwrap() as u32)
            } else {
                exe.to_string()
            };
            app_log(&format!("Launching: {}", resolved));

            let mut game_type = "native";
            if steam_app_id.is_some() {
                let home = dirs::home_dir().unwrap_or_default();
                let marker = home
                    .join(".metalsharp")
                    .join("games")
                    .join(steam_app_id.unwrap().to_string())
                    .join(".metalsharp_prepared");
                if let Ok(content) = std::fs::read_to_string(&marker) {
                    if content.contains("is_dotnet=true") {
                        app_log("Detected XNA/FNA game — using mono runtime");
                        game_type = "xna_fna";
                    }
                }
            }

            match launch::launch(&resolved, game_type) {
                Ok(pid) => {
                    app_log(&format!("Process started: pid {}", pid));
                    resp(200, json!({"ok": true, "pid": pid}))
                },
                Err(e) => {
                    app_log(&format!("Launch failed: {}", e));
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/game/launch-auto") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    let launch_method = body.get("launchMethod").and_then(|v| v.as_str()).unwrap_or("auto");
                    app_log(&format!("Auto-launching game: appid {} method {}", id, launch_method));
                    match launch::launch_with_method(id as u32, launch_method) {
                        Ok((pid, game_type)) => {
                            app_log(&format!("Launched appid {} as {} (pid {})", id, game_type, pid));
                            resp(200, json!({"ok": true, "pid": pid, "gameType": game_type, "appid": id}))
                        },
                        Err(e) => {
                            app_log(&format!("Auto-launch failed: {}", e));
                            resp(500, json!({"ok": false, "error": e.to_string()}))
                        },
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/kill") => {
            let body = read_body(req);
            let pid = body.get("pid").and_then(|v| v.as_u64()).unwrap_or(0) as i32;
            let appid = body.get("appid").and_then(|v| v.as_u64()).map(|v| v as u32);
            app_log(&format!("Killing process: pid {} appid {:?}", pid, appid));
            if let Some(aid) = appid {
                match launch::kill_game(aid) {
                    Ok(_) => resp(200, json!({"ok": true})),
                    Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                }
            } else {
                match launch::kill(pid) {
                    Ok(_) => resp(200, json!({"ok": true})),
                    Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                }
            }
        },
        (Method::Post, "/steam/uninstall-game") => {
            let body = read_body(req);
            match body.get("appid").and_then(|v| v.as_u64()).map(|v| v as u32) {
                Some(appid) => {
                    app_log(&format!("Uninstalling game: appid {}", appid));
                    match steam::uninstall_game(appid) {
                        Ok(r) => resp(200, r),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        _ => resp(404, json!({"ok": false, "error": "not found"})),
    }
}

fn resp(code: u16, body: serde_json::Value) -> (u16, Vec<u8>) {
    (code, body.to_string().into_bytes())
}

fn app_log(msg: &str) {
    let home = dirs::home_dir().unwrap_or_default();
    let log_dir = home.join(".metalsharp").join("logs");
    let _ = std::fs::create_dir_all(&log_dir);
    let now = chrono_now();
    let line = format!("[{}] {}\n", now, msg);
    let log_path = log_dir.join(format!("{}.log", chrono_date()));
    let _ = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
        .and_then(|mut f| std::io::Write::write_all(&mut f, line.as_bytes()));
}

fn chrono_now() -> String {
    let d = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = d.as_secs();
    let h = (secs / 3600) % 24;
    let m = (secs / 60) % 60;
    let s = secs % 60;
    format!("{:02}:{:02}:{:02}", h, m, s)
}

fn chrono_date() -> String {
    let d = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = d.as_secs();
    let days = secs / 86400;
    let y = 1970 + (days * 400).div_ceil(146097);
    let mut remaining = days - (((y - 1) * 365) + ((y - 1) / 4) - ((y - 1) / 100) + ((y - 1) / 400));
    let ml = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut mo = 1;
    for (i, &md) in ml.iter().enumerate() {
        if remaining < md {
            mo = i + 1;
            break;
        }
        remaining -= md;
    }
    format!("{:04}-{:02}-{:02}", y, mo, remaining + 1)
}

fn resolve_game_exe(appid: u32) -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let game_dir = home.join(".metalsharp").join("games").join(appid.to_string());

    for entry in walkdir::WalkDir::new(&game_dir).max_depth(3).into_iter().flatten() {
        if let Some(ext) = entry.path().extension() {
            if ext == "exe" {
                let name = entry.file_name().to_string_lossy().to_string();
                let name_lower = name.to_lowercase();
                if name_lower.starts_with("terraria") && !name_lower.contains("server")
                    || name_lower.starts_with("hl2") && !name_lower.contains("launcher")
                    || !name_lower.contains("setup")
                        && !name_lower.contains("redist")
                        && !name_lower.contains("dotnet")
                        && !name_lower.contains("installer")
                        && !name_lower.contains("uninstall")
                        && !name_lower.contains("vcredist")
                        && !name_lower.contains("crashhandler")
                {
                    return entry.path().to_string_lossy().to_string();
                }
            }
        }
    }

    for entry in walkdir::WalkDir::new(&game_dir).max_depth(3).into_iter().flatten() {
        if let Some(ext) = entry.path().extension() {
            if ext == "exe" {
                let name = entry.file_name().to_string_lossy().to_lowercase();
                if !name.contains("setup")
                    && !name.contains("redist")
                    && !name.contains("dotnet")
                    && !name.contains("installer")
                    && !name.contains("uninstall")
                    && !name.contains("vcredist")
                    && !name.contains("server")
                    && !name.contains("crashhandler")
                {
                    return entry.path().to_string_lossy().to_string();
                }
            }
        }
    }

    game_dir.to_string_lossy().to_string()
}

fn read_body(req: &mut tiny_http::Request) -> serde_json::Map<String, serde_json::Value> {
    let mut buf = Vec::new();
    let _ = req.as_reader().read_to_end(&mut buf);
    serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(&buf).unwrap_or_default()
}
