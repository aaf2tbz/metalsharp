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

mod anticheat;
mod bottles;
mod d3d12_runtime_doctor;
mod installer;
mod kernel_translation;
mod launch;
mod launcher_evidence;
mod migrate;
mod mtsp;
mod platform;
mod scan;
mod setup;
mod sharp_library;
mod steam;
mod updater;

use serde_json::json;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use tiny_http::{Header, Method, Response, Server};

static RUNNING_GAMES: OnceLock<Mutex<HashMap<u32, i32>>> = OnceLock::new();
static ISSUE_LOG_COUNTER: AtomicU64 = AtomicU64::new(0);

fn running_games() -> &'static Mutex<HashMap<u32, i32>> {
    RUNNING_GAMES.get_or_init(|| Mutex::new(HashMap::new()))
}

fn register_game_pid(appid: u32, pid: u32) {
    if let Ok(mut map) = running_games().lock() {
        map.insert(appid, pid as i32);
    }
}

fn unregister_game_pid(appid: u32) {
    if let Ok(mut map) = running_games().lock() {
        map.remove(&appid);
    }
}

fn get_game_pid(appid: u32) -> Option<i32> {
    running_games().lock().ok()?.get(&appid).copied()
}

fn prune_inactive_game_pids() {
    if let Ok(mut map) = running_games().lock() {
        map.retain(|_, &mut pid| launch::is_process_active(pid));
    }
}

enum RouteResponse {
    Json(u16, Vec<u8>),
    Raw(u16, Vec<u8>, String),
}

fn main() {
    let port = std::env::var("METALSHARP_PORT").unwrap_or_else(|_| "9274".into());
    let addr = format!("127.0.0.1:{}", port);
    let server = Arc::new({
        let mut attempts = 0u32;
        let max_attempts = 30u32;
        loop {
            match Server::http(&addr) {
                Ok(s) => break s,
                Err(e) => {
                    attempts += 1;
                    if attempts >= max_attempts {
                        eprintln!("failed to bind {} after {} attempts: {}", addr, max_attempts, e);
                        std::process::exit(1);
                    }
                    eprintln!("bind {} attempt {}/{} failed: {} — retrying in 500ms", addr, attempts, max_attempts, e);
                    std::thread::sleep(std::time::Duration::from_millis(500));
                },
            }
        }
    });

    ctrlc::set_handler(move || {
        app_log("Shutting down — cleaning up running games");
        if let Ok(map) = running_games().lock() {
            for (&appid, &pid) in map.iter() {
                app_log(&format!("Killing game appid={} pid={}", appid, pid));
                let _ = launch::kill_process_tree(pid);
            }
        }
        std::process::exit(0);
    })
    .unwrap_or_else(|e| eprintln!("ctrlc handler warning: {}", e));

    eprintln!("metalsharp-backend listening on {}", addr);
    app_log(&format!("MetalSharp v{} backend started on {}", env!("CARGO_PKG_VERSION"), addr));

    if crate::steam::is_wine_steam_running() {
        let _ = kernel_translation::ipc_bridge::start_ipc_listener();
    }

    let cors_header = Header::from_bytes(&b"Access-Control-Allow-Origin"[..], &b"*"[..]).unwrap();
    let json_header = Header::from_bytes(&b"Content-Type"[..], &b"application/json"[..]).unwrap();

    loop {
        let mut request = match server.recv() {
            Ok(r) => r,
            Err(_) => break,
        };

        let route_resp = route(&mut request);
        match route_resp {
            RouteResponse::Json(code, body) => {
                let resp = Response::from_data(body)
                    .with_header(cors_header.clone())
                    .with_header(json_header.clone())
                    .with_status_code(code);
                let _ = request.respond(resp);
            },
            RouteResponse::Raw(code, data, mime) => {
                let content_header =
                    Header::from_bytes(&b"Content-Type"[..], mime.as_bytes()).unwrap_or_else(|_| json_header.clone());
                let resp = Response::from_data(data)
                    .with_header(cors_header.clone())
                    .with_header(content_header)
                    .with_status_code(code);
                let _ = request.respond(resp);
            },
        }
    }
}

