mod scan;
mod steam;
mod launch;

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
        (Method::Get, "/status") => resp(200, json!({
            "ok": true,
            "version": "0.1.0"
        })),
        (Method::Get, "/scan") => match scan::scan_all() {
            Ok(result) => resp(200, result),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Get, "/steam/status") => resp(200, steam::status()),
        (Method::Post, "/steam/install") => match steam::install_steam() {
            Ok(p) => resp(200, json!({"ok": true, "path": p})),
            Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
        },
        (Method::Post, "/steam/download-game") => {
            let body = read_body(req);
            let appid = body.get("steamAppId").and_then(|v| v.as_u64());
            match appid {
                Some(id) => match steam::download_game(id as u32) {
                    Ok(games) => resp(200, json!({"ok": true, "games": games})),
                    Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
                },
                None => resp(400, json!({"ok": false, "error": "steamAppId required"})),
            }
        }
        (Method::Get, "/steam/download-progress") => {
            let home = dirs::home_dir().unwrap_or_default();
            let path = home.join(".metalsharp").join("download_progress.json");
            if path.exists() {
                match std::fs::read_to_string(&path) {
                    Ok(s) => match serde_json::from_str::<serde_json::Value>(&s) {
                        Ok(v) => resp(200, v),
                        Err(_) => resp(200, json!({"progress": null})),
                    },
                    Err(_) => resp(200, json!({"progress": null})),
                }
            } else {
                resp(200, json!({"progress": null}))
            }
        }
        (Method::Get, "/logs") => {
            let home = dirs::home_dir().unwrap_or_default();
            let log_path = home.join(".metalsharp").join("logs");
            let mut entries = Vec::new();
            if let Ok(mut rd) = std::fs::read_dir(&log_path) {
                while let Some(Ok(e)) = rd.next() {
                    let p = e.path();
                    if p.extension().map(|e| e == "log").unwrap_or(false) {
                        if let Ok(content) = std::fs::read_to_string(&p) {
                            let lines: Vec<&str> = content.lines().take(500).collect();
                            entries.push(json!({
                                "name": p.file_name().unwrap_or_default().to_string_lossy(),
                                "lines": lines,
                            }));
                        }
                    }
                }
            }
            resp(200, json!({"ok": true, "logs": entries}))
        }
        (Method::Get, "/config") => resp(200, launch::get_config()),
        (Method::Post, "/config") => {
            let body = read_body(req);
            let mode = body.get("launchMode").and_then(|v| v.as_str()).unwrap_or("native");
            match launch::set_config(mode) {
                Ok(cfg) => resp(200, cfg),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        }
        (Method::Post, "/launch") => {
            let body = read_body(req);
            let exe = body.get("exePath").and_then(|v| v.as_str()).unwrap_or("");
            let opts = launch::LaunchOptions::from_map(&body);
            match launch::launch(exe, &opts) {
                Ok(pid) => resp(200, json!({"ok": true, "pid": pid})),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        }
        (Method::Post, "/kill") => {
            let body = read_body(req);
            let pid = body.get("pid").and_then(|v| v.as_u64()).unwrap_or(0) as i32;
            match launch::kill(pid) {
                Ok(_) => resp(200, json!({"ok": true})),
                Err(e) => resp(500, json!({"ok": false, "error": e.to_string()})),
            }
        }
        _ => resp(404, json!({"ok": false, "error": "not found"})),
    }
}

fn resp(code: u16, body: serde_json::Value) -> (u16, Vec<u8>) {
    (code, body.to_string().into_bytes())
}

fn read_body(req: &mut tiny_http::Request) -> serde_json::Map<String, serde_json::Value> {
    let mut buf = Vec::new();
    let _ = req.as_reader().read_to_end(&mut buf);
    serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(&buf)
        .unwrap_or_default()
}