fn route(req: &mut tiny_http::Request) -> RouteResponse {
    let method = req.method().clone();
    let url = req.url().to_string();
    let path = url.split('?').next().unwrap_or(&url);

    match (method, path) {
        (Method::Get, "/status") => {
            app_log("Backend status checked");
            resp(
                200,
                json!({
                    "ok": true,
                    "version": env!("CARGO_PKG_VERSION"),
                    "pid": std::process::id(),
                    "dev_mode": std::env::var("METALSHARP_DEV").map(|v| v == "1").unwrap_or(false),
                    "metalsharp_home": crate::platform::metalsharp_home_dir().to_string_lossy().to_string(),
                }),
            )
        },
        (Method::Get, "/runtime/host-abi") => resp(
            200,
            json!({
                "ok": true,
                "magic": "MSAB",
                "version": {"major": 1, "minor": 0},
                "services": [
                    "process",
                    "paths",
                    "logging",
                    "steam",
                    "graphics",
                    "audio",
                    "input",
                    "managed_runtime"
                ],
                "steam_bridge": {
                    "default_port": 18733,
                    "active_port": mtsp::launcher::bridge_port(),
                    "env": "METALSHARP_STEAM_BRIDGE_PORT"
                },
                "managed_runtime_env": [
                    "METALSHARP_MONO_LIB",
                    "METALSHARP_MONO_ROOT",
                    "METALSHARP_MONO_ASSEMBLY_DIR",
                    "METALSHARP_MONO_CONFIG_DIR"
                ]
            }),
        ),
        (Method::Get, "/update/check") => resp(200, updater::check_for_update()),
        (Method::Post, "/update/start") => match updater::start_update() {
            Ok(v) => resp(200, v),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Get, "/update/progress") => resp(200, updater::read_update_progress()),
        (Method::Get, "/update/dmg-path") => match updater::get_downloaded_dmg() {
            Some((path, version)) => resp(200, json!({"ok": true, "path": path, "version": version})),
            None => resp(200, json!({"ok": false, "error": "no downloaded DMG"})),
        },
        (Method::Post, "/update/cleanup") => resp(200, updater::cleanup_downloaded_dmgs()),
        (Method::Get, "/update/migrate/check") => resp(200, migrate::needs_migration()),
        (Method::Post, "/update/migrate/start") => match migrate::start_migration() {
            Ok(v) => resp(200, v),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Get, "/update/migrate/progress") => resp(200, migrate::read_migrate_progress()),
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
        (Method::Get, "/setup/agility-versions") => resp(200, setup::agility_known_sdk_versions()),
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
        (Method::Post, "/setup/install-vcpp-x64") => {
            let home = dirs::home_dir().unwrap_or_default();
            let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
            if !prefix.join("drive_c/windows/system32").exists() {
                resp(400, json!({"ok": false, "error": "Wine prefix not ready — install runtime and Steam first"}))
            } else {
                match bottles::vcpp_ensure_and_install_x64(&prefix) {
                    Ok(()) => resp(200, json!({"ok": true})),
                    Err(e) => resp(500, json!({"ok": false, "error": e})),
                }
            }
        },
        (Method::Post, "/setup/install-vcpp-x86") => {
            let home = dirs::home_dir().unwrap_or_default();
            let prefix = crate::platform::metalsharp_home_dir_for(&home).join("prefix-steam");
            if !prefix.join("drive_c/windows/system32").exists() {
                resp(400, json!({"ok": false, "error": "Wine prefix not ready — install runtime and Steam first"}))
            } else {
                match bottles::vcpp_ensure_and_install_x86(&prefix) {
                    Ok(()) => resp(200, json!({"ok": true})),
                    Err(e) => resp(500, json!({"ok": false, "error": e})),
                }
            }
        },
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
        (Method::Post, "/game/resolve-routing") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    let pipeline = bottles::resolve_steam_pipeline_for_request(id as u32, None);
                    let node = mtsp::engine::get_pipeline(pipeline);
                    let recipe = mtsp::rules::get_game_recipe(id as u32);
                    let preferred_pipeline = bottles::preferred_pipeline_for_steam_app(id as u32);
                    resp(
                        200,
                        json!({
                            "ok": true,
                            "appid": id,
                            "pipeline": pipeline,
                            "pipeline_name": node.name,
                            "preferred_pipeline": preferred_pipeline.map(|p| p.to_legacy_method().to_string()),
                            "graphics_backend": node.graphics_backend,
                            "backend": node.backend,
                            "offline_capable": recipe.as_ref().map(|r| r.offline_capable).unwrap_or(false),
                            "anticheat": recipe.as_ref().and_then(|r| r.anticheat.clone()),
                            "recipe": recipe,
                        }),
                    )
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
                Ok(_) => {
                    let library = steam::library();
                    let total = library.get("total").and_then(|t| t.as_u64()).unwrap_or(0);
                    app_log(&format!("Steam API key sync loaded {} games", total));
                    resp(
                        200,
                        json!({
                            "ok": true,
                            "library": library,
                            "sync": steam::api_key_sync_state(),
                        }),
                    )
                },
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Post, "/steam/install") => match steam::install_steam() {
            Ok(p) => resp(200, json!({"ok": true, "path": p})),
            Err(e) => {
                app_issue_log("steam-install", "wine-steam", &e.to_string(), &[]);
                resp(500, json!({"ok": false, "error": e.to_string()}))
            },
        },
        (Method::Post, "/steam/launch") => {
            app_log("Launching Wine Steam...");
            match steam::launch_wine_steam() {
                Ok(v) => resp(200, v),
                Err(e) => {
                    app_issue_log("steam-launch", "wine-steam", &e.to_string(), &[]);
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/steam/stop") => {
            app_log("Stopping Wine Steam...");
            match steam::stop_wine_steam() {
                Ok(v) => resp(200, v),
                Err(e) => {
                    app_issue_log("steam-stop", "wine-steam", &e.to_string(), &[]);
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/steam/mac-launch") => {
            app_log("Launching macOS Steam...");
            match steam::launch_macos_steam() {
                Ok(v) => resp(200, v),
                Err(e) => {
                    app_issue_log("steam-launch", "macos-steam", &e.to_string(), &[]);
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/steam/mac-install") => {
            app_log("Opening macOS Steam installer...");
            match steam::install_macos_steam() {
                Ok(v) => resp(200, v),
                Err(e) => {
                    app_issue_log("steam-install", "macos-steam", &e.to_string(), &[]);
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/steam/mac-stop") => {
            app_log("Stopping macOS Steam...");
            match steam::stop_macos_steam() {
                Ok(v) => resp(200, v),
                Err(e) => {
                    app_issue_log("steam-stop", "macos-steam", &e.to_string(), &[]);
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/steam/mac-launch-game") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    app_log(&format!("Launching game via macOS Steam: appid {}", id));
                    match steam::launch_macos_steam_game(id as u32) {
                        Ok(v) => resp(200, v),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Get, "/steam/is-running") => resp(200, json!({"ok": true, "running": steam::is_wine_steam_running()})),
        (Method::Get, "/steam/bridge-status") => {
            let running = mtsp::launcher::bridge_is_running();
            resp(200, json!({"ok": true, "running": running, "port": mtsp::launcher::bridge_port()}))
        },
        (Method::Post, "/steam/bridge-start") => match mtsp::launcher::ensure_bridge_running() {
            Ok(_) => resp(200, json!({"ok": true, "port": mtsp::launcher::bridge_port()})),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Get, "/steam/watch-steamapps") => {
            let new_ids = steam::watch_steamapps();
            resp(200, json!({"ok": true, "new_appids": new_ids}))
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
            match parse_request_appid(&body) {
                Ok(id) => {
                    let launch_method = body
                        .get("launchMethod")
                        .or_else(|| body.get("pipeline"))
                        .and_then(|v| v.as_str())
                        .unwrap_or("steam");
                    let route_pipeline = match mtsp::engine::PipelineId::from_str_flexible(launch_method) {
                        Some(mtsp::engine::PipelineId::Steam) => None,
                        Some(pipeline) => Some(bottles::resolve_steam_pipeline_for_request(id, Some(pipeline))),
                        None if launch_method.eq_ignore_ascii_case("steam") => None,
                        None => Some(bottles::resolve_steam_pipeline_for_request(id, None)),
                    };
                    app_log(&format!("Launching game via Wine Steam: appid {}, route {}", id, launch_method));
                    let launch_result = match route_pipeline {
                        Some(pipeline) => {
                            let bottle = match bottles::prepare_steam_game_launch(id, pipeline) {
                                Ok(bottle) => bottle,
                                Err(e) => return resp(500, json!({"ok": false, "error": e.to_string()})),
                            };
                            let (mut env, launch_recipe) =
                                match mtsp::launcher::prepare_steam_pipeline_env(id, pipeline) {
                                    Ok(prepared) => prepared,
                                    Err(e) => return resp(500, json!({"ok": false, "error": e.to_string()})),
                                };
                            let offline_direct = bottles::steam_pipeline_defaults_offline(pipeline);
                            if offline_direct {
                                let Some(game_dir) = launch_recipe.game_dir.as_ref() else {
                                    return resp(404, json!({"ok": false, "error": "Game directory not found"}));
                                };
                                crate::mtsp::launcher::deploy_eac_toggle(game_dir);
                                if let Some(home) = dirs::home_dir() {
                                    crate::mtsp::launcher::deploy_goldberg_for_pipeline(&home, game_dir, id, pipeline);
                                }
                                env.push(("SteamAppId".to_string(), id.to_string()));
                                env.push(("SteamGameId".to_string(), id.to_string()));
                                env.push(("METALSHARP_OFFLINE_MODE".to_string(), "1".to_string()));
                            }
                            let compatdata = bottles::load_steam_compatdata(id).ok();
                            let is_gptk_direct = matches!(pipeline, mtsp::engine::PipelineId::D3DMetal);
                            let steam_started = if is_gptk_direct {
                                false
                            } else {
                                match steam::ensure_wine_steam_ready_for_game_launch() {
                                    Ok(started) => started,
                                    Err(e) => return resp(500, json!({"ok": false, "error": e.to_string()})),
                                }
                            };
                            let bottle_prefix = std::path::PathBuf::from(&bottle.prefix_path);
                            mtsp::launcher::launch_steam_bottle_with_pipeline(id, pipeline, &bottle_prefix, &env).map(
                                |(pid, game_type, log_path)| {
                                    register_game_pid(id, pid);
                                    let compatdata = bottles::set_launch_started(&bottle.id, pid, &log_path)
                                        .ok()
                                        .and_then(|manifest| bottles::save_steam_compatdata(&manifest, pipeline).ok())
                                        .or(compatdata);
                                    json!({
                                        "ok": true,
                                        "pid": pid,
                                        "appid": id,
                                        "gameType": game_type,
                                        "bottle_id": bottle.id,
                                        "bottle_prefix": bottle.prefix_path,
                                        "launch_log": log_path.to_string_lossy().to_string(),
                                        "compatdata": compatdata,
                                        "pipeline": pipeline,
                                        "recipe": launch_recipe,
                                        "steam_started": steam_started,
                                        "steam_runtime": if offline_direct { "offline" } else { "background" },
                                        "offline_mode": offline_direct,
                                        "eac_toggle_deployed": offline_direct,
                                        "env_applied_to": "game_process",
                                        "env_handoff": env.iter().map(|(k, _)| k).collect::<Vec<_>>(),
                                    })
                                },
                            )
                        },
                        None => {
                            let pipeline = bottles::resolve_steam_pipeline_for_request(id, None);
                            let bottle = match bottles::prepare_steam_game_launch(id, pipeline) {
                                Ok(bottle) => bottle,
                                Err(e) => return resp(500, json!({"ok": false, "error": e.to_string()})),
                            };
                            let compatdata = bottles::load_steam_compatdata(id).ok();
                            steam::launch_game_via_steam(id).map(|mut v| {
                                if let Some(obj) = v.as_object_mut() {
                                    obj.insert("bottle_id".into(), json!(bottle.id));
                                    obj.insert("bottle_prefix".into(), json!(bottle.prefix_path));
                                    obj.insert("compatdata".into(), json!(compatdata));
                                }
                                v
                            })
                        },
                    };
                    match launch_result {
                        Ok(v) => resp(200, v),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                Err(error) => resp(400, json!({"ok": false, "error": error})),
            }
        },
        (Method::Post, "/steam/launch-offline") => {
            let body = read_body(req);
            match parse_request_appid(&body) {
                Ok(id) => {
                    let recipe = mtsp::rules::get_game_recipe(id);
                    let pipeline = bottles::resolve_steam_pipeline_for_request(id, None);
                    let node = mtsp::engine::get_pipeline(pipeline);

                    if !bottles::steam_pipeline_defaults_offline(pipeline)
                        && !recipe.as_ref().map(|r| r.offline_capable).unwrap_or(false)
                    {
                        return resp(
                            400,
                            json!({
                                "ok": false,
                                "error": "Game is not marked as offline-capable",
                                "hint": "Use D3DMetal or set offline_capable = true in mtsp-rules.toml for this appid"
                            }),
                        );
                    }

                    let game_dir = crate::setup::resolve_game_dir(id);
                    let Some(ref dir) = game_dir else {
                        return resp(404, json!({"ok": false, "error": "Game directory not found"}));
                    };

                    crate::mtsp::launcher::deploy_eac_toggle(&std::path::PathBuf::from(dir));
                    if let Some(home) = dirs::home_dir() {
                        crate::mtsp::launcher::deploy_goldberg_for_pipeline(
                            &home,
                            &std::path::PathBuf::from(dir),
                            id,
                            pipeline,
                        );
                    }

                    let bottle = match bottles::prepare_steam_game_launch(id, pipeline) {
                        Ok(bottle) => bottle,
                        Err(e) => return resp(500, json!({"ok": false, "error": e.to_string()})),
                    };

                    let (mut env, launch_recipe) = match mtsp::launcher::prepare_steam_pipeline_env(id, pipeline) {
                        Ok(prepared) => prepared,
                        Err(e) => return resp(500, json!({"ok": false, "error": e.to_string()})),
                    };

                    env.push(("SteamAppId".to_string(), id.to_string()));
                    env.push(("SteamGameId".to_string(), id.to_string()));
                    env.push(("METALSHARP_OFFLINE_MODE".to_string(), "1".to_string()));

                    let bottle_prefix = std::path::PathBuf::from(&bottle.prefix_path);
                    let launch_result =
                        mtsp::launcher::launch_steam_bottle_with_pipeline(id, pipeline, &bottle_prefix, &env);

                    app_log(&format!(
                        "[OFFLINE] appid={} pipeline={} backend={} eac_toggle=true anticheat={}",
                        id,
                        node.name,
                        node.graphics_backend,
                        recipe.as_ref().and_then(|r| r.anticheat.as_ref()).map(|s| s.as_str()).unwrap_or("none")
                    ));

                    match launch_result {
                        Ok((pid, game_type, log_path)) => resp(
                            200,
                            json!({
                                "ok": true,
                                "pid": pid,
                                "appid": id,
                                "gameType": game_type,
                                "bottle_id": bottle.id,
                                "bottle_prefix": bottle.prefix_path,
                                "launch_log": log_path.to_string_lossy().to_string(),
                                "pipeline": pipeline,
                                "graphics_backend": node.graphics_backend,
                                "offline_mode": true,
                                "eac_toggle_deployed": true,
                                "anticheat": recipe.and_then(|r| r.anticheat),
                            }),
                        ),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                Err(error) => resp(400, json!({"ok": false, "error": error})),
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
            let log_path = crate::platform::metalsharp_home_dir_for(&home).join("logs");
            let mut entries = Vec::new();
            let mut files: Vec<std::path::PathBuf> = if log_path.exists() {
                walkdir::WalkDir::new(&log_path)
                    .max_depth(2)
                    .into_iter()
                    .flatten()
                    .filter(|entry| entry.path().extension().map(|e| e == "log").unwrap_or(false))
                    .map(|entry| entry.path().to_path_buf())
                    .collect()
            } else {
                Vec::new()
            };
            files.sort_by_key(|path| {
                std::fs::metadata(path).and_then(|meta| meta.modified()).unwrap_or(std::time::UNIX_EPOCH)
            });
            files.reverse();
            for p in files.iter().take(8) {
                if let Ok(content) = std::fs::read_to_string(p) {
                    let lines: Vec<&str> = content.lines().rev().take(500).collect();
                    entries.push(json!({
                        "name": p.strip_prefix(&log_path).unwrap_or(p).to_string_lossy(),
                        "lines": lines.into_iter().rev().collect::<Vec<&str>>(),
                    }));
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
        (Method::Get, "/logs/stream") => {
            let home = dirs::home_dir().unwrap_or_default();
            let log_dir = crate::platform::metalsharp_home_dir_for(&home).join("logs");
            let url_str = req.url().to_string();
            let after: usize = url_str
                .split("after=")
                .nth(1)
                .and_then(|v| v.split('&').next())
                .and_then(|v| v.parse().ok())
                .unwrap_or(0);
            let log_path = log_dir.join(format!("{}.log", chrono_date()));
            if let Ok(content) = std::fs::read_to_string(&log_path) {
                let all_lines: Vec<&str> = content.lines().collect();
                let total = all_lines.len();
                let new_lines: Vec<&str> = if after < total { all_lines[after..].to_vec() } else { Vec::new() };
                resp(
                    200,
                    json!({
                        "ok": true,
                        "total": total,
                        "lines": new_lines,
                    }),
                )
            } else {
                resp(200, json!({"ok": true, "total": 0, "lines": []}))
            }
        },
        (Method::Get, "/logs/crash-reports") => {
            let home = dirs::home_dir().unwrap_or_default();
            let ms_home = crate::platform::metalsharp_home_dir_for(&home);
            let mut reports = Vec::new();

            let game_base = ms_home.join("games");
            if let Ok(rd) = std::fs::read_dir(&game_base) {
                for entry in rd.flatten() {
                    if entry.path().is_dir() {
                        let appid_str = entry.file_name().to_string_lossy().to_string();
                        let appid: u32 = appid_str.parse().unwrap_or(0);
                        let pipeline = if appid > 0 {
                            crate::bottles::resolve_steam_pipeline_for_request(appid, None)
                        } else {
                            crate::mtsp::engine::PipelineId::M11
                        };
                        let pipeline_label = pipeline_label_for(pipeline);
                        let _ = scan_crash_files(&entry.path(), &appid_str, pipeline_label, &mut reports, 0);
                    }
                }
            }

            let bottles_dir = ms_home.join("bottles");
            if let Ok(rd) = std::fs::read_dir(&bottles_dir) {
                for entry in rd.flatten() {
                    let bottle_id = entry.file_name().to_string_lossy().to_string();
                    let logs_dir = entry.path().join("logs");
                    if !logs_dir.is_dir() {
                        continue;
                    }
                    let appid: u32 = bottle_id.strip_prefix("steam_").and_then(|s| s.parse().ok()).unwrap_or(0);
                    let pipeline = if appid > 0 {
                        crate::bottles::resolve_steam_pipeline_for_request(appid, None)
                    } else {
                        crate::mtsp::engine::PipelineId::M11
                    };
                    let pipeline_label = pipeline_label_for(pipeline);
                    let _ = scan_crash_files(&logs_dir, &bottle_id, pipeline_label, &mut reports, 0);
                }
            }

            let steam_dumps =
                ms_home.join("prefix-steam").join("drive_c").join("Program Files (x86)").join("Steam").join("dumps");
            if steam_dumps.is_dir() {
                let _ = scan_steam_dumps(&steam_dumps, &mut reports);
            }

            let prefix = ms_home.join("prefix-steam").join("drive_c");
            for crash_dir in [
                prefix.join("users").join("steamuser").join("AppData").join("Local").join("CrashDumps"),
                prefix.join("ProgramData").join("CrashDumps"),
            ] {
                if crash_dir.is_dir() {
                    let _ = scan_crash_files(&crash_dir, "system", "System", &mut reports, 0);
                }
            }

            reports.sort_by(|a: &serde_json::Value, b: &serde_json::Value| {
                b.get("timestamp")
                    .unwrap_or(&json!(""))
                    .as_str()
                    .unwrap_or("")
                    .cmp(a.get("timestamp").unwrap_or(&json!("")).as_str().unwrap_or(""))
            });
            resp(200, json!({"ok": true, "reports": reports}))
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
        (Method::Get, "/mtsp/pipelines") => {
            let url_str = req.url().to_string();
            let appid: u32 = url_str
                .split("appid=")
                .nth(1)
                .and_then(|v| v.split('&').next())
                .and_then(|v| v.parse().ok())
                .unwrap_or(0);
            let pipeline_id = bottles::resolve_steam_pipeline_for_request(appid, None);
            let node = mtsp::engine::get_pipeline(pipeline_id);
            let preferred_pipeline = bottles::preferred_pipeline_for_steam_app(appid);
            let all_pipelines: Vec<serde_json::Value> = mtsp::engine::pipelines()
                .iter()
                .filter(|p| p.id.is_user_selectable())
                .map(|p| {
                    serde_json::json!({
                        "id": p.id.user_selectable_id().unwrap_or("auto"),
                        "name": p.id.user_selectable_name().unwrap_or(p.name),
                        "description": p.description,
                        "backend": p.backend,
                        "experimental": p.experimental,
                        "requires_wine": p.requires_wine,
                    })
                })
                .collect();
            resp(
                200,
                json!({
                    "ok": true,
                    "appid": appid,
                    "recommended": pipeline_id.user_selectable_id().unwrap_or("auto"),
                    "recommended_name": pipeline_id.user_selectable_name().unwrap_or("Auto"),
                    "preferred": preferred_pipeline.and_then(|p| p.user_selectable_id().map(|id| id.to_string())),
                    "preferred_name": preferred_pipeline.and_then(|p| {
                        p.user_selectable_name().map(|name| name.to_string())
                    }),
                    "pipelines": all_pipelines,
                }),
            )
        },
        (Method::Post, "/mtsp/prepare") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => match mtsp::launcher::prepare_pipeline(id as u32) {
                    Ok(v) => resp(200, v),
                    Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/mtsp/recipe") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    let method = body.get("launchMethod").and_then(|v| v.as_str()).unwrap_or("auto");
                    let pipeline = bottles::resolve_steam_pipeline_for_request(
                        id as u32,
                        mtsp::engine::PipelineId::from_str_flexible(method),
                    );
                    let node = mtsp::engine::get_pipeline(pipeline);
                    match mtsp::recipe::build_launch_recipe(id as u32, node) {
                        Ok(recipe) => resp(200, json!({"ok": true, "appid": id, "recipe": recipe})),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/mtsp/doctor") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            match appid {
                Some(id) => {
                    let method = body.get("launchMethod").and_then(|v| v.as_str()).unwrap_or("auto");
                    let pipeline = bottles::resolve_steam_pipeline_for_request(
                        id as u32,
                        mtsp::engine::PipelineId::from_str_flexible(method),
                    );
                    let node = mtsp::engine::get_pipeline(pipeline);
                    let report = mtsp::recipe::diagnose_launch_request(id as u32, node);
                    resp(200, json!({"ok": true, "appid": id, "report": report}))
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Get, "/goldberg/status") => {
            let url_str = req.url().to_string();
            let appid: u32 = url_str
                .split("appid=")
                .nth(1)
                .and_then(|v| v.split('&').next())
                .and_then(|v| v.parse().ok())
                .unwrap_or(0);
            let game_dir =
                crate::setup::resolve_windows_game_dir(appid).or_else(|| crate::setup::resolve_game_dir(appid));
            let pipeline = bottles::resolve_steam_pipeline_for_request(appid, None);
            let active = game_dir
                .as_ref()
                .and_then(|d| {
                    dirs::home_dir().map(|home| mtsp::launcher::goldberg_status_for_pipeline(&home, d, pipeline))
                })
                .unwrap_or(false);
            let pipeline_id = pipeline.user_selectable_id().unwrap_or_else(|| pipeline.to_legacy_method());
            resp(200, json!({"ok": true, "appid": appid, "goldberg_active": active, "pipeline": pipeline_id}))
        },
        (Method::Post, "/goldberg/toggle") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            let enable = body.get("enable").and_then(|v| v.as_bool()).unwrap_or(true);
            match appid {
                Some(id) => {
                    let aid = id as u32;
                    let game_dir =
                        crate::setup::resolve_windows_game_dir(aid).or_else(|| crate::setup::resolve_game_dir(aid));
                    match game_dir {
                        Some(dir) if dir.exists() => {
                            let pipeline = bottles::resolve_steam_pipeline_for_request(aid, None);
                            let pipeline_id =
                                pipeline.user_selectable_id().unwrap_or_else(|| pipeline.to_legacy_method());
                            if enable {
                                let home = dirs::home_dir().unwrap_or_default();
                                mtsp::launcher::deploy_goldberg_for_pipeline(&home, &dir.to_path_buf(), aid, pipeline);
                                app_log(&format!("[STEAM_EMU] enabled for appid {}", aid));
                                resp(200, json!({"ok": true, "goldberg_active": true, "pipeline": pipeline_id}))
                            } else {
                                let home = dirs::home_dir().unwrap_or_default();
                                mtsp::launcher::cleanup_goldberg_for_pipeline(&home, &dir, pipeline);
                                app_log(&format!("[STEAM_EMU] disabled for appid {}", aid));
                                resp(200, json!({"ok": true, "goldberg_active": false, "pipeline": pipeline_id}))
                            }
                        },
                        _ => resp(404, json!({"ok": false, "error": "game directory not found"})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Get, "/sharp-library") => resp(200, sharp_library::handle_get_library()),
        (Method::Get, "/bottles") => resp(200, bottles::handle_list_bottles()),
        (Method::Get, "/bottles/profiles") => resp(200, bottles::handle_list_runtime_profiles()),
        (Method::Get, "/bottles/compatibility-matrix") => resp(200, bottles::handle_compatibility_matrix()),
        (Method::Get, "/bottles/redist-sources") => resp(200, bottles::handle_redist_sources()),
        (Method::Post, "/bottles/record-compatibility") => {
            let body = read_body(req);
            resp(200, bottles::handle_record_compatibility_case(&body))
        },
        (Method::Post, "/bottles/sync-steam") => resp(200, bottles::handle_sync_steam_bottles()),
        (Method::Post, "/bottles/get") => {
            let body = read_body(req);
            resp(200, bottles::handle_get_bottle(&body))
        },
        (Method::Post, "/bottles/refresh") => {
            let body = read_body(req);
            resp(200, bottles::handle_refresh_bottle(&body))
        },
        (Method::Post, "/bottles/doctor") => {
            let body = read_body(req);
            resp(200, bottles::handle_diagnose_bottle(&body))
        },
        (Method::Post, "/bottles/prepare") => {
            let body = read_body(req);
            resp(200, bottles::handle_prepare_bottle(&body))
        },
        (Method::Post, "/bottles/repair-component") => {
            let body = read_body(req);
            resp(200, bottles::handle_repair_component(&body))
        },
        (Method::Post, "/bottles/set-runtime-profile") => {
            let body = read_body(req);
            resp(200, bottles::handle_set_runtime_profile(&body))
        },
        (Method::Post, "/bottles/edit") => {
            let body = read_body(req);
            resp(200, bottles::handle_edit_bottle(&body))
        },
        (Method::Post, "/bottles/set-windows-version") => {
            let body = read_body(req);
            resp(200, bottles::handle_set_windows_version(&body))
        },
        (Method::Post, "/bottles/relaunch-installer") => {
            let body = read_body(req);
            resp(200, sharp_library::handle_relaunch_bottle_installer(&body))
        },
        (Method::Post, "/bottles/apply-font-subs") => {
            let body = read_body(req);
            resp(200, bottles::handle_apply_font_substitutions(&body))
        },
        (Method::Post, "/bottles/seed-post-wineboot") => {
            let body = read_body(req);
            resp(200, bottles::handle_seed_post_wineboot(&body))
        },
        (Method::Post, "/bottles/verify-directx") => {
            let body = read_body(req);
            resp(200, bottles::handle_verify_directx(&body))
        },
        (Method::Post, "/steam/install-recipe-deps") => {
            let body = read_body(req);
            resp(200, bottles::handle_install_recipe_deps(&body))
        },
        (Method::Post, "/steam/runtime-doctor") => {
            let body = read_body(req);
            resp(200, bottles::handle_steam_runtime_doctor(&body))
        },
        (Method::Post, "/steam/d3d12-runtime-doctor") => {
            let body = read_body(req);
            resp(200, d3d12_runtime_doctor::handle_steam_d3d12_runtime_doctor(&body))
        },
        (Method::Post, "/steam/compatdata") => {
            let body = read_body(req);
            resp(200, bottles::handle_steam_compatdata(&body))
        },
        (Method::Post, "/steam/anticheat-evidence") => {
            let body = read_body(req);
            resp(200, anticheat::handle_steam_anticheat_evidence(&body))
        },
        (Method::Post, "/steam/anticheat-probe") => {
            let body = read_body(req);
            resp(200, anticheat::handle_steam_anticheat_probe(&body))
        },
        (Method::Post, "/steam/anticheat-delta-audit") => {
            let body = read_body(req);
            resp(200, anticheat::handle_steam_anticheat_delta_audit(&body))
        },
        (Method::Post, "/steam/anticheat-contract-probe") => {
            let body = read_body(req);
            resp(200, anticheat::handle_steam_anticheat_contract_probe(&body))
        },
        (Method::Post, "/steam/anticheat-substrate-decision") => {
            let body = read_body(req);
            resp(200, anticheat::handle_steam_anticheat_substrate_decision(&body))
        },
        (Method::Post, "/steam/anticheat-substrate-plan") => {
            let body = read_body(req);
            resp(200, anticheat::handle_steam_anticheat_substrate_plan(&body))
        },
        (Method::Post, "/kernel-translation/probe") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_kernel_translation_probe(&body))
        },
        (Method::Post, "/kernel-translation/host-probe") => {
            let body = read_body(req);
            resp(200, kernel_translation::probe::handle_kernel_probe(&body))
        },
        (Method::Post, "/kernel-translation/handle/create") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_create(&body))
        },
        (Method::Post, "/kernel-translation/handle/close") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_close(&body))
        },
        (Method::Post, "/kernel-translation/handle/duplicate") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_duplicate(&body))
        },
        (Method::Post, "/kernel-translation/handle/query") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_query(&body))
        },
        (Method::Post, "/kernel-translation/handle/enumerate") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_enumerate(&body))
        },
        (Method::Post, "/kernel-translation/handle/system-info") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_system_handle_information(&body))
        },
        (Method::Post, "/kernel-translation/handle/table-status") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_table_status(&body))
        },
        (Method::Post, "/kernel-translation/handle/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_table::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/handle/enumerate-fds") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_bridge::handle_enumerate_fds(&body))
        },
        (Method::Post, "/kernel-translation/handle/enumerate-ports") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_bridge::handle_enumerate_ports(&body))
        },
        (Method::Post, "/kernel-translation/handle/unified-snapshot") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_bridge::handle_unified_snapshot(&body))
        },
        (Method::Post, "/kernel-translation/handle/snapshot-all") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_bridge::handle_snapshot_all(&body))
        },
        (Method::Post, "/kernel-translation/integrity/query-signing-level") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_query_signing_level(&body))
        },
        (Method::Post, "/kernel-translation/integrity/query-process-signing") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_query_process_signing(&body))
        },
        (Method::Post, "/kernel-translation/integrity/register-pe-module") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_register_pe_module(&body))
        },
        (Method::Post, "/kernel-translation/integrity/register-macho-module") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_register_macho_module(&body))
        },
        (Method::Post, "/kernel-translation/integrity/set-cached-signing-level") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_set_cached_signing_level(&body))
        },
        (Method::Post, "/kernel-translation/integrity/list-modules") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_list_signed_modules(&body))
        },
        (Method::Post, "/kernel-translation/integrity/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::code_integrity::handle_seed_integrity_demo(&body))
        },
        (Method::Post, "/kernel-translation/apc/queue") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_queue_apc(&body))
        },
        (Method::Post, "/kernel-translation/apc/test-alert") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_test_alert(&body))
        },
        (Method::Post, "/kernel-translation/apc/wait-alertable") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_wait_alertable(&body))
        },
        (Method::Post, "/kernel-translation/apc/allocate-trampoline") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_allocate_trampoline(&body))
        },
        (Method::Post, "/kernel-translation/apc/suspend-thread") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_suspend_thread(&body))
        },
        (Method::Post, "/kernel-translation/apc/get-thread-context") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_get_thread_context(&body))
        },
        (Method::Post, "/kernel-translation/apc/set-thread-context") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_set_thread_context(&body))
        },
        (Method::Post, "/kernel-translation/apc/inject-sequence") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_inject_apc_sequence(&body))
        },
        (Method::Post, "/kernel-translation/apc/queue-status") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_apc_queue_status(&body))
        },
        (Method::Post, "/kernel-translation/apc/trampoline-status") => {
            let body = read_body(req);
            resp(200, kernel_translation::apc::handle_trampoline_status(&body))
        },
        (Method::Post, "/kernel-translation/es/register-callback") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_register_callback(&body))
        },
        (Method::Post, "/kernel-translation/es/unregister-callback") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_unregister_callback(&body))
        },
        (Method::Post, "/kernel-translation/es/list-callbacks") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_list_callbacks(&body))
        },
        (Method::Post, "/kernel-translation/es/fire-process-event") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_fire_process_event(&body))
        },
        (Method::Post, "/kernel-translation/es/fire-thread-event") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_fire_thread_event(&body))
        },
        (Method::Post, "/kernel-translation/es/fire-image-event") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_fire_image_event(&body))
        },
        (Method::Post, "/kernel-translation/es/process-events") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_process_events(&body))
        },
        (Method::Post, "/kernel-translation/es/thread-events") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_thread_events(&body))
        },
        (Method::Post, "/kernel-translation/es/image-events") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_image_events(&body))
        },
        (Method::Post, "/kernel-translation/es/create-ipc-channel") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_create_ipc_channel(&body))
        },
        (Method::Post, "/kernel-translation/es/ipc-channels") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_ipc_channels(&body))
        },
        (Method::Post, "/kernel-translation/es/status") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_es_status(&body))
        },
        (Method::Post, "/kernel-translation/es/detect-events") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_detect_events(&body))
        },
        (Method::Post, "/kernel-translation/es/nt-callback-bridge") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_nt_callback_bridge(&body))
        },
        (Method::Post, "/kernel-translation/es/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_bridge::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/thread/snapshot") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_snapshot_threads(&body))
        },
        (Method::Post, "/kernel-translation/thread/compute-delta") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_compute_delta(&body))
        },
        (Method::Post, "/kernel-translation/thread/create-watcher") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_create_watcher(&body))
        },
        (Method::Post, "/kernel-translation/thread/destroy-watcher") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_destroy_watcher(&body))
        },
        (Method::Post, "/kernel-translation/thread/list-watchers") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_list_watchers(&body))
        },
        (Method::Post, "/kernel-translation/thread/poll-watcher") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_poll_watcher(&body))
        },
        (Method::Post, "/kernel-translation/thread/info") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_thread_info(&body))
        },
        (Method::Post, "/kernel-translation/thread/list-deltas") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_list_deltas(&body))
        },
        (Method::Post, "/kernel-translation/thread/configure-notifications") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_configure_notifications(&body))
        },
        (Method::Post, "/kernel-translation/thread/mechanism-survey") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_mechanism_survey(&body))
        },
        (Method::Post, "/kernel-translation/thread/watcher-status") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_watcher_status(&body))
        },
        (Method::Post, "/kernel-translation/thread/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::thread_notify::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/ob/register-callback") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_register_callback(&body))
        },
        (Method::Post, "/kernel-translation/ob/unregister-callback") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_unregister_callback(&body))
        },
        (Method::Post, "/kernel-translation/ob/list-registrations") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_list_registrations(&body))
        },
        (Method::Post, "/kernel-translation/ob/protect-process") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_protect_process(&body))
        },
        (Method::Post, "/kernel-translation/ob/unprotect-process") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_unprotect_process(&body))
        },
        (Method::Post, "/kernel-translation/ob/list-protected") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_list_protected(&body))
        },
        (Method::Post, "/kernel-translation/ob/simulate-operation") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_simulate_operation(&body))
        },
        (Method::Post, "/kernel-translation/ob/pre-operations") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_pre_operations(&body))
        },
        (Method::Post, "/kernel-translation/ob/post-operations") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_post_operations(&body))
        },
        (Method::Post, "/kernel-translation/ob/access-log") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_access_log(&body))
        },
        (Method::Post, "/kernel-translation/ob/capability-survey") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_capability_survey(&body))
        },
        (Method::Post, "/kernel-translation/ob/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::handle_callbacks::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/driver/load") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_load_driver(&body))
        },
        (Method::Post, "/kernel-translation/driver/unload") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_unload_driver(&body))
        },
        (Method::Post, "/kernel-translation/driver/list") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_list_drivers(&body))
        },
        (Method::Post, "/kernel-translation/driver/create-device") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_create_device(&body))
        },
        (Method::Post, "/kernel-translation/driver/list-devices") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_list_devices(&body))
        },
        (Method::Post, "/kernel-translation/driver/dispatch-irp") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_dispatch_irp(&body))
        },
        (Method::Post, "/kernel-translation/driver/list-irps") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_list_irps(&body))
        },
        (Method::Post, "/kernel-translation/driver/register-ioctl") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_register_ioctl(&body))
        },
        (Method::Post, "/kernel-translation/driver/decode-ioctl") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_decode_ioctl(&body))
        },
        (Method::Post, "/kernel-translation/driver/list-ioctls") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_list_ioctls(&body))
        },
        (Method::Post, "/kernel-translation/driver/type-mapping-survey") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_type_mapping_survey(&body))
        },
        (Method::Post, "/kernel-translation/driver/extension-template") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_extension_template(&body))
        },
        (Method::Post, "/kernel-translation/driver/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::driver_model::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/simulate-check") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_simulate_check(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/run-all-checks") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_run_all_checks(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/check-results") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_check_results(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/hw-breakpoint-map") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_hw_breakpoint_map(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/full-breakpoint-map") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_full_breakpoint_map(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/module-sanitize") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_module_sanitize(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/add-sanitize-rule") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_add_sanitize_rule(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/timing-analysis") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_timing_analysis(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/filesystem-check") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_filesystem_check(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/status-survey") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_status_survey(&body))
        },
        (Method::Post, "/kernel-translation/anti-debug/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::anti_debug::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/integration/extension-install") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_extension_install(&body))
        },
        (Method::Post, "/kernel-translation/integration/extension-activate") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_extension_activate(&body))
        },
        (Method::Post, "/kernel-translation/integration/extension-deactivate") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_extension_deactivate(&body))
        },
        (Method::Post, "/kernel-translation/integration/extension-crash") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_extension_simulate_crash(&body))
        },
        (Method::Post, "/kernel-translation/integration/extension-status") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_extension_status(&body))
        },
        (Method::Post, "/kernel-translation/integration/simulate-pipeline") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_simulate_pipeline(&body))
        },
        (Method::Post, "/kernel-translation/integration/bottle-configure") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_bottle_configure(&body))
        },
        (Method::Post, "/kernel-translation/integration/bottle-get-config") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_bottle_get_config(&body))
        },
        (Method::Post, "/kernel-translation/integration/bottle-list-configs") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_bottle_list_configs(&body))
        },
        (Method::Post, "/kernel-translation/integration/runtime-doctor") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_runtime_doctor(&body))
        },
        (Method::Post, "/kernel-translation/integration/log-translation") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_log_translation(&body))
        },
        (Method::Post, "/kernel-translation/integration/query-translation-log") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_query_translation_log(&body))
        },
        (Method::Post, "/kernel-translation/integration/register-multi-ac") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_register_multi_ac(&body))
        },
        (Method::Post, "/kernel-translation/integration/list-multi-ac") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_list_multi_ac(&body))
        },
        (Method::Post, "/kernel-translation/integration/simulate-conflict") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_simulate_conflict(&body))
        },
        (Method::Post, "/kernel-translation/integration/performance-profile") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_performance_profile(&body))
        },
        (Method::Post, "/kernel-translation/integration/list-performance") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_list_performance(&body))
        },
        (Method::Post, "/kernel-translation/integration/fallback-mode") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_fallback_mode_status(&body))
        },
        (Method::Post, "/kernel-translation/integration/full-stack-status") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_full_stack_status(&body))
        },
        (Method::Post, "/kernel-translation/integration/seed-demo") => {
            let body = read_body(req);
            resp(200, kernel_translation::integration::handle_seed_demo(&body))
        },
        (Method::Post, "/kernel-translation/ipc/start") => match kernel_translation::ipc_bridge::start_ipc_listener() {
            Ok(()) => {
                resp(200, serde_json::json!({"ok": true, "bind_addr": kernel_translation::ipc_bridge::IPC_BIND_ADDR}))
            },
            Err(e) => resp(500, serde_json::json!({"ok": false, "error": e})),
        },
        (Method::Post, "/kernel-translation/ipc/stop") => {
            resp(200, kernel_translation::ipc_bridge::stop_ipc_listener())
        },
        (Method::Get, "/kernel-translation/ipc/status") => {
            let body = read_body(req);
            resp(200, kernel_translation::ipc_bridge::handle_ipc_status(&body))
        },
        (Method::Get, "/kernel-translation/ipc/handles") => {
            let body = read_body(req);
            resp(200, kernel_translation::ipc_bridge::handle_ipc_handles(&body))
        },
        (Method::Post, "/kernel-translation/es-live/start") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_live::handle_es_live_start(&body))
        },
        (Method::Post, "/kernel-translation/es-live/stop") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_live::handle_es_live_stop(&body))
        },
        (Method::Get, "/kernel-translation/es-live/status") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_live::handle_es_live_status(&body))
        },
        (Method::Get, "/kernel-translation/es-live/events") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_live::handle_es_live_events(&body))
        },
        (Method::Get, "/kernel-translation/es-live/processes") => {
            let body = read_body(req);
            resp(200, kernel_translation::es_live::handle_es_live_processes(&body))
        },
        (Method::Get, "/mscompatdb/rules") => resp(200, mtsp::mscompatdb::handle_generate_compatdb_rules()),
        (Method::Post, "/mscompatdb/generate") => {
            let home = dirs::home_dir().unwrap_or_default();
            let ms_root = crate::platform::metalsharp_home_dir_for(&home).join("runtime").join("wine");
            match mtsp::mscompatdb::write_compatdb_rules(&ms_root) {
                Ok(()) => resp(
                    200,
                    json!({"ok": true, "path": ms_root.join("share/metalsharp/mscompatdb-rules.json").to_string_lossy().to_string()}),
                ),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        },
        (Method::Post, "/launcher/evidence") => {
            let body = read_body(req);
            resp(200, launcher_evidence::handle_launcher_evidence(&body))
        },
        (Method::Get, "/eac-toggle/status") => {
            let url_str = req.url().to_string();
            let appid: u32 = url_str
                .split("appid=")
                .nth(1)
                .and_then(|v| v.split('&').next())
                .and_then(|v| v.parse().ok())
                .unwrap_or(0);
            let game_dir = crate::setup::resolve_game_dir(appid);
            let active = game_dir.as_ref().map(|d| mtsp::launcher::eac_toggle_status(d)).unwrap_or(false);
            resp(200, json!({"ok": true, "appid": appid, "eac_toggle_active": active}))
        },
        (Method::Post, "/eac-toggle/toggle") => {
            let body = read_body(req);
            let appid = body.get("appid").and_then(|v| v.as_u64());
            let enable = body.get("enable").and_then(|v| v.as_bool()).unwrap_or(true);
            match appid {
                Some(id) => {
                    let aid = id as u32;
                    let game_dir = crate::setup::resolve_game_dir(aid);
                    match game_dir {
                        Some(dir) if dir.exists() => {
                            if enable {
                                mtsp::launcher::deploy_eac_toggle(&dir);
                                app_log(&format!("[EAC-TOGGLE] enabled for appid {}", aid));
                                resp(200, json!({"ok": true, "eac_toggle_active": true}))
                            } else {
                                mtsp::launcher::cleanup_eac_toggle(&dir);
                                app_log(&format!("[EAC-TOGGLE] disabled for appid {}", aid));
                                resp(200, json!({"ok": true, "eac_toggle_active": false}))
                            }
                        },
                        _ => resp(404, json!({"ok": false, "error": "game directory not found"})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/sharp-library/install") => {
            let body = read_body(req);
            app_log(&format!("[SHARP-LIB] install: {}", body.get("srcPath").and_then(|v| v.as_str()).unwrap_or("?")));
            resp(200, sharp_library::handle_install(&body))
        },
        (Method::Post, "/sharp-library/import-bottle-app") => {
            let body = read_body(req);
            app_log(&format!(
                "[SHARP-LIB] import bottle app: {}",
                body.get("bottleId").and_then(|v| v.as_str()).unwrap_or("?")
            ));
            resp(200, sharp_library::handle_import_bottle_app(&body))
        },
        (Method::Post, "/sharp-library/uninstall") => {
            let body = read_body(req);
            let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("?");
            app_log(&format!("[SHARP-LIB] uninstall: {}", id));
            resp(200, sharp_library::handle_uninstall(&body))
        },
        (Method::Post, "/sharp-library/launch") => {
            let body = read_body(req);
            let id = body.get("id").and_then(|v| v.as_str()).unwrap_or("?");
            let engine = body.get("engine").and_then(|v| v.as_str()).unwrap_or("wine_bare");
            app_log(&format!("[SHARP-LIB] launch: {} engine: {}", id, engine));
            let result = sharp_library::handle_launch(&body);
            if result.get("ok").and_then(|v| v.as_bool()).unwrap_or(false) {
                app_log(&format!(
                    "[SHARP-LIB] launched pid {}",
                    result.get("pid").and_then(|v| v.as_u64()).unwrap_or(0)
                ));
            } else {
                let error = result.get("error").and_then(|v| v.as_str()).unwrap_or("unknown launch error");
                app_issue_log("sharp-launch", id, error, &[format!("engine={}", engine), format!("request_id={}", id)]);
            }
            resp(200, result)
        },
        (Method::Post, "/sharp-library/doctor") => {
            let body = read_body(req);
            resp(200, sharp_library::handle_doctor(&body))
        },
        (Method::Post, "/sharp-library/set-cover") => {
            let body = read_body(req);
            resp(200, sharp_library::handle_set_cover(&body))
        },
        (Method::Post, "/sharp-library/set-cover-position") => {
            let body = read_body(req);
            resp(200, sharp_library::handle_set_cover_position(&body))
        },
        (Method::Post, "/sharp-library/set-engine") => {
            let body = read_body(req);
            resp(200, sharp_library::handle_set_engine(&body))
        },
        (Method::Post, "/sharp-library/set-launch-args") => {
            let body = read_body(req);
            resp(200, sharp_library::handle_set_launch_args(&body))
        },
        (Method::Get, "/sharp-library/cover") => {
            let url_str = req.url().to_string();
            let id = url_str.split("id=").nth(1).and_then(|v| v.split('&').next()).unwrap_or("");
            match sharp_library::get_cover_path(id) {
                Some(path) => {
                    let data = std::fs::read(&path).unwrap_or_default();
                    let ext = path.extension().and_then(|ext| ext.to_str()).unwrap_or_default();
                    let mime = if ext.eq_ignore_ascii_case("png") {
                        "image/png"
                    } else if ext.eq_ignore_ascii_case("svg") {
                        "image/svg+xml"
                    } else if ext.eq_ignore_ascii_case("webp") {
                        "image/webp"
                    } else {
                        "image/jpeg"
                    };
                    resp_raw(200, data, mime)
                },
                None => resp(404, json!({"ok": false, "error": "cover not found"})),
            }
        },
        (Method::Post, "/launch") => {
            let body = read_body(req);
            let exe = body.get("exePath").and_then(|v| v.as_str()).unwrap_or("");
            let steam_app_id = body.get("steamAppId").and_then(|v| v.as_u64());
            let resolved = if let Some(sid) = steam_app_id {
                if !exe.contains(".exe") {
                    resolve_game_exe(sid as u32)
                } else {
                    exe.to_string()
                }
            } else {
                exe.to_string()
            };
            app_log(&format!("Launching: {}", resolved));

            let mut game_type = "native";
            if let Some(sid) = steam_app_id {
                let home = dirs::home_dir().unwrap_or_default();
                let marker = crate::platform::metalsharp_home_dir_for(&home)
                    .join("games")
                    .join(sid.to_string())
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
                    app_issue_log("launch", &resolved, &e.to_string(), &[format!("game_type={}", game_type)]);
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
                    let resolved_pipeline = Some(crate::mtsp::rules::resolve_requested_pipeline(
                        id as u32,
                        crate::mtsp::engine::PipelineId::from_str_flexible(launch_method),
                    ));
                    let engine_desc = resolved_pipeline
                        .map(|p| crate::mtsp::engine::get_pipeline(p).description)
                        .unwrap_or("Unknown");
                    app_log(&format!("[LAUNCH] appid {} | engine: {} | method: {}", id, engine_desc, launch_method));
                    let pipeline = match resolved_pipeline {
                        Some(p) => p,
                        None => {
                            app_log(&format!("[LAUNCH FAILED] appid {} | no pipeline resolved", id));
                            app_issue_log(
                                "game-launch",
                                &id.to_string(),
                                "no pipeline resolved",
                                &[format!("launch_method={}", launch_method)],
                            );
                            return resp(500, json!({"ok": false, "error": "no pipeline resolved"}));
                        },
                    };
                    let result = crate::mtsp::launcher::launch_with_pipeline(id as u32, pipeline);
                    match result {
                        Ok((pid, game_type)) => {
                            register_game_pid(id as u32, pid);
                            app_log(&format!("[LAUNCHED] appid {} | pid {} | engine: {}", id, pid, game_type));
                            resp(
                                200,
                                json!({"ok": true, "pid": pid, "gameType": game_type, "appid": id, "engine": engine_desc}),
                            )
                        },
                        Err(e) => {
                            app_log(&format!("[LAUNCH FAILED] appid {} | error: {}", id, e));
                            app_issue_log(
                                "game-launch",
                                &id.to_string(),
                                &e.to_string(),
                                &[format!("engine={}", engine_desc), format!("launch_method={}", launch_method)],
                            );
                            resp(500, json!({"ok": false, "error": e.to_string()}))
                        },
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Get, "/game/running") => {
            prune_inactive_game_pids();
            let map = running_games().lock().unwrap_or_else(|e| e.into_inner());
            let running: Vec<serde_json::Value> =
                map.iter().map(|(&appid, &pid)| json!({"appid": appid, "pid": pid})).collect();
            resp(200, json!({"ok": true, "running": running}))
        },
        (Method::Get, "/game/dual-info") => {
            let url_str = req.url().to_string();
            let appid: u32 = url_str
                .split("appid=")
                .nth(1)
                .and_then(|v| v.split('&').next())
                .and_then(|v| v.parse().ok())
                .unwrap_or(0);
            if appid == 0 {
                return resp(400, json!({"ok": false, "error": "appid required"}));
            }
            let dual = scan::resolve_dual_game_dir(appid);
            resp(
                200,
                json!({
                    "ok": true,
                    "appid": appid,
                    "has_native_build": dual.has_native_build,
                    "macos_dir": dual.macos_dir.map(|p| p.to_string_lossy().to_string()),
                    "macos_app": dual.macos_app.map(|p| p.to_string_lossy().to_string()),
                    "wine_dir": dual.wine_dir.map(|p| p.to_string_lossy().to_string()),
                }),
            )
        },
        (Method::Post, "/kill") => {
            let body = read_body(req);
            let pid_param = body.get("pid").and_then(|v| v.as_u64()).unwrap_or(0) as i32;
            let appid = body.get("appid").and_then(|v| v.as_u64()).map(|v| v as u32);

            let target_pid = if let Some(aid) = appid {
                let registered = get_game_pid(aid);
                let pid = registered.unwrap_or(pid_param);
                app_log(&format!("[STOP] appid {} | registered pid {:?} | param pid {}", aid, registered, pid_param));

                match launch::kill_game_with_pid(aid, pid) {
                    Ok(_) => {
                        unregister_game_pid(aid);
                        app_log(&format!("[STOPPED] appid {} | pid {}", aid, pid));
                        return resp(200, json!({"ok": true, "pid": pid}));
                    },
                    Err(e) => {
                        app_log(&format!("[STOP FAILED] appid {} | error: {}", aid, e));
                        app_issue_log(
                            "stop",
                            &aid.to_string(),
                            &e.to_string(),
                            &[format!("registered_pid={:?}", registered), format!("requested_pid={}", pid_param)],
                        );
                        return resp(500, json!({"ok": false, "error": e.to_string()}));
                    },
                }
            } else {
                pid_param
            };

            if target_pid <= 0 {
                return resp(400, json!({"ok": false, "error": "pid required"}));
            }

            match launch::kill_process_tree(target_pid) {
                Ok(_) => {
                    app_log(&format!("[STOPPED] pid {}", target_pid));
                    resp(200, json!({"ok": true, "pid": target_pid}))
                },
                Err(e) => {
                    app_issue_log("stop", &target_pid.to_string(), &e.to_string(), &[]);
                    resp(500, json!({"ok": false, "error": e.to_string()}))
                },
            }
        },
        (Method::Post, "/steam/uninstall-game") => {
            let body = read_body(req);
            match body.get("appid").and_then(|v| v.as_u64()).map(|v| v as u32) {
                Some(appid) => {
                    if migrate::is_migrating() {
                        return resp(
                            409,
                            json!({"ok": false, "error": "Migration is running. Wait for it to finish before uninstalling games."}),
                        );
                    }
                    app_log(&format!("Uninstalling game: appid {}", appid));
                    match steam::uninstall_game(appid) {
                        Ok(r) => resp(200, r),
                        Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                    }
                },
                None => resp(400, json!({"ok": false, "error": "appid required"})),
            }
        },
        (Method::Post, "/cache/clear") => {
            let body = read_body(req);
            let cache_type = body.get("type").and_then(|v| v.as_str()).unwrap_or("shader");
            let home = dirs::home_dir().unwrap_or_default();
            let target = cache_dir_for_type(&home, cache_type);
            let (total_bytes, file_count) = dir_stats(&target);
            if target.exists() {
                let _ = std::fs::remove_dir_all(&target);
                let _ = std::fs::create_dir_all(&target);
            }
            app_log(&format!("Cleared {} cache: {} files, {} bytes", cache_type, file_count, total_bytes));
            resp(
                200,
                json!({
                    "ok": true,
                    "cache_type": cache_type,
                    "files_removed": file_count,
                    "bytes_freed": total_bytes,
                }),
            )
        },
        (Method::Get, "/cache/size") => {
            let home = dirs::home_dir().unwrap_or_default();
            let shader_dir = cache_dir_for_type(&home, "shader");
            let pipeline_dir = cache_dir_for_type(&home, "pipeline");
            let _ = std::fs::create_dir_all(&shader_dir);
            let _ = std::fs::create_dir_all(&pipeline_dir);
            resp(
                200,
                json!({
                    "ok": true,
                    "shader_cache": cache_summary(&shader_dir),
                    "pipeline_cache": cache_summary(&pipeline_dir),
                }),
            )
        },
        _ => resp(404, json!({"ok": false, "error": "not found"})),
    }
}

fn resp(code: u16, body: serde_json::Value) -> RouteResponse {
    RouteResponse::Json(code, body.to_string().into_bytes())
}

fn resp_raw(code: u16, data: Vec<u8>, mime: &str) -> RouteResponse {
    RouteResponse::Raw(code, data, mime.to_string())
}

fn app_log(msg: &str) {
    let log_dir = logs_dir();
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

fn app_issue_log(kind: &str, subject: &str, summary: &str, details: &[String]) {
    let log_dir = logs_dir();
    let _ = std::fs::create_dir_all(&log_dir);
    let sequence = ISSUE_LOG_COUNTER.fetch_add(1, Ordering::Relaxed);
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.subsec_nanos())
        .unwrap_or_default();
    let file_name = format!(
        "issue-{}-{}-{:09}-{}-{}-{}.log",
        chrono_file_stamp(),
        std::process::id(),
        nanos,
        sequence,
        slugify(kind),
        slugify(subject),
    );
    let path = log_dir.join(file_name);
    let mut body = vec![
        format!("timestamp: {}", chrono_now()),
        format!("kind: {}", kind),
        format!("subject: {}", subject),
        format!("summary: {}", summary),
        String::new(),
    ];
    body.extend(details.iter().cloned());
    let _ = std::fs::write(&path, body.join("\n"));
    app_log(&format!("[ISSUE] {} | {} | {}", kind, subject, summary));
}

fn logs_dir() -> std::path::PathBuf {
    crate::platform::metalsharp_home_dir().join("logs")
}

fn chrono_now() -> String {
    local_date(&["+%Y-%m-%d %H:%M:%S %Z"]).unwrap_or_else(|| {
        let d = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
        format!("{}", d.as_secs())
    })
}

fn chrono_date() -> String {
    local_date(&["+%Y-%m-%d"]).unwrap_or_else(|| {
        let d = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
        format!("{}", d.as_secs() / 86400)
    })
}

fn chrono_file_stamp() -> String {
    local_date(&["+%Y-%m-%d_%H-%M-%S"]).unwrap_or_else(|| {
        let d = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
        d.as_secs().to_string()
    })
}

fn slugify(value: &str) -> String {
    let slug: String =
        value.chars().map(|ch| if ch.is_ascii_alphanumeric() { ch.to_ascii_lowercase() } else { '-' }).collect();
    let trimmed = slug.trim_matches('-');
    if trimmed.is_empty() {
        "unknown".into()
    } else {
        trimmed.chars().take(80).collect()
    }
}

fn local_date(args: &[&str]) -> Option<String> {
    let output = std::process::Command::new("/bin/date").args(args).output().ok()?;
    if !output.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

fn local_date_for_epoch(secs: u64) -> String {
    local_date(&["-r", &secs.to_string(), "+%Y-%m-%d %H:%M:%S %Z"]).unwrap_or_else(|| secs.to_string())
}

fn cache_dir_for_type(home: &std::path::Path, cache_type: &str) -> std::path::PathBuf {
    match cache_type {
        "pipeline" => crate::platform::metalsharp_home_dir_for(&home).join("pipeline-cache"),
        _ => crate::platform::metalsharp_home_dir_for(&home).join("shader-cache"),
    }
}

fn dir_stats(path: &std::path::Path) -> (u64, u64) {
    let mut bytes = 0;
    let mut files = 0;
    if !path.exists() {
        return (bytes, files);
    }
    for entry in walkdir::WalkDir::new(path).into_iter().flatten() {
        if let Ok(meta) = entry.metadata() {
            if meta.is_file() {
                bytes += meta.len();
                files += 1;
            }
        }
    }
    (bytes, files)
}

fn cache_summary(path: &std::path::Path) -> serde_json::Value {
    let (bytes, files) = dir_stats(path);
    let mut directories = 0u64;
    let mut app_dirs = 0u64;
    let mut newest_modified = 0u64;

    if path.exists() {
        for entry in walkdir::WalkDir::new(path).min_depth(1).into_iter().flatten() {
            if let Ok(meta) = entry.metadata() {
                if meta.is_dir() {
                    directories += 1;
                    if entry.depth() == 2 && entry.file_name().to_string_lossy().chars().all(|c| c.is_ascii_digit()) {
                        app_dirs += 1;
                    }
                }
                if let Ok(modified) = meta.modified() {
                    let secs = modified.duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs();
                    newest_modified = newest_modified.max(secs);
                }
            }
        }
    }

    let status = if !path.exists() {
        "missing"
    } else if files == 0 {
        "empty"
    } else {
        "active"
    };

    json!({
        "bytes": bytes,
        "files": files,
        "directories": directories,
        "apps": app_dirs,
        "path": path.to_string_lossy(),
        "status": status,
        "last_modified": if newest_modified > 0 { json!(local_date_for_epoch(newest_modified)) } else { json!(null) },
    })
}

fn resolve_game_exe(appid: u32) -> String {
    let home = dirs::home_dir().unwrap_or_default();
    let game_dir = crate::platform::metalsharp_home_dir_for(&home).join("games").join(appid.to_string());

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

fn parse_request_appid(body: &serde_json::Map<String, serde_json::Value>) -> Result<u32, &'static str> {
    let Some(value) = body.get("appid") else {
        return Err("appid required");
    };
    let Some(raw) = value.as_u64() else {
        return Err("appid must be a positive numeric Steam appid");
    };
    let appid = u32::try_from(raw).map_err(|_| "appid out of range")?;
    if appid == 0 {
        return Err("appid must be greater than zero");
    }
    Ok(appid)
}

fn pipeline_label_for(pipeline: crate::mtsp::engine::PipelineId) -> &'static str {
    match pipeline {
        crate::mtsp::engine::PipelineId::M12 => "M12",
        crate::mtsp::engine::PipelineId::M11 => "M11",
        crate::mtsp::engine::PipelineId::M9 => "M9",
        crate::mtsp::engine::PipelineId::FnaArm64 => "FNA/Mono",
        _ => "Other",
    }
}

fn pipeline_label_for_exe(exe_name: &str) -> &'static str {
    let exe_lower = exe_name.to_lowercase();
    let home = dirs::home_dir().unwrap_or_default();
    let ms_home = crate::platform::metalsharp_home_dir_for(&home);
    if let Ok(rd) = std::fs::read_dir(ms_home.join("bottles")) {
        for entry in rd.flatten() {
            let bottle_id = entry.file_name().to_string_lossy().to_string();
            let appid: u32 = match bottle_id.strip_prefix("steam_").and_then(|s| s.parse().ok()) {
                Some(id) => id,
                None => continue,
            };
            let manifest_path = entry.path().join("bottle.json");
            let manifest: serde_json::Value =
                match std::fs::read_to_string(&manifest_path).ok().and_then(|s| serde_json::from_str(&s).ok()) {
                    Some(v) => v,
                    None => continue,
                };
            if let Some(name) = manifest.get("game_name").and_then(|v| v.as_str()) {
                let name_lower = name.to_lowercase().replace(' ', "");
                let exe_base = exe_lower.trim_end_matches(".exe").replace(' ', "");
                if exe_base.contains(&name_lower) || name_lower.contains(&exe_base) {
                    let pipeline = crate::bottles::resolve_steam_pipeline_for_request(appid, None);
                    return pipeline_label_for(pipeline);
                }
            }
        }
    }
    "System"
}

fn scan_steam_dumps(dir: &std::path::Path, reports: &mut Vec<serde_json::Value>) {
    if let Ok(rd) = std::fs::read_dir(dir) {
        for entry in rd.flatten() {
            let path = entry.path();
            let name = entry.file_name().to_string_lossy().to_string();
            let name_lower = name.to_lowercase();
            let is_dump = name_lower.ends_with(".dmp") || name_lower.ends_with(".mdmp") || name_lower.contains("crash");
            if is_dump {
                let metadata = std::fs::metadata(&path).ok();
                let size = metadata.as_ref().map(|m| m.len()).unwrap_or(0);
                let modified = metadata.and_then(|m| m.modified().ok());
                let timestamp = modified
                    .map(|t| {
                        let d = t.duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
                        local_date_for_epoch(d.as_secs())
                    })
                    .unwrap_or_else(|| "unknown".into());

                let exe_name = name.split('_').nth(1).unwrap_or("").to_string();
                let pipeline = if exe_name.is_empty() { "System" } else { pipeline_label_for_exe(&exe_name) };

                reports.push(json!({
                    "file": path.to_string_lossy(),
                    "name": name,
                    "source": "steam-dumps",
                    "pipeline": pipeline,
                    "timestamp": timestamp,
                    "size_bytes": size,
                }));
            }
        }
    }
}

fn scan_crash_files(
    dir: &std::path::Path,
    source: &str,
    pipeline: &str,
    reports: &mut Vec<serde_json::Value>,
    depth: u32,
) {
    if depth > 2 {
        return;
    }
    let crash_patterns = ["crash", ".dmp", ".mdmp", "crashdump", "crash_report"];
    if let Ok(rd) = std::fs::read_dir(dir) {
        for entry in rd.flatten() {
            let path = entry.path();
            let name = entry.file_name().to_string_lossy().to_lowercase();
            let is_crash = crash_patterns.iter().any(|p| name.contains(p));
            if is_crash {
                let metadata = std::fs::metadata(&path).ok();
                let size = metadata.as_ref().map(|m| m.len()).unwrap_or(0);
                let modified = metadata.and_then(|m| m.modified().ok());
                let timestamp = modified
                    .map(|t| {
                        let d = t.duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
                        local_date_for_epoch(d.as_secs())
                    })
                    .unwrap_or_else(|| "unknown".into());
                reports.push(json!({
                    "file": path.to_string_lossy(),
                    "name": entry.file_name().to_string_lossy().to_string(),
                    "source": source,
                    "pipeline": pipeline,
                    "timestamp": timestamp,
                    "size_bytes": size,
                }));
                persist_crash_log(source, &path, &timestamp, size);
            }
            if path.is_dir() {
                let sub_source = source.to_string();
                scan_crash_files(&path, &sub_source, pipeline, reports, depth + 1);
            }
        }
    }
}

fn persist_crash_log(source: &str, path: &std::path::Path, timestamp: &str, size: u64) {
    let log_dir = logs_dir();
    let _ = std::fs::create_dir_all(&log_dir);
    let name = path.file_name().unwrap_or_default().to_string_lossy();
    let file_name = format!("crash-{}-{}-{}.log", slugify(source), slugify(&name), slugify(timestamp));
    let log_path = log_dir.join(file_name);
    if log_path.exists() {
        return;
    }
    let body = [
        format!("timestamp: {}", chrono_now()),
        format!("crash_timestamp: {}", timestamp),
        format!("source: {}", source),
        format!("file: {}", path.to_string_lossy()),
        format!("size_bytes: {}", size),
    ]
    .join("\n");
    let _ = std::fs::write(log_path, body);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn request_appid_rejects_missing_string_zero_and_oversized_values() {
        let missing = serde_json::Map::new();
        assert_eq!(parse_request_appid(&missing), Err("appid required"));

        let mut string_appid = serde_json::Map::new();
        string_appid.insert("appid".into(), json!("620"));
        assert_eq!(parse_request_appid(&string_appid), Err("appid must be a positive numeric Steam appid"));

        let mut zero_appid = serde_json::Map::new();
        zero_appid.insert("appid".into(), json!(0));
        assert_eq!(parse_request_appid(&zero_appid), Err("appid must be greater than zero"));

        let mut oversized_appid = serde_json::Map::new();
        oversized_appid.insert("appid".into(), json!(u64::from(u32::MAX) + 1));
        assert_eq!(parse_request_appid(&oversized_appid), Err("appid out of range"));
    }

    #[test]
    fn request_appid_accepts_u32_range_values() {
        let mut body = serde_json::Map::new();
        body.insert("appid".into(), json!(620));

        assert_eq!(parse_request_appid(&body), Ok(620));
    }
}
